#include "ServerCore.h"
#include "../Game/SessionManager.h"
#include "../Game/RoomManager.h"
#include "../Packet/Logic/Logic.h"
#include "../Utils/Logger.h"
#include "../Utils/Config.h"
#include "../Utils/ServerStats.h"
#include "../Utils/AuthService.h"
#include "../Utils/HttpClient.h"
#include <ws2tcpip.h>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <cctype>

ObjectPool<IOContext> g_ioContextPool;

static int64_t UnixTimeMsNow()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

static bool TryParseJsonIntByKey(const std::string& json, const std::string& key, int& out)
{
    const std::string marker = "\"" + key + "\":";
    auto pos = json.find(marker);
    if (pos == std::string::npos) {
        return false;
    }
    pos += marker.size();
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    if (pos >= json.size() || json.compare(pos, 4, "null") == 0) {
        return false;
    }

    bool neg = false;
    if (json[pos] == '-') {
        neg = true;
        ++pos;
    }

    int value = 0;
    bool hasDigit = false;
    while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos]))) {
        hasDigit = true;
        value = value * 10 + (json[pos] - '0');
        ++pos;
    }
    if (!hasDigit) {
        return false;
    }

    out = neg ? -value : value;
    return true;
}

static bool IsJsonNullByKey(const std::string& json, const std::string& key)
{
    const std::string marker = "\"" + key + "\":";
    auto pos = json.find(marker);
    if (pos == std::string::npos) {
        return false;
    }
    pos += marker.size();
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    return pos < json.size() && json.compare(pos, 4, "null") == 0;
}

ServerCore::ServerCore() = default;

ServerCore::~ServerCore()
{
    m_healthServer.Stop();
    Stop();
    if (m_registryThread.joinable()) m_registryThread.join();
    if (m_listenSocket != INVALID_SOCKET) closesocket(m_listenSocket);
    if (m_hIOCP != INVALID_HANDLE_VALUE) CloseHandle(m_hIOCP);
    WSACleanup();
    AuthService::Instance().Stop();
    Logger::Instance().Shutdown();
}

bool ServerCore::Init(int port)
{
    m_port = port;

    const auto& cfg = Config::Instance().Get();
    if (!cfg.logFile.empty()) {
        Logger::Instance().SetLogFile(cfg.logFile);
    }

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_ERROR("WSAStartup failed: " << WSAGetLastError());
        return false;
    }

    m_listenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (m_listenSocket == INVALID_SOCKET) {
        LOG_ERROR("WSASocket(listen) failed: " << WSAGetLastError());
        return false;
    }

    BOOL reuse = TRUE;
    setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(static_cast<u_short>(port));

    if (bind(m_listenSocket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        LOG_ERROR("bind failed: " << WSAGetLastError());
        return false;
    }

    if (listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        LOG_ERROR("listen failed: " << WSAGetLastError());
        return false;
    }

    m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (m_hIOCP == NULL) {
        LOG_ERROR("CreateIoCompletionPort failed: " << GetLastError());
        return false;
    }

    if (CreateIoCompletionPort((HANDLE)m_listenSocket, m_hIOCP, 0, 0) == NULL) {
        LOG_ERROR("Associating listen socket with IOCP failed: " << GetLastError());
        return false;
    }

    GUID guidAcceptEx = WSAID_ACCEPTEX;
    DWORD bytes = 0;
    if (WSAIoctl(m_listenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER,
                 &guidAcceptEx, sizeof(guidAcceptEx),
                 &m_acceptExFn, sizeof(m_acceptExFn),
                 &bytes, NULL, NULL) == SOCKET_ERROR) {
        LOG_ERROR("WSAIoctl(AcceptEx) failed: " << WSAGetLastError());
        return false;
    }

    GameLogic::RegisterHandlers(m_packetManager);

    // AuthService 시작 (session-server 토큰 검증 전용 스레드 풀)
    AuthService::Instance().Start(cfg.authWorkerCount);

    // 헬스체크/메트릭 HTTP 서버 시작
    m_healthServer.Start(cfg.healthServerPort, [this]() {
        const auto& s = ServerStats::Instance();
        std::ostringstream oss;
        oss << "{"
            << "\"status\":\"ok\","
            << "\"sessions\":" << s.currentConnections.load(std::memory_order_relaxed) << ","
            << "\"total_connections\":" << s.totalConnections.load(std::memory_order_relaxed) << ","
            << "\"rooms\":" << RoomManager::Instance().GetRoomInfoList().size() << ","
            << "\"packets_received\":" << s.totalPacketsReceived.load(std::memory_order_relaxed) << ","
            << "\"packets_sent\":" << s.totalPacketsSent.load(std::memory_order_relaxed) << ","
            << "\"rate_limit_rejections\":" << s.totalRateLimitRejections.load(std::memory_order_relaxed) << ","
            << "\"oversized_rejections\":" << s.totalOversizedPacketRejections.load(std::memory_order_relaxed) << ","
            << "\"policy_connection_rejections\":" << s.totalPolicyConnectionRejections.load(std::memory_order_relaxed) << ","
            << "\"control_plane_status_code\":" << m_lastControlPlaneStatusCode.load(std::memory_order_relaxed) << ","
            << "\"last_report_unix_ms\":" << m_lastControlPlaneReportUnixMs.load(std::memory_order_relaxed) << ","
            << "\"policy_pull_status_code\":" << m_lastPolicyPullStatusCode.load(std::memory_order_relaxed) << ","
            << "\"last_policy_pull_unix_ms\":" << m_lastPolicyPullUnixMs.load(std::memory_order_relaxed) << ","
            << "\"max_conn_per_min_total\":" << m_policyMaxConnectionsPerMinuteTotal.load(std::memory_order_relaxed)
            << "}";
        return oss.str();
    }, [this]() {
        const auto& s = ServerStats::Instance();
        std::ostringstream oss;
        oss << "realtime_current_connections " << s.currentConnections.load(std::memory_order_relaxed) << "\n"
            << "realtime_total_connections " << s.totalConnections.load(std::memory_order_relaxed) << "\n"
            << "realtime_packets_received_total " << s.totalPacketsReceived.load(std::memory_order_relaxed) << "\n"
            << "realtime_packets_sent_total " << s.totalPacketsSent.load(std::memory_order_relaxed) << "\n"
            << "realtime_bytes_received_total " << s.totalBytesReceived.load(std::memory_order_relaxed) << "\n"
            << "realtime_bytes_sent_total " << s.totalBytesSent.load(std::memory_order_relaxed) << "\n"
            << "realtime_rate_limit_rejections_total " << s.totalRateLimitRejections.load(std::memory_order_relaxed) << "\n"
            << "realtime_oversized_rejections_total " << s.totalOversizedPacketRejections.load(std::memory_order_relaxed) << "\n"
            << "realtime_policy_connection_rejections_total " << s.totalPolicyConnectionRejections.load(std::memory_order_relaxed) << "\n"
            << "realtime_control_plane_status_code " << m_lastControlPlaneStatusCode.load(std::memory_order_relaxed) << "\n"
            << "realtime_last_report_unix_ms " << m_lastControlPlaneReportUnixMs.load(std::memory_order_relaxed) << "\n"
            << "realtime_policy_pull_status_code " << m_lastPolicyPullStatusCode.load(std::memory_order_relaxed) << "\n"
            << "realtime_last_policy_pull_unix_ms " << m_lastPolicyPullUnixMs.load(std::memory_order_relaxed) << "\n"
            << "realtime_policy_max_connections_per_min_total " << m_policyMaxConnectionsPerMinuteTotal.load(std::memory_order_relaxed) << "\n";
        return oss.str();
    });

    LOG_INFO("ServerCore initialized on port " << port
             << " | control-plane=" << cfg.apiGatewayHost << ":" << cfg.apiGatewayPort
             << " | health-port=" << cfg.healthServerPort);
    return true;
}

void ServerCore::Start()
{
    m_running.store(true);

    const auto& cfg = Config::Instance().Get();

    int threadCount = static_cast<int>(std::thread::hardware_concurrency()) * cfg.workerThreadMultiplier;
    if (threadCount <= 0) threadCount = 4;
    for (int i = 0; i < threadCount; ++i) {
        m_workerThreads.emplace_back(&ServerCore::WorkerThread, this);
    }

    m_tickThread      = std::thread(&ServerCore::GameTickThread,  this);
    m_heartbeatThread = std::thread(&ServerCore::HeartbeatThread, this);
    m_statsThread     = std::thread(&ServerCore::StatsThread,     this);
    m_registryThread  = std::thread(&ServerCore::RegistryThread,  this);

    PostAccept();

    LOG_INFO("ServerCore started with " << threadCount << " worker threads");

    for (auto& t : m_workerThreads) t.join();
}

void ServerCore::Stop()
{
    if (!m_running.exchange(false)) return;

    for (size_t i = 0; i < m_workerThreads.size(); ++i) {
        PostQueuedCompletionStatus(m_hIOCP, 0, 0, NULL);
    }

    if (m_tickThread.joinable())      m_tickThread.join();
    if (m_heartbeatThread.joinable()) m_heartbeatThread.join();
    if (m_statsThread.joinable())     m_statsThread.join();
}

void ServerCore::GameTickThread()
{
    const auto& cfg = Config::Instance().Get();
    const auto interval = std::chrono::milliseconds(cfg.tickIntervalMs);
    while (m_running.load()) {
        RoomManager::Instance().TickAll(cfg.reconnectWindowMs);
        std::this_thread::sleep_for(interval);
    }
}

void ServerCore::HeartbeatThread()
{
    constexpr auto CHECK_INTERVAL = std::chrono::seconds(5);
    while (m_running.load()) {
        std::this_thread::sleep_for(CHECK_INTERVAL);

        const auto& cfg = Config::Instance().Get();

        // 세션 타임아웃 체크
        int64_t timeoutMs = cfg.heartbeatTimeoutMs;
        auto sessions = SessionManager::Instance().GetAll();
        for (auto& session : sessions) {
            if (session->IsTimedOut(timeoutMs)) {
                LOG_WARN("Heartbeat timeout: sessionId=" << session->GetSessionId()
                         << " accountId=" << session->GetAccountId());
                HandleDisconnect(session);
            }
        }

        // 빈 룸 GC
        int removed = RoomManager::Instance().RemoveIdleRooms(cfg.idleRoomTimeoutMs);
        if (removed > 0) {
            LOG_INFO("RoomGC: removed " << removed << " idle room(s)");
        }
    }
}

void ServerCore::StatsThread()
{
    const auto interval = std::chrono::milliseconds(Config::Instance().Get().statsPrintIntervalMs);
    while (m_running.load()) {
        std::this_thread::sleep_for(interval);
        ServerStats::Instance().Print();
    }
}

void ServerCore::PostAccept()
{
    IOContext* context = new IOContext(EIOType::ACCEPT, nullptr);
    context->acceptSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (context->acceptSocket == INVALID_SOCKET) {
        LOG_ERROR("WSASocket(accept) failed: " << WSAGetLastError());
        delete context;
        return;
    }

    DWORD bytesReceived = 0;
    constexpr DWORD addrLen = sizeof(sockaddr_in) + 16;

    BOOL ok = m_acceptExFn(
        m_listenSocket, context->acceptSocket, context->buffer,
        0, addrLen, addrLen, &bytesReceived, &context->overlapped);

    if (!ok && WSAGetLastError() != WSA_IO_PENDING) {
        LOG_ERROR("AcceptEx failed: " << WSAGetLastError());
        closesocket(context->acceptSocket);
        delete context;
        return;
    }
}

void ServerCore::HandleAcceptCompletion(IOContext* context, bool success)
{
    SOCKET clientSocket = context->acceptSocket;
    delete context;

    if (!success) {
        LOG_WARN("AcceptEx completion failed");
        if (clientSocket != INVALID_SOCKET) closesocket(clientSocket);
        PostAccept();
        return;
    }

    setsockopt(clientSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
               (char*)&m_listenSocket, sizeof(m_listenSocket));

    sockaddr_in clientAddr = {};
    int addrLen = sizeof(clientAddr);
    getpeername(clientSocket, (sockaddr*)&clientAddr, &addrLen);

    auto session = std::make_shared<ServerSession>();
    session->Init(clientSocket, clientAddr);

    if (CreateIoCompletionPort((HANDLE)clientSocket, m_hIOCP, 0, 0) == NULL) {
        LOG_ERROR("Associating client socket with IOCP failed: " << GetLastError());
        closesocket(clientSocket);
        PostAccept();
        return;
    }

    uint64_t sessionId = SessionManager::Instance().Add(session);
    if (!ConsumeGlobalConnectionToken()) {
        LOG_WARN("Global connection policy exceeded - closing sessionId=" << sessionId);
        STATS_INC(totalPolicyConnectionRejections);
        session->Disconnect();
        SessionManager::Instance().Remove(sessionId);
        PostAccept();
        return;
    }

    session->InitRateLimit(Config::Instance().Get().packetRateLimitPerSec);
    STATS_INC(totalConnections);
    STATS_INC(currentConnections);
    LOG_INFO("Client connected. sessionId=" << sessionId);

    PostRecv(session);
    PostAccept();
}

void ServerCore::PostRecv(const std::shared_ptr<ServerSession>& session)
{
    if (session->IsDisconnected()) return;

    IOContext*   context   = session->GetRecvContext();
    RecvBuffer&  recvBuf   = session->GetRecvBuffer();

    std::memset(&context->overlapped, 0, sizeof(OVERLAPPED));

    char* writePtr  = recvBuf.GetWriteBufferPtr();
    int   writeSize = recvBuf.GetWriteBufferSize();

    context->wsaBuf.buf = writePtr;
    context->wsaBuf.len = writeSize;

    DWORD recvBytes = 0, flags = 0;

    if (WSARecv(session->GetSocket(), &context->wsaBuf, 1, &recvBytes, &flags,
                &context->overlapped, NULL) == SOCKET_ERROR)
    {
        if (WSAGetLastError() != WSA_IO_PENDING) {
            LOG_WARN("WSARecv failed: " << WSAGetLastError());
            HandleDisconnect(session);
        }
    }
}

void ServerCore::HandleRecv(const std::shared_ptr<ServerSession>& session,
                             DWORD bytesTransferred, IOContext* /*ioContext*/)
{
    RecvBuffer& recvBuf = session->GetRecvBuffer();
    recvBuf.Write(bytesTransferred);
    session->UpdateActivity();

    STATS_ADD(totalBytesReceived, static_cast<uint64_t>(bytesTransferred));

    const int maxPacketSize = Config::Instance().Get().maxPacketSize;
    char* packetData = nullptr;
    int   packetSize = 0;
    while (recvBuf.TryGetPacket(packetData, packetSize)) {
        // 패킷 크기 제한
        if (packetSize > maxPacketSize) {
            LOG_WARN("Oversized packet (" << packetSize << " bytes) from sessionId="
                     << session->GetSessionId() << " - disconnecting");
            STATS_INC(totalOversizedPacketRejections);
            HandleDisconnect(session);
            return;
        }
        // 패킷 속도 제한
        if (!session->CheckRateLimit()) {
            LOG_WARN("Rate limit exceeded: sessionId=" << session->GetSessionId()
                     << " accountId=" << session->GetAccountId() << " - disconnecting");
            STATS_INC(totalRateLimitRejections);
            HandleDisconnect(session);
            return;
        }
        STATS_INC(totalPacketsReceived);
        m_packetManager.HandlePacket(session, packetData, packetSize);
        recvBuf.Pop(packetSize);
    }

    PostRecv(session);
}

void ServerCore::HandleDisconnect(const std::shared_ptr<ServerSession>& session)
{
    if (!session->Disconnect()) return;

    LOG_INFO("Client disconnected. sessionId=" << session->GetSessionId());
    STATS_DEC(currentConnections);
    SessionManager::Instance().Remove(session->GetSessionId());

    GameRoom* room = session->GetRoom();
    if (room != nullptr) {
        session->SetRoom(nullptr);
        room->PushJob([room, session]() { room->HandleLeave(session); });
    }
}

void ServerCore::WorkerThread()
{
    while (true) {
        DWORD      bytesTransferred = 0;
        ULONG_PTR  completionKey    = 0;
        LPOVERLAPPED overlapped     = nullptr;

        BOOL success = GetQueuedCompletionStatus(
            m_hIOCP, &bytesTransferred, &completionKey, &overlapped, INFINITE);

        if (overlapped == nullptr) break; // Stop() 신호

        IOContext* context = reinterpret_cast<IOContext*>(overlapped);

        if (context->ioType == EIOType::ACCEPT) {
            HandleAcceptCompletion(context, success);
            continue;
        }

        std::shared_ptr<ServerSession> session = context->session;

        if (!success) {
            HandleDisconnect(session);
            if (context->ioType == EIOType::SEND) g_ioContextPool.Deallocate(context);
            continue;
        }

        if (context->ioType == EIOType::RECV && bytesTransferred == 0) {
            HandleDisconnect(session);
            continue;
        }

        switch (context->ioType) {
            case EIOType::RECV:
                HandleRecv(session, bytesTransferred, context);
                break;
            case EIOType::SEND:
                g_ioContextPool.Deallocate(context);
                break;
            default:
                break;
        }
    }
}

void ServerCore::RegistryThread()
{
    uint64_t prevTotalConnections = 0;
    uint64_t prevTotalRejections = 0;
    int64_t lastPolicyRefreshMs = 0;

    while (m_running.load()) {
        const auto& cfg = Config::Instance().Get();
        const auto intervalMs = std::max(1000, cfg.heartbeatReportIntervalMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));

        const int64_t nowMs = UnixTimeMsNow();
        if (nowMs - lastPolicyRefreshMs >= std::max(1000, cfg.policyRefreshIntervalMs)) {
            RefreshTrafficPolicy();
            lastPolicyRefreshMs = nowMs;
        }

        if (cfg.gameServerApiKey.empty()) {
            LOG_WARN("game_server_api_key is empty; skipping heartbeat report");
            continue;
        }

        const auto& s = ServerStats::Instance();
        uint64_t totalConnections = s.totalConnections.load(std::memory_order_relaxed);
        uint64_t totalRejections =
            s.totalRateLimitRejections.load(std::memory_order_relaxed) +
            s.totalOversizedPacketRejections.load(std::memory_order_relaxed);

        uint64_t recentConnections = (totalConnections >= prevTotalConnections)
            ? (totalConnections - prevTotalConnections)
            : totalConnections;
        uint64_t recentRejections = (totalRejections >= prevTotalRejections)
            ? (totalRejections - prevTotalRejections)
            : totalRejections;

        prevTotalConnections = totalConnections;
        prevTotalRejections = totalRejections;

        int currentPlayers = static_cast<int>(s.currentConnections.load(std::memory_order_relaxed));

        std::ostringstream body;
        body << "{"
             << "\"currentPlayers\":" << currentPlayers << ","
             << "\"maxPlayers\":" << cfg.maxTotalPlayers << ","
             << "\"version\":\"" << cfg.gameServerVersion << "\"," 
             << "\"recentConnections\":" << recentConnections << ","
             << "\"recentRejections\":" << recentRejections
             << "}";

        HttpClient::Headers headers = {
            {"X-Api-Key", cfg.gameServerApiKey},
        };

        auto resp = HttpClient::Post(
            cfg.apiGatewayHost,
            cfg.apiGatewayPort,
            "/api/game-servers/heartbeat",
            body.str(),
            cfg.controlPlaneTimeoutMs,
            headers);

        m_lastControlPlaneStatusCode.store(resp.statusCode, std::memory_order_relaxed);
        m_lastControlPlaneReportUnixMs.store(nowMs, std::memory_order_relaxed);

        if (!resp.ok()) {
            LOG_WARN("Heartbeat report failed (status=" << resp.statusCode
                     << ", body=" << resp.body << ")");
        } else {
            LOG_INFO("Heartbeat report sent: players=" << currentPlayers
                     << " recentConnections=" << recentConnections
                     << " recentRejections=" << recentRejections);
        }
    }
}

bool ServerCore::ConsumeGlobalConnectionToken()
{
    const int limitPerMinute = m_policyMaxConnectionsPerMinuteTotal.load(std::memory_order_relaxed);
    if (limitPerMinute <= 0) {
        return true;
    }

    const int64_t nowMs = UnixTimeMsNow();
    const int refillWindowMs = 60000;
    int64_t last = m_globalConnLastRefillMs.load(std::memory_order_relaxed);
    if (last == 0) {
        m_globalConnTokens.store(limitPerMinute, std::memory_order_relaxed);
        m_globalConnLastRefillMs.store(nowMs, std::memory_order_relaxed);
    } else if (nowMs - last >= refillWindowMs) {
        m_globalConnTokens.store(limitPerMinute, std::memory_order_relaxed);
        m_globalConnLastRefillMs.store(nowMs, std::memory_order_relaxed);
    }

    int prev = m_globalConnTokens.fetch_sub(1, std::memory_order_relaxed);
    return prev > 0;
}

void ServerCore::RefreshTrafficPolicy()
{
    const auto& cfg = Config::Instance().Get();
    if (cfg.gameServerApiKey.empty()) {
        return;
    }

    HttpClient::Headers headers = {
        {"X-Api-Key", cfg.gameServerApiKey},
    };

    auto resp = HttpClient::Get(
        cfg.apiGatewayHost,
        cfg.apiGatewayPort,
        "/api/game-servers/traffic-policy",
        cfg.controlPlaneTimeoutMs,
        headers);

    m_lastPolicyPullStatusCode.store(resp.statusCode, std::memory_order_relaxed);
    m_lastPolicyPullUnixMs.store(UnixTimeMsNow(), std::memory_order_relaxed);

    if (!resp.ok()) {
        LOG_WARN("Traffic policy pull failed (status=" << resp.statusCode << ", body=" << resp.body << ")");
        return;
    }

    int maxConnPerMinuteTotal = 0;
    if (TryParseJsonIntByKey(resp.body, "maxConnectionsPerMinuteTotal", maxConnPerMinuteTotal) && maxConnPerMinuteTotal > 0) {
        m_policyMaxConnectionsPerMinuteTotal.store(maxConnPerMinuteTotal, std::memory_order_relaxed);
        // 정책이 바뀌면 다음 윈도우부터 즉시 반영되도록 토큰을 리셋합니다.
        m_globalConnTokens.store(maxConnPerMinuteTotal, std::memory_order_relaxed);
        m_globalConnLastRefillMs.store(UnixTimeMsNow(), std::memory_order_relaxed);
        LOG_INFO("Traffic policy applied: maxConnectionsPerMinuteTotal=" << maxConnPerMinuteTotal);
    } else if (IsJsonNullByKey(resp.body, "maxConnectionsPerMinuteTotal")) {
        m_policyMaxConnectionsPerMinuteTotal.store(0, std::memory_order_relaxed);
        m_globalConnTokens.store(0, std::memory_order_relaxed);
        m_globalConnLastRefillMs.store(UnixTimeMsNow(), std::memory_order_relaxed);
        LOG_INFO("Traffic policy applied: maxConnectionsPerMinuteTotal=unlimited");
    }
}

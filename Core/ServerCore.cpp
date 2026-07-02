#include "ServerCore.h"
#include "../Game/SessionManager.h"
#include "../Game/RoomManager.h"
#include "../Packet/Logic/Logic.h"
#include "../Utils/Logger.h"
#include "../Utils/Config.h"
#include "../Utils/ServerStats.h"
#include "../Utils/AuthService.h"
#include <ws2tcpip.h>
#include <cstring>

ObjectPool<IOContext> g_ioContextPool;

ServerCore::ServerCore() = default;

ServerCore::~ServerCore()
{
    Stop();
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

    LOG_INFO("ServerCore initialized on port " << port
             << " | session-server=" << cfg.sessionServerHost << ":" << cfg.sessionServerPort);
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
    const auto interval = std::chrono::milliseconds(Config::Instance().Get().tickIntervalMs);
    while (m_running.load()) {
        RoomManager::Instance().TickAll();
        std::this_thread::sleep_for(interval);
    }
}

void ServerCore::HeartbeatThread()
{
    constexpr auto CHECK_INTERVAL = std::chrono::seconds(5);
    while (m_running.load()) {
        std::this_thread::sleep_for(CHECK_INTERVAL);

        int64_t timeoutMs = Config::Instance().Get().heartbeatTimeoutMs;
        auto sessions = SessionManager::Instance().GetAll();
        for (auto& session : sessions) {
            if (session->IsTimedOut(timeoutMs)) {
                LOG_WARN("Heartbeat timeout: sessionId=" << session->GetSessionId()
                         << " accountId=" << session->GetAccountId());
                HandleDisconnect(session);
            }
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

    char* packetData = nullptr;
    int   packetSize = 0;
    while (recvBuf.TryGetPacket(packetData, packetSize)) {
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

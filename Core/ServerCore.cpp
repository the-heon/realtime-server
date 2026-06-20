#include "ServerCore.h"
#include "../Game/SessionManager.h"
#include "../Game/RoomManager.h"
#include "../Packet/Logic/Logic.h"
#include "../Utils/Logger.h"
#include <ws2tcpip.h>
#include <cstring>

// IOContext들을 재사용하는 전역 풀. 주로 SEND에 쓰입니다 (RECV는 세션당 1개를
// 세션 자신이 들고 있고, ACCEPT는 빈도가 낮아 그냥 new/delete 합니다).
ObjectPool<IOContext> g_ioContextPool;

ServerCore::ServerCore() = default;

ServerCore::~ServerCore()
{
    Stop();
    if (m_listenSocket != INVALID_SOCKET) {
        closesocket(m_listenSocket);
    }
    if (m_hIOCP != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hIOCP);
    }
    WSACleanup();
}

bool ServerCore::Init(int port)
{
    m_port = port;

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
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<u_short>(port));

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

    // AcceptEx는 일반 API가 아니라 WSAIoctl로 함수 포인터를 받아와야 하는
    // Winsock 확장 함수입니다.
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

    LOG_INFO("ServerCore initialized on port " << port);
    return true;
}

void ServerCore::Start()
{
    m_running.store(true);

    int threadCount = (int)std::thread::hardware_concurrency() * 2;
    if (threadCount <= 0) threadCount = 4;
    for (int i = 0; i < threadCount; ++i) {
        m_workerThreads.emplace_back(&ServerCore::WorkerThread, this);
    }

    m_tickThread = std::thread(&ServerCore::GameTickThread, this);

    PostAccept();

    LOG_INFO("ServerCore started with " << threadCount << " worker threads");

    for (auto& t : m_workerThreads) {
        t.join();
    }
}

void ServerCore::Stop()
{
    if (!m_running.exchange(false)) {
        return; // 이미 멈춰 있음
    }

    // overlapped=NULL인 완료 통지를 워커 수만큼 보내서 각 WorkerThread의
    // GetQueuedCompletionStatus를 깨우고 루프를 빠져나가게 합니다.
    for (size_t i = 0; i < m_workerThreads.size(); ++i) {
        PostQueuedCompletionStatus(m_hIOCP, 0, 0, NULL);
    }

    if (m_tickThread.joinable()) {
        m_tickThread.join();
    }
}

void ServerCore::GameTickThread()
{
    while (m_running.load()) {
        RoomManager::Instance().TickAll();
        std::this_thread::sleep_for(TICK_INTERVAL);
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
        m_listenSocket,
        context->acceptSocket,
        context->buffer,
        0, // 초기 수신 데이터는 받지 않고 연결 수락만 함
        addrLen,
        addrLen,
        &bytesReceived,
        &context->overlapped);

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

    // AcceptEx로 만든 소켓은 리슨 소켓의 속성을 상속하므로, getpeername 등을
    // 정상적으로 쓰려면 SO_UPDATE_ACCEPT_CONTEXT를 설정해줘야 합니다.
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
    LOG_INFO("Client connected. sessionId=" << sessionId);

    PostRecv(session);

    // 다음 연결도 계속 받을 수 있도록 바로 재무장
    PostAccept();
}

void ServerCore::PostRecv(const std::shared_ptr<ServerSession>& session)
{
    if (session->IsDisconnected()) {
        return;
    }

    IOContext* context = session->GetRecvContext();
    std::memset(&context->overlapped, 0, sizeof(OVERLAPPED));

    RecvBuffer& recvBuf = session->GetRecvBuffer();
    // 항상 GetWriteBufferPtr() -> GetWriteBufferSize() 순서로 호출해야 합니다
    // (필요하면 Ptr() 호출 시점에 내부적으로 압축이 일어납니다).
    char* writePtr = recvBuf.GetWriteBufferPtr();
    int writeSize = recvBuf.GetWriteBufferSize();

    context->wsaBuf.buf = writePtr;
    context->wsaBuf.len = writeSize;

    DWORD recvBytes = 0;
    DWORD flags = 0;

    if (WSARecv(session->GetSocket(), &context->wsaBuf, 1, &recvBytes, &flags, &context->overlapped, NULL) == SOCKET_ERROR) {
        if (WSAGetLastError() != WSA_IO_PENDING) {
            LOG_WARN("WSARecv failed: " << WSAGetLastError());
            HandleDisconnect(session);
        }
    }
}

void ServerCore::HandleRecv(const std::shared_ptr<ServerSession>& session, DWORD bytesTransferred, IOContext* /*ioContext*/)
{
    RecvBuffer& recvBuf = session->GetRecvBuffer();
    recvBuf.Write(bytesTransferred);

    char* packetData = nullptr;
    int packetSize = 0;
    while (recvBuf.TryGetPacket(packetData, packetSize)) {
        m_packetManager.HandlePacket(session, packetData, packetSize);
        recvBuf.Pop(packetSize);
    }

    PostRecv(session);
}

void ServerCore::HandleDisconnect(const std::shared_ptr<ServerSession>& session)
{
    if (!session->Disconnect()) {
        return; // 다른 스레드가 이미 처리함
    }

    LOG_INFO("Client disconnected. sessionId=" << session->GetSessionId());
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
        DWORD bytesTransferred = 0;
        ULONG_PTR completionKey = 0;
        LPOVERLAPPED overlapped = nullptr;

        BOOL success = GetQueuedCompletionStatus(
            m_hIOCP, &bytesTransferred, &completionKey, &overlapped, INFINITE);

        if (overlapped == nullptr) {
            break; // Stop()이 보낸 종료 신호
        }

        IOContext* context = reinterpret_cast<IOContext*>(overlapped);

        if (context->ioType == EIOType::ACCEPT) {
            HandleAcceptCompletion(context, success);
            continue;
        }

        // RECV/SEND는 IOContext가 직접 들고 있는 shared_ptr로 세션을 얻습니다 -
        // 이 I/O가 진행 중인 한 세션은 절대 삭제되지 않습니다.
        std::shared_ptr<ServerSession> session = context->session;

        if (!success) {
            HandleDisconnect(session);
            if (context->ioType == EIOType::SEND) {
                g_ioContextPool.Deallocate(context);
            }
            continue;
        }

        if (context->ioType == EIOType::RECV && bytesTransferred == 0) {
            // 상대가 정상적으로 연결을 닫음 (그레이스풀 클로즈)
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

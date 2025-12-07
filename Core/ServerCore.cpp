#include "ServerCore.h"

CMemoryPool g_overlapPool(100);

bool CIOCPServer::Init(int port)
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;

    m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (m_hIOCP == NULL) return false;

    m_listenSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (m_listenSocket == INVALID_SOCKET) return false;

    // 리스닝 소켓을 IOCP에 등록하지 않고, Accept 완료 후에 클라이언트 소켓을 등록합니다.

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(port);

    if (bind(m_listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) return false;
    if (listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR) return false;

    std::cout << "Server Initialized on port " << port << std::endl;
    return true;
}

void CIOCPServer::Start()
{
    // 워커 스레드 시작 (CPU 코어 수 * 2 + 1 등을 권장)
    int coreCount = std::thread::hardware_concurrency();
    int threadCount = coreCount * 2;
    for (int i = 0; i < threadCount; ++i) {
        m_workerThreads.emplace_back(&CIOCPServer::WorkerThread, this);
    }

    // Accept 루프 (별도의 Accept 전용 스레드를 사용하는 것이 일반적입니다.)
    while (true)
    {
        // 간단화를 위해 블로킹 Accept 사용 (실제 IOCP에선 WSAAcceptEx를 사용)
        CSession* newSession = new CSession();
        int addrLen = sizeof(newSession->addr);
        newSession->sock = accept(m_listenSocket, (sockaddr*)&newSession->addr, &addrLen);

        if (newSession->sock != INVALID_SOCKET)
        {
            // 클라이언트 소켓을 IOCP에 등록
            if (CreateIoCompletionPort((HANDLE)newSession->sock, m_hIOCP, (ULONG_PTR)newSession, 0) == NULL)
            {
                closesocket(newSession->sock);
                delete newSession;
                continue;
            }
            std::cout << "Client connected." << std::endl;
            // 최초 수신 요청
            PostRecv(newSession);
        }
        else
        {
            delete newSession;
            // 에러 처리
        }
    }

    // (cleanup 코드 생략)
}

// ----------------------------------------------------
// IOCP 워커 스레드 핵심
// ----------------------------------------------------

void CIOCPServer::WorkerThread()
{
    DWORD bytesTransferred;
    CSession* session = nullptr;
    LPOVERLAPPED overlapped = nullptr;

    while (true)
    {
        BOOL success = GetQueuedCompletionStatus(
            m_hIOCP,
            &bytesTransferred,
            (PULONG_PTR)&session, // CompletionKey (CSession 포인터)
            &overlapped,
            INFINITE);

        if (overlapped == nullptr) break; // 서버 종료 시그널

        COverlappedEx* ioData = (COverlappedEx*)overlapped;

        if (session == nullptr || success == FALSE || bytesTransferred == 0)
        {
            // 연결 끊김 처리 (closesocket 등)
            std::cout << "Client disconnected or I/O Error." << std::endl;
            // ...
            // 메모리 풀에 반환
            g_overlapPool.Deallocate(ioData);
            continue;
        }

        switch (ioData->type)
        {
            case IoType::RECV:
                HandleRecv(session, bytesTransferred);
                break;
            case IoType::SEND:
                // 송신 완료 처리
                break;
            // ...
        }

        // COverlappedEx 객체를 풀에 반환하고 재사용 준비
        g_overlapPool.Deallocate(ioData);
    }
}

// ----------------------------------------------------
// I/O 및 패킷 처리
// ----------------------------------------------------

void CIOCPServer::PostRecv(CSession* session)
{
    // 풀에서 COverlappedEx 객체를 꺼내와 재사용
    COverlappedEx* ioData = g_overlapPool.Allocate();

    ioData->wsaBuf.len = 4096; // 버퍼 사이즈
    ioData->wsaBuf.buf = ioData->buffer;
    ioData->type = IoType::RECV;

    DWORD flags = 0;
    
    // 비동기 수신 요청
    if (WSARecv(session->sock, &(ioData->wsaBuf), 1, NULL, &flags, &(ioData->overlapped), NULL) == SOCKET_ERROR)
    {
        if (WSAGetLastError() != WSA_IO_PENDING)
        {
            // 즉시 실패 시 풀에 반환
            g_overlapPool.Deallocate(ioData);
            // 에러 처리
        }
    }
}

void CIOCPServer::HandleRecv(CSession* session, DWORD bytes)
{
    // 받은 데이터는 COverlappedEx::buffer에 bytes만큼 있습니다.
    COverlappedEx* ioData = (COverlappedEx*)session; // (주의: 실제론 ioData를 다시 복구해야 함)
    // 이 예시에서는 Simplification을 위해 Recv 완료된 ioData를 사용하지 않고 바로 재요청합니다. 
    // 실제 구현에서는 CRecvBuffer를 사용하여 패킷 경계를 처리해야 합니다.

    // ********* 패킷 분리 및 ProtoBuf 처리 (핵심) *********
    // 실제로는 CRecvBuffer에 데이터를 쌓고 패킷 단위로 분리해야 합니다.
    // 여기서는 간단히 전체 데이터를 하나의 패킷으로 가정합니다.

    // 1. 데이터 추출 (실제로는 ioData의 버퍼에서 추출)
    // ioData->buffer를 사용해야 하지만, 현재 예시 구조상 불가하여 임시로 가정
    
    // ********* 실제 구현 코드 (개념) *********
    // session->recvBuffer.Write(ioData->buffer, bytes);
    // while (session->recvBuffer.TryGetPacket(out_data, out_size)) {
    //     HandlePacket(session, out_data, out_size);
    // }
    // *******************************************
    
    // 2. 패킷 처리 후, 다시 수신 요청을 Post
    PostRecv(session);
}

void CIOCPServer::HandlePacket(CSession* session, const char* data, int size)
{
    // 1. 패킷 ID 추출 (Header 구조를 가정하고 읽음)
    // 실제 Protocol Buffers는 Header를 정의해야 합니다. (예: [4byte ID][4byte Size][Payload])
    if (size < sizeof(uint32_t)) return;

    uint32_t packetId;
    memcpy(&packetId, data, sizeof(uint32_t));
    const char* payload = data + sizeof(uint32_t);
    int payloadSize = size - sizeof(uint32_t);

    switch (static_cast<PacketID>(packetId))
    {
        case PacketID::ID_C_LOGIN:
        {
            tutorial::C_LOGIN loginMsg;
            if (loginMsg.ParseFromArray(payload, payloadSize))
            {
                std::cout << "Login Request: ID=" << loginMsg.user_id() << std::endl;
                
                // 2. 응답 메시지 생성
                tutorial::S_LOGIN loginResult;
                loginResult.set_success(true);
                // 3. 직렬화 및 전송 (Send 함수는 별도 구현 필요)
                // SendPacket(session, PacketID::ID_S_LOGIN, loginResult);
            }
        }
        break;
        case PacketID::ID_C_CHAT:
        // ...
        break;
        default:
            std::cout << "Unknown Packet ID: " << packetId << std::endl;
            break;
    }
}

#include "ServerCore.h" // ServerCore 정의 및 OverlappedEx, IoType 정의 포함
#include "ServerSession.h"    // ServerSession 정의 포함
#include "../Packet/Logic/Packet.pb.h" // Protobuf 헤더

// CMemoryPool의 생성자는 보통 사이즈를 받지 않고, 초기화 시점에 설정됩니다.
// 여기서는 제공하신 코드에 맞춰 인스턴스를 유지합니다.
MemoryPool g_overlapPool(100);

// ==========================================================
// Init & Start (초기화 및 서버 시작)
// ==========================================================

bool ServerCore::Init(int port)
{
    // ... (WSAStartup, CreateIoCompletionPort, WSASocket, bind, listen 코드는 동일)
    // ... (생략)
    return true;
}

void ServerCore::Start()
{
    // 워커 스레드 시작
    int coreCount = std::thread::hardware_concurrency();
    int threadCount = coreCount * 2;
    for (int i = 0; i < threadCount; ++i) {
        m_workerThreads.emplace_back(&ServerCore::WorkerThread, this);
    }

    // Accept 루프
    while (true)
    {
        // 1. ServerSession 객체 생성 (메모리 풀 사용 권장)
        ServerSession* newServerSession = new ServerSession();
        sockaddr_in clientAddr = {};
        int addrLen = sizeof(clientAddr);

        // 2. 블로킹 Accept 사용
        SOCKET newSock = accept(m_listenSocket, (sockaddr*)&clientAddr, &addrLen);

        if (newSock != INVALID_SOCKET)
        {
            // 3. ServerSession 초기화 (이전 오류 수정)
            // newServerSession 내부의 m_sock과 m_addr 멤버를 설정해야 합니다.
            // ServerSession::Init(SOCKET, const sockaddr_in&) 함수가 존재한다고 가정하고 호출합니다.
            newServerSession->Init(newSock, clientAddr); 

            // 4. 클라이언트 소켓을 IOCP에 등록
            // CompletionKey로 ServerSession* 포인터를 넘깁니다.
            if (CreateIoCompletionPort((HANDLE)newSock, m_hIOCP, (ULONG_PTR)newServerSession, 0) == NULL)
            {
                // IOCP 등록 실패 시
                newServerSession->Disconnect(); // 소켓 닫고
                delete newServerSession;       // 메모리 해제
                continue;
            }
            std::cout << "Client connected." << std::endl;
            
            // 5. 최초 수신 요청 Post
            PostRecv(newServerSession);
        }
        else
        {
            // accept 실패 시: 세션 메모리만 해제
            delete newServerSession;
            // 에러 처리: LOG_ERROR("Accept failed: %d", WSAGetLastError());
        }
    }
}

// ==========================================================
// IOCP 워커 스레드 핵심
// ==========================================================

void IOCPServer::WorkerThread()
{
    DWORD bytesTransferred;
    ServerSession* ServerSession = nullptr;
    LPOVERLAPPED overlapped = nullptr; // LPOVERLAPPED는 COverlappedEx 구조체의 주소입니다.

    while (true)
    {
        BOOL success = GetQueuedCompletionStatus(
            m_hIOCP,
            &bytesTransferred,
            (PULONG_PTR)&ServerSession, // CompletionKey (ServerSession 포인터)
            &overlapped,
            INFINITE);

        if (overlapped == nullptr) break; // 서버 종료 시그널

        // 1. LPOVERLAPPED를 COverlappedEx 포인터로 복원
        OverlappedEx* ioData = (COverlappedEx*)overlapped;

        if (ServerSession == nullptr || success == FALSE || bytesTransferred == 0)
        {
            // 연결 끊김/오류 처리
            std::cout << "Client disconnected or I/O Error." << std::endl;
            ServerSession->Disconnect(); // 세션의 소켓 정리 및 상태 변경
            delete ServerSession;        // 세션 메모리 해제 (또는 세션 풀에 반환)

            // ioData(COverlappedEx) 객체를 풀에 반환
            g_overlapPool.Deallocate(ioData); 
            continue;
        }
        
        // 2. I/O 타입에 따라 분기
        switch (ioData->type)
        {
            case IoType::RECV:
                HandleRecv(ServerSession, bytesTransferred, ioData); // ioData를 넘겨 수신 데이터에 접근하도록 수정
                break;
            case IoType::SEND:
                // 송신 완료 처리
                break;
            // ...
        }

        // 3. COverlappedEx 객체를 풀에 반환하고 재사용 준비
        g_overlapPool.Deallocate(ioData);
    }
}

// ==========================================================
// I/O 및 패킷 처리
// ==========================================================

void CIOCPServer::PostRecv(ServerSession* ServerSession)
{
    // 1. 풀에서 COverlappedEx 객체를 할당
    COverlappedEx* ioData = g_overlapPool.Allocate();
    
    // 할당 시점에 wsaBuf, buffer 등이 초기화되었다고 가정 (COverlappedEx 생성자 참조)
    ioData->type = IoType::RECV;
    
    DWORD flags = 0;
    
    // 2. 비동기 수신 요청
    if (WSARecv(ServerSession->GetSocket(), &(ioData->wsaBuf), 1, NULL, &flags, &(ioData->overlapped), NULL) == SOCKET_ERROR)
    {
        if (WSAGetLastError() != WSA_IO_PENDING)
        {
            // 즉시 실패 시 풀에 반환
            g_overlapPool.Deallocate(ioData);
            // 에러 처리 및 세션 종료
            ServerSession->Disconnect(); 
        }
    }
}

// HandleRecv 함수의 시그니처 수정: COverlappedEx*를 인자로 받아 수신 데이터에 접근
void CIOCPServer::HandleRecv(ServerSession* ServerSession, DWORD bytes, COverlappedEx* ioData)
{
    // 1. 수신 버퍼에 데이터 저장 (실제 구현)
    // ServerSession->recvBuffer.Write(ioData->buffer, bytes);
    
    // 2. 패킷 분리 및 처리 루프
    // 실제로는 ServerSession에 패킷을 재조립하는 로직이 필요합니다. 
    // 여기서는 간단히 ioData의 전체 데이터를 하나의 패킷으로 간주합니다.
    HandlePacket(ServerSession, ioData->buffer, bytes);
    
    // 3. 패킷 처리 후, 다시 수신 요청을 Post (다음 패킷을 받기 위해)
    PostRecv(ServerSession);
}

void CIOCPServer::HandlePacket(ServerSession* ServerSession, const char* data, int size)
{
    // 1. 패킷 ID 추출 (Header 구조를 가정하고 읽음)
    if (size < sizeof(uint32_t)) return;

    uint32_t packetId;
    memcpy(&packetId, data, sizeof(uint32_t));
    const char* payload = data + sizeof(uint32_t);
    int payloadSize = size - sizeof(uint32_t);

    switch (static_cast<PacketID>(packetId))
    {
        case PacketID::ID_C_LOGIN:
        {
            // tutorial::C_LOGIN loginMsg;
            // if (loginMsg.ParseFromArray(payload, payloadSize)) // Protobuf 파싱 가정
            // {
            //     // ... 로직 처리
            //     // tutorial::S_LOGIN loginResult;
            //     // SendPacket(ServerSession, PacketID::ID_S_LOGIN, loginResult);
            // }
            std::cout << "Received C_LOGIN Packet (ID: " << packetId << ", Size: " << size << ")" << std::endl;
        }
        break;
        case PacketID::ID_C_CHAT:
            std::cout << "Received C_CHAT Packet (ID: " << packetId << ", Size: " << size << ")" << std::endl;
        break;
        default:
            std::cout << "Unknown Packet ID: " << packetId << std::endl;
            break;
    }
}
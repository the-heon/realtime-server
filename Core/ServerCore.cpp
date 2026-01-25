#include "ServerCore.h"
#include <iostream>
#include <thread>
#include <vector>
// Init: 소켓 생성, 바인드, 리슨 및 IOCP 포트 생성 로직 (생략된 부분 요약)
bool ServerCore::Init(int port) {
    // 실제 구현 시 WSAStartup -> socket -> bind -> listen -> CreateIoCompletionPort 과정이 들어갑니다.
    return true; 
}

void ServerCore::Start() {
    int threadCount = std::thread::hardware_concurrency() * 2;
    for (int i = 0; i < threadCount; ++i) {
        m_workerThreads.emplace_back(&ServerCore::WorkerThread, this);
    }

    while (true) {
        sockaddr_in clientAddr = {};
        int addrLen = sizeof(clientAddr);
        SOCKET newSock = accept(m_listenSocket, (sockaddr*)&clientAddr, &addrLen);

        if (newSock != INVALID_SOCKET) {
            ServerSession* session = new ServerSession();
            session->Init(newSock, clientAddr);

            // 1. 소켓을 IOCP에 등록 (CompletionKey로 session 전달)
            if (CreateIoCompletionPort((HANDLE)newSock, m_hIOCP, (ULONG_PTR)session, 0) == NULL) {
                delete session;
                continue;
            }
            // 2. 최초 수신 예약
            PostRecv(session);
        }
    }
}

void ServerCore::WorkerThread() {
    DWORD bytesTransferred = 0;
    ServerSession* session = nullptr;
    LPOVERLAPPED overlapped = nullptr;

    while (true) {
        // 3. 완료된 I/O 작업 기다리기
        BOOL success = GetQueuedCompletionStatus(
            m_hIOCP,
            &bytesTransferred,
            (PULONG_PTR)&session, // CompletionKey
            &overlapped,          // 확장 구조체의 시작점 포인터
            INFINITE);

        if (overlapped == nullptr) break;

        // 4. LPOVERLAPPED를 우리가 정의한 IOContext로 형변환
        IOContext* context = reinterpret_cast<IOContext*>(overlapped);

        // 5. 연결 종료 및 에러 처리
        if (!success || bytesTransferred == 0) {
            std::cout << "Client Disconnected" << std::endl;
            if (session) {
                session->Disconnect();
                delete session; 
            }
            continue;
        }

        // 6. 작업 유형(ioType)에 따른 처리
        switch (context->ioType) {
            case EIOType::RECV:
                HandleRecv(session, bytesTransferred, context);
                break;
            case EIOType::SEND:
                // 송신 완료 처리 (필요 시)
                break;
        }
    }
}

void ServerCore::PostRecv(ServerSession* session) {
    IOContext* context = session->GetRecvContext();
    
    // OVERLAPPED 구조체는 호출 전 반드시 초기화
    memset(&context->overlapped, 0, sizeof(OVERLAPPED));

    DWORD flags = 0;
    DWORD recvBytes = 0;

    // 7. 비동기 수신 요청
    if (WSARecv(session->GetSocket(), &context->wsaBuf, 1, &recvBytes, &flags, &context->overlapped, NULL) == SOCKET_ERROR) {
        if (WSAGetLastError() != WSA_IO_PENDING) {
            session->Disconnect();
            delete session;
        }
    }
}

void ServerCore::HandleRecv(ServerSession* session, DWORD bytesTransferred, IOContext* ioContext) {
    // 8. 수신된 데이터 패킷 처리 로직으로 전달
    HandlePacket(session, ioContext->wsaBuf.buf, bytesTransferred);
    
    // 9. 다음 수신을 위해 다시 PostRecv
    PostRecv(session);
}

void ServerCore::HandlePacket(ServerSession* session, const char* data, int size) {
    // 실제 로직: 패킷 ID 확인 및 Protobuf 파싱 등
    std::cout << "Received " << size << " bytes" << std::endl;
}
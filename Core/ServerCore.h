#pragma once
#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include "ServerSession.h"
#include "../Packet/PacketManager.h"

class ServerCore
{
public:
    ServerCore();
    ~ServerCore();

    // 리슨 소켓 생성/바인드/리슨, IOCP 핸들 생성, AcceptEx 함수 포인터 로딩까지 합니다.
    bool Init(int port);

    // 워커 스레드들 + accept 루프 + 게임 틱 스레드를 시작하고, 이 호출 스레드는
    // 그대로 블록됩니다 (서버 프로세스가 떠 있는 동안 리턴하지 않습니다).
    void Start();

    void Stop();

private:
    void WorkerThread();
    void GameTickThread();

    void PostAccept();
    void HandleAcceptCompletion(IOContext* context, bool success);

    void PostRecv(const std::shared_ptr<ServerSession>& session);
    void HandleRecv(const std::shared_ptr<ServerSession>& session, DWORD bytesTransferred, IOContext* ioContext);

    // 한 세션에 대해 더 이상 새 I/O를 걸지 않게 막고, SessionManager/현재 룸에서
    // 빼는 정리를 합니다. 여러 스레드(RECV 완료/SEND 완료)에서 동시에 불려도
    // 실제 정리는 정확히 한 번만 일어납니다.
    void HandleDisconnect(const std::shared_ptr<ServerSession>& session);

    static constexpr std::chrono::milliseconds TICK_INTERVAL{ 33 }; // ~30Hz

    PacketManager m_packetManager;

    HANDLE m_hIOCP = INVALID_HANDLE_VALUE;
    SOCKET m_listenSocket = INVALID_SOCKET;
    LPFN_ACCEPTEX m_acceptExFn = nullptr;
    int m_port = 0;

    std::atomic<bool> m_running{ false };
    std::vector<std::thread> m_workerThreads;
    std::thread m_tickThread;
};

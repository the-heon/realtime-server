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

    bool Init(int port);
    void Start();
    void Stop();

private:
    void WorkerThread();
    void GameTickThread();
    void HeartbeatThread();  // 타임아웃 세션을 주기적으로 정리
    void StatsThread();      // 서버 통계를 주기적으로 출력

    void PostAccept();
    void HandleAcceptCompletion(IOContext* context, bool success);

    void PostRecv(const std::shared_ptr<ServerSession>& session);
    void HandleRecv(const std::shared_ptr<ServerSession>& session, DWORD bytesTransferred, IOContext* ioContext);
    void HandleDisconnect(const std::shared_ptr<ServerSession>& session);

    PacketManager m_packetManager;

    HANDLE         m_hIOCP        = INVALID_HANDLE_VALUE;
    SOCKET         m_listenSocket = INVALID_SOCKET;
    LPFN_ACCEPTEX  m_acceptExFn   = nullptr;
    int            m_port         = 0;

    std::atomic<bool>        m_running{ false };
    std::vector<std::thread> m_workerThreads;
    std::thread              m_tickThread;
    std::thread              m_heartbeatThread;
    std::thread              m_statsThread;
};

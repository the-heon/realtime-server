#pragma once
#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <string>
#include <cstdint>
#include "ServerSession.h"
#include "../Packet/PacketManager.h"
#include "../Utils/HealthServer.h"

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
    std::thread              m_registryThread;

    HealthServer m_healthServer;
    std::atomic<int> m_lastControlPlaneStatusCode{ 0 };
    std::atomic<int64_t> m_lastControlPlaneReportUnixMs{ 0 };
    std::atomic<int> m_lastPolicyPullStatusCode{ 0 };
    std::atomic<int64_t> m_lastPolicyPullUnixMs{ 0 };
    std::atomic<int> m_policyMaxConnectionsPerMinuteTotal{ 0 };
    std::atomic<int> m_globalConnTokens{ 0 };
    std::atomic<int64_t> m_globalConnLastRefillMs{ 0 };

    void RegistryThread(); // api-gateway를 통해 api-server로 heartbeat 전송
    bool ConsumeGlobalConnectionToken();
    void RefreshTrafficPolicy();

    // JSON 직렬화 없이 간단히 문자열 조합
    static std::string BuildJson(const std::string& key, const std::string& val)
    { return "{\"" + key + "\":\"" + val + "\"}"; }
};

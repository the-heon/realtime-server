#include "pch.h"
#include "Memory/MemoryPool.h"
// 클라이언트 세션 정보 (CompletionKey로 사용)
class CSession
{
public:
    SOCKET sock = INVALID_SOCKET;
    sockaddr_in addr = {};
    // ... 추가 세션 정보 (ID, 상태 등)
};

// 전역 메모리 풀 (예시)
extern CMemoryPool g_overlapPool;

class CIOCPServer
{
public:
    bool Init(int port);
    void Start();

private:
    void StartWorkerThreads();
    void WorkerThread();
    void HandleRecv(CSession* session, DWORD bytes);
    void HandlePacket(CSession* session, const char* data, int size);
    void PostRecv(CSession* session);

private:
    HANDLE m_hIOCP = INVALID_HANDLE_VALUE;
    SOCKET m_listenSocket = INVALID_SOCKET;
    std::vector<std::thread> m_workerThreads;
};
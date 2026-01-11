#include "../Packet/PacketHeader.h"
#include "../Memory/memoryPool.h"
#include "ServerSession.h"
#include "../pch.h"
// 전역 메모리 풀 (예시)
extern MemoryPool g_overlapPool;

class ServerCore
{
public:
    bool Init(int port);
    void Start();

private:
    void StartWorkerThreads();
    void WorkerThread();
    void HandleRecv(ServerSession* ServerSession, DWORD bytes);
    void HandlePacket(ServerSession* ServerSession, const char* data, int size);
    void PostRecv(ServerSession* ServerSession);

private:
    HANDLE m_hIOCP = INVALID_HANDLE_VALUE;
    SOCKET m_listenSocket = INVALID_SOCKET;
    std::vector<std::thread> m_workerThreads;
};
#include "../pch.h"
#include "../Packet/PacketHeader.h"
#include "../Memory/memoryPool.h"
#include "CSession.h"
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
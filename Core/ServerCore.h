#pragma once
#include <winsock2.h>
#include <iostream>
#include <thread>
#include <vector>
enum class EIOType { RECV, SEND, ACCEPT };

class ServerSession;
struct IOContext
{
    OVERLAPPED overlapped = {}; 
    EIOType ioType = EIOType::RECV; 
    ServerSession* serverSession = nullptr;  // 'session' → 'serverSession'
    WSABUF wsaBuf = {};

    IOContext(EIOType type, ServerSession* owner);
};

class ServerSession
{
public:
    ServerSession();
    ~ServerSession();

    void Init(SOCKET socket, const sockaddr_in& addr);
    void Disconnect();

    SOCKET GetSocket() const { return m_sock; }
    IOContext* GetRecvContext() { return m_recvContext; }

private:
    SOCKET m_sock = INVALID_SOCKET;
    sockaddr_in m_addr = {};
    static constexpr int BUF_SIZE = 4096;
    char m_recvBuffer[BUF_SIZE]; 
    IOContext* m_recvContext = nullptr;
};

class ServerCore
{
public:
    bool Init(int port);
    void Start();

private:
    void WorkerThread();
    void PostRecv(ServerSession* session);
    void HandleRecv(ServerSession* session, DWORD bytesTransferred, IOContext* ioContext);
    void HandlePacket(ServerSession* session, const char* data, int size);

private:
    HANDLE m_hIOCP = INVALID_HANDLE_VALUE;
    SOCKET m_listenSocket = INVALID_SOCKET;
    std::vector<std::thread> m_workerThreads;
};
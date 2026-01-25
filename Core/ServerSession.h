#pragma once
#include <winsock2.h>
#include <iostream>
#include <vector>

// I/O 작업 유형
enum class EIOType { RECV, SEND, ACCEPT };

// 전방 선언
class ServerSession;

// IOCP Overlapped 구조체 확장
struct IOContext
{
    OVERLAPPED overlapped = {};
    EIOType ioType = EIOType::RECV;
    ServerSession* serverSession = nullptr;
    WSABUF wsaBuf = {};

    IOContext(EIOType type, ServerSession* owner); // 구현은 cpp에서

    IOContext(const IOContext&) = delete;
    IOContext& operator=(const IOContext&) = delete;
};

// 세션 클래스
class ServerSession
{
public:
    ServerSession();
    ~ServerSession();

    void Init(SOCKET socket, const sockaddr_in& addr);
    void Disconnect();

    SOCKET GetSocket() const { return m_sock; }
    IOContext* GetRecvContext() { return m_recvContext; }
    char* GetRecvBuffer() { return m_recvBuffer; }

private:
    SOCKET m_sock = INVALID_SOCKET;
    sockaddr_in m_addr = {};

    static constexpr int BUF_SIZE = 4096;
    char m_recvBuffer[BUF_SIZE];
    IOContext* m_recvContext = nullptr;
};
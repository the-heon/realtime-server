#pragma once
#include <winsock2.h>
#include <iostream>
#include <vector>

// I/O 작업 유형
enum class EIOType { RECV, SEND, ACCEPT };

// 전방 선언
class ServerSession;

struct IOContext
{
    OVERLAPPED overlapped = {};
    EIOType ioType = EIOType::RECV;
    ServerSession* serverSession = nullptr;
    WSABUF wsaBuf = {};
    
    // 🌟 전송/수신 데이터를 직접 들고 있을 버퍼 추가
    static constexpr int MAX_BUF_SIZE = 4096;
    char buffer[MAX_BUF_SIZE] = {}; 

    IOContext(EIOType type, ServerSession* owner);

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
    // Protobuf 메시지를 인자로 받는 Send
    bool Send(const google::protobuf::Message& message);
    
    // 실제 WSASend를 호출하는 비동기 전송 함수
    void PostSend(IOContext* sendContext);

private:
    SOCKET m_sock = INVALID_SOCKET;
    sockaddr_in m_addr = {};

    static constexpr int BUF_SIZE = 4096;
    char m_recvBuffer[BUF_SIZE];
    IOContext* m_recvContext = nullptr;
};
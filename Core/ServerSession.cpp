#include "ServerSession.h"
#include <string.h>
#include "MemoryPool.h"
#include "google/protobuf/message.h"

extern MemoryPool g_ioContextPool;

// IOContext 구현
IOContext::IOContext(EIOType type, ServerSession* owner)
    : ioType(type), serverSession(owner)
{
    memset(&overlapped, 0, sizeof(OVERLAPPED));
}

// ServerSession 구현
ServerSession::ServerSession()
{
    m_recvContext = new IOContext(EIOType::RECV, this);
}

ServerSession::~ServerSession()
{
    delete m_recvContext;
    Disconnect();
}

void ServerSession::Init(SOCKET socket, const sockaddr_in& addr)
{
    m_sock = socket;
    m_addr = addr;
    // 버퍼 연결
    m_recvContext->wsaBuf.buf = m_recvBuffer;
    m_recvContext->wsaBuf.len = BUF_SIZE;
}

void ServerSession::Disconnect()
{
    if (m_sock != INVALID_SOCKET)
    {
        closesocket(m_sock);
        m_sock = INVALID_SOCKET;
    }
}

bool ServerSession::Send(const google::protobuf::Message& message)
{
    // 1. 패킷 크기 계산 및 할당
    int payloadSize = static_cast<int>(message.ByteSizeLong());
    int totalSize = payloadSize + sizeof(uint32_t); // 패킷ID(또는 헤더 크기) 포함

    // 2. 전송용 IOContext 할당 (메모리 풀 사용)
    IOContext* sendContext = g_ioContextPool.Allocate(EIOType::SEND, this);
    
    // 3. 데이터 직렬화 (간단한 헤더 구조: [Size(4)][Data...])
    // 실제 프로젝트의 헤더 구조에 맞게 수정하세요.
    char* targetBuf = sendContext->buffer; // IOContext에 정의된 buffer 사용
    
    // 예시: 첫 4바이트에 패킷 아이디나 크기 기록 (여기선 간단히)
    // message.SerializeToArray(targetBuf + sizeof(uint32_t), payloadSize);
    
    // 실제 구현: Protobuf 메시지를 버퍼에 직렬화
    if (!message.SerializeToArray(targetBuf, payloadSize))
    {
        g_ioContextPool.Deallocate(sendContext);
        return false;
    }

    sendContext->wsaBuf.buf = targetBuf;
    sendContext->wsaBuf.len = payloadSize;

    // 4. 비동기 전송 요청
    PostSend(sendContext);

    return true;
}

void ServerSession::PostSend(IOContext* sendContext)
{
    DWORD sendBytes = 0;
    DWORD flags = 0;

    // 비동기 전송 시작
    if (WSASend(m_sock, &sendContext->wsaBuf, 1, &sendBytes, flags, &sendContext->overlapped, NULL) == SOCKET_ERROR)
    {
        if (WSAGetLastError() != WSA_IO_PENDING)
        {
            std::cerr << "WSASend Failed: " << WSAGetLastError() << std::endl;
            g_ioContextPool.Deallocate(sendContext);
            Disconnect();
        }
    }
}
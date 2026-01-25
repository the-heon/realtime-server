#include "ServerSession.h"
#include <string.h>

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
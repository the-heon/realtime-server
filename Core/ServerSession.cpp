#include "ServerSession.h"
#include <cstring>
#include "google/protobuf/message.h"
#include "../Utils/Logger.h"

// IOContext 구현
IOContext::IOContext(EIOType type, std::shared_ptr<ServerSession> owner)
    : ioType(type), session(std::move(owner))
{
    std::memset(&overlapped, 0, sizeof(OVERLAPPED));
}

void IOContext::Reset(EIOType type, std::shared_ptr<ServerSession> owner)
{
    std::memset(&overlapped, 0, sizeof(OVERLAPPED));
    ioType = type;
    session = std::move(owner);
    wsaBuf = {};
    acceptSocket = INVALID_SOCKET;
}

// ServerSession 구현
ServerSession::ServerSession() = default;

ServerSession::~ServerSession()
{
    delete m_recvContext;
}

void ServerSession::Init(SOCKET socket, const sockaddr_in& addr)
{
    m_sock = socket;
    m_addr = addr;
    // shared_from_this()는 이 세션이 이미 shared_ptr로 소유되고 있어야 호출할 수
    // 있으므로(make_shared 직후), 생성자가 아니라 여기서 recv context를 만듭니다.
    m_recvContext = new IOContext(EIOType::RECV, shared_from_this());
}

bool ServerSession::Disconnect()
{
    bool already = m_disconnected.exchange(true);
    if (!already && m_sock != INVALID_SOCKET) {
        closesocket(m_sock);
        m_sock = INVALID_SOCKET;
    }
    return !already;
}

bool ServerSession::SendInternal(uint16_t packetId, const google::protobuf::Message& message)
{
    if (IsDisconnected()) {
        return false;
    }

    int payloadSize = static_cast<int>(message.ByteSizeLong());
    int totalSize = static_cast<int>(sizeof(PacketHeader)) + payloadSize;

    if (totalSize > IOContext::MAX_BUF_SIZE) {
        LOG_ERROR("Send: packet too large (" << totalSize << " bytes), packetId=" << packetId);
        return false;
    }

    IOContext* sendContext = g_ioContextPool.Allocate(EIOType::SEND, shared_from_this());

    // [PacketHeader][Protobuf 직렬화 데이터] 형태로 버퍼를 채웁니다.
    PacketHeader* header = reinterpret_cast<PacketHeader*>(sendContext->buffer);
    header->packetSize = static_cast<uint16_t>(payloadSize);
    header->packetId = packetId;

    if (!message.SerializeToArray(sendContext->buffer + sizeof(PacketHeader), payloadSize)) {
        g_ioContextPool.Deallocate(sendContext);
        return false;
    }

    sendContext->wsaBuf.buf = sendContext->buffer;
    sendContext->wsaBuf.len = totalSize;

    PostSend(sendContext);
    return true;
}

void ServerSession::PostSend(IOContext* sendContext)
{
    if (IsDisconnected()) {
        g_ioContextPool.Deallocate(sendContext);
        return;
    }

    DWORD sendBytes = 0;
    DWORD flags = 0;

    if (WSASend(m_sock, &sendContext->wsaBuf, 1, &sendBytes, flags, &sendContext->overlapped, NULL) == SOCKET_ERROR)
    {
        if (WSAGetLastError() != WSA_IO_PENDING)
        {
            LOG_ERROR("WSASend failed: " << WSAGetLastError());
            g_ioContextPool.Deallocate(sendContext);
            Disconnect();
        }
    }
    // 성공/WSA_IO_PENDING이면 sendContext는 완료 통지가 올 때까지 살아 있고,
    // 그 안의 shared_ptr<ServerSession>이 세션을 붙잡아 둡니다.
}

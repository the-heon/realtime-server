#include "ServerSession.h"
#include <cstring>
#include "google/protobuf/message.h"
#include "../Utils/Logger.h"
#include "../Utils/ServerStats.h"

// IOContext 구현

IOContext::IOContext(EIOType type, std::shared_ptr<ServerSession> owner)
    : ioType(type), session(std::move(owner))
{
    std::memset(&overlapped, 0, sizeof(OVERLAPPED));
}

void IOContext::Reset(EIOType type, std::shared_ptr<ServerSession> owner)
{
    std::memset(&overlapped, 0, sizeof(OVERLAPPED));
    ioType        = type;
    session       = std::move(owner);
    wsaBuf        = {};
    acceptSocket  = INVALID_SOCKET;
    sharedBuffer.reset(); // 이전에 공유 버퍼를 들고 있었다면 레퍼런스 해제
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
    UpdateActivity(); // 연결 직후부터 타임아웃 카운트 시작
    // shared_from_this()는 make_shared 이후에만 유효하므로 생성자가 아닌 여기서 생성
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
    if (IsDisconnected()) return false;

    int payloadSize = static_cast<int>(message.ByteSizeLong());
    int totalSize   = static_cast<int>(sizeof(PacketHeader)) + payloadSize;

    if (totalSize > IOContext::MAX_BUF_SIZE) {
        LOG_ERROR("Send: packet too large (" << totalSize << " bytes), packetId=" << packetId);
        return false;
    }

    IOContext* ctx = g_ioContextPool.Allocate(EIOType::SEND, shared_from_this());

    PacketHeader* header = reinterpret_cast<PacketHeader*>(ctx->buffer);
    header->packetSize   = static_cast<uint16_t>(payloadSize);
    header->packetId     = packetId;

    if (!message.SerializeToArray(ctx->buffer + sizeof(PacketHeader), payloadSize)) {
        g_ioContextPool.Deallocate(ctx);
        return false;
    }

    ctx->wsaBuf.buf = ctx->buffer;
    ctx->wsaBuf.len = static_cast<ULONG>(totalSize);

    STATS_ADD(totalBytesSent, static_cast<uint64_t>(totalSize));
    STATS_INC(totalPacketsSent);

    PostSend(ctx);
    return true;
}

bool ServerSession::SendShared(std::shared_ptr<std::vector<char>> packet)
{
    if (IsDisconnected()) return false;

    IOContext* ctx      = g_ioContextPool.Allocate(EIOType::SEND, shared_from_this());
    ctx->sharedBuffer   = packet; // keep-alive
    ctx->wsaBuf.buf     = packet->data();
    ctx->wsaBuf.len     = static_cast<ULONG>(packet->size());

    STATS_ADD(totalBytesSent, static_cast<uint64_t>(packet->size()));
    STATS_INC(totalPacketsSent);

    PostSend(ctx);
    return true;
}

bool ServerSession::SendRaw(uint16_t packetId, const void* data, int dataSize)
{
    if (IsDisconnected()) return false;

    int totalSize = static_cast<int>(sizeof(PacketHeader)) + dataSize;
    if (totalSize > IOContext::MAX_BUF_SIZE) {
        LOG_ERROR("SendRaw: packet too large (" << totalSize << " bytes), packetId=" << packetId);
        return false;
    }

    IOContext* ctx = g_ioContextPool.Allocate(EIOType::SEND, shared_from_this());

    PacketHeader* header = reinterpret_cast<PacketHeader*>(ctx->buffer);
    header->packetSize   = static_cast<uint16_t>(dataSize);
    header->packetId     = packetId;

    if (dataSize > 0 && data != nullptr) {
        std::memcpy(ctx->buffer + sizeof(PacketHeader), data, dataSize);
    }

    ctx->wsaBuf.buf = ctx->buffer;
    ctx->wsaBuf.len = static_cast<ULONG>(totalSize);

    STATS_ADD(totalBytesSent, static_cast<uint64_t>(totalSize));
    STATS_INC(totalPacketsSent);

    PostSend(ctx);
    return true;
}

void ServerSession::PostSend(IOContext* ctx)
{
    if (IsDisconnected()) {
        g_ioContextPool.Deallocate(ctx);
        return;
    }

    DWORD sendBytes = 0;
    DWORD flags     = 0;

    if (WSASend(m_sock, &ctx->wsaBuf, 1, &sendBytes, flags, &ctx->overlapped, NULL) == SOCKET_ERROR)
    {
        if (WSAGetLastError() != WSA_IO_PENDING)
        {
            LOG_ERROR("WSASend failed: " << WSAGetLastError());
            g_ioContextPool.Deallocate(ctx);
            Disconnect();
        }
    }
}

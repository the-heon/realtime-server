#pragma once
#include <winsock2.h>
#include <memory>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include "../Packet/RecvBuffer.h"
#include "../Packet/PacketHeader.h"
#include "../Packet/PacketID.h"
#include "../Utils/ObjectPool.h"

enum class EIOType { RECV, SEND, ACCEPT };

class ServerSession;
class GameRoom;

struct IOContext
{
    OVERLAPPED overlapped = {};
    EIOType    ioType     = EIOType::RECV;

    // shared_ptr로 들고 있으면, 이 I/O가 아직 완료되지 않은 동안에는 세션이
    // 절대 삭제되지 않습니다.
    std::shared_ptr<ServerSession> session;

    WSABUF wsaBuf = {};

    // 자체 소유 버퍼 (단일 수신자 전송 / Accept 용)
    static constexpr int MAX_BUF_SIZE = 4096;
    char buffer[MAX_BUF_SIZE] = {};

    // 브로드캐스트 최적화: 한 번 직렬화한 버퍼를 N개의 WSASend가 공유.
    // set되면 wsaBuf.buf가 이 버퍼를 가리키며, 모든 WSASend가 완료될 때까지
    // 레퍼런스 카운트가 유지됩니다.
    std::shared_ptr<std::vector<char>> sharedBuffer;

    // ACCEPT 전용: AcceptEx로 미리 만들어 둔 클라이언트 소켓
    SOCKET acceptSocket = INVALID_SOCKET;

    IOContext(EIOType type, std::shared_ptr<ServerSession> owner);

    IOContext(const IOContext&) = delete;
    IOContext& operator=(const IOContext&) = delete;

    // ObjectPool<IOContext>가 재사용할 때 호출합니다.
    void Reset(EIOType type, std::shared_ptr<ServerSession> owner);
};

extern ObjectPool<IOContext> g_ioContextPool;

// 세션 클래스.
// 항상 std::make_shared<ServerSession>()로 만들고, enable_shared_from_this를 통해
// 자기 자신을 가리키는 shared_ptr을 IOContext에 넘겨줍니다.
class ServerSession : public std::enable_shared_from_this<ServerSession>
{
public:
    ServerSession();
    ~ServerSession();

    void Init(SOCKET socket, const sockaddr_in& addr);

    bool Disconnect();
    bool IsDisconnected() const { return m_disconnected.load(); }

    SOCKET    GetSocket()      const { return m_sock; }
    IOContext* GetRecvContext()       { return m_recvContext; }
    RecvBuffer& GetRecvBuffer()      { return m_recvBuffer; }

    uint64_t GetSessionId() const { return m_sessionId; }
    void     SetSessionId(uint64_t id) { m_sessionId = id; }

    const std::string& GetAccountId() const { return m_accountId; }
    void SetAccountId(const std::string& id) { m_accountId = id; }

    GameRoom* GetRoom() const { return m_room.load(std::memory_order_acquire); }
    void      SetRoom(GameRoom* room) { m_room.store(room, std::memory_order_release); }

    // --- 하트비트 ---
    // Recv 완료 시마다 호출해 마지막 활동 시간을 갱신합니다.
    void UpdateActivity()
    {
        using namespace std::chrono;
        m_lastActivityMs.store(
            duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count(),
            std::memory_order_relaxed);
    }

    // HeartbeatThread가 주기적으로 호출. timeoutMs 동안 아무 패킷도 없으면 true.
    bool IsTimedOut(int64_t timeoutMs) const
    {
        using namespace std::chrono;
        int64_t now = duration_cast<milliseconds>(
            steady_clock::now().time_since_epoch()).count();
        return (now - m_lastActivityMs.load(std::memory_order_relaxed)) > timeoutMs;
    }

    // --- 전송 ---

    // Protobuf 메시지 전송 (PacketTraits<T>로 ID를 컴파일 타임에 결정)
    template<typename T>
    bool Send(const T& message)
    {
        return SendInternal(static_cast<uint16_t>(PacketTraits<T>::ID), message);
    }

    // 이미 직렬화된 공유 버퍼를 그대로 WSASend.
    // BroadcastShared에서 한 번 직렬화한 버퍼를 N개 세션에 전달할 때 사용.
    bool SendShared(std::shared_ptr<std::vector<char>> packet);

    // Protobuf 없이 원시 바이너리 전송 (Ping/Pong, RoomList 등)
    bool SendRaw(uint16_t packetId, const void* data, int dataSize);

private:
    bool SendInternal(uint16_t packetId, const google::protobuf::Message& message);
    void PostSend(IOContext* sendContext);

    SOCKET       m_sock   = INVALID_SOCKET;
    sockaddr_in  m_addr   = {};
    std::atomic<bool> m_disconnected{ false };

    uint64_t    m_sessionId  = 0;
    std::string m_accountId;
    std::atomic<GameRoom*> m_room{ nullptr };

    // 마지막 수신 시각 (ms, steady_clock 기준)
    std::atomic<int64_t> m_lastActivityMs{ 0 };

    static constexpr int RECV_BUF_CAPACITY = 8192;
    RecvBuffer m_recvBuffer{ RECV_BUF_CAPACITY };
    IOContext* m_recvContext = nullptr;

    friend class ServerCore;
};

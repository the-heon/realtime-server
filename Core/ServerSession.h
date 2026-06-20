#pragma once
#include <winsock2.h>
#include <memory>
#include <atomic>
#include <cstdint>
#include <string>
#include "../Packet/RecvBuffer.h"
#include "../Packet/PacketHeader.h"
#include "../Packet/PacketID.h"
#include "../Utils/ObjectPool.h"

// I/O 작업 유형
enum class EIOType { RECV, SEND, ACCEPT };

class ServerSession;
class GameRoom;

struct IOContext
{
    OVERLAPPED overlapped = {};
    EIOType ioType = EIOType::RECV;

    // shared_ptr로 들고 있으면, 이 I/O가 아직 완료되지 않은 동안에는 세션이
    // 절대 삭제되지 않습니다 - 수동 레퍼런스 카운트가 따로 필요 없습니다.
    std::shared_ptr<ServerSession> session;

    WSABUF wsaBuf = {};

    // 전송용 데이터를 직접 들고 있을 버퍼 (수신은 RecvBuffer를 직접 WSARecv 타깃으로 씁니다)
    static constexpr int MAX_BUF_SIZE = 4096;
    char buffer[MAX_BUF_SIZE] = {};

    // ACCEPT 전용: AcceptEx로 미리 만들어 둔 클라이언트 소켓
    SOCKET acceptSocket = INVALID_SOCKET;

    IOContext(EIOType type, std::shared_ptr<ServerSession> owner);

    IOContext(const IOContext&) = delete;
    IOContext& operator=(const IOContext&) = delete;

    // ObjectPool<IOContext>가 재사용할 때 호출합니다 (생성자와 같은 시그니처).
    void Reset(EIOType type, std::shared_ptr<ServerSession> owner);
};

// IOContext들을 재사용하는 전역 풀 (Core/ServerCore.cpp에서 정의)
extern ObjectPool<IOContext> g_ioContextPool;

// 세션 클래스.
// 항상 std::make_shared<ServerSession>()로 만들고, enable_shared_from_this를 통해
// 자기 자신을 가리키는 shared_ptr을 IOContext에 넘겨줍니다. 그래야 "이 세션으로
// 진행 중인 I/O가 다 끝나기 전엔 절대 삭제되지 않는다"가 자동으로 보장됩니다.
class ServerSession : public std::enable_shared_from_this<ServerSession>
{
public:
    ServerSession();
    ~ServerSession();

    // make_shared로 생성한 직후 호출하세요 (shared_from_this()를 쓰기 위해
    // 생성자 안에서는 recv IOContext를 만들 수 없습니다).
    void Init(SOCKET socket, const sockaddr_in& addr);

    // 연결을 끊습니다. 여러 스레드에서 동시에 호출돼도 소켓은 한 번만 닫힙니다.
    // 반환값 true는 "이 호출이 처음으로 끊은 것"이라는 뜻입니다.
    bool Disconnect();
    bool IsDisconnected() const { return m_disconnected.load(); }

    SOCKET GetSocket() const { return m_sock; }
    IOContext* GetRecvContext() { return m_recvContext; }
    RecvBuffer& GetRecvBuffer() { return m_recvBuffer; }

    uint64_t GetSessionId() const { return m_sessionId; }
    void SetSessionId(uint64_t id) { m_sessionId = id; }

    const std::string& GetAccountId() const { return m_accountId; }
    void SetAccountId(const std::string& accountId) { m_accountId = accountId; }

    // GameRoom의 틱 스레드가 쓰고, 패킷을 처리하는 IOCP 워커 스레드가 읽으므로 atomic.
    GameRoom* GetRoom() const { return m_room.load(std::memory_order_acquire); }
    void SetRoom(GameRoom* room) { m_room.store(room, std::memory_order_release); }

    // PacketTraits<T>::ID로 헤더(packetId/packetSize)를 채워서 비동기 전송합니다.
    template<typename T>
    bool Send(const T& message)
    {
        return SendInternal(static_cast<uint16_t>(PacketTraits<T>::ID), message);
    }

private:
    bool SendInternal(uint16_t packetId, const google::protobuf::Message& message);
    void PostSend(IOContext* sendContext);

    SOCKET m_sock = INVALID_SOCKET;
    sockaddr_in m_addr = {};
    std::atomic<bool> m_disconnected{ false };

    uint64_t m_sessionId = 0;
    std::string m_accountId;
    std::atomic<GameRoom*> m_room{ nullptr };

    static constexpr int RECV_BUF_CAPACITY = 8192;
    RecvBuffer m_recvBuffer{ RECV_BUF_CAPACITY };
    IOContext* m_recvContext = nullptr;

    friend class ServerCore;
};

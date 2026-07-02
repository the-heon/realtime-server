#pragma once
#include <string>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <utility>
#include <chrono>
#include "../Utils/JobQueue.h"
#include "../Utils/Logger.h"
#include "../Core/ServerSession.h"
#include "../Packet/PacketID.h"
#include "../Packet/PacketHeader.h"
#include "../Packet/Logic/Packet.pb.h"

enum class RoomState : uint8_t
{
    WAITING  = 0,  // 대기실 - 입장/레디 가능
    IN_GAME  = 1,  // 게임 진행 중
    FINISHED = 2,  // 결과 표시 (5초 후 자동 WAITING 전환)
};

// 하나의 "게임 룸" = 참가자 목록 + 매 틱 처리할 Job 큐 + 틱마다 보내는 상태 브로드캐스트.
//
// 멤버십(m_members)과 보류 중인 이동(m_pendingMoves)은 Tick()을 호출하는 단 하나의
// 스레드에서만 접근합니다. 다른 스레드(IOCP 워커)는 절대 이 두 멤버를 직접 건드리지
// 않고, 항상 PushJob()으로 "틱 스레드가 대신 실행해 줄 일"을 예약합니다.
// 그래서 룸 내부에는 락이 전혀 없습니다 - JobQueue 자체의 락만 존재합니다.
class GameRoom
{
public:
    explicit GameRoom(std::string name, int maxPlayers = 16)
        : m_name(std::move(name)), m_maxPlayers(maxPlayers) {}

    const std::string& GetName()       const { return m_name; }
    int                GetMaxPlayers() const { return m_maxPlayers; }
    size_t             MemberCount()   const { return m_members.size(); }
    RoomState          GetState()      const { return m_state; }

    void PushJob(Job job) { m_jobQueue.Push(std::move(job)); }

    void Tick()
    {
        m_jobQueue.Flush();

        if (m_state == RoomState::FINISHED) {
            if (NowMs() - m_finishedAt > 5000) {
                m_state = RoomState::WAITING;
                BroadcastRoomState();
                LOG_INFO("[Room:" << m_name << "] reset to WAITING");
            }
            return;
        }

        BroadcastPendingMoves();
    }

    void HandleEnter(const std::shared_ptr<ServerSession>& session)
    {
        if (m_state == RoomState::IN_GAME) {
            Protocol::S_ROOM_ENTER ack;
            ack.set_success(false);
            ack.set_message("게임이 진행 중입니다.");
            session->Send(ack);
            return;
        }
        if (static_cast<int>(m_members.size()) >= m_maxPlayers) {
            Protocol::S_ROOM_ENTER ack;
            ack.set_success(false);
            ack.set_message("방이 꽉 찼습니다. (" + std::to_string(m_maxPlayers) + "명 제한)");
            session->Send(ack);
            return;
        }

        m_members.insert(session);
        session->SetRoom(this);

        Protocol::S_ROOM_ENTER ack;
        ack.set_success(true);
        ack.set_message("입장 성공");
        ack.set_room_name(m_name);
        session->Send(ack);

        Protocol::S_ROOM_USER_ENTERED notice;
        notice.set_account_id(session->GetAccountId());
        BroadcastExcept(notice, session);

        BroadcastRoomState();
    }

    void HandleLeave(const std::shared_ptr<ServerSession>& session)
    {
        if (m_members.erase(session) == 0) return;
        session->SetRoom(nullptr);
        m_pendingMoves.erase(session->GetAccountId());
        m_readyPlayers.erase(session->GetAccountId());

        Protocol::S_ROOM_USER_LEFT notice;
        notice.set_account_id(session->GetAccountId());
        Broadcast(notice);

        // IN_GAME 도중 이탈 → 남은 플레이어가 자동 승리
        if (m_state == RoomState::IN_GAME && !m_members.empty()) {
            HandleGameEnd((*m_members.begin())->GetAccountId());
            return;
        }

        BroadcastRoomState();
    }

    void HandleMove(const std::string& accountId, float x, float y)
    {
        if (m_state != RoomState::IN_GAME) return;
        m_pendingMoves[accountId] = { x, y };
    }

    // C_READY 수신 시 호출 - ready 상태 토글
    void HandleReady(const std::string& accountId)
    {
        if (m_state != RoomState::WAITING) return;

        if (m_readyPlayers.count(accountId)) {
            m_readyPlayers.erase(accountId);
        } else {
            m_readyPlayers.insert(accountId);
        }

        BroadcastRoomState();
        TryStartGame();
    }

    // 단일 수신자 또는 소규모 전송
    template<typename T>
    void Broadcast(const T& message)
    {
        for (const auto& session : m_members) {
            session->Send(message);
        }
    }

    template<typename T>
    void BroadcastExcept(const T& message, const std::shared_ptr<ServerSession>& exclude)
    {
        for (const auto& session : m_members) {
            if (session != exclude) session->Send(message);
        }
    }

    // 브로드캐스트 최적화: 메시지를 한 번만 직렬화하고 공유 버퍼를 N개 세션에 전달
    template<typename T>
    void BroadcastShared(const T& message)
    {
        if (m_members.empty()) return;

        int payloadSize = static_cast<int>(message.ByteSizeLong());
        int totalSize   = static_cast<int>(sizeof(PacketHeader)) + payloadSize;

        auto buf = std::make_shared<std::vector<char>>(totalSize);
        PacketHeader* header = reinterpret_cast<PacketHeader*>(buf->data());
        header->packetSize   = static_cast<uint16_t>(payloadSize);
        header->packetId     = static_cast<uint16_t>(PacketTraits<T>::ID);
        message.SerializeToArray(buf->data() + sizeof(PacketHeader), payloadSize);

        for (const auto& session : m_members) {
            session->SendShared(buf);
        }
    }

    // Raw 바이너리 브로드캐스트 (Ping/RoomState/GameStart/GameResult 등)
    void SendRawBroadcast(uint16_t packetId, const void* data, int dataSize)
    {
        for (const auto& session : m_members) {
            session->SendRaw(packetId, data, dataSize);
        }
    }

private:
    static int64_t NowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // 모든 플레이어가 레디이고 최소 2명 이상이면 게임 시작
    void TryStartGame()
    {
        if (m_members.size() < 2) return;
        if (m_readyPlayers.size() < m_members.size()) return;

        m_state = RoomState::IN_GAME;
        m_readyPlayers.clear();

        SendRawBroadcast(static_cast<uint16_t>(PacketID::S_GAME_START), nullptr, 0);
        BroadcastRoomState();

        LOG_INFO("[Room:" << m_name << "] game started (" << m_members.size() << " players)");
    }

    void HandleGameEnd(const std::string& winnerId)
    {
        m_state      = RoomState::FINISHED;
        m_finishedAt = NowMs();

        // S_GAME_RESULT: [uint16 winnerLen][char* winnerId]
        std::vector<uint8_t> data;
        auto nameLen = static_cast<uint16_t>(winnerId.size());
        data.push_back(nameLen & 0xFF);
        data.push_back((nameLen >> 8) & 0xFF);
        for (char c : winnerId) data.push_back(static_cast<uint8_t>(c));

        SendRawBroadcast(static_cast<uint16_t>(PacketID::S_GAME_RESULT),
                         data.data(), static_cast<int>(data.size()));
        BroadcastRoomState();

        LOG_INFO("[Room:" << m_name << "] game ended, winner: " << winnerId);
    }

    // S_ROOM_STATE: [uint8 state][uint16 totalPlayers][uint16 readyCount]
    void BroadcastRoomState()
    {
        if (m_members.empty()) return;

        uint8_t data[5];
        data[0] = static_cast<uint8_t>(m_state);
        auto total = static_cast<uint16_t>(m_members.size());
        auto ready = static_cast<uint16_t>(m_readyPlayers.size());
        data[1] = total & 0xFF;  data[2] = (total >> 8) & 0xFF;
        data[3] = ready & 0xFF;  data[4] = (ready >> 8) & 0xFF;

        SendRawBroadcast(static_cast<uint16_t>(PacketID::S_ROOM_STATE), data, sizeof(data));
    }

    void BroadcastPendingMoves()
    {
        if (m_pendingMoves.empty()) return;

        Protocol::S_MOVE_BROADCAST snapshot;
        for (auto& [accountId, pos] : m_pendingMoves) {
            Protocol::MovedActor* actor = snapshot.add_actors();
            actor->set_account_id(accountId);
            actor->set_x(pos.first);
            actor->set_y(pos.second);
        }
        m_pendingMoves.clear();

        BroadcastShared(snapshot);
    }

    std::string m_name;
    int         m_maxPlayers;
    JobQueue    m_jobQueue;

    RoomState m_state      = RoomState::WAITING;
    int64_t   m_finishedAt = 0;

    std::unordered_set<std::string>                                    m_readyPlayers; // 틱 스레드 전용
    std::unordered_set<std::shared_ptr<ServerSession>>                 m_members;      // 틱 스레드 전용
    std::unordered_map<std::string, std::pair<float, float>>           m_pendingMoves; // 틱 스레드 전용
};

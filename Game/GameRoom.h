#pragma once
#include <string>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <utility>
#include "../Utils/JobQueue.h"
#include "../Core/ServerSession.h"
#include "../Packet/PacketID.h"
#include "../Packet/PacketHeader.h"
#include "../Packet/Logic/Packet.pb.h"

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

    void PushJob(Job job) { m_jobQueue.Push(std::move(job)); }

    void Tick()
    {
        m_jobQueue.Flush();
        BroadcastPendingMoves();
    }

    void HandleEnter(const std::shared_ptr<ServerSession>& session)
    {
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
    }

    void HandleLeave(const std::shared_ptr<ServerSession>& session)
    {
        if (m_members.erase(session) == 0) return;
        session->SetRoom(nullptr);
        m_pendingMoves.erase(session->GetAccountId());

        Protocol::S_ROOM_USER_LEFT notice;
        notice.set_account_id(session->GetAccountId());
        BroadcastExcept(notice, session);
    }

    void HandleMove(const std::string& accountId, float x, float y)
    {
        m_pendingMoves[accountId] = { x, y };
    }

    // 단일 수신자 또는 소규모 전송: 메시지를 각 세션마다 개별 직렬화
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

    // 브로드캐스트 최적화: 메시지를 한 번만 직렬화하고 공유 버퍼를 N개 세션에 전달.
    // 틱마다 호출되는 S_MOVE_BROADCAST처럼 모든 멤버에게 동일한 내용을 보낼 때 사용.
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

private:
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

        BroadcastShared(snapshot); // 한 번만 직렬화
    }

    std::string m_name;
    int         m_maxPlayers;
    JobQueue    m_jobQueue;

    std::unordered_set<std::shared_ptr<ServerSession>>              m_members;      // 틱 스레드 전용
    std::unordered_map<std::string, std::pair<float, float>>        m_pendingMoves; // 틱 스레드 전용
};

#pragma once
#include <string>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <utility>
#include "../Utils/JobQueue.h"
#include "../Core/ServerSession.h"
#include "../Packet/Logic/Packet.pb.h"

// 하나의 "게임 룸" = 참가자 목록 + 매 틱 처리할 Job 큐 + 틱마다 보내는 상태 브로드캐스트.
//
// 멤버십(m_members)과 보류 중인 이동(m_pendingMoves)은 Tick()을 호출하는 단 하나의
// 스레드에서만 접근합니다. 다른 스레드(IOCP 워커)는 절대 이 두 멤버를 직접 건드리지
// 않고, 항상 PushJob()으로 "틱 스레드가 대신 실행해 줄 일"을 예약합니다.
// 그래서 룸 내부에는 락이 전혀 없습니다 - JobQueue 자체의 락만 존재합니다.
//
// Job 람다는 항상 shared_ptr<ServerSession>을 값으로 캡처합니다. 그래서 큐에 들어간
// 뒤에 클라이언트가 끊기더라도, 그 Job이 실제로 실행될 때까지는 세션이 살아 있습니다.
class GameRoom
{
public:
    explicit GameRoom(std::string name) : m_name(std::move(name)) {}

    const std::string& GetName() const { return m_name; }

    void PushJob(Job job) { m_jobQueue.Push(std::move(job)); }

    // 틱 스레드에서 주기적으로 호출합니다: 쌓인 Job 실행 -> 이동 스냅샷 브로드캐스트.
    void Tick()
    {
        m_jobQueue.Flush();
        BroadcastPendingMoves();
    }

    // 아래 Handle*들은 모두 "틱 스레드 안에서" 실행된다는 전제입니다.
    // 직접 호출하지 말고 PushJob([this, session]{ HandleEnter(session); }) 형태로
    // 예약하세요 (RoomManager/Logic 핸들러가 그렇게 합니다).
    void HandleEnter(const std::shared_ptr<ServerSession>& session)
    {
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
        if (m_members.erase(session) == 0) {
            return; // 이미 나간 세션
        }
        session->SetRoom(nullptr);
        m_pendingMoves.erase(session->GetAccountId());

        Protocol::S_ROOM_USER_LEFT notice;
        notice.set_account_id(session->GetAccountId());
        BroadcastExcept(notice, session);
    }

    void HandleMove(const std::string& accountId, float x, float y)
    {
        // 같은 틱 안에서 여러 번 이동해도 마지막 위치만 브로드캐스트하면 되므로
        // 그냥 덮어씁니다.
        m_pendingMoves[accountId] = { x, y };
    }

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
            if (session != exclude) {
                session->Send(message);
            }
        }
    }

    size_t MemberCount() const { return m_members.size(); }

private:
    void BroadcastPendingMoves()
    {
        if (m_pendingMoves.empty()) {
            return;
        }

        Protocol::S_MOVE_BROADCAST snapshot;
        for (auto& [accountId, pos] : m_pendingMoves) {
            Protocol::MovedActor* actor = snapshot.add_actors();
            actor->set_account_id(accountId);
            actor->set_x(pos.first);
            actor->set_y(pos.second);
        }
        m_pendingMoves.clear();

        Broadcast(snapshot);
    }

    std::string m_name;
    JobQueue m_jobQueue;

    std::unordered_set<std::shared_ptr<ServerSession>> m_members;             // 틱 스레드 전용
    std::unordered_map<std::string, std::pair<float, float>> m_pendingMoves;  // 틱 스레드 전용
};

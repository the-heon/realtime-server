#pragma once
#include <unordered_map>
#include <memory>
#include <mutex>
#include <cstdint>

class ServerSession;

// 접속 중인 모든 세션을 sessionId로 추적합니다.
// shared_ptr로 들고 있으므로, 여기서 Remove()해도 다른 곳(예: GameRoom의 보류
// Job)이 여전히 shared_ptr을 들고 있다면 세션은 그 일이 끝날 때까지 살아 있습니다.
class SessionManager
{
public:
    static SessionManager& Instance();

    // 세션을 등록하고 새로 발급한 sessionId를 반환합니다.
    uint64_t Add(const std::shared_ptr<ServerSession>& session);
    void Remove(uint64_t sessionId);
    std::shared_ptr<ServerSession> Find(uint64_t sessionId);
    size_t Count() const;

private:
    SessionManager() = default;

    mutable std::mutex m_lock;
    std::unordered_map<uint64_t, std::shared_ptr<ServerSession>> m_sessions;
    uint64_t m_nextSessionId = 1;
};

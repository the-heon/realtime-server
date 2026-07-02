#pragma once
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <cstdint>

class ServerSession;

// 접속 중인 모든 세션을 sessionId로 추적합니다.
class SessionManager
{
public:
    static SessionManager& Instance();

    uint64_t Add(const std::shared_ptr<ServerSession>& session);
    void     Remove(uint64_t sessionId);
    std::shared_ptr<ServerSession> Find(uint64_t sessionId);
    size_t   Count() const;

    // HeartbeatThread가 타임아웃 검사를 위해 세션 스냅샷을 가져갈 때 사용.
    std::vector<std::shared_ptr<ServerSession>> GetAll() const;

private:
    SessionManager() = default;

    mutable std::mutex m_lock;
    std::unordered_map<uint64_t, std::shared_ptr<ServerSession>> m_sessions;
    uint64_t m_nextSessionId = 1;
};

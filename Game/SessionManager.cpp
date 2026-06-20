#include "SessionManager.h"
#include "../Core/ServerSession.h"

SessionManager& SessionManager::Instance()
{
    static SessionManager instance;
    return instance;
}

uint64_t SessionManager::Add(const std::shared_ptr<ServerSession>& session)
{
    std::lock_guard<std::mutex> lock(m_lock);
    uint64_t id = m_nextSessionId++;
    session->SetSessionId(id);
    m_sessions.emplace(id, session);
    return id;
}

void SessionManager::Remove(uint64_t sessionId)
{
    std::lock_guard<std::mutex> lock(m_lock);
    m_sessions.erase(sessionId);
}

std::shared_ptr<ServerSession> SessionManager::Find(uint64_t sessionId)
{
    std::lock_guard<std::mutex> lock(m_lock);
    auto it = m_sessions.find(sessionId);
    return it != m_sessions.end() ? it->second : nullptr;
}

size_t SessionManager::Count() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return m_sessions.size();
}

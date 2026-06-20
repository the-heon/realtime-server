#include "RoomManager.h"
#include <vector>

RoomManager& RoomManager::Instance()
{
    static RoomManager instance;
    return instance;
}

GameRoom* RoomManager::GetOrCreate(const std::string& name)
{
    std::lock_guard<std::mutex> lock(m_lock);
    auto it = m_rooms.find(name);
    if (it != m_rooms.end()) {
        return it->second.get();
    }
    auto room = std::make_unique<GameRoom>(name);
    GameRoom* raw = room.get();
    m_rooms.emplace(name, std::move(room));
    return raw;
}

GameRoom* RoomManager::Find(const std::string& name)
{
    std::lock_guard<std::mutex> lock(m_lock);
    auto it = m_rooms.find(name);
    return it != m_rooms.end() ? it->second.get() : nullptr;
}

void RoomManager::TickAll()
{
    std::vector<GameRoom*> rooms;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        rooms.reserve(m_rooms.size());
        for (auto& [name, room] : m_rooms) {
            rooms.push_back(room.get());
        }
    }
    // 락을 들고 있지 않은 상태에서 Tick() 호출 - GetOrCreate()가 그 사이 다른
    // 룸을 추가해도 막히지 않습니다.
    for (GameRoom* room : rooms) {
        room->Tick();
    }
}

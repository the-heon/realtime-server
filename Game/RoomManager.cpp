#include "RoomManager.h"
#include "../Utils/Config.h"
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
    if (it != m_rooms.end()) return it->second.get();

    int maxPlayers = Config::Instance().Get().maxRoomPlayers;
    auto room = std::make_unique<GameRoom>(name, maxPlayers);
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
    for (GameRoom* room : rooms) room->Tick();
}

std::vector<RoomInfo> RoomManager::GetRoomInfoList() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    std::vector<RoomInfo> list;
    list.reserve(m_rooms.size());
    for (const auto& [name, room] : m_rooms) {
        list.push_back({ name,
                         static_cast<int>(room->MemberCount()),
                         room->GetMaxPlayers() });
    }
    return list;
}

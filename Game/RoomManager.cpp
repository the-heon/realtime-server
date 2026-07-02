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

void RoomManager::TickAll(int reconnectWindowMs)
{
    std::vector<GameRoom*> rooms;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        rooms.reserve(m_rooms.size());
        for (auto& [name, room] : m_rooms) {
            rooms.push_back(room.get());
        }
    }
    for (GameRoom* room : rooms) room->Tick(reconnectWindowMs);
}

int RoomManager::RemoveIdleRooms(int64_t idleMs)
{
    std::lock_guard<std::mutex> lock(m_lock);
    int removed = 0;
    using namespace std::chrono;
    int64_t now = duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count();

    for (auto it = m_rooms.begin(); it != m_rooms.end(); ) {
        int64_t emptySince = it->second->EmptySinceMs();
        if (emptySince > 0 && (now - emptySince) > idleMs) {
            it = m_rooms.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

GameRoom* RoomManager::FindReconnect(const std::string& accountId)
{
    std::lock_guard<std::mutex> lock(m_lock);
    for (auto& [name, room] : m_rooms) {
        if (room->HasPendingReconnect(accountId)) {
            return room.get();
        }
    }
    return nullptr;
}

std::vector<RoomInfo> RoomManager::GetRoomInfoList() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    std::vector<RoomInfo> list;
    list.reserve(m_rooms.size());
    for (const auto& [name, room] : m_rooms) {
        list.push_back({ name,
                         static_cast<int>(room->MemberCount()),
                         room->GetMaxPlayers(),
                         room->GetState() });
    }
    return list;
}

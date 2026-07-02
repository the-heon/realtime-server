#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include "GameRoom.h"

struct RoomInfo
{
    std::string name;
    int         playerCount;
    int         maxPlayers;
    RoomState   state;
};

// 이름으로 룸을 찾거나 없으면 만들어주는 레지스트리.
// 모든 룸의 Tick()을 한 스레드에서 순서대로 돌리는 TickAll()도 여기서 제공합니다.
class RoomManager
{
public:
    static RoomManager& Instance();

    GameRoom* GetOrCreate(const std::string& name);
    GameRoom* Find(const std::string& name);

    void TickAll(int reconnectWindowMs = 30000);

    // 현재 존재하는 모든 룸의 정보 스냅샷 (C_ROOM_LIST 응답용)
    std::vector<RoomInfo> GetRoomInfoList() const;

    // idleMs 동안 아무도 없는 룸을 제거합니다 (HeartbeatThread에서 호출)
    int RemoveIdleRooms(int64_t idleMs);

    // accountId에 재접속 슬롯이 있는 룸을 반환합니다 (없으면 nullptr)
    GameRoom* FindReconnect(const std::string& accountId);

private:
    RoomManager() = default;

    mutable std::mutex m_lock;
    std::unordered_map<std::string, std::unique_ptr<GameRoom>> m_rooms;
};

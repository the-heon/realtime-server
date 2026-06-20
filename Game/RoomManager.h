#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include "GameRoom.h"

// 이름으로 룸을 찾거나 없으면 만들어주는 레지스트리.
// 모든 룸의 Tick()을 한 스레드에서 순서대로 돌리는 TickAll()도 여기서 제공합니다 -
// "룸 상태는 틱 스레드 하나만 건드린다"는 전제를 룸이 여러 개여도 유지하기 위함입니다.
class RoomManager
{
public:
    static RoomManager& Instance();

    GameRoom* GetOrCreate(const std::string& name);
    GameRoom* Find(const std::string& name);

    void TickAll();

private:
    RoomManager() = default;

    std::mutex m_lock;
    std::unordered_map<std::string, std::unique_ptr<GameRoom>> m_rooms;
};

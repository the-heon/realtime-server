#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>

class ServerSession;

// 패킷 처리 함수의 시그니처.
// shared_ptr로 받는 이유: 핸들러가 GameRoom::PushJob처럼 처리를 나중으로
// 미루는 경우, 그 람다가 세션을 shared_ptr로 캡처해서 안전하게 들고 있어야 합니다.
using PacketHandlerFunc = std::function<void(const std::shared_ptr<ServerSession>&, char*, int)>;

class PacketManager
{
public:
    PacketManager() = default;

    // 특정 PacketID에 대한 처리 함수(핸들러)를 등록
    void RegisterHandler(uint16_t packetId, PacketHandlerFunc handler);

    // 분리된 패킷을 받아 ID에 따라 핸들러를 호출하는 Dispatcher 역할
    void HandlePacket(const std::shared_ptr<ServerSession>& session, char* packetData, int size);

private:
    std::map<uint16_t, PacketHandlerFunc> m_handlers;
};

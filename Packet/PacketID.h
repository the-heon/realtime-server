#pragma once
#include <cstdint>
#include "Logic/Packet.pb.h"

// 패킷 ID: PacketHeader.packetId 에 들어가는 값.
// 메시지 타입이 늘어나면 여기에 추가하고, 아래 PacketTraits 특수화도 같이 추가합니다.
enum class PacketID : uint16_t
{
    C_LOGIN = 1,
    S_LOGIN = 2,
    C_CHAT = 3,
    S_CHAT = 4,
    C_ROOM_ENTER = 5,
    S_ROOM_ENTER = 6,
    C_ROOM_LEAVE = 7,
    S_ROOM_LEAVE = 8,
    S_ROOM_USER_ENTERED = 9,
    S_ROOM_USER_LEFT = 10,
    C_MOVE = 11,
    S_MOVE_BROADCAST = 12,
};

// 프로토버프 메시지 타입 <-> PacketID 매핑.
// ServerSession::Send<T>()가 T로부터 PacketID를 컴파일 타임에 알아내기 위해 사용합니다.
template<typename T>
struct PacketTraits;

#define DECLARE_PACKET_TRAITS(MessageType, EnumValue) \
    template<> struct PacketTraits<Protocol::MessageType> { \
        static constexpr PacketID ID = PacketID::EnumValue; \
    };

DECLARE_PACKET_TRAITS(C_LOGIN, C_LOGIN)
DECLARE_PACKET_TRAITS(S_LOGIN, S_LOGIN)
DECLARE_PACKET_TRAITS(C_CHAT, C_CHAT)
DECLARE_PACKET_TRAITS(S_CHAT, S_CHAT)
DECLARE_PACKET_TRAITS(C_ROOM_ENTER, C_ROOM_ENTER)
DECLARE_PACKET_TRAITS(S_ROOM_ENTER, S_ROOM_ENTER)
DECLARE_PACKET_TRAITS(C_ROOM_LEAVE, C_ROOM_LEAVE)
DECLARE_PACKET_TRAITS(S_ROOM_LEAVE, S_ROOM_LEAVE)
DECLARE_PACKET_TRAITS(S_ROOM_USER_ENTERED, S_ROOM_USER_ENTERED)
DECLARE_PACKET_TRAITS(S_ROOM_USER_LEFT, S_ROOM_USER_LEFT)
DECLARE_PACKET_TRAITS(C_MOVE, C_MOVE)
DECLARE_PACKET_TRAITS(S_MOVE_BROADCAST, S_MOVE_BROADCAST)

#undef DECLARE_PACKET_TRAITS

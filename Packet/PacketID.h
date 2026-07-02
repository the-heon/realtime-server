#pragma once
#include <cstdint>
#include "Logic/Packet.pb.h"

// 패킷 ID: PacketHeader.packetId에 들어가는 값.
// 메시지 타입이 늘어나면 여기에 추가하고, Protobuf 기반 메시지는
// 아래 PacketTraits 특수화도 같이 추가합니다.
enum class PacketID : uint16_t
{
    // --- 기존 Protobuf 기반 패킷 ---
    C_LOGIN            = 1,
    S_LOGIN            = 2,
    C_CHAT             = 3,
    S_CHAT             = 4,
    C_ROOM_ENTER       = 5,
    S_ROOM_ENTER       = 6,
    C_ROOM_LEAVE       = 7,
    S_ROOM_LEAVE       = 8,
    S_ROOM_USER_ENTERED = 9,
    S_ROOM_USER_LEFT   = 10,
    C_MOVE             = 11,
    S_MOVE_BROADCAST   = 12,

    // --- Raw 바이너리 패킷 (Protobuf 미사용) ---
    // C_PING payload: [uint64 clientTimestampMs]
    // S_PONG payload: [uint64 clientTimestampMs][uint64 serverTimestampMs]
    C_PING             = 13,
    S_PONG             = 14,

    // C_ROOM_LIST payload: 없음
    // S_ROOM_LIST payload: [uint16 count]
    //   per room: [uint16 nameLen][char* name][uint16 playerCount][uint16 maxPlayers]
    C_ROOM_LIST        = 15,
    S_ROOM_LIST        = 16,

    // --- 게임 상태 머신 패킷 ---
    // C_READY payload: 없음 (ready 토글)
    // S_ROOM_STATE payload: [uint8 state][uint16 totalPlayers][uint16 readyCount]
    //   state: 0=WAITING, 1=IN_GAME, 2=FINISHED
    C_READY            = 17,
    S_ROOM_STATE       = 18,

    // S_GAME_START payload: 없음 (모든 플레이어 준비 완료 → 게임 시작 신호)
    S_GAME_START       = 19,

    // S_GAME_RESULT payload: [uint16 winnerLen][char* winnerId]
    S_GAME_RESULT      = 20,
};

// Protobuf 메시지 타입 <-> PacketID 매핑 (ServerSession::Send<T>() 에서 사용)
template<typename T>
struct PacketTraits;

#define DECLARE_PACKET_TRAITS(MessageType, EnumValue) \
    template<> struct PacketTraits<Protocol::MessageType> { \
        static constexpr PacketID ID = PacketID::EnumValue; \
    };

DECLARE_PACKET_TRAITS(C_LOGIN,             C_LOGIN)
DECLARE_PACKET_TRAITS(S_LOGIN,             S_LOGIN)
DECLARE_PACKET_TRAITS(C_CHAT,              C_CHAT)
DECLARE_PACKET_TRAITS(S_CHAT,              S_CHAT)
DECLARE_PACKET_TRAITS(C_ROOM_ENTER,        C_ROOM_ENTER)
DECLARE_PACKET_TRAITS(S_ROOM_ENTER,        S_ROOM_ENTER)
DECLARE_PACKET_TRAITS(C_ROOM_LEAVE,        C_ROOM_LEAVE)
DECLARE_PACKET_TRAITS(S_ROOM_LEAVE,        S_ROOM_LEAVE)
DECLARE_PACKET_TRAITS(S_ROOM_USER_ENTERED, S_ROOM_USER_ENTERED)
DECLARE_PACKET_TRAITS(S_ROOM_USER_LEFT,    S_ROOM_USER_LEFT)
DECLARE_PACKET_TRAITS(C_MOVE,              C_MOVE)
DECLARE_PACKET_TRAITS(S_MOVE_BROADCAST,    S_MOVE_BROADCAST)

#undef DECLARE_PACKET_TRAITS

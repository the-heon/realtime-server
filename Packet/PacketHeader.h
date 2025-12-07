#include "pch.h"
struct PacketHeader
{
    // 패킷 본문(Payload)의 크기 (Protobuf 직렬화 데이터 크기)
    uint16_t packetSize; 

    // 패킷의 고유 ID (PacketID enum 값)
    uint16_t packetId;
};
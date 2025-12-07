  
#include "Core/ServerCore.h"

// <functional>, <map>, "ServerCore.h" 등은 pch.h에 포함되므로 #include 하지 않습니다.
// CSession 클래스는 ServerCore.h에 정의되어 있으며, pch.h를 통해 인식됩니다.

// PacketHandlerFunc: 패킷 처리 함수의 시그니처 정의
// (CSession*, Protobuf 본문 데이터 포인터, 본문 크기)를 전달받음
// std::function은 pch.h를 통해 인식됩니다.
using PacketHandlerFunc = std::function<void(CSession*, char*, int)>; 

class CPacketManager
{
public:
    CPacketManager();

    // 특정 PacketID에 대한 처리 함수(핸들러)를 등록
    void RegisterHandler(uint16_t packetId, PacketHandlerFunc handler);

    // 분리된 패킷을 받아 ID에 따라 핸들러를 호출하는 Dispatcher 역할
    void HandlePacket(CSession* session, char* packetData, int size); 

private:
    std::map<uint16_t, PacketHandlerFunc> m_handlers; // std::map은 pch.h를 통해 인식됩니다.
};

// ==========================================================
// 예시: 실제 패킷 처리 함수 선언 (Logic Layer에서 구현)
// ==========================================================
// 이 함수들은 Logic Layer의 .cpp 파일에 구현되며, 그 파일에서 "Packet.pb.h"를 include 합니다.
void Handle_C_LOGIN(CSession* session, char* payload, int payloadSize);
void Handle_C_CHAT(CSession* session, char* payload, int payloadSize);
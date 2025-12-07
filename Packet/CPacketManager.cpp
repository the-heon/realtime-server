#include "CPacketManager.h" 
#include "PacketHeader.h" 
// CSession, std::map, std::function, uint16_t는 pch.h를 통해 인식됩니다.


// ==========================================================
// CPacketManager 구현
// ==========================================================

// 🚀 생성자
CPacketManager::CPacketManager()
{
    // 생성자에서는 특별한 초기화가 필요 없습니다.
    // m_handlers 맵은 자동으로 초기화됩니다.
}

// 📦 핸들러 등록
// 특정 PacketID에 대한 처리 함수(핸들러)를 맵에 등록합니다.
void CPacketManager::RegisterHandler(uint16_t packetId, PacketHandlerFunc handler)
{
    // emplace를 사용하여 맵에 (packetId, handler) 쌍을 삽입합니다.
    m_handlers.emplace(packetId, handler);
}

// 🧠 패킷 처리 (Dispatcher 역할)
// 수신된 전체 패킷 데이터에서 ID를 추출하여 등록된 핸들러를 호출합니다.
void CPacketManager::HandlePacket(CSession* session, char* packetData, int size)
{
    // 패킷의 최소 크기 검사: 적어도 헤더 크기 이상이어야 합니다.
    if (size < sizeof(PacketHeader))
    {
        // 로그 출력 또는 에러 처리 (필요에 따라)
        // 예를 들어: LOG(Error, "Packet size too small: %d", size);
        return; 
    }

    // 1. 헤더 추출
    // 수신된 데이터의 시작 주소를 PacketHeader 구조체로 캐스팅하여 ID를 읽습니다.
    PacketHeader* header = reinterpret_cast<PacketHeader*>(packetData);
    
    // 2. 패킷 ID 및 본문 크기 확인
    uint16_t packetId = header->packetId;
    int payloadSize = size - sizeof(PacketHeader); // 전체 크기 - 헤더 크기 = 본문 크기
    char* payload = packetData + sizeof(PacketHeader); // 본문 데이터의 시작 주소

    // 3. 등록된 핸들러 검색
    auto it = m_handlers.find(packetId);

    // 4. 핸들러 존재 여부 확인 및 호출
    if (it != m_handlers.end())
    {
        // 핸들러가 존재하면 호출
        // it->second는 등록된 PacketHandlerFunc 함수 객체입니다.
        // CSession*, Protobuf 본문 데이터 포인터, 본문 크기를 전달합니다.
        it->second(session, payload, payloadSize);
    }
    else
    {
        // 해당 ID에 대한 핸들러가 등록되지 않았을 경우 (미처리 패킷)
        // 예를 들어: LOG(Warning, "Unhandled Packet ID: %d", packetId);
    }
}


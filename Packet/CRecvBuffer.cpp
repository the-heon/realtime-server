#include "CRecvBuffer.h"
#include "PacketHeader.h"
bool CRecvBuffer::TryGetPacket(char*& outData, int& outSize)
{
    // 1. 현재 버퍼에 최소한 헤더 크기만큼의 데이터가 있는지 확인
    if (GetDataSize() < sizeof(PacketHeader)) {
        return false;
    }

    // 2. 헤더 읽기 (현재 tail 위치에서)
    PacketHeader* header = (PacketHeader*)(m_data.data() + m_tail);
    
    // 3. 전체 패킷 크기 확인 (헤더 크기 + 본문 크기)
    int totalPacketSize = sizeof(PacketHeader) + header->packetSize; 
    
    // 4. 전체 패킷이 버퍼에 모두 도착했는지 확인
    if (GetDataSize() < totalPacketSize) {
        return false; // 아직 다 못 받음
    }

    // 5. 패킷 분리 준비
    outData = (char*)(m_data.data() + m_tail);
    outSize = totalPacketSize;
    return true; // 패킷 하나 완성
}
#include "RecvBuffer.h"
#include "PacketHeader.h"
#include <cstring>

RecvBuffer::RecvBuffer(int capacity)
    : m_capacity(capacity), m_data(capacity), m_head(0), m_tail(0)
{
}

void RecvBuffer::Write(int size)
{
    m_head += size;
}

bool RecvBuffer::TryGetPacket(char*& outData, int& outSize)
{
    // 1. 현재 버퍼에 최소한 헤더 크기만큼의 데이터가 있는지 확인
    if (GetDataSize() < (int)sizeof(PacketHeader)) {
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

void RecvBuffer::Pop(int size)
{
    m_tail += size;
    if (m_tail == m_head) {
        // 버퍼를 전부 읽었으면 처음으로 되돌려 다음 압축을 피합니다.
        m_tail = 0;
        m_head = 0;
    }
}

int RecvBuffer::GetDataSize() const
{
    return m_head - m_tail;
}

char* RecvBuffer::GetWriteBufferPtr()
{
    // 남은 쓰기 공간이 없으면 먼저 압축해서 공간을 확보합니다.
    if (m_head >= m_capacity) {
        Compact();
    }
    return m_data.data() + m_head;
}

int RecvBuffer::GetWriteBufferSize() const
{
    return m_capacity - m_head;
}

void RecvBuffer::Compact()
{
    int dataSize = GetDataSize();
    if (m_tail > 0 && dataSize > 0) {
        std::memmove(m_data.data(), m_data.data() + m_tail, dataSize);
    }
    m_tail = 0;
    m_head = dataSize;
}

#pragma once
#include <vector>

class RecvBuffer
{
public:
    // 생성자: 버퍼 용량을 지정하여 초기화
    explicit RecvBuffer(int capacity);

    // IOCP 수신 완료 시 호출: 실제로 받은 크기(size)만큼 쓰기 위치(m_head)를 이동
    void Write(int size); // WSARecv 완료 시 호출

    // 버퍼에서 완전한 패킷 1개를 분리하여 데이터 포인터와 크기를 반환
    bool TryGetPacket(char*& outData, int& outSize);

    // 패킷 소비 후, 버퍼의 읽기 위치(m_tail)를 이동
    void Pop(int size);

    // 현재 버퍼에 쌓인 데이터 크기 반환
    int GetDataSize() const;

    // WSARecv를 위한 쓰기 가능한 공간의 포인터와 크기를 반환.
    // 공간이 부족하면 내부적으로 압축(Compact)한 뒤 포인터를 돌려주므로,
    // 항상 GetWriteBufferPtr() -> GetWriteBufferSize() 순서로 호출해야 합니다.
    char* GetWriteBufferPtr();
    int GetWriteBufferSize() const;

private:
    // 읽은 데이터([0, m_tail))를 버리고 남은 데이터([m_tail, m_head))를 앞으로 당깁니다.
    void Compact();

    const int m_capacity; // 총 버퍼 용량 (const로 변경)
    std::vector<char> m_data; // 실제 데이터 저장 공간
    int m_head; // 쓰기 위치 (데이터가 들어오는 곳)
    int m_tail; // 읽기 위치 (데이터가 나가는 곳)
};

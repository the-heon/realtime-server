#include "pch.h"
class CRecvBuffer
{
public:
    // 생성자: 버퍼 용량을 지정하여 초기화
    CRecvBuffer(int capacity);

    // IOCP 수신 완료 시 호출: 실제로 받은 크기(size)만큼 쓰기 위치(m_head)를 이동
    void Write(int size); // WSARecv 완료 시 호출

    // 버퍼에서 완전한 패킷 1개를 분리하여 데이터 포인터와 크기를 반환
    bool TryGetPacket(char*& outData, int& outSize); 

    // 패킷 소비 후, 버퍼의 읽기 위치(m_tail)를 이동
    void Pop(int size);
    
    // 현재 버퍼에 쌓인 데이터 크기 반환
    int GetDataSize() const; 

    // WSARecv를 위한 쓰기 가능한 공간의 포인터와 크기를 반환
    char* GetWriteBufferPtr();
    int GetWriteBufferSize() const;

private:
    const int m_capacity; // 총 버퍼 용량 (const로 변경)
    std::vector<char> m_data; // 실제 데이터 저장 공간
    int m_head; // 쓰기 위치 (데이터가 들어오는 곳)
    int m_tail; // 읽기 위치 (데이터가 나가는 곳)
};
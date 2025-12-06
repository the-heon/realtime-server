// MemoryPool.h
#include "pch.h"

// I/O 타입 정의
enum class IoType { RECV, SEND, ACCEPT };

// OVERLAPPED 구조체 확장 (재사용될 객체)
struct COverlappedEx
{
    OVERLAPPED overlapped;
    WSABUF wsaBuf;
    IoType type;
    int bufferSize;
    char buffer[4096]; // 임시 버퍼 공간 (실제 환경에선 별도 버퍼 풀 사용 권장)

    void Reset() {
        ZeroMemory(&overlapped, sizeof(overlapped));
        wsaBuf.buf = buffer;
        wsaBuf.len = sizeof(buffer);
        type = IoType::RECV; // 기본은 수신
    }
};

class CMemoryPool
{
public:
    CMemoryPool(int initialCount) {
        for (int i = 0; i < initialCount; ++i) {
            auto obj = new COverlappedEx();
            m_pool.push(obj);
        }
    }

    ~CMemoryPool() {
        while (!m_pool.empty()) {
            delete m_pool.front();
            m_pool.pop();
        }
    }

    COverlappedEx* Allocate() {
        if (m_pool.empty()) {
            // 풀이 비면 새로 생성 (실제 서버는 여기서 더 많이 확보)
            return new COverlappedEx();
        }
        COverlappedEx* obj = m_pool.front();
        m_pool.pop();
        obj->Reset();
        return obj;
    }

    void Deallocate(COverlappedEx* obj) {
        m_pool.push(obj);
    }

private:
    std::queue<COverlappedEx*> m_pool;
};
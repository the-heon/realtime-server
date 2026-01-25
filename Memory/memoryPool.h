#pragma once
#include <queue>
#include <mutex>
#include "ServerSession.h" // IOContext 정의를 위해 필요

class MemoryPool
{
public:
    MemoryPool(int initialCount) {
        for (int i = 0; i < initialCount; ++i) {
            // IOContext는 생성 시 type과 owner(session)가 필요하지만, 
            // 풀에 미리 담아둘 때는 초기값이 중요하지 않으므로 기본값으로 생성합니다.
            auto obj = new IOContext(EIOType::RECV, nullptr);
            m_pool.push(obj);
        }
    }

    ~MemoryPool() {
        std::lock_guard<std::mutex> lock(m_lock);
        while (!m_pool.empty()) {
            delete m_pool.front();
            m_pool.pop();
        }
    }

    // 풀에서 하나 꺼내오기
    IOContext* Allocate(EIOType type, ServerSession* session) {
        std::lock_guard<std::mutex> lock(m_lock);
        IOContext* obj = nullptr;

        if (m_pool.empty()) {
            obj = new IOContext(type, session);
        } else {
            obj = m_pool.front();
            m_pool.pop();
            // 꺼낸 객체를 현재 용도에 맞게 리셋
            memset(&obj->overlapped, 0, sizeof(OVERLAPPED));
            obj->ioType = type;
            obj->serverSession = session;
        }
        return obj;
    }

    // 다 쓴 객체 반납하기
    void Deallocate(IOContext* obj) {
        if (obj == nullptr) return;
        std::lock_guard<std::mutex> lock(m_lock);
        obj->serverSession = nullptr; // 세션 연결 해제
        m_pool.push(obj);
    }

private:
    std::mutex m_lock;
    std::queue<IOContext*> m_pool;
};
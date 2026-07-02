#pragma once
#include <queue>
#include <memory>
#include "Lock.h"

// 아무 타입이나 재사용 가능한 범용 오브젝트 풀.
// T는 T(Args...) 생성자와, 재사용 시 같은 Args...로 상태를 초기화하는
// void Reset(Args...) 멤버 함수를 가지고 있어야 합니다.
//
// std::mutex 대신 SpinLock을 씁니다: 풀의 임계 구간은 큐 push/pop 하나뿐으로
// 매우 짧고, 고빈도 IOCP 워커 스레드들이 동시에 접근하므로 컨텍스트 스위치 없이
// 스핀하는 편이 실질 처리량에 유리합니다.
template<typename T>
class ObjectPool
{
public:
    ObjectPool() = default;

    ~ObjectPool()
    {
        std::lock_guard<SpinLock> lk(m_lock);
        while (!m_pool.empty()) {
            delete m_pool.front();
            m_pool.pop();
        }
    }

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    template<typename... Args>
    T* Allocate(Args&&... args)
    {
        std::lock_guard<SpinLock> lk(m_lock);
        if (m_pool.empty()) {
            return new T(std::forward<Args>(args)...);
        }
        T* obj = m_pool.front();
        m_pool.pop();
        obj->Reset(std::forward<Args>(args)...);
        return obj;
    }

    void Deallocate(T* obj)
    {
        if (obj == nullptr) return;
        std::lock_guard<SpinLock> lk(m_lock);
        m_pool.push(obj);
    }

    size_t Size() const
    {
        std::lock_guard<SpinLock> lk(m_lock);
        return m_pool.size();
    }

private:
    mutable SpinLock  m_lock;
    std::queue<T*>    m_pool;
};

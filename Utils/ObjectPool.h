#pragma once
#include <queue>
#include <mutex>
#include <memory>

// 아무 타입이나 재사용 가능한 범용 오브젝트 풀.
// T는 T(Args...) 생성자와, 재사용 시 같은 Args...로 상태를 초기화하는
// void Reset(Args...) 멤버 함수를 가지고 있어야 합니다.
template<typename T>
class ObjectPool
{
public:
    // T가 기본 생성자를 갖지 않을 수도 있어서(예: IOContext) 미리 채워두지 않고,
    // 처음 필요한 시점에 Allocate()의 Args...로 생성합니다.
    ObjectPool() = default;

    ~ObjectPool()
    {
        std::lock_guard<std::mutex> lock(m_lock);
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
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_pool.empty()) {
            return new T(args...);
        }
        T* obj = m_pool.front();
        m_pool.pop();
        obj->Reset(args...);
        return obj;
    }

    void Deallocate(T* obj)
    {
        if (obj == nullptr) return;
        std::lock_guard<std::mutex> lock(m_lock);
        m_pool.push(obj);
    }

    size_t Size() const
    {
        std::lock_guard<std::mutex> lock(m_lock);
        return m_pool.size();
    }

private:
    mutable std::mutex m_lock;
    std::queue<T*> m_pool;
};

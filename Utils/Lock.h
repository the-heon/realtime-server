#pragma once
#include <atomic>
#include <thread>

// 짧은 critical section을 위한 스핀락. std::mutex보다 컨텍스트 스위치 비용이
// 없어서, JobQueue/ObjectPool처럼 아주 짧게 잠그고 푸는 자료구조에 적합합니다.
// 오래 점유되는 락(DB I/O 등)에는 절대 쓰지 마세요 - CPU를 계속 태웁니다.
class SpinLock
{
public:
    void Lock()
    {
        while (m_flag.exchange(true, std::memory_order_acquire)) {
            // 스핀 중 다른 스레드에게 양보해서 CPU를 덜 태움
            std::this_thread::yield();
        }
    }

    void Unlock()
    {
        m_flag.store(false, std::memory_order_release);
    }

    // std::lock_guard / std::unique_lock 과 함께 쓰기 위한 BasicLockable 인터페이스
    void lock() { Lock(); }
    void unlock() { Unlock(); }

private:
    std::atomic<bool> m_flag{ false };
};

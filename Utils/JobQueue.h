#pragma once
#include <functional>
#include <queue>
#include "Lock.h"

// 여러 IOCP 워커 스레드가 동시에 Push할 수 있지만, Flush()는 항상 그 Job들의
// "주인" 스레드(예: 한 GameRoom의 틱 스레드) 하나만 호출한다는 전제의 큐입니다.
// 이렇게 하면 룸 상태(참가자 목록, 위치 등)를 건드리는 코드는 그 룸의 틱
// 스레드 안에서만 실행되므로, 룸 내부 로직 자체에는 락이 필요 없어집니다.
//
// 임계 구간이 큐 push/swap 하나뿐이므로 SpinLock으로 교체해 mutex 오버헤드를 줄입니다.
using Job = std::function<void()>;

class JobQueue
{
public:
    void Push(Job job)
    {
        std::lock_guard<SpinLock> lk(m_lock);
        m_jobs.push(std::move(job));
    }

    // 현재 쌓여 있는 Job을 모두 꺼내서 순서대로 실행합니다.
    // Flush 실행 도중 Push된 Job은 다음 Flush에서 처리됩니다.
    void Flush()
    {
        std::queue<Job> jobs;
        {
            std::lock_guard<SpinLock> lk(m_lock);
            std::swap(jobs, m_jobs);
        }
        while (!jobs.empty()) {
            jobs.front()();
            jobs.pop();
        }
    }

    bool Empty() const
    {
        std::lock_guard<SpinLock> lk(m_lock);
        return m_jobs.empty();
    }

private:
    mutable SpinLock  m_lock;
    std::queue<Job>   m_jobs;
};

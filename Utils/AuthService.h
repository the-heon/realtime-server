#pragma once
#include <string>
#include <functional>
#include <queue>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

// session-server에 비동기로 토큰을 검증하는 서비스.
//
// IOCP 워커 스레드가 C_LOGIN을 받으면, 이 서비스에 검증 Job을 맡기고 즉시 리턴합니다.
// 내부 스레드 풀이 session-server를 HTTP로 호출한 뒤 콜백으로 결과를 돌려줍니다.
// 이렇게 하면 HTTP 레이턴시 동안 IOCP 워커 스레드가 블록되지 않습니다.
//
// C_LOGIN 패킷의 password 필드를 session_token으로 사용합니다.
// (protoc 재생성 없이 기존 proto 구조를 재활용하는 방법)
class AuthService
{
public:
    // valid: 토큰 유효 여부, accountId: session-server가 확인한 계정 ID
    using Callback = std::function<void(bool valid, std::string accountId)>;

    static AuthService& Instance()
    {
        static AuthService svc;
        return svc;
    }

    // 서버 시작 시 호출. workerCount만큼 전용 스레드를 생성합니다.
    void Start(int workerCount = 4);

    // 서버 종료 시 호출. 큐에 남은 Job을 모두 처리한 뒤 스레드를 정지합니다.
    void Stop();

    // token 검증을 큐에 등록합니다. cb는 검증 완료 시 AuthService 스레드에서 호출됩니다.
    void ValidateAsync(const std::string& token, Callback cb);

    // 현재 대기 중인 검증 Job 수
    size_t PendingCount() const;

private:
    AuthService() = default;
    void WorkerLoop();

    struct Job { std::string token; Callback cb; };

    mutable std::mutex      m_mutex;
    std::condition_variable m_cv;
    std::queue<Job>         m_queue;
    std::vector<std::thread> m_workers;
    std::atomic<bool>        m_running{ false };
};

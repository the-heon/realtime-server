#include "AuthService.h"
#include "HttpClient.h"
#include "Config.h"
#include "Logger.h"
#include <string>

// session-server 응답 예시:
//   {"valid":true,"account_id":"alice"}
//   {"valid":false}
static bool ParseValidateResponse(const std::string& body,
                                   std::string& outAccountId)
{
    // "valid":true 가 있으면 성공
    if (body.find("\"valid\":true") == std::string::npos) return false;

    // account_id 추출: "account_id":"<value>"
    const std::string key = "\"account_id\":\"";
    auto pos = body.find(key);
    if (pos == std::string::npos) return false;

    pos += key.size();
    auto end = body.find('"', pos);
    if (end == std::string::npos) return false;

    outAccountId = body.substr(pos, end - pos);
    return !outAccountId.empty();
}

void AuthService::Start(int workerCount)
{
    m_running.store(true);
    for (int i = 0; i < workerCount; ++i) {
        m_workers.emplace_back(&AuthService::WorkerLoop, this);
    }
    LOG_INFO("AuthService started with " << workerCount << " workers");
}

void AuthService::Stop()
{
    bool wasRunning = m_running.exchange(false);
    if (!wasRunning) return;

    m_cv.notify_all();
    for (auto& t : m_workers) {
        if (t.joinable()) t.join();
    }
    m_workers.clear();
    LOG_INFO("AuthService stopped");
}

void AuthService::ValidateAsync(const std::string& token, Callback cb)
{
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_queue.push({ token, std::move(cb) });
    }
    m_cv.notify_one();
}

size_t AuthService::PendingCount() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_queue.size();
}

void AuthService::WorkerLoop()
{
    const auto& cfg = Config::Instance().Get();

    while (true)
    {
        Job job;
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            m_cv.wait(lk, [this]() {
                return !m_queue.empty() || !m_running.load(std::memory_order_relaxed);
            });

            if (m_queue.empty()) break; // 종료 신호 + 큐 비어있음

            job = std::move(m_queue.front());
            m_queue.pop();
        }

        // session-server GET /auth/validate?token=<token>
        std::string path = "/auth/validate?token=" + job.token;
        auto resp = HttpClient::Get(cfg.sessionServerHost, cfg.sessionServerPort, path);

        bool valid = false;
        std::string accountId;

        if (resp.ok()) {
            valid = ParseValidateResponse(resp.body, accountId);
        } else {
            LOG_WARN("AuthService: validate failed, status=" << resp.statusCode
                     << " token=" << job.token.substr(0, 8) << "...");
        }

        job.cb(valid, accountId);
    }
}

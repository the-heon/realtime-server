#pragma once
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <chrono>
#include <ctime>

enum class LogLevel { Info, Warn, Error };

// 비동기 콘솔/파일 로거.
// 워커 스레드가 직접 cout/cerr을 잠그는 대신 메시지를 큐에만 push하고,
// 전용 백그라운드 스레드가 드레인한다 -> 워커 스레드 블록 최소화.
class Logger
{
public:
    static Logger& Instance()
    {
        static Logger instance;
        return instance;
    }

    ~Logger() { Shutdown(); }

    void SetLogFile(const std::string& path)
    {
        std::lock_guard<std::mutex> lk(m_fileMutex);
        m_file.open(path, std::ios::app);
    }

    void SetMinLevel(LogLevel level) { m_minLevel.store(level); }

    void Log(LogLevel level, const std::string& message)
    {
        if (level < m_minLevel.load(std::memory_order_relaxed)) return;

        std::string entry = "[" + Timestamp() + "][" + LevelTag(level) + "] " + message + "\n";

        {
            std::lock_guard<std::mutex> lk(m_queueMutex);
            m_queue.push({ level, std::move(entry) });
        }
        m_cv.notify_one();
    }

    void Shutdown()
    {
        bool wasRunning = m_running.exchange(false);
        if (!wasRunning) return;
        m_cv.notify_all();
        if (m_worker.joinable()) m_worker.join();
    }

private:
    struct Entry { LogLevel level; std::string text; };

    Logger()
    {
        m_running.store(true);
        m_worker = std::thread([this]() { WorkerLoop(); });
    }

    void WorkerLoop()
    {
        while (true)
        {
            std::unique_lock<std::mutex> lk(m_queueMutex);
            m_cv.wait(lk, [this]() {
                return !m_queue.empty() || !m_running.load(std::memory_order_relaxed);
            });

            std::queue<Entry> batch;
            std::swap(batch, m_queue);
            lk.unlock();

            while (!batch.empty())
            {
                auto& e = batch.front();
                (e.level == LogLevel::Error ? std::cerr : std::cout) << e.text;

                {
                    std::lock_guard<std::mutex> fl(m_fileMutex);
                    if (m_file.is_open()) m_file << e.text;
                }

                batch.pop();
            }

            if (!m_running.load(std::memory_order_relaxed))
            {
                // 종료 전 남은 메시지를 한 번 더 확인
                std::lock_guard<std::mutex> lk2(m_queueMutex);
                if (m_queue.empty()) break;
            }
        }
    }

    static const char* LevelTag(LogLevel level)
    {
        switch (level) {
            case LogLevel::Info:  return "INFO";
            case LogLevel::Warn:  return "WARN";
            case LogLevel::Error: return "ERROR";
        }
        return "?";
    }

    static std::string Timestamp()
    {
        auto now = std::chrono::system_clock::now();
        std::time_t t  = std::chrono::system_clock::to_time_t(now);
        std::tm tm = {};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buf[32] = {};
        std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
        return buf;
    }

    std::atomic<bool>     m_running{false};
    std::atomic<LogLevel> m_minLevel{LogLevel::Info};

    std::mutex              m_queueMutex;
    std::condition_variable m_cv;
    std::queue<Entry>       m_queue;

    std::mutex    m_fileMutex;
    std::ofstream m_file;

    std::thread m_worker;
};

#define LOG_INFO(msg)  do { std::ostringstream _oss; _oss << msg; Logger::Instance().Log(LogLevel::Info,  _oss.str()); } while(0)
#define LOG_WARN(msg)  do { std::ostringstream _oss; _oss << msg; Logger::Instance().Log(LogLevel::Warn,  _oss.str()); } while(0)
#define LOG_ERROR(msg) do { std::ostringstream _oss; _oss << msg; Logger::Instance().Log(LogLevel::Error, _oss.str()); } while(0)

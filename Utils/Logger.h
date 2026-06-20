#pragma once
#include <iostream>
#include <sstream>
#include <mutex>
#include <chrono>
#include <ctime>

enum class LogLevel { Info, Warn, Error };

// 여러 워커 스레드가 동시에 로그를 찍어도 한 줄이 섞이지 않도록 보장하는 간단한 콘솔 로거.
class Logger
{
public:
    static Logger& Instance()
    {
        static Logger instance;
        return instance;
    }

    void Log(LogLevel level, const std::string& message)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::ostream& out = (level == LogLevel::Error) ? std::cerr : std::cout;
        out << "[" << Timestamp() << "][" << LevelTag(level) << "] " << message << std::endl;
    }

private:
    Logger() = default;

    static const char* LevelTag(LogLevel level)
    {
        switch (level) {
            case LogLevel::Info:  return "INFO";
            case LogLevel::Warn:  return "WARN";
            case LogLevel::Error: return "ERROR";
        }
        return "?";
    }

    // Timestamp()는 항상 Log()의 락 안에서만 호출되므로 localtime()의
    // 비-재진입성은 문제가 되지 않습니다.
    static std::string Timestamp()
    {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm* tmBuf = std::localtime(&t);
        char buf[32] = {};
        std::strftime(buf, sizeof(buf), "%H:%M:%S", tmBuf);
        return buf;
    }

    std::mutex m_mutex;
};

#define LOG_INFO(msg) do { std::ostringstream oss; oss << msg; Logger::Instance().Log(LogLevel::Info, oss.str()); } while (0)
#define LOG_WARN(msg) do { std::ostringstream oss; oss << msg; Logger::Instance().Log(LogLevel::Warn, oss.str()); } while (0)
#define LOG_ERROR(msg) do { std::ostringstream oss; oss << msg; Logger::Instance().Log(LogLevel::Error, oss.str()); } while (0)

#pragma once
#include <string>
#include <unordered_map>
#include <fstream>

struct ServerConfig
{
    int port                   = 7777;
    int workerThreadMultiplier = 2;
    int tickIntervalMs         = 33;
    int heartbeatTimeoutMs     = 30000;
    int maxRoomPlayers         = 16;
    int statsPrintIntervalMs   = 10000;
    std::string logFile        = "";

    // session-server 연동 (토큰 검증용 내부망 직접 호출)
    std::string sessionServerHost    = "127.0.0.1";
    int         sessionServerPort    = 8080;
    int         authWorkerCount      = 4;    // AuthService 전용 스레드 수
    int         authTimeoutMs        = 3000; // session-server HTTP 타임아웃
};

// key=value 형식 설정 파일 로더. 파일이 없으면 기본값 유지.
// 주석은 #으로 시작, 공백은 자동 trim.
class Config
{
public:
    static Config& Instance()
    {
        static Config cfg;
        return cfg;
    }

    bool Load(const std::string& path)
    {
        std::ifstream f(path);
        if (!f.is_open()) return false;

        std::string line;
        while (std::getline(f, line))
        {
            if (line.empty() || line[0] == '#') continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            m_values[Trim(line.substr(0, eq))] = Trim(line.substr(eq + 1));
        }

        Apply();
        return true;
    }

    const ServerConfig& Get() const { return m_cfg; }

private:
    void Apply()
    {
        auto getInt = [&](const std::string& k, int fallback) -> int {
            auto it = m_values.find(k);
            if (it == m_values.end() || it->second.empty()) return fallback;
            try { return std::stoi(it->second); } catch (...) { return fallback; }
        };
        auto getStr = [&](const std::string& k) -> std::string {
            auto it = m_values.find(k);
            return it != m_values.end() ? it->second : "";
        };

        m_cfg.port                   = getInt("port",                    m_cfg.port);
        m_cfg.workerThreadMultiplier = getInt("worker_thread_multiplier", m_cfg.workerThreadMultiplier);
        m_cfg.tickIntervalMs         = getInt("tick_interval_ms",         m_cfg.tickIntervalMs);
        m_cfg.heartbeatTimeoutMs     = getInt("heartbeat_timeout_ms",     m_cfg.heartbeatTimeoutMs);
        m_cfg.maxRoomPlayers         = getInt("max_room_players",         m_cfg.maxRoomPlayers);
        m_cfg.statsPrintIntervalMs   = getInt("stats_print_interval_ms",  m_cfg.statsPrintIntervalMs);
        m_cfg.logFile                = getStr("log_file");
        m_cfg.sessionServerHost      = getStr("session_server_host").empty()
                                           ? m_cfg.sessionServerHost
                                           : getStr("session_server_host");
        m_cfg.sessionServerPort      = getInt("session_server_port",      m_cfg.sessionServerPort);
        m_cfg.authWorkerCount        = getInt("auth_worker_count",        m_cfg.authWorkerCount);
        m_cfg.authTimeoutMs          = getInt("auth_timeout_ms",          m_cfg.authTimeoutMs);
    }

    static std::string Trim(const std::string& s)
    {
        const char* ws = " \t\r\n";
        size_t a = s.find_first_not_of(ws);
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(ws);
        return s.substr(a, b - a + 1);
    }

    std::unordered_map<std::string, std::string> m_values;
    ServerConfig m_cfg;
};

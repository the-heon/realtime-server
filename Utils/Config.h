#pragma once
#include <string>
#include <unordered_map>
#include <fstream>
#include <cstdlib>

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
    int         authWorkerCount      = 4;
    int         authTimeoutMs        = 3000;

    // 자가 등록 (session-server의 게임서버 레지스트리에 등록)
    std::string externalHost         = "127.0.0.1"; // 클라이언트에게 노출될 IP
    int         maxTotalPlayers      = 500;          // 레지스트리 등록 시 사용

    // 헬스체크 HTTP 서버
    int healthServerPort             = 8081;

    // 보안 / 패킷 가드
    int maxPacketSize                = 4096; // bytes, 초과 시 세션 강제 종료
    int packetRateLimitPerSec        = 100;  // 초당 최대 패킷 수 (burst = *2)

    // 룸 GC
    int idleRoomTimeoutMs            = 60000; // 빈 룸 유지 시간 (ms)
    int reconnectWindowMs            = 30000; // 재접속 허용 시간 (ms)

    // control-plane 연동 (api-gateway를 통해 api-server heartbeat 보고)
    std::string apiGatewayHost       = "127.0.0.1";
    int         apiGatewayPort       = 5000;
    std::string gameServerApiKey     = "";
    std::string gameServerVersion    = "dev";
    int         controlPlaneTimeoutMs = 3000;
    int         heartbeatReportIntervalMs = 10000;
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

        auto getStrOrDefault = [&](const std::string& k, const std::string& def) -> std::string {
            auto it = m_values.find(k);
            return (it != m_values.end() && !it->second.empty()) ? it->second : def;
        };
        m_cfg.externalHost           = getStrOrDefault("external_host",          m_cfg.externalHost);
        m_cfg.maxTotalPlayers        = getInt("max_total_players",               m_cfg.maxTotalPlayers);
        m_cfg.healthServerPort       = getInt("health_server_port",              m_cfg.healthServerPort);
        m_cfg.maxPacketSize          = getInt("max_packet_size",                 m_cfg.maxPacketSize);
        m_cfg.packetRateLimitPerSec  = getInt("packet_rate_limit_per_sec",       m_cfg.packetRateLimitPerSec);
        m_cfg.idleRoomTimeoutMs      = getInt("idle_room_timeout_ms",            m_cfg.idleRoomTimeoutMs);
        m_cfg.reconnectWindowMs      = getInt("reconnect_window_ms",             m_cfg.reconnectWindowMs);
        m_cfg.apiGatewayHost         = getStrOrDefault("api_gateway_host",       m_cfg.apiGatewayHost);
        m_cfg.apiGatewayPort         = getInt("api_gateway_port",                m_cfg.apiGatewayPort);
        m_cfg.gameServerApiKey       = getStrOrDefault("game_server_api_key",    m_cfg.gameServerApiKey);
        m_cfg.gameServerVersion      = getStrOrDefault("game_server_version",    m_cfg.gameServerVersion);
        m_cfg.controlPlaneTimeoutMs  = getInt("control_plane_timeout_ms",        m_cfg.controlPlaneTimeoutMs);
        m_cfg.heartbeatReportIntervalMs = getInt("heartbeat_report_interval_ms", m_cfg.heartbeatReportIntervalMs);

        // 배포 환경에서는 파일 대신 환경변수로 API 키를 주입할 수 있게 허용
        if (m_cfg.gameServerApiKey.empty()) {
            const char* envApiKey = std::getenv("GAME_SERVER_API_KEY");
            if (envApiKey != nullptr) {
                m_cfg.gameServerApiKey = envApiKey;
            }
        }
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

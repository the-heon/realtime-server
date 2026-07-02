#include "HealthServer.h"
#include "Logger.h"
#include <ws2tcpip.h>
#include <string>

void HealthServer::Start(int port, MetricsFn fn)
{
    m_metricsFn = std::move(fn);
    m_running.store(true);

    m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listenSocket == INVALID_SOCKET) {
        LOG_ERROR("HealthServer: socket() failed");
        return;
    }

    BOOL reuse = TRUE;
    setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    // accept 타임아웃 설정 (1초) - Stop() 호출 후 빠르게 종료하기 위해
    DWORD tv = 1000;
    setsockopt(m_listenSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

    sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(static_cast<u_short>(port));

    if (bind(m_listenSocket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR ||
        listen(m_listenSocket, 8) == SOCKET_ERROR) {
        LOG_ERROR("HealthServer: bind/listen failed on port " << port);
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
        return;
    }

    LOG_INFO("HealthServer listening on port " << port);
    m_thread = std::thread([this]() { WorkerLoop(); });
}

void HealthServer::Stop()
{
    if (!m_running.exchange(false)) return;
    if (m_listenSocket != INVALID_SOCKET) {
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
    }
    if (m_thread.joinable()) m_thread.join();
}

void HealthServer::WorkerLoop()
{
    while (m_running.load()) {
        SOCKET client = accept(m_listenSocket, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue; // timeout or stop

        // 요청 전체를 읽지 않고 바로 응답 (헬스체크는 method/path 무관)
        std::string body = m_metricsFn ? m_metricsFn() : "{\"status\":\"ok\"}";

        std::string response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: " + std::to_string(body.size()) + "\r\n"
            "Connection: close\r\n"
            "\r\n" + body;

        send(client, response.c_str(), static_cast<int>(response.size()), 0);
        closesocket(client);
    }
}

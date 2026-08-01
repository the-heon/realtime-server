#include "HealthServer.h"
#include "Logger.h"
#include <ws2tcpip.h>
#include <string>

void HealthServer::Start(int port, HealthFn healthFn, MetricsFn metricsFn)
{
    m_healthFn = std::move(healthFn);
    m_metricsFn = std::move(metricsFn);
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

        char requestBuf[1024] = {};
        int received = recv(client, requestBuf, sizeof(requestBuf) - 1, 0);
        std::string request = received > 0 ? std::string(requestBuf, received) : "";

        bool metricsPath = request.rfind("GET /metrics", 0) == 0;
        std::string body;
        std::string contentType;
        if (metricsPath) {
            body = m_metricsFn ? m_metricsFn() : "";
            contentType = "text/plain; version=0.0.4";
        } else {
            body = m_healthFn ? m_healthFn() : "{\"status\":\"ok\"}";
            contentType = "application/json";
        }

        std::string response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: " + contentType + "\r\n"
            "Content-Length: " + std::to_string(body.size()) + "\r\n"
            "Connection: close\r\n"
            "\r\n" + body;

        send(client, response.c_str(), static_cast<int>(response.size()), 0);
        closesocket(client);
    }
}

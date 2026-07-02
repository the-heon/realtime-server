#pragma once
#include <winsock2.h>
#include <atomic>
#include <thread>
#include <string>
#include <functional>

// 로드밸런서/오케스트레이터용 최소 HTTP 헬스체크 서버.
// 별도 포트(기본 8081)에서 GET /health 요청을 받아 JSON으로 응답합니다.
// WinHTTP 없이 raw Winsock으로 구현 (수신 전용, 단일 스레드).
class HealthServer
{
public:
    using MetricsFn = std::function<std::string()>; // JSON body를 반환하는 콜백

    void Start(int port, MetricsFn fn);
    void Stop();

private:
    void WorkerLoop();

    SOCKET      m_listenSocket = INVALID_SOCKET;
    std::thread m_thread;
    std::atomic<bool> m_running{ false };
    MetricsFn   m_metricsFn;
};

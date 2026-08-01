#pragma once
#include <atomic>
#include <cstdint>
#include "Logger.h"

// 서버 전반의 통계를 원자적으로 추적한다.
// 워커 스레드가 lock 없이 업데이트할 수 있도록 모든 카운터를 atomic으로 선언.
class ServerStats
{
public:
    static ServerStats& Instance()
    {
        static ServerStats stats;
        return stats;
    }

    std::atomic<uint64_t> totalConnections{0};
    std::atomic<uint64_t> currentConnections{0};
    std::atomic<uint64_t> totalPacketsReceived{0};
    std::atomic<uint64_t> totalPacketsSent{0};
    std::atomic<uint64_t> totalBytesReceived{0};
    std::atomic<uint64_t> totalBytesSent{0};
    std::atomic<uint64_t> totalRateLimitRejections{0};
    std::atomic<uint64_t> totalOversizedPacketRejections{0};
    std::atomic<uint64_t> totalPolicyConnectionRejections{0};

    void Print() const
    {
        LOG_INFO("[STATS]"
            << " conns=" << currentConnections.load(std::memory_order_relaxed)
            << " total_conns=" << totalConnections.load(std::memory_order_relaxed)
            << " pkts_recv=" << totalPacketsReceived.load(std::memory_order_relaxed)
            << " pkts_sent=" << totalPacketsSent.load(std::memory_order_relaxed)
            << " bytes_recv=" << totalBytesReceived.load(std::memory_order_relaxed)
                << " bytes_sent=" << totalBytesSent.load(std::memory_order_relaxed)
                << " reject_rate=" << totalRateLimitRejections.load(std::memory_order_relaxed)
                    << " reject_oversize=" << totalOversizedPacketRejections.load(std::memory_order_relaxed)
                    << " reject_policy_conn=" << totalPolicyConnectionRejections.load(std::memory_order_relaxed));
    }

private:
    ServerStats() = default;
};

#define STATS_INC(field)        ServerStats::Instance().field.fetch_add(1,   std::memory_order_relaxed)
#define STATS_DEC(field)        ServerStats::Instance().field.fetch_sub(1,   std::memory_order_relaxed)
#define STATS_ADD(field, val)   ServerStats::Instance().field.fetch_add(val, std::memory_order_relaxed)

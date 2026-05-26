//network_monitor.h:
#pragma once

#include <cstdint>
#include <atomic>
#include <vector>
#include <deque>
#include <mutex>
#include <thread>
#include <chrono>
#include <string>

// Network quality monitoring for local network
class NetworkMonitor {
public:
    struct LinkMetrics {
        double latency = 0.0;              // ms
        double jitter = 0.0;               // ms
        double bandwidth = 0.0;            // bps
        int packetsSent = 0;
        int packetsReceived = 0;
        int packetsLost = 0;
        double packetLossRate = 0.0;       // 0.0 - 1.0
        int64_t totalBytesSent = 0;
        int64_t totalBytesReceived = 0;
    };

    struct NetworkQuality {
        enum class Grade {
            EXCELLENT = 5,  // RTT < 20ms, loss < 1%
            GOOD = 4,       // RTT < 50ms, loss < 3%
            FAIR = 3,       // RTT < 100ms, loss < 5%
            POOR = 2,       // RTT < 200ms, loss < 10%
            CRITICAL = 1    // RTT > 200ms or loss > 10%
        };

        Grade grade = Grade::GOOD;
        std::string description;
    };

    NetworkMonitor();
    ~NetworkMonitor();

    // Start monitoring
    bool StartMonitoring();
    void StopMonitoring();

    // Record packet event
    void RecordPacketSent(int64_t packetSize, int64_t timestamp = 0);
    void RecordPacketReceived(int64_t packetSize, int64_t timestamp = 0);
    void RecordPacketLoss(int count = 1);

    // Record RTT measurement
    void RecordRoundTripTime(double rttMs);

    // Get current metrics
    LinkMetrics GetMetrics() const;
    NetworkQuality GetNetworkQuality() const;

    // Network availability check
    bool IsNetworkAvailable() const;

    // Estimate available bandwidth
    double EstimateBandwidth();

    // Statistics
    struct MonitoringStats {
        int64_t uptimeSeconds = 0;
        int monitoringCycles = 0;
        double averageLatency = 0.0;
        double averageJitter = 0.0;
        double peakLatency = 0.0;
        double minLatency = 0.0;
        int64_t totalDataSent = 0;
        int64_t totalDataReceived = 0;
    };

    MonitoringStats GetStatistics() const;

private:
    // Packet tracking
    struct PacketRecord {
        int64_t timestamp;
        int64_t size;
        bool received;
    };

    std::deque<PacketRecord> packetHistory;
    std::deque<double> rttHistory;

    // Metrics
    LinkMetrics currentMetrics;
    mutable std::mutex metricsMutex;

    // Monitoring state
    std::atomic<bool> isMonitoring{ false };
    std::thread monitoringThread;

    // Statistics
    MonitoringStats statistics;

    // Helper functions
    void MonitoringLoop();
    void CalculateMetrics();
    NetworkQuality::Grade CalculateQualityGrade() const;
    double CalculateJitter();
    double EstimateBandwidthFromHistory();
};

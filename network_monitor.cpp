//network_monitor.cpp:
#include "network_monitor.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <iostream>

NetworkMonitor::NetworkMonitor() {
}

NetworkMonitor::~NetworkMonitor() {
    StopMonitoring();
}

bool NetworkMonitor::StartMonitoring() {
    if (isMonitoring.exchange(true)) {
        return false;  // Already monitoring
    }

    monitoringThread = std::thread([this] { MonitoringLoop(); });
    return true;
}

void NetworkMonitor::StopMonitoring() {
    if (!isMonitoring.exchange(false)) {
        return;
    }

    if (monitoringThread.joinable()) {
        monitoringThread.join();
    }
}

void NetworkMonitor::RecordPacketSent(int64_t packetSize, int64_t timestamp) {
    if (timestamp == 0) {
        timestamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    }

    {
        std::lock_guard<std::mutex> lock(metricsMutex);
        PacketRecord record;
        record.timestamp = timestamp;
        record.size = packetSize;
        record.received = false;

        packetHistory.push_back(record);
        currentMetrics.packetsSent++;
        currentMetrics.totalBytesSent += packetSize;

        // Keep history to last 1000 packets
        if (packetHistory.size() > 1000) {
            packetHistory.pop_front();
        }
    }
}

void NetworkMonitor::RecordPacketReceived(int64_t packetSize, int64_t timestamp) {
    if (timestamp == 0) {
        timestamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    }

    {
        std::lock_guard<std::mutex> lock(metricsMutex);
        PacketRecord record;
        record.timestamp = timestamp;
        record.size = packetSize;
        record.received = true;

        packetHistory.push_back(record);
        currentMetrics.packetsReceived++;
        currentMetrics.totalBytesReceived += packetSize;
    }
}

void NetworkMonitor::RecordPacketLoss(int count) {
    {
        std::lock_guard<std::mutex> lock(metricsMutex);
        currentMetrics.packetsLost += count;
    }
}

void NetworkMonitor::RecordRoundTripTime(double rttMs) {
    {
        std::lock_guard<std::mutex> lock(metricsMutex);
        rttHistory.push_back(rttMs);

        if (rttHistory.size() > 100) {
            rttHistory.pop_front();
        }

        // Update latency metrics
        if (!rttHistory.empty()) {
            currentMetrics.latency = rttHistory.back();
            statistics.peakLatency = *std::max_element(rttHistory.begin(), rttHistory.end());
            statistics.minLatency = *std::min_element(rttHistory.begin(), rttHistory.end());
        }
    }
}

NetworkMonitor::LinkMetrics NetworkMonitor::GetMetrics() const {
    std::lock_guard<std::mutex> lock(metricsMutex);
    return currentMetrics;
}

NetworkMonitor::NetworkQuality NetworkMonitor::GetNetworkQuality() const {
    NetworkQuality quality;
    quality.grade = CalculateQualityGrade();

    switch (quality.grade) {
        case NetworkQuality::Grade::EXCELLENT:
            quality.description = "Excellent - RTT < 20ms, Loss < 1%";
            break;
        case NetworkQuality::Grade::GOOD:
            quality.description = "Good - RTT < 50ms, Loss < 3%";
            break;
        case NetworkQuality::Grade::FAIR:
            quality.description = "Fair - RTT < 100ms, Loss < 5%";
            break;
        case NetworkQuality::Grade::POOR:
            quality.description = "Poor - RTT < 200ms, Loss < 10%";
            break;
        case NetworkQuality::Grade::CRITICAL:
            quality.description = "Critical - RTT > 200ms or Loss > 10%";
            break;
    }

    return quality;
}

bool NetworkMonitor::IsNetworkAvailable() const {
    std::lock_guard<std::mutex> lock(metricsMutex);
    return currentMetrics.packetsReceived > 0 || currentMetrics.packetsSent > 0;
}

double NetworkMonitor::EstimateBandwidth() {
    std::lock_guard<std::mutex> lock(metricsMutex);
    return EstimateBandwidthFromHistory();
}

NetworkMonitor::MonitoringStats NetworkMonitor::GetStatistics() const {
    std::lock_guard<std::mutex> lock(metricsMutex);
    return statistics;
}

void NetworkMonitor::MonitoringLoop() {
    while (isMonitoring.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        CalculateMetrics();
        statistics.monitoringCycles++;
    }
}

void NetworkMonitor::CalculateMetrics() {
    std::lock_guard<std::mutex> lock(metricsMutex);

    // Calculate packet loss rate
    if (currentMetrics.packetsSent > 0) {
        currentMetrics.packetLossRate = static_cast<double>(currentMetrics.packetsLost) / currentMetrics.packetsSent;
    }

    // Calculate jitter
    currentMetrics.jitter = CalculateJitter();

    // Estimate bandwidth
    currentMetrics.bandwidth = EstimateBandwidthFromHistory();

    // Update average latency
    if (!rttHistory.empty()) {
        double sum = std::accumulate(rttHistory.begin(), rttHistory.end(), 0.0);
        statistics.averageLatency = sum / rttHistory.size();
    }
}

NetworkMonitor::NetworkQuality::Grade NetworkMonitor::CalculateQualityGrade() const {
    if (currentMetrics.latency < 20 && currentMetrics.packetLossRate < 0.01) {
        return NetworkQuality::Grade::EXCELLENT;
    } else if (currentMetrics.latency < 50 && currentMetrics.packetLossRate < 0.03) {
        return NetworkQuality::Grade::GOOD;
    } else if (currentMetrics.latency < 100 && currentMetrics.packetLossRate < 0.05) {
        return NetworkQuality::Grade::FAIR;
    } else if (currentMetrics.latency < 200 && currentMetrics.packetLossRate < 0.10) {
        return NetworkQuality::Grade::POOR;
    }

    return NetworkQuality::Grade::CRITICAL;
}

double NetworkMonitor::CalculateJitter() {
    if (rttHistory.size() < 2) {
        return 0.0;
    }

    double mean = std::accumulate(rttHistory.begin(), rttHistory.end(), 0.0) / rttHistory.size();
    double variance = 0.0;

    for (double rtt : rttHistory) {
        variance += (rtt - mean) * (rtt - mean);
    }

    variance /= rttHistory.size();
    return std::sqrt(variance);
}

double NetworkMonitor::EstimateBandwidthFromHistory() {
    if (packetHistory.size() < 2) {
        return 0.0;
    }

    // Estimate bandwidth from last 100 packets
    int windowSize = std::min(100, static_cast<int>(packetHistory.size()));
    auto startIt = packetHistory.end() - windowSize;

    int64_t totalBytes = 0;
    int64_t timeDiff = 0;

    for (auto it = startIt; it != packetHistory.end(); ++it) {
        totalBytes += it->size;
    }

    if (startIt != packetHistory.end()) {
        timeDiff = packetHistory.back().timestamp - startIt->timestamp;
    }

    if (timeDiff > 0) {
        // Convert bytes/ns to bits/second
        double bandwidthBps = (totalBytes * 8.0 * 1e9) / timeDiff;
        return bandwidthBps;
    }

    return 0.0;
}

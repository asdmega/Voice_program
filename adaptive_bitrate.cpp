//adaptive_bitrate.cpp:
#include "adaptive_bitrate.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <iostream>

AdaptiveBitrateController::AdaptiveBitrateController() {
    lastUpdateTime = std::chrono::high_resolution_clock::now();
}

AdaptiveBitrateController::~AdaptiveBitrateController() {
}

bool AdaptiveBitrateController::Initialize(const BitrateConfig& cfg) {
    config = cfg;
    currentBitrate = config.startingBitrate;
    previousBitrate = config.startingBitrate;

    statistics.currentBitrate = currentBitrate;
    statistics.averageBitrate = currentBitrate;

    return true;
}

void AdaptiveBitrateController::UpdateMetrics(const NetworkMetrics& metrics) {
    {
        std::lock_guard<std::mutex> lock(statsMutex);
        CalculateBitrate(metrics);
    }

    updateCount++;
    if (updateCount % 10 == 0) {  // Every 10 updates
        AddToHistory();
    }
}

int AdaptiveBitrateController::GetRecommendedBitrate() const {
    std::lock_guard<std::mutex> lock(statsMutex);
    return statistics.currentBitrate;
}

AdaptiveBitrateController::CongestionLevel AdaptiveBitrateController::GetCongestionLevel() const {
    return lastCongestionLevel;
}

void AdaptiveBitrateController::SetBitrate(int bitrate) {
    currentBitrate = std::max(config.minBitrate, std::min(config.maxBitrate, bitrate));

    {
        std::lock_guard<std::mutex> lock(statsMutex);
        statistics.currentBitrate = currentBitrate;
        statistics.bitrateAdjustments++;
    }
}

AdaptiveBitrateController::CongestionLevel AdaptiveBitrateController::DetectCongestion(const NetworkMetrics& metrics) {
    // Detect congestion based on multiple factors

    double congestionScore = 0.0;

    // Packet loss is a strong indicator (weight: 40%)
    if (metrics.packetLossRate > 0.10) {
        congestionScore += 0.4;
    }
    else if (metrics.packetLossRate > 0.05) {
        congestionScore += 0.2;
    }
    else if (metrics.packetLossRate > 0.02) {
        congestionScore += 0.1;
    }

    // High RTT indicates congestion (weight: 30%)
    if (metrics.roundTripTime > 200) {  // 200ms
        congestionScore += 0.3;
    }
    else if (metrics.roundTripTime > 100) {  // 100ms
        congestionScore += 0.15;
    }
    else if (metrics.roundTripTime > 50) {  // 50ms
        congestionScore += 0.08;
    }

    // High jitter (weight: 20%)
    if (metrics.jitter > 50) {  // 50ms
        congestionScore += 0.2;
    }
    else if (metrics.jitter > 20) {  // 20ms
        congestionScore += 0.1;
    }

    // Frame drops (weight: 10%)
    if (metrics.framesEncoded > 0) {
        double dropRate = static_cast<double>(metrics.framesDropped) / metrics.framesEncoded;
        if (dropRate > 0.10) {
            congestionScore += 0.1;
        }
    }

    if (congestionScore > 0.7) {
        return CongestionLevel::CRITICAL;
    }
    else if (congestionScore > 0.5) {
        return CongestionLevel::HIGH;
    }
    else if (congestionScore > 0.2) {
        return CongestionLevel::MODERATE;
    }

    return CongestionLevel::LOW;
}

int AdaptiveBitrateController::CalculateBitrateAgressive(const NetworkMetrics& metrics) {
    int newBitrate = currentBitrate;
    
    if (metrics.packetLossRate < 0.02 && metrics.roundTripTime < 50) {
        // Network is good, increase bitrate quickly
        newBitrate = static_cast<int>(currentBitrate * 1.15);  // +15%
        consecutiveIncreaseAttempts++;
    } else if (metrics.packetLossRate > 0.05 || metrics.roundTripTime > 150) {
        // Network is congested, decrease bitrate quickly
        newBitrate = static_cast<int>(currentBitrate * 0.80);  // -20%
        consecutiveIncreaseAttempts = 0;
    }
    
    // Prevent too many consecutive increases
    if (consecutiveIncreaseAttempts > 5) {
        newBitrate = std::min(newBitrate, static_cast<int>(currentBitrate * 1.05));
        consecutiveIncreaseAttempts = 3;
    }
    
    return std::max(config.minBitrate, std::min(config.maxBitrate, newBitrate));
}

int AdaptiveBitrateController::CalculateBitrateBalanced(const NetworkMetrics& metrics) {
    int newBitrate = currentBitrate;
    
    // Balanced approach: moderate response
    if (metrics.packetLossRate < 0.01) {
        // Very low loss, can increase slightly
        newBitrate = static_cast<int>(currentBitrate * 1.08);  // +8%
        consecutiveIncreaseAttempts++;
    } else if (metrics.packetLossRate > 0.05) {
        // High loss, decrease
        newBitrate = static_cast<int>(currentBitrate * 0.85);  // -15%
        consecutiveIncreaseAttempts = 0;
    } else if (metrics.roundTripTime > 150) {
        // Moderate decrease for high latency
        newBitrate = static_cast<int>(currentBitrate * 0.90);  // -10%
    }
    
    // Limit consecutive increases
    if (consecutiveIncreaseAttempts > 8) {
        newBitrate = currentBitrate;
        consecutiveIncreaseAttempts = 5;
    }
    
    return std::max(config.minBitrate, std::min(config.maxBitrate, newBitrate));
}

int AdaptiveBitrateController::CalculateBitrateConservative(const NetworkMetrics& metrics) {
    int newBitrate = currentBitrate;
    
    // Conservative approach: slow, stable changes
    if (metrics.packetLossRate < 0.005 && metrics.roundTripTime < 30) {
        // Excellent conditions, tiny increase
        newBitrate = static_cast<int>(currentBitrate * 1.03);  // +3%
        consecutiveIncreaseAttempts++;
    } else if (metrics.packetLossRate > 0.10 || metrics.roundTripTime > 200) {
        // Poor conditions, decrease
        newBitrate = static_cast<int>(currentBitrate * 0.90);  // -10%
        consecutiveIncreaseAttempts = 0;
    }
    
    // Strict limit on consecutive increases
    if (consecutiveIncreaseAttempts > 10) {
        newBitrate = currentBitrate;
        consecutiveIncreaseAttempts = 8;
    }
    
    return std::max(config.minBitrate, std::min(config.maxBitrate, newBitrate));
}

void AdaptiveBitrateController::CalculateBitrate(const NetworkMetrics& metrics) {
    // Detect current congestion level
    CongestionLevel newCongestionLevel = DetectCongestion(metrics);
    
    if (newCongestionLevel != lastCongestionLevel) {
        statistics.congestionEvents++;
        lastCongestionLevel = newCongestionLevel;
    }
    
    // Calculate new bitrate based on strategy
    int newBitrate = currentBitrate;
    
    switch (config.strategy) {
        case Strategy::AGGRESSIVE:
            newBitrate = CalculateBitrateAgressive(metrics);
            break;
        case Strategy::BALANCED:
            newBitrate = CalculateBitrateBalanced(metrics);
            break;
        case Strategy::CONSERVATIVE:
            newBitrate = CalculateBitrateConservative(metrics);
            break;
    }
    
    // Apply change with smoothing
    if (newBitrate != currentBitrate) {
        previousBitrate = currentBitrate;
        currentBitrate = newBitrate;
        statistics.bitrateAdjustments++;
    }
    
    // Update statistics
    statistics.currentBitrate = currentBitrate;
    statistics.averagePacketLoss = GetAveragePacketLoss();
    statistics.averageRTT = GetAverageRTT();
    statistics.averageBitrate = GetAverageBitrate();
    
    if (metrics.bandwidth > 0) {
        statistics.estimatedBandwidth = metrics.bandwidth;
    }
}

void AdaptiveBitrateController::AddToHistory() {
    bitrateHistory.push_back(currentBitrate);
    if (bitrateHistory.size() > 100) {
        bitrateHistory.pop_front();
    }
}

double AdaptiveBitrateController::GetAveragePacketLoss() const {
    if (packetLossHistory.empty()) return 0.0;
    
    double sum = std::accumulate(packetLossHistory.begin(), packetLossHistory.end(), 0.0);
    return sum / packetLossHistory.size();
}

double AdaptiveBitrateController::GetAverageRTT() const {
    if (rttHistory.empty()) return 0.0;
    
    double sum = std::accumulate(rttHistory.begin(), rttHistory.end(), 0.0);
    return sum / rttHistory.size();
}

double AdaptiveBitrateController::GetAverageBitrate() const {
    if (bitrateHistory.empty()) return currentBitrate;
    
    double sum = std::accumulate(bitrateHistory.begin(), bitrateHistory.end(), 0.0);
    return sum / bitrateHistory.size();
}

AdaptiveBitrateController::ControllerStats AdaptiveBitrateController::GetStatistics() const {
    std::lock_guard<std::mutex> lock(statsMutex);
    return statistics;
}

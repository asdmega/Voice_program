//adaptive_bitrate.h:
#pragma once

#include <vector>
#include <cstdint>
#include <atomic>
#include <deque>
#include <chrono>
#include <mutex>

// Adaptive bitrate control for network optimization
class AdaptiveBitrateController {
public:
    enum class CongestionLevel {
        LOW = 0,        // Network is clear
        MODERATE = 1,   // Some congestion
        HIGH = 2,       // Significant congestion
        CRITICAL = 3    // Network nearly unusable
    };

    enum class Strategy {
        AGGRESSIVE = 0,     // Quickly respond to network changes
        BALANCED = 1,       // Moderate response
        CONSERVATIVE = 2    // Slow, stable changes
    };

    struct BitrateConfig {
        int minBitrate = 16000;      // 16 kbps minimum
        int maxBitrate = 128000;     // 128 kbps maximum
        int targetBitrate = 32000;   // 64 kbps target
        int startingBitrate = 32000; // Starting point
        Strategy strategy = Strategy::BALANCED;
        
        // REMB (Receiver Estimated Maximum Bitrate) parameters
        int rembWindow = 500;        // Measurement window in ms
        double overuseThreshold = 1.15;  // Overuse detector threshold
    };

    struct NetworkMetrics {
        double packetLossRate = 0.0;        // 0.0 - 1.0
        double roundTripTime = 0;           // ms
        double bandwidth = 0;               // bps (estimate)
        double jitter = 0;                  // ms
        CongestionLevel congestion = CongestionLevel::LOW;
        int packetsLost = 0;
        int packetsSent = 0;
        int framesDropped = 0;
        int framesEncoded = 0;
    };

    AdaptiveBitrateController();
    ~AdaptiveBitrateController();

    bool Initialize(const BitrateConfig& config);

    // Update network metrics
    void UpdateMetrics(const NetworkMetrics& metrics);

    // Get current recommended bitrate
    int GetRecommendedBitrate() const;

    // Get current congestion level
    CongestionLevel GetCongestionLevel() const;

    // Manual bitrate override (for debugging)
    void SetBitrate(int bitrate);

    // Statistics
    struct ControllerStats {
        int currentBitrate = 0;
        double averageBitrate = 0.0;
        int bitrateAdjustments = 0;
        double averagePacketLoss = 0.0;
        double averageRTT = 0.0;
        int congestionEvents = 0;
        double estimatedBandwidth = 0.0;
    };

    ControllerStats GetStatistics() const;

private:
    BitrateConfig config;
    int currentBitrate = 0;
    int previousBitrate = 0;

    // History tracking
    std::deque<int> bitrateHistory;
    std::deque<double> packetLossHistory;
    std::deque<double> rttHistory;
    std::deque<double> bandwidthHistory;
    
    // Statistics
    mutable std::mutex statsMutex;
    ControllerStats statistics;

    // Time tracking
    std::chrono::high_resolution_clock::time_point lastUpdateTime;
    int updateCount = 0;

    // Algorithm state
    CongestionLevel lastCongestionLevel = CongestionLevel::LOW;
    int consecutiveIncreaseAttempts = 0;

    // Helper functions
    void CalculateBitrate(const NetworkMetrics& metrics);
    CongestionLevel DetectCongestion(const NetworkMetrics& metrics);
    int CalculateBitrateAgressive(const NetworkMetrics& metrics);
    int CalculateBitrateBalanced(const NetworkMetrics& metrics);
    int CalculateBitrateConservative(const NetworkMetrics& metrics);
    
    void AddToHistory();
    double GetAveragePacketLoss() const;
    double GetAverageRTT() const;
    double GetAverageBitrate() const;
};

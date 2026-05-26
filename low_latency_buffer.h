//low_latency_buffer.h:
#pragma once

#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <cstdint>
#include <atomic>
#include <chrono>
#include <map>       // For std::map
#include <deque>     // For std::deque
#include <utility>   // For std::pair

// Low-latency, lock-free circular buffer for audio/video frames
template<typename T>
class LowLatencyBuffer {
public:
    struct BufferConfig {
        size_t maxSize = 100;              // Max frames in buffer
        int targetLatencyMs = 20;          // Target latency
        bool enableJitterBuffer = true;
        int jitterBufferSizeMs = 50;
    };

    LowLatencyBuffer() : writeIndex(0), readIndex(0), frameCount(0) {
    }

    bool Initialize(const BufferConfig& config) {
        this->config = config;
        buffer.resize(config.maxSize);
        timestamps.resize(config.maxSize);
        return true;
    }

    // Push frame to buffer (high priority, real-time)
    bool PushFrame(const T& frame, int64_t timestamp) {
        std::lock_guard<std::mutex> lock(bufferMutex);

        // Check if buffer is full, drop oldest frame if needed
        if (frameCount >= config.maxSize) {
            readIndex = (readIndex + 1) % config.maxSize;
            frameCount--;
        }

        buffer[writeIndex] = frame;
        timestamps[writeIndex] = timestamp;
        writeIndex = (writeIndex + 1) % config.maxSize;
        frameCount++;

        frameAvailable.notify_one();
        return true;
    }

    // Pop frame from buffer with timeout
    bool PopFrame(T& frame, int64_t timeoutMs = 0) {
        std::unique_lock<std::mutex> lock(bufferMutex);

        if (frameCount == 0) {
            if (timeoutMs > 0) {
                return frameAvailable.wait_for(
                    lock,
                    std::chrono::milliseconds(timeoutMs),
                    [this] { return frameCount > 0; }
                );
            }
            return false;
        }

        frame = buffer[readIndex];
        readIndex = (readIndex + 1) % config.maxSize;
        frameCount--;

        return true;
    }

    // Check if buffer has frames
    bool HasFrames() const {
        std::lock_guard<std::mutex> lock(bufferMutex);
        return frameCount > 0;
    }

    // Get current buffer fill level (0.0 - 1.0)
    double GetBufferLevel() const {
        std::lock_guard<std::mutex> lock(bufferMutex);
        return static_cast<double>(frameCount) / config.maxSize;
    }

    // Clear buffer
    void Clear() {
        std::lock_guard<std::mutex> lock(bufferMutex);
        writeIndex = 0;
        readIndex = 0;
        frameCount = 0;
    }

    // Get frame count
    size_t GetFrameCount() const {
        std::lock_guard<std::mutex> lock(bufferMutex);
        return frameCount;
    }

    // Get estimated latency
    double GetEstimatedLatencyMs() const {
        std::lock_guard<std::mutex> lock(bufferMutex);
        if (frameCount == 0) return 0;

        int64_t oldestTimestamp = timestamps[readIndex];
        int64_t newestTimestamp = timestamps[(writeIndex - 1 + config.maxSize) % config.maxSize];

        return static_cast<double>(newestTimestamp - oldestTimestamp) / 1e6;  // ns to ms
    }

private:
    BufferConfig config;
    std::vector<T> buffer;
    std::vector<int64_t> timestamps;

    size_t writeIndex;
    size_t readIndex;
    size_t frameCount;

    mutable std::mutex bufferMutex;
    std::condition_variable frameAvailable;
};

// Adaptive jitter buffer for handling network variations
class AdaptiveJitterBuffer {
public:
    struct JitterConfig {
        int minBufferMs = 20;      // Minimum buffer size
        int maxBufferMs = 200;     // Maximum buffer size
        int targetBufferMs = 50;   // Target latency
        int updateIntervalMs = 100;
    };

    AdaptiveJitterBuffer();
    ~AdaptiveJitterBuffer();

    bool Initialize(const JitterConfig& config);

    // Push packet with sequence number
    void PushPacket(const std::vector<uint8_t>& data, uint32_t sequenceNumber, int64_t timestamp);

    // Pop complete frame when ready
    std::vector<uint8_t> PopFrame(int timeoutMs = 0);

    // Check if frame is ready
    bool IsFrameReady() const;

    // Update network quality to adjust buffer
    void UpdateNetworkQuality(double packetLoss, double jitter);

    // Get current buffer size
    int GetCurrentBufferSizeMs() const;

    // Statistics
    struct JitterStats {
        int packetsReceived = 0;
        int packetsLost = 0;
        int framesDecoded = 0;
        int framesDropped = 0;
        double averageJitter = 0.0;
        double currentBufferMs = 0.0;
    };

    JitterStats GetStatistics() const;

private:
    JitterConfig config;
    int currentBufferMs = 50;

    std::map<uint32_t, std::pair<std::vector<uint8_t>, int64_t>> packetBuffer;
    uint32_t nextSequenceNumber = 0;

    mutable std::mutex bufferMutex;
    std::deque<double> jitterHistory;

    JitterStats statistics;

    void AdjustBufferSize(double packetLoss, double jitter);
};

//low_latency_buffer.cpp:
#include "low_latency_buffer.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <map>

AdaptiveJitterBuffer::AdaptiveJitterBuffer() {
}

AdaptiveJitterBuffer::~AdaptiveJitterBuffer() {
}

bool AdaptiveJitterBuffer::Initialize(const JitterConfig& cfg) {
    config = cfg;
    currentBufferMs = config.targetBufferMs;
    nextSequenceNumber = 0;
    return true;
}

void AdaptiveJitterBuffer::PushPacket(
    const std::vector<uint8_t>& data,
    uint32_t sequenceNumber,
    int64_t timestamp
) {
    std::lock_guard<std::mutex> lock(bufferMutex);

    packetBuffer[sequenceNumber] = { data, timestamp };
    statistics.packetsReceived++;

    // If this is the first packet, set our expected sequence
    if (nextSequenceNumber == 0) {
        nextSequenceNumber = sequenceNumber;
    }
}

std::vector<uint8_t> AdaptiveJitterBuffer::PopFrame(int timeoutMs) {
    std::lock_guard<std::mutex> lock(bufferMutex);

    if (!IsFrameReady()) {
        return std::vector<uint8_t>();
    }

    // Collect packets in order until we have a complete frame
    std::vector<uint8_t> frameData;

    auto it = packetBuffer.find(nextSequenceNumber);
    if (it != packetBuffer.end()) {
        frameData = it->second.first;
        packetBuffer.erase(it);
        nextSequenceNumber++;
        statistics.framesDecoded++;
    }

    return frameData;
}

bool AdaptiveJitterBuffer::IsFrameReady() const {
    return packetBuffer.find(nextSequenceNumber) != packetBuffer.end();
}

void AdaptiveJitterBuffer::UpdateNetworkQuality(double packetLoss, double jitter) {
    std::lock_guard<std::mutex> lock(bufferMutex);

    jitterHistory.push_back(jitter);
    if (jitterHistory.size() > 100) {
        jitterHistory.pop_front();
    }

    // Calculate average jitter
    if (!jitterHistory.empty()) {
        statistics.averageJitter =
            std::accumulate(jitterHistory.begin(), jitterHistory.end(), 0.0) /
            jitterHistory.size();
    }

    AdjustBufferSize(packetLoss, jitter);
}

int AdaptiveJitterBuffer::GetCurrentBufferSizeMs() const {
    std::lock_guard<std::mutex> lock(bufferMutex);
    return currentBufferMs;
}

AdaptiveJitterBuffer::JitterStats AdaptiveJitterBuffer::GetStatistics() const {
    std::lock_guard<std::mutex> lock(bufferMutex);
    JitterStats stats = statistics;  // Copy to avoid modifying in const
    stats.currentBufferMs = currentBufferMs;
    return stats;
}

void AdaptiveJitterBuffer::AdjustBufferSize(double packetLoss, double jitter) {
    // Dynamically adjust buffer based on network conditions

    int newBufferMs = currentBufferMs;

    if (packetLoss > 0.05 || jitter > 30) {
        // Network is bad, increase buffer
        newBufferMs = static_cast<int>(currentBufferMs * 1.2);
    }
    else if (packetLoss < 0.01 && jitter < 10) {
        // Network is good, decrease buffer
        newBufferMs = static_cast<int>(currentBufferMs * 0.9);
    }

    // Clamp to configured limits
    newBufferMs = std::max(config.minBufferMs, std::min(config.maxBufferMs, newBufferMs));

    // Only update if change is significant (> 5ms)
    if (std::abs(newBufferMs - currentBufferMs) > 5) {
        currentBufferMs = newBufferMs;
    }
}

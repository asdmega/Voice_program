//advanced_audio_codec.h:
#pragma once

#include <vector>
#include <memory>
#include <opus/opus.h>
#include <cstdint>
#include <atomic>
#include <queue>
#include <mutex>
#include "audio_defs.h"

#pragma comment(lib, "opus.lib")

// Advanced audio codec with low-latency, high-quality compression
class AdvancedAudioCodec {
public:
    enum class BitrateMode {
        CONSTANT = 0,      // Fixed bitrate
        VARIABLE = 1,      // Variable bitrate (more efficient)
        CONSTRAINED = 2    // Constrained VBR
    };

    struct AudioConfig {
        int targetBitrate = 32000;        // 32 kbps default
        BitrateMode bitrateMode = BitrateMode::VARIABLE;
        int complexity = 9;               // 0-10, higher = better quality but more CPU
        bool enableDTX = true;            // Discontinuous Transmission (silence detection)
        bool enableFEC = true;            // Forward Error Correction
        int packetLoss = 0;               // Simulate packet loss 0-100
        bool enableCBR = false;           // Constant Bit Rate mode
    };

    struct EncodedPacket {
        std::vector<uint8_t> data;
        int64_t timestamp;
        bool isKeyFrame;
        int originalSize;
        float compressionRatio;
        int quality;
    };

    AdvancedAudioCodec();
    ~AdvancedAudioCodec();

    // Initialize codec with configuration
    bool Initialize(const AudioConfig& config);

    // Encode PCM audio to Opus
    std::vector<uint8_t> Encode(const std::vector<int16_t>& pcmData);

    // Decode Opus to PCM
    std::vector<int16_t> Decode(const std::vector<uint8_t>& opusData);

    // Set bitrate dynamically
    void SetBitrate(int bitrate);

    // Set complexity (affects quality vs CPU)
    void SetComplexity(int complexity);

    // Get current bitrate
    int GetCurrentBitrate() const;

    // Get statistics
    struct Statistics {
        int64_t packetsEncoded = 0;
        int64_t packetsDecoded = 0;
        int64_t bytesIn = 0;
        int64_t bytesOut = 0;
        double averageCompressionRatio = 0.0;
        double averageBitrate = 0.0;
    };

    Statistics GetStatistics() const;
    void ResetStatistics();

    // Quality levels for adaptive bitrate
    static constexpr int QUALITY_LEVELS[] = {16000, 24000, 32000, 48000, 64000};
    static constexpr int NUM_QUALITY_LEVELS = 5;

protected:
    OpusEncoder* encoder = nullptr;
    OpusDecoder* decoder = nullptr;
    AudioConfig config;

    // Statistics
    mutable std::mutex statsMutex;
    Statistics statistics;

    // Buffer for encoding
    std::vector<uint8_t> encodeBuffer;
};

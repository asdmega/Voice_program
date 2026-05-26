//voice_framework.h:
#pragma once

#include "advanced_audio_codec.h"
#include "secure_channel.h"
#include "reliable_transport.h"
#include "adaptive_bitrate.h"
#include "network_monitor.h"
#include "low_latency_buffer.h"
#include "audio_processor.h"
#include "advanced_audio_processor.h"

#include <memory>
#include <string>
#include <vector>
#include <winsock2.h>

// Unified voice application framework with all optimizations
class VoiceApplicationFramework {
public:
    struct FrameworkConfig {
        // Audio
        int audioSampleRate = 48000;
        int audioChannels = 1;
        int audioFrameSizeMs = 10;
        int audioTargetBitrate = 32000;

        // Encryption
        std::string presharedKey = "VoiceApp2024SecureLocalNetwork";
        bool enableEncryption = true;

        // Network
        std::string localIP = "0.0.0.0";
        int audioPort = 12345;
        bool enableNetworkMonitoring = true;
        
        // Buffers
        int jitterBufferMs = 50;
        int outputBufferFrames = 100;
        
        // Audio Processing
        bool enableAudioProcessing = true;
        bool enableAdvancedProcessing = true;
        
        // Noise Gate (for quiet voice)
        float noiseGateThreshold = -45.0f;  // dB
        float noiseGateAttack = 5.0f;       // ms
        float noiseGateRelease = 100.0f;    // ms
        
        // AGC
        float agcTargetLevel = -20.0f;      // dB
        float agcMaxGain = 30.0f;           // dB
        float agcMinGain = -10.0f;          // dB
        
        // Noise Suppression
        float noiseSuppressionLevel = 0.7f; // 0.0 - 1.0
        
        // De-esser (for crackling)
        float deesserThreshold = -25.0f;    // dB
        
        // Limiter
        float limiterThreshold = -3.0f;     // dB
    };

    struct QualityReport {
        double currentLatency = 0.0;
        double packetLossRate = 0.0;
        double jitter = 0.0;
        int currentBitrate = 0;
        std::string networkGrade;
        std::string recommendations;
        
        // Audio processing stats
        float inputLevel = -100.0f;
        float outputLevel = -100.0f;
        float currentGain = 0.0f;
        bool noiseGateOpen = false;
        float noiseEstimate = -100.0f;
        float deesserReduction = 0.0f;
        float limiterReduction = 0.0f;
        bool voiceDetected = false;
    };

    struct FrameworkStats {
        int64_t audioPacketsEncoded = 0;
        int64_t audioPacketsDecoded = 0;
        double audioCompressionRatio = 0.0;
        int64_t packetsEncrypted = 0;
        int64_t packetsDecrypted = 0;
        int64_t authenticationFailures = 0;
        int64_t packetsReliablySent = 0;
        int64_t packetsLost = 0;
        int64_t packetsRetransmitted = 0;
        int currentBitrate = 0;
        int bitrateAdjustments = 0;
        double averageLatency = 0.0;
        double averageJitter = 0.0;
        double averagePacketLoss = 0.0;
        int64_t framesProcessed = 0;
        float averageInputLevel = -100.0f;
        float averageOutputLevel = -100.0f;
        float averageGainApplied = 0.0f;
    };

    VoiceApplicationFramework();
    ~VoiceApplicationFramework();

    bool Initialize(const FrameworkConfig& config);
    void Shutdown();

    // Encoding pipeline with audio processing
    std::vector<uint8_t> EncodeAudio(const std::vector<int16_t>& pcmData);
    std::vector<uint8_t> EncodeAudioRaw(const std::vector<int16_t>& pcmData);

    // Decoding pipeline
    std::vector<int16_t> DecodeAudio(const std::vector<uint8_t>& compressedData);

    // Send audio securely
    bool SendAudioPacket(const std::vector<int16_t>& pcmData, const std::string& remoteIP);

    // Receive audio
    bool ReceiveAudioPacket(std::vector<int16_t>& pcmData);
    
    // Process audio locally
    void ProcessAudioLocal(std::vector<int16_t>& pcmData);

    QualityReport GetQualityReport() const;
    FrameworkStats GetFrameworkStats() const;
    
    // Audio processing control
    void SetNoiseGateEnabled(bool enabled);
    void SetAGCEnabled(bool enabled);
    void SetNoiseSuppressionEnabled(bool enabled);
    void SetDeesserEnabled(bool enabled);
    void SetLimiterEnabled(bool enabled);
    void SetNoiseGateThreshold(float thresholdDb);
    void SetAGCTargetLevel(float targetDb);
    void SetNoiseSuppressionLevel(float level);
    
    // Get audio processor stats
    AudioProcessor::ProcessorStats GetAudioProcessorStats() const;
    AdvancedAudioProcessor::AdvancedStats GetAdvancedAudioStats() const;

private:
    FrameworkConfig config;

    // Components
    std::unique_ptr<AdvancedAudioCodec> audioCodec;
    std::unique_ptr<SecureChannel> secureChannel;
    std::unique_ptr<ReliableTransport> reliableTransport;
    std::unique_ptr<AdaptiveBitrateController> bitrateController;
    std::unique_ptr<NetworkMonitor> networkMonitor;
    std::unique_ptr<LowLatencyBuffer<std::vector<uint8_t>>> inputBuffer;
    std::unique_ptr<LowLatencyBuffer<std::vector<uint8_t>>> outputBuffer;
    std::unique_ptr<AdaptiveJitterBuffer> jitterBuffer;
    
    // Audio processors
    std::unique_ptr<AudioProcessor> audioProcessor;
    std::unique_ptr<AdvancedAudioProcessor> advancedProcessor;
    bool useAdvancedProcessing = true;

    // Socket management
    SOCKET audioSocket = INVALID_SOCKET;

    // Helper functions
    void UpdateNetworkMetrics();
    std::string GetNetworkGrade() const;
    std::string GetRecommendations() const;
    void InitializeAudioProcessor();
    void UpdateAudioStats();
};

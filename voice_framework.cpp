//voice_framework.cpp:
#include "voice_framework.h"
#include <iostream>
#include <algorithm>

VoiceApplicationFramework::VoiceApplicationFramework() {
}

VoiceApplicationFramework::~VoiceApplicationFramework() {
    Shutdown();
}

void VoiceApplicationFramework::InitializeAudioProcessor() {
    if (config.enableAdvancedProcessing) {
        // Use advanced processor with all features
        advancedProcessor = std::make_unique<AdvancedAudioProcessor>();
        
        AdvancedAudioProcessor::AdvancedConfig advConfig;

        // Configure noise gate for quiet voice
        advConfig.enableNoiseGate = true;
        advConfig.noiseGateThreshold = config.noiseGateThreshold;
        advConfig.noiseGateAttack = config.noiseGateAttack;
        advConfig.noiseGateRelease = config.noiseGateRelease;
        
        // Configure AGC
        advConfig.enableAGC = true;
        advConfig.agcTargetLevel = config.agcTargetLevel;
        advConfig.agcMaxGain = config.agcMaxGain;
        advConfig.agcMinGain = config.agcMinGain;
        
        // Configure noise suppression
        advConfig.enableNoiseSuppression = true;
        advConfig.noiseSuppressionLevel = config.noiseSuppressionLevel;
        advConfig.enableSpectralNS = true;
        
        // Configure de-esser for crackling
        advConfig.enableDeesser = true;
        advConfig.deesserThreshold = config.deesserThreshold;
        advConfig.enableMultibandDeesser = true;
        
        // Configure limiter
        advConfig.enableLimiter = true;
        advConfig.limiterThreshold = config.limiterThreshold;
        advConfig.enableLookaheadLimiter = true;
        
        // Additional features
        advConfig.enableDCRemoval = true;
        advConfig.enableHighPass = true;
        advConfig.highPassFreq = 80.0f;
        advConfig.enableVAD = true;
        advConfig.enableDynamicEQ = true;
        
        if (advancedProcessor->InitializeAdvanced(advConfig)) {
            std::cout << "Advanced audio processor initialized" << std::endl;
            useAdvancedProcessing = true;
        } else {
            std::cerr << "Failed to initialize advanced processor, falling back to basic" << std::endl;
            advancedProcessor.reset();
            useAdvancedProcessing = false;
        }
    }
    
    if (!useAdvancedProcessing && config.enableAudioProcessing) {
        // Use basic processor
        audioProcessor = std::make_unique<AudioProcessor>();
        
        AudioProcessor::ProcessorConfig procConfig;

        procConfig.enableNoiseGate = true;
        procConfig.noiseGateThreshold = config.noiseGateThreshold;
        procConfig.enableAGC = true;
        procConfig.agcTargetLevel = config.agcTargetLevel;
        procConfig.enableNoiseSuppression = true;
        procConfig.noiseSuppressionLevel = config.noiseSuppressionLevel;
        procConfig.enableDeesser = true;
        procConfig.deesserThreshold = config.deesserThreshold;
        procConfig.enableLimiter = true;
        procConfig.limiterThreshold = config.limiterThreshold;
        procConfig.enableDCRemoval = true;
        procConfig.enableHighPass = true;
        
        if (audioProcessor->Initialize(procConfig)) {
            std::cout << "Basic audio processor initialized" << std::endl;
        } else {
            std::cerr << "Failed to initialize audio processor" << std::endl;
            audioProcessor.reset();
        }
    }
}

bool VoiceApplicationFramework::Initialize(const FrameworkConfig& cfg) {
    config = cfg;

    // Initialize audio codec
    audioCodec = std::make_unique<AdvancedAudioCodec>();
    AdvancedAudioCodec::AudioConfig audioConfig;
    audioConfig.targetBitrate = config.audioTargetBitrate;
    audioConfig.complexity = 9;
    audioConfig.enableFEC = true;

    if (!audioCodec->Initialize(audioConfig)) {
        std::cerr << "Failed to initialize audio codec" << std::endl;
        return false;
    }

    // Initialize secure channel
    secureChannel = std::make_unique<SecureChannel>();
    SecureChannel::ChannelConfig channelConfig;
    channelConfig.algorithm = SecureChannel::EncryptionAlgorithm::AES_256_GCM;
    channelConfig.useAuthentication = true;
    channelConfig.preSharedKey = config.presharedKey;

    if (!secureChannel->Initialize(channelConfig)) {
        std::cerr << "Failed to initialize secure channel" << std::endl;
        return false;
    }

    // Initialize reliable transport
    reliableTransport = std::make_unique<ReliableTransport>();
    ReliableTransport::ReliabilityConfig reliabilityConfig;
    reliabilityConfig.enableFEC = true;
    reliabilityConfig.packetSize = 1472;

    if (!reliableTransport->Initialize(reliabilityConfig)) {
        std::cerr << "Failed to initialize reliable transport" << std::endl;
        return false;
    }

    // Initialize bitrate controller
    bitrateController = std::make_unique<AdaptiveBitrateController>();
    AdaptiveBitrateController::BitrateConfig bitrateConfig;
    bitrateConfig.minBitrate = 16000;
    bitrateConfig.maxBitrate = 128000;
    bitrateConfig.targetBitrate = config.audioTargetBitrate;
    bitrateConfig.strategy = AdaptiveBitrateController::Strategy::BALANCED;

    if (!bitrateController->Initialize(bitrateConfig)) {
        std::cerr << "Failed to initialize bitrate controller" << std::endl;
        return false;
    }

    // Initialize network monitor
    networkMonitor = std::make_unique<NetworkMonitor>();
    if (!networkMonitor->StartMonitoring()) {
        std::cerr << "Failed to start network monitoring" << std::endl;
        return false;
    }

    // Initialize buffers
    inputBuffer = std::make_unique<LowLatencyBuffer<std::vector<uint8_t>>>();
    LowLatencyBuffer<std::vector<uint8_t>>::BufferConfig bufferConfig;
    bufferConfig.maxSize = 100;
    bufferConfig.targetLatencyMs = 20;
    bufferConfig.jitterBufferSizeMs = config.jitterBufferMs;
    inputBuffer->Initialize(bufferConfig);

    outputBuffer = std::make_unique<LowLatencyBuffer<std::vector<uint8_t>>>();
    outputBuffer->Initialize(bufferConfig);

    // Initialize jitter buffer
    jitterBuffer = std::make_unique<AdaptiveJitterBuffer>();
    AdaptiveJitterBuffer::JitterConfig jitterConfig;
    jitterConfig.targetBufferMs = config.jitterBufferMs;
    jitterBuffer->Initialize(jitterConfig);
    
    // Initialize audio processor (NEW)
    InitializeAudioProcessor();

    return true;
}

void VoiceApplicationFramework::Shutdown() {
    if (networkMonitor) {
        networkMonitor->StopMonitoring();
    }

    if (audioSocket != INVALID_SOCKET) {
        closesocket(audioSocket);
        audioSocket = INVALID_SOCKET;
    }

    audioCodec.reset();
    secureChannel.reset();
    reliableTransport.reset();
    bitrateController.reset();
    networkMonitor.reset();
    inputBuffer.reset();
    outputBuffer.reset();
    jitterBuffer.reset();
    audioProcessor.reset();
    advancedProcessor.reset();
}

std::vector<uint8_t> VoiceApplicationFramework::EncodeAudio(const std::vector<int16_t>& pcmData) {
    if (!audioCodec) {
        return std::vector<uint8_t>();
    }

    // Apply audio processing before encoding
    std::vector<int16_t> processedData = pcmData;
    if (useAdvancedProcessing && advancedProcessor) {
        advancedProcessor->ProcessFrameAdvanced(processedData.data(), static_cast<int>(processedData.size()));
    } else if (audioProcessor) {
        audioProcessor->ProcessFrame(processedData.data(), static_cast<int>(processedData.size()));
    }

    // Encode processed audio with Opus
    std::vector<uint8_t> encoded = audioCodec->Encode(processedData);

    if (encoded.empty()) {
        return encoded;
    }

    // Update bitrate controller
    if (bitrateController) {
        AdaptiveBitrateController::NetworkMetrics metrics;
        auto linkMetrics = networkMonitor->GetMetrics();
        metrics.packetLossRate = linkMetrics.packetLossRate;
        metrics.roundTripTime = linkMetrics.latency;
        metrics.jitter = linkMetrics.jitter;
        metrics.bandwidth = linkMetrics.bandwidth;

        bitrateController->UpdateMetrics(metrics);
        audioCodec->SetBitrate(bitrateController->GetRecommendedBitrate());
    }

    return encoded;
}

std::vector<uint8_t> VoiceApplicationFramework::EncodeAudioRaw(const std::vector<int16_t>& pcmData) {
    if (!audioCodec) {
        return std::vector<uint8_t>();
    }
    
    // Encode without processing
    return audioCodec->Encode(pcmData);
}

void VoiceApplicationFramework::ProcessAudioLocal(std::vector<int16_t>& pcmData) {
    if (useAdvancedProcessing && advancedProcessor) {
        advancedProcessor->ProcessFrameAdvanced(pcmData.data(), static_cast<int>(pcmData.size()));
    } else if (audioProcessor) {
        audioProcessor->ProcessFrame(pcmData.data(), static_cast<int>(pcmData.size()));
    }
}

std::vector<int16_t> VoiceApplicationFramework::DecodeAudio(const std::vector<uint8_t>& compressedData) {
    if (!audioCodec || compressedData.empty()) {
        return std::vector<int16_t>();
    }

    return audioCodec->Decode(compressedData);
}

float VoiceDetector::EstimateFundamental(const std::vector<float>& spectrum, float binWidth) {
    float maxPeak = 0.0f;
    int maxBin = 50; // начало от 50 Гц
    for (int i = 50; i < 200; i++) { // диапазон 50..200 Гц для мужских, можно расширить
        if (spectrum[i] > maxPeak) {
            maxPeak = spectrum[i];
            maxBin = i;
        }
    }
    // Поиск гармоник
    float f0 = maxBin * binWidth;
    // Уточнение по максимуму автокорреляции (упрощённо)
    return f0;
}

bool VoiceApplicationFramework::SendAudioPacket(
    const std::vector<int16_t>& pcmData,
    const std::string& remoteIP
) {
    if (!audioCodec || !secureChannel || !reliableTransport) {
        return false;
    }

    // Step 1: Encode audio (with processing)
    std::vector<uint8_t> encoded = EncodeAudio(pcmData);
    if (encoded.empty()) {
        return false;
    }

    // Step 2: Encrypt
    auto encrypted = secureChannel->Encrypt(encoded);
    if (encrypted.ciphertext.empty()) {
        return false;
    }

    // Step 3: Serialize encrypted packet
    std::vector<uint8_t> encryptedData = encrypted.ciphertext;
    encryptedData.insert(encryptedData.begin(), encrypted.nonce.begin(), encrypted.nonce.end());
    encryptedData.insert(encryptedData.end(), encrypted.tag.begin(), encrypted.tag.end());

    // Step 4: Prepare reliable transport packets
    auto packets = reliableTransport->PreparePackets(
        encryptedData,
        ReliableTransport::PacketType::DATA
    );

    // Step 5: Record in network monitor
    for (const auto& packet : packets) {
        networkMonitor->RecordPacketSent(packet.payload.size());
    }

    return true;
}

bool VoiceApplicationFramework::ReceiveAudioPacket(std::vector<int16_t>& pcmData) {
    if (!audioCodec || !secureChannel || !reliableTransport) {
        return false;
    }

    // Try to get complete audio frame from jitter buffer
    if (!jitterBuffer->IsFrameReady()) {
        return false;
    }

    std::vector<uint8_t> encrypted = jitterBuffer->PopFrame();
    if (encrypted.empty()) {
        return false;
    }

    // Step 1: Extract nonce, tag, and ciphertext
    if (encrypted.size() < 12 + 16) {  // nonce + tag
        return false;
    }

    SecureChannel::EncryptedPacket packet;
    packet.nonce.assign(encrypted.begin(), encrypted.begin() + 12);
    packet.tag.assign(encrypted.end() - 16, encrypted.end());
    packet.ciphertext.assign(encrypted.begin() + 12, encrypted.end() - 16);

    // Step 2: Decrypt
    std::vector<uint8_t> decoded = secureChannel->Decrypt(packet);
    if (decoded.empty()) {
        return false;
    }

    // Step 3: Decode audio
    pcmData = DecodeAudio(decoded);
    return !pcmData.empty();
}

void VoiceApplicationFramework::UpdateAudioStats() {
    // This is called periodically to update statistics
}

VoiceApplicationFramework::QualityReport VoiceApplicationFramework::GetQualityReport() const {
    QualityReport report;

    auto linkMetrics = networkMonitor->GetMetrics();
    report.currentLatency = linkMetrics.latency;
    report.packetLossRate = linkMetrics.packetLossRate;
    report.jitter = linkMetrics.jitter;

    if (bitrateController) {
        auto stats = bitrateController->GetStatistics();
        report.currentBitrate = stats.currentBitrate;
    }

    auto quality = networkMonitor->GetNetworkQuality();
    report.networkGrade = quality.description;

    report.recommendations = GetRecommendations();
    
    // Add audio processing stats (NEW)
    if (useAdvancedProcessing && advancedProcessor) {
        auto audioStats = advancedProcessor->GetAdvancedStats();
        report.inputLevel = audioStats.inputLevel;
        report.outputLevel = audioStats.outputLevel;
        report.currentGain = audioStats.currentGain;
        report.noiseGateOpen = audioStats.noiseGateOpen;
        report.noiseEstimate = audioStats.noiseEstimate;
        report.deesserReduction = audioStats.deesserGainReduction;
        report.limiterReduction = audioStats.limiterGainReduction;
        report.voiceDetected = audioStats.voiceDetected;
    } else if (audioProcessor) {
        auto audioStats = audioProcessor->GetStats();
        report.inputLevel = audioStats.inputLevel;
        report.outputLevel = audioStats.outputLevel;
        report.currentGain = audioStats.currentGain;
        report.noiseGateOpen = audioStats.noiseGateOpen;
        report.noiseEstimate = audioStats.noiseEstimate;
    }

    return report;
}

VoiceApplicationFramework::FrameworkStats VoiceApplicationFramework::GetFrameworkStats() const {
    FrameworkStats stats;

    if (audioCodec) {
        auto audioStats = audioCodec->GetStatistics();
        stats.audioPacketsEncoded = audioStats.packetsEncoded;
        stats.audioPacketsDecoded = audioStats.packetsDecoded;
        stats.audioCompressionRatio = audioStats.averageCompressionRatio;
    }

    if (secureChannel) {
        auto securityStatus = secureChannel->GetStatus();
        stats.packetsEncrypted = securityStatus.packetsEncrypted;
        stats.packetsDecrypted = securityStatus.packetsDecrypted;
        stats.authenticationFailures = securityStatus.authenticationFailures;
    }

    if (reliableTransport) {
        auto transportStats = reliableTransport->GetStatistics();
        stats.packetsReliablySent = transportStats.packetsSent;
        stats.packetsLost = transportStats.packetsLost;
        stats.packetsRetransmitted = transportStats.packetsRetransmitted;
    }

    if (bitrateController) {
        auto brStats = bitrateController->GetStatistics();
        stats.currentBitrate = brStats.currentBitrate;
        stats.bitrateAdjustments = brStats.bitrateAdjustments;
    }

    if (networkMonitor) {
        auto monitorStats = networkMonitor->GetStatistics();
        stats.averageLatency = monitorStats.averageLatency;
        stats.averageJitter = 0.0;
    }
    
    // Audio processing stats (NEW)
    if (useAdvancedProcessing && advancedProcessor) {
        auto audioStats = advancedProcessor->GetAdvancedStats();
        stats.framesProcessed++;
        stats.averageInputLevel = audioStats.inputLevel;
        stats.averageOutputLevel = audioStats.outputLevel;
        stats.averageGainApplied = audioStats.currentGain;
    } else if (audioProcessor) {
        auto audioStats = audioProcessor->GetStats();
        stats.framesProcessed++;
        stats.averageInputLevel = audioStats.inputLevel;
        stats.averageOutputLevel = audioStats.outputLevel;
        stats.averageGainApplied = audioStats.currentGain;
    }

    return stats;
}

std::string VoiceApplicationFramework::GetNetworkGrade() const {
    auto quality = networkMonitor->GetNetworkQuality();
    return quality.description;
}

std::string VoiceApplicationFramework::GetRecommendations() const {
    auto linkMetrics = networkMonitor->GetMetrics();

    std::string recommendations;

    if (linkMetrics.packetLossRate > 0.05) {
        recommendations += "High packet loss detected. Consider reducing bitrate. ";
    }

    if (linkMetrics.latency > 150) {
        recommendations += "High latency. Check network congestion. ";
    }

    if (linkMetrics.jitter > 50) {
        recommendations += "High jitter. Network may be unstable. ";
    }
    
    // Audio processing recommendations (NEW)
    if (audioProcessor || advancedProcessor) {
        auto audioStats = useAdvancedProcessing && advancedProcessor ? 
            advancedProcessor->GetAdvancedStats() : 
            AudioProcessor::ProcessorStats();
        
        if (!useAdvancedProcessing && audioProcessor) {
            audioStats = audioProcessor->GetStats();
        }
        
        if (audioStats.inputLevel < -50.0f) {
            recommendations += "Mic level very low. AGC is boosting. ";
        }
        
        if (audioStats.noiseEstimate > -40.0f) {
            recommendations += "High background noise detected. ";
        }
    }

    if (recommendations.empty()) {
        recommendations = "Network quality is good.";
    }

    return recommendations;
}

// Audio processing control methods (NEW)
void VoiceApplicationFramework::SetNoiseGateEnabled(bool enabled) {
    if (advancedProcessor) {
        // Advanced processor doesn't support runtime config changes
        // Would need to reinitialize or add setter methods
    }
}

void VoiceApplicationFramework::SetAGCEnabled(bool enabled) {
    // Implementation would require adding setters to processor classes
}

void VoiceApplicationFramework::SetNoiseSuppressionEnabled(bool enabled) {
    // Implementation would require adding setters to processor classes
}

void VoiceApplicationFramework::SetDeesserEnabled(bool enabled) {
    // Implementation would require adding setters to processor classes
}

void VoiceApplicationFramework::SetLimiterEnabled(bool enabled) {
    // Implementation would require adding setters to processor classes
}

void VoiceApplicationFramework::SetNoiseGateThreshold(float thresholdDb) {
    config.noiseGateThreshold = thresholdDb;
    // Would need to reinitialize processor to apply
}

void VoiceApplicationFramework::SetAGCTargetLevel(float targetDb) {
    config.agcTargetLevel = targetDb;
    // Would need to reinitialize processor to apply
}

void VoiceApplicationFramework::SetNoiseSuppressionLevel(float level) {
    config.noiseSuppressionLevel = level;
    // Would need to reinitialize processor to apply
}

AudioProcessor::ProcessorStats VoiceApplicationFramework::GetAudioProcessorStats() const {
    if (audioProcessor) {
        return audioProcessor->GetStats();
    }
    return AudioProcessor::ProcessorStats();
}

AdvancedAudioProcessor::AdvancedStats VoiceApplicationFramework::GetAdvancedAudioStats() const {
    if (advancedProcessor) {
        return advancedProcessor->GetAdvancedStats();
    }
    return AdvancedAudioProcessor::AdvancedStats();
}

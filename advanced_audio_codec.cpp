//advanced_audio_codec.cpp:
#include "advanced_audio_codec.h"
#include <cstring>
#include <algorithm>
#include <iostream>
#include <opus/opus.h>

#pragma comment(lib, "opus.lib")

AdvancedAudioCodec::AdvancedAudioCodec() : encoder(nullptr), decoder(nullptr) {
}

AdvancedAudioCodec::~AdvancedAudioCodec() {
    if (encoder) {
        opus_encoder_destroy(encoder);
        encoder = nullptr;
    }
    if (decoder) {
        opus_decoder_destroy(decoder);
        decoder = nullptr;
    }
}

bool AdvancedAudioCodec::Initialize(const AudioConfig& cfg) {
    config = cfg;
    
    int error = OPUS_OK;
    
    // Create encoder
    encoder = opus_encoder_create(
        config.sampleRate,
        config.channels,
        OPUS_APPLICATION_VOIP,  // Optimized for voice
        &error
    );
    
    if (error != OPUS_OK || !encoder) {
        std::cerr << "Opus encoder creation failed: " << error << std::endl;
        return false;
    }

    // Create decoder
    decoder = opus_decoder_create(
        config.sampleRate,
        config.channels,
        &error
    );
    
    if (error != OPUS_OK || !decoder) {
        std::cerr << "Opus decoder creation failed: " << error << std::endl;
        opus_encoder_destroy(encoder);
        encoder = nullptr;
        return false;
    }

    // Configure encoder for low-latency, high-quality
    opus_encoder_ctl(encoder, OPUS_SET_BITRATE(config.targetBitrate));
    opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(config.complexity));
    
    if (config.enableDTX) {
        opus_encoder_ctl(encoder, OPUS_SET_DTX(1));
    }
    
    if (config.enableFEC) {
        opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(1));
        opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(config.packetLoss));
    }

    if (config.enableCBR) {
        opus_encoder_ctl(encoder, OPUS_SET_VBR(0));  // Constant Bitrate
    } else {
        opus_encoder_ctl(encoder, OPUS_SET_VBR(1));  // Variable Bitrate
    }

    // Maximum signal bandwidth
    opus_encoder_ctl(encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_FULLBAND));

    encodeBuffer.resize(4000);  // Max Opus packet size

    return true;
}

std::vector<uint8_t> AdvancedAudioCodec::Encode(const std::vector<int16_t>& pcmData) {
    if (!encoder || pcmData.empty()) {
        return std::vector<uint8_t>();
    }

    std::vector<uint8_t> encoded(4000);
    
    int encodedSize = opus_encode(
        encoder,
        pcmData.data(),
        static_cast<int>(pcmData.size() / config.channels),
        encoded.data(),
        static_cast<int>(encoded.size())
    );

    if (encodedSize < 0) {
        std::cerr << "Opus encoding error: " << encodedSize << std::endl;
        return std::vector<uint8_t>();
    }

    encoded.resize(encodedSize);

    // Update statistics
    {
        std::lock_guard<std::mutex> lock(statsMutex);
        statistics.packetsEncoded++;
        statistics.bytesIn += pcmData.size();
        statistics.bytesOut += encodedSize;
        
        if (statistics.packetsEncoded > 0) {
            statistics.averageCompressionRatio = 
                static_cast<double>(statistics.bytesIn) / statistics.bytesOut;
            statistics.averageBitrate = 
                (statistics.bytesOut * 8.0 * config.sampleRate) / 
                (config.frameSize * statistics.packetsEncoded);
        }
    }

    return encoded;
}

std::vector<int16_t> AdvancedAudioCodec::Decode(const std::vector<uint8_t>& opusData, int frameSize) {
    if (!decoder || opusData.empty()) {
        return std::vector<int16_t>();
    }

    std::vector<int16_t> decoded(frameSize * config.channels);
    
    int decodedSize = opus_decode(
        decoder,
        opusData.data(),
        static_cast<int>(opusData.size()),
        decoded.data(),
        frameSize,
        0  // decode_fec = 0 (no packet loss concealment for normal frames)
    );

    if (decodedSize < 0) {
        std::cerr << "Opus decoding error: " << decodedSize << std::endl;
        return std::vector<int16_t>();
    }

    decoded.resize(decodedSize * config.channels);

    // Update statistics
    {
        std::lock_guard<std::mutex> lock(statsMutex);
        statistics.packetsDecoded++;
    }

    return decoded;
}

void AdvancedAudioCodec::SetBitrate(int bitrate) {
    if (!encoder) return;
    
    config.targetBitrate = bitrate;
    opus_encoder_ctl(encoder, OPUS_SET_BITRATE(bitrate));
}

void AdvancedAudioCodec::SetComplexity(int complexity) {
    if (!encoder) return;
    
    config.complexity = std::max(0, std::min(10, complexity));
    opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(config.complexity));
}

int AdvancedAudioCodec::GetCurrentBitrate() const {
    return config.targetBitrate;
}

AdvancedAudioCodec::Statistics AdvancedAudioCodec::GetStatistics() const {
    std::lock_guard<std::mutex> lock(statsMutex);
    return statistics;
}

void AdvancedAudioCodec::ResetStatistics() {
    std::lock_guard<std::mutex> lock(statsMutex);
    statistics = Statistics();
}

#pragma once

#include <vector>
#include <complex>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <memory>                    // Добавлено
#include "audio_defs.h"
#include "voice_detector.h"

// ============================================================================
// Adaptive Spectral Noise Suppression System
// ============================================================================

class AdaptiveNoiseSuppression {
public:
    struct Config {
        float minGainFloor = 0.10f;
        float maxGainCeiling = 1.0f;
        bool enableAdaptiveFlooring = true;
    };

    struct NoiseProfile {
        std::vector<float> mean;
        std::vector<float> variance;
        std::vector<float> min;
        std::vector<float> max;
        int updateCount = 0;
    };

    AdaptiveNoiseSuppression();
    ~AdaptiveNoiseSuppression();

    bool Initialize(const Config& config);
    void ProcessFrame(float* audioData, int samples);
    void UpdateNoiseProfile(const std::vector<float>& magnitude, float voiceConfidence);
    void ResetNoiseProfile();
    float GetNoiseFloorDb() const;
    void SetVoiceConfidence(float confidence) { currentVoiceConfidence_ = confidence; }
    void SetVoiceDetector(VoiceDetector* detector) {
        voiceDetector_ = detector ? std::shared_ptr<VoiceDetector>(detector, [](VoiceDetector*) {}) : std::weak_ptr<VoiceDetector>();
    }
private:
    std::weak_ptr<VoiceDetector> voiceDetector_;   // Исправлено

    // Spectral gain history
    std::vector<float> prevGain;
    float gainSmoothingFactor = 0.85f;
    int noiseUpdateCounter = 0;
    float currentVoiceConfidence_ = 0.5f;   // добавьте это поле
    float colaNormFactor_;
    Config config_;
    bool initialized_ = false;

    std::vector<float> window_;
    std::vector<float> fftBuffer_;
    std::vector<float> overlapBuffer_;
    int overlapIndex_ = 0;

    NoiseProfile noiseProfile_;
    float noiseFloorDb_ = -60.0f;

    float adaptationRate_ = 0.02f;
    float confidenceDamping_ = 0.8f;
    int frameCounter_ = 0;

    std::vector<float> gainHistory_;

    // Helper functions
    void ApplyWindow(float* frame, int size);
    void ExtractOverlap(const float* input, int samples, std::vector<float>& frame);
    void PlaceOverlap(const float* frame, float* output, int samples);

    std::vector<float> ComputeSpectralGain(
        const std::vector<float>& magnitude,
        float voiceConfidence
    );

    // НОВЫЙ МЕТОД
    float EstimateVoiceConfidence(const float* audioData, int samples);

};

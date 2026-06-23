//advanced_audio_processor.h:
#pragma once

#include "audio_processor.h"
#include "voice_detector.h"
#include "adaptive_noise_suppression.h"
#include "distance_analyzer.h"
#include <complex>
#include <vector>
#include <memory>

// Advanced Audio Processor with professional-grade algorithms
// Includes: Spectral Noise Suppression, Multiband De-esser, Lookahead Limiter

class AdvancedAudioProcessor : public AudioProcessor {
public:
    struct AdvancedConfig : public ProcessorConfig {
        // Advanced Noise Suppression
        bool enableSpectralNS = true;
        float nsReductionAmount = 0.8f;     // How much noise to remove
        float nsSmoothingFactor = 0.8f;     // Temporal smoothing
        int nsAttackBlocks = 5;             // Attack time in blocks
        int nsReleaseBlocks = 50;           // Release time in blocks
        
        // Multiband De-esser
        bool enableMultibandDeesser = true;
        float deesserFreqLow = 3500.0f;     // расширили диапазон
        float deesserFreqMid = 6500.0f;
        float deesserFreqHigh = 11000.0f;

        float deesserThresholdLow = -18.0f;   // ← было -30
        float deesserThresholdMid = -22.0f;   // ← было -25
        float deesserThresholdHigh = -26.0f;   // ← было -20

        // Lookahead Limiter
        bool enableLookaheadLimiter = true;
        int lookaheadSamples = 48;          // 1ms at 48kHz
        float limiterCeiling = -1.0f;       // dB (true peak)
        
        // Dynamic EQ (for harsh frequencies)
        bool enableDynamicEQ = true;
        float harshFreqCenter = 3000.0f;    // Hz
        float harshFreqQ = 2.0f;
        float harshThreshold = -35.0f;
        
        // Voice Activity Detection (for smarter processing)
        bool enableVAD = true;
        float vadThreshold = -37.0f;     // было -34
        int vadHangover = 10;            // было 45 — теперь держит речь дольше

        // Noise Gate — теперь адаптивный
        float noiseGateThreshold = -38.0f;     // было -32
        float noiseGateAttack = 2.0f;          // быстрее реагирует
        float noiseGateRelease = 120.0f;       // плавнее отпускает
        float noiseGateHold = 180.0f;          // держит голос дольше

        // === АВТОМАТИЧЕСКАЯ АДАПТАЦИЯ ===
        bool enableAutoAdaptation = true;          // включена по умолчанию
        float adaptationSpeed = 0.15f;             // скорость подстройки (0.1 = медленно, 0.3 = быстро)

        float dynamicNoiseGateThreshold = -38.0f;
        float dynamicVADConfidence = 0.32f;
        float dynamicAGCTargetLevel = -20.0f;
        float dynamicVoiceLevelThreshold = -42.0f;
    };

    struct AdvancedStats : public ProcessorStats {
        bool voiceDetected = false;
        bool voiceActive = false;
        float voiceConfidence = 0.0f;  // <-- ДОБАВИТЬ
        float deesserGainReduction = 0.0f;
        float limiterGainReduction = 0.0f;
        float spectralNoiseLevel = -100.0f;
        float dynamicEQGain = 0.0f;
        float currentNoiseGateThreshold = -45.0f;
        float currentAGCTarget = -20.0f;
        bool shouldSend = false;
    };

    AdvancedAudioProcessor();
    ~AdvancedAudioProcessor();

    void SetVoiceThreshold(float threshold) {
        if (voiceDetector_) {
            voiceDetector_->SetThreshold(threshold);
        }
    }

    bool InitializeAdvanced(const AdvancedConfig& config);
    bool ApplyNoiseGateAndReturnState(float* audioData, int samples);
    void AdaptAllParametersAutomatically(const VoiceDetector::DetectionResult& detection, float currentRMS, bool gateOpen);
    void ProcessFrameAdvanced(float* audioData, int samples);
    void ProcessFrameAdvanced(int16_t* audioData, int samples);
    void StartCalibration();
    bool IsCalibrated() const;
    int GetCalibrationProgress() const;
    struct VoiceDetectorStats {
        bool isVoice = false;
        float confidence = 0.0f;
        float noiseFloor = -100.0f;
        float voiceLevel = -100.0f;
        bool isCalibrated = false;
        int calibrationProgress = 0;

        // Новые поля
        bool isImpulseNoise = false;
        bool isTonalNoise = false;
        bool isFrictionNoise = false;
        bool isPlosiveNoise = false;
        std::string rejectionReason;
    };
    VoiceDetectorStats GetVoiceDetectorStats() const;


    AdvancedStats GetAdvancedStats() const;
    AdvancedConfig advConfig;
    AdvancedStats advStats;

    float dynamicVoiceLevelThreshold = -42.0f;     // порог уровня голоса
    float dynamicVADConfidence = 0.30f;            // порог уверенности VAD
    float dynamicNoiseGateThreshold = -38.0f;
    float dynamicAGCTargetLevel = -18.0f;
    // В protected или public секции:
    void ApplyImpulseSuppressor(float* data, int samples);
    // Дополнительно (рекомендую)
    float dynamicDeesserThreshold = -22.0f;
protected:
    std::vector<float> prevGain;   // для временного сглаживания в ComputeSpectralGain
    std::vector<float> prevSpectralGain;   // для межкадрового сглаживания
    float spectralGainSmoothing = 0.7f;    // коэффициент сглаживания (0-1)
    std::unique_ptr<VoiceDetector> voiceDetector_;
    std::unique_ptr<AdaptiveNoiseSuppression> noiseSuppressor_;
    std::unique_ptr<DistanceAnalyzer> distanceAnalyzer_;

    mutable std::mutex advStatsMutex;

    // Spectral processing
    static constexpr int SPECTRAL_FFT_SIZE = 2048;
    static constexpr int SPECTRAL_HOP_SIZE = 512;
    static constexpr int SPECTRAL_OVERLAP = 4;
    
    std::vector<float> spectralWindow;
    std::vector<std::complex<float>> fftBuffer;
    std::vector<float> overlapBuffer[2];
    int overlapIndex = 0;
    
    // Noise profile
    std::vector<float> noiseProfile;
    std::vector<float> noiseProfileTmp;
    int noiseProfileFrames = 0;
    bool noiseProfileCollected = false;
    
    // Spectral smoothing
    std::vector<float> smoothedMagnitude;
    std::vector<float> previousGain;
    
    // Multiband de-esser filters
    struct FilterBank {
        float low[6] = {0};     // Coeffs for low band
        float mid[6] = {0};     // Coeffs for mid band
        float high[6] = {0};    // Coeffs for high band
        BiquadState lowState[2];
        BiquadState midState[2];
        BiquadState highState[2];
    };
    FilterBank deesserFilters;
    
    // Lookahead buffer
    std::vector<float> lookaheadBuffer[2];
    int lookaheadIndex = 0;
    
    // VAD state
    struct VADState {
        float energy = 0.0f;
        int hangoverCount = 0;
        bool speechActive = false;
    };
    VADState vadState[2];
    
    // Dynamic EQ state
    struct DynamicEQState {
        float envelope = 0.0f;
        float currentGain = 1.0f;
        BiquadState filterState[2];
    };
    DynamicEQState deqState[2];
    float deqCoeffs[6] = {0};

    // Private methods
    void InitializeSpectralProcessing();
    void ApplySpectralNoiseSuppression(float* audioData, int samples);
    void UpdateNoiseProfile(const std::vector<float>& magnitude);
    std::vector<float> ComputeSpectralGain(const std::vector<float>& magnitude);
    
    void InitializeMultibandDeesser();
    void AdaptParametersAutomatically(float currentRMS, bool voiceDetected);
    void ApplyMultibandDeesser(float* audioData, int samples);
    void CalculateBandpassCoeffs(float* coeffs, float freq, float bandwidth, bool isHighpass);
    
    void InitializeLookaheadLimiter();
    void ApplyLookaheadLimiter(float* audioData, int samples);
    
    void InitializeDynamicEQ();
    void ApplyHighFrequencyLimiter(float* audioData, int samples);
    void ApplyDynamicEQ(float* audioData, int samples);
    void CalculatePeakingEQCoeffs(float* coeffs, float freq, float q, float gainDb);
    
    void ApplyVAD(const float* audioData, int samples);

};

//audio_processor.h:
#pragma once

#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <deque>
#include <mutex>


// Professional Audio Processor for Voice Communication
// Solves: crackling, quiet voice, background noise, distortion
// No external APIs - pure C++ implementation

class AudioProcessor {
public:
    struct ProcessorConfig {
        int sampleRate = 48000;
        int channels = 1;
        int frameSize = 480;  // 10ms at 48kHz
        
        // Noise Gate (for quiet voice)
        bool enableNoiseGate = true;
        float noiseGateThreshold = -45.0f;  // dB
        float noiseGateAttack = 5.0f;       // ms
        float noiseGateRelease = 100.0f;    // ms
        float noiseGateHold = 100.0f;       // ms
        
        // AGC (Automatic Gain Control)
        bool enableAGC = true;
        float agcTargetLevel = -20.0f;      // dB
        float agcMaxGain = 30.0f;           // dB
        float agcMinGain = -10.0f;          // dB
        float agcAttack = 50.0f;            // ms
        float agcRelease = 200.0f;          // ms
        
        // Noise Suppression
        bool enableNoiseSuppression = true;
        float noiseSuppressionLevel = 0.6f; // 0.0 - 1.0
        
        // De-esser (for crackling/harsh sounds)
        bool enableDeesser = true;
        float deesserThreshold = -25.0f;    // dB
        float deesserFrequency = 6500.0f;   // Hz (sibilance range)
        float deesserRatio = 4.0f;          // compression ratio
        
        // DC Offset Removal
        bool enableDCRemoval = true;
        float dcRemovalAlpha = 0.995f;      // filter coefficient
        
        // Soft Limiter (prevent clipping)
        bool enableLimiter = true;
        float limiterThreshold = -3.0f;     // dB
        float limiterRelease = 10.0f;       // ms
        
        // High-pass filter (remove low rumble)
        bool enableHighPass = true;
        float highPassFreq = 80.0f;         // Hz
        
        // Low-pass filter (remove high frequency noise)
        bool enableLowPass = false;
        float lowPassFreq = 12000.0f;       // Hz
    };

    struct ProcessorStats {
        float currentGain = 0.0f;           // Current AGC gain in dB
        float noiseGateOpen = false;        // Noise gate state
        float inputLevel = -100.0f;         // Input level in dB
        float outputLevel = -100.0f;        // Output level in dB
        float noiseEstimate = -100.0f;      // Estimated noise floor
        float suppressionAmount = 0.0f;     // Current suppression
    };

    AudioProcessor();
    ~AudioProcessor();

    bool Initialize(const ProcessorConfig& config);
    void Shutdown();

    // Process audio frame - main entry point
    void ProcessFrame(int16_t* audioData, int samples);
    void ProcessFrame(float* audioData, int samples);

    // Individual processors (can be called separately)
    void ApplyDCRemoval(float* audioData, int samples);
    void ApplyHighPassFilter(float* audioData, int samples);
    void ApplyLowPassFilter(float* audioData, int samples);
    void ApplyNoiseGate(float* audioData, int samples);
    void ApplyAGC(float* audioData, int samples);
    void ApplyNoiseSuppression(float* audioData, int samples);
    void ApplyDeesser(float* audioData, int samples);
    void ApplyLimiter(float* audioData, int samples);

    // Get current statistics
    ProcessorStats GetStats() const;
    
    // Reset all processors
    void Reset();

    // Utility functions
    static float DbToLinear(float db);
    static float LinearToDb(float linear);
    static void FloatToInt16(const float* input, int16_t* output, int samples);
    static void Int16ToFloat(const int16_t* input, float* output, int samples);

protected:
    ProcessorConfig config;
    mutable std::mutex statsMutex;
    ProcessorStats stats;

    // DC Removal state
    float dcState[2] = {0.0f, 0.0f};  // For stereo
    
    // High-pass filter state (biquad)
    struct BiquadState {
        float x1 = 0.0f, x2 = 0.0f;
        float y1 = 0.0f, y2 = 0.0f;
    };
    BiquadState hpState[2];
    BiquadState lpState[2];
    float hpCoeffs[6] = {0};  // b0, b1, b2, a0, a1, a2
    float lpCoeffs[6] = {0};

    // Noise Gate state
    struct NoiseGateState {
        float envelope = 0.0f;
        float gain = 1.0f;
        int holdCount = 0;
        bool isOpen = false;
        float noiseEstimate = 0.0f;
        uint8_t consecutiveOpenFrames = 0;  // НОВОЕ: для минимальной длительности
    };
    NoiseGateState ngState[2];

    // AGC state
    struct AGCState {
        float currentGain = 0.0f;
        float targetGain = 0.0f;
        float envelope = 0.0f;
    };
    AGCState agcState[2];

    // Noise Suppression state
    struct NSState {
        std::vector<float> noiseProfile;
        std::vector<float> smoothedSpectrum;
        std::deque<float> energyHistory;
        float noiseEstimate = 0.0f;
        int framesSinceSpeech = 0;
    };
    NSState nsState[2];

    // De-esser state
    struct DeesserState {
        float envelope = 0.0f;
        float gain = 1.0f;
        std::deque<float> buffer;
    };
    DeesserState deesserState[2];

    // Limiter state
    struct LimiterState {
        float envelope = 0.0f;
        float gain = 1.0f;
    };
    LimiterState limiterState[2];

    // FFT-related for noise suppression
    static constexpr int FFT_SIZE = 512;
    static constexpr int OVERLAP = 2;
    std::vector<float> fftWindow;
    std::vector<float> fftBuffer[2];
    int fftBufferIndex = 0;

    // Precomputed tables
    std::vector<float> hammingWindow;
    std::vector<float> sineTable;

    // Private helper methods
    void CalculateHighPassCoeffs();
    void CalculateLowPassCoeffs();
    void ProcessBiquad(float* audioData, int samples, const float* coeffs, BiquadState* state, int stride = 1);
    void ProcessFFTNoiseSuppression(float* audioData, int channel);
    void SimpleNoiseSuppression(float* audioData, int samples, int channel);
    void UpdateNoiseEstimate(const float* audioData, int samples, int channel);
    float CalculateRMS(const float* audioData, int samples);
    float CalculatePeak(const float* audioData, int samples);
    
    // Optimized FFT for noise suppression
    void FFT(std::vector<float>& real, std::vector<float>& imag, bool inverse);
    void ApplySpectralGating(float* magnitude, int size, int channel);

};

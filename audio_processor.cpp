//audio_processor.cpp:
#include "audio_processor.h"
#include <cstring>
#include <algorithm>
#include <numeric>
#include <cmath>
#include "advanced_audio_processor.h"

// Constants
static const float PI = 3.14159265358979323846f;
static const float TWO_PI = 2.0f * PI;

AudioProcessor::AudioProcessor() {
}

AudioProcessor::~AudioProcessor() {
    Shutdown();
}

bool AudioProcessor::Initialize(const ProcessorConfig& cfg) {
    config = cfg;
    
    // Initialize FFT window
    hammingWindow.resize(FFT_SIZE);
    for (int i = 0; i < FFT_SIZE; i++) {
        hammingWindow[i] = 0.54f - 0.46f * cosf(TWO_PI * i / (FFT_SIZE - 1));
    }
    
    // Initialize FFT buffers
    for (int ch = 0; ch < 2; ch++) {
        fftBuffer[ch].resize(FFT_SIZE, 0.0f);
        nsState[ch].noiseProfile.resize(FFT_SIZE / 2 + 1, 0.0f);
        nsState[ch].smoothedSpectrum.resize(FFT_SIZE / 2 + 1, 0.0f);
    }
    
    // Calculate filter coefficients
    CalculateHighPassCoeffs();
    CalculateLowPassCoeffs();
    
    // Reset all states
    Reset();
    
    return true;
}

void AudioProcessor::Shutdown() {
    // Cleanup if needed
}

void AudioProcessor::Reset() {
    std::lock_guard<std::mutex> lock(statsMutex);
    
    // Reset DC removal
    memset(dcState, 0, sizeof(dcState));
    
    // Reset biquad filters
    memset(&hpState, 0, sizeof(hpState));
    memset(&lpState, 0, sizeof(lpState));
    
    // Reset noise gate
    for (int i = 0; i < 2; i++) {
        ngState[i] = NoiseGateState();
        agcState[i] = AGCState();
        nsState[i] = NSState();
        nsState[i].noiseProfile.resize(FFT_SIZE / 2 + 1, 0.0f);
        nsState[i].smoothedSpectrum.resize(FFT_SIZE / 2 + 1, 0.0f);
        deesserState[i] = DeesserState();
        limiterState[i] = LimiterState();
    }
    
    fftBufferIndex = 0;
    stats = ProcessorStats();
}

void AudioProcessor::ProcessFrame(int16_t* audioData, int samples) {
    if (!audioData || samples <= 0) return;
    
    // Convert to float
    std::vector<float> floatData(samples);
    Int16ToFloat(audioData, floatData.data(), samples);
    
    // Process in float
    ProcessFrame(floatData.data(), samples);
    
    // Convert back to int16
    FloatToInt16(floatData.data(), audioData, samples);
}

void AudioProcessor::ProcessFrame(float* audioData, int samples) {
    if (!audioData || samples <= 0) return;
    
    int frames = samples / config.channels;
    
    // Calculate input level
    float inputRMS = 0.0f;
    for (int i = 0; i < samples; i++) {
        inputRMS += audioData[i] * audioData[i];
    }
    inputRMS = sqrtf(inputRMS / samples);
    
    {
        std::lock_guard<std::mutex> lock(statsMutex);
        stats.inputLevel = LinearToDb(inputRMS + 1e-10f);
    }
    
    // 1. DC Offset Removal (first to prevent low-frequency artifacts)
    if (config.enableDCRemoval) {
        ApplyDCRemoval(audioData, samples);
    }
    
    // 2. High-pass filter (remove rumble)
    if (config.enableHighPass) {
        ApplyHighPassFilter(audioData, samples);
    }
    
    // 3. Low-pass filter (optional, remove high freq noise)
    if (config.enableLowPass) {
        ApplyLowPassFilter(audioData, samples);
    }
    
    // 4. Noise Suppression (before AGC to avoid amplifying noise)
    if (config.enableNoiseSuppression) {
        ApplyNoiseSuppression(audioData, samples);
    }
    
    // 5. Noise Gate (for quiet voice)
    if (config.enableNoiseGate) {
        ApplyNoiseGate(audioData, samples);
    }
    
    // 6. AGC (boost quiet voice)
    if (config.enableAGC) {
        ApplyAGC(audioData, samples);
    }
    
    // 7. De-esser (remove harsh sibilance/crackling)
    if (config.enableDeesser) {
        ApplyDeesser(audioData, samples);
    }
    
    // 8. Limiter (prevent clipping)
    if (config.enableLimiter) {
        ApplyLimiter(audioData, samples);
    }
    
    // Calculate output level
    float outputRMS = 0.0f;
    for (int i = 0; i < samples; i++) {
        outputRMS += audioData[i] * audioData[i];
    }
    outputRMS = sqrtf(outputRMS / samples);
    
    {
        std::lock_guard<std::mutex> lock(statsMutex);
        stats.outputLevel = LinearToDb(outputRMS + 1e-10f);
    }
}

// ============================================================================
// DC OFFSET REMOVAL
// ============================================================================
void AudioProcessor::ApplyDCRemoval(float* audioData, int samples) {
    for (int ch = 0; ch < config.channels; ch++) {
        float* state = &dcState[ch];
        
        for (int i = ch; i < samples; i += config.channels) {
            float input = audioData[i];
            float output = input - *state;
            *state = config.dcRemovalAlpha * *state + (1.0f - config.dcRemovalAlpha) * input;
            audioData[i] = output;
        }
    }
}

// ============================================================================
// BIQUAD FILTERS
// ============================================================================
void AudioProcessor::CalculateHighPassCoeffs() {
    float fc = config.highPassFreq / config.sampleRate;
    float Q = 0.707f;  // Butterworth
    
    float w0 = TWO_PI * fc;
    float cosw0 = cosf(w0);
    float sinw0 = sinf(w0);
    float alpha = sinw0 / (2.0f * Q);
    
    float a0 = 1.0f + alpha;
    
    hpCoeffs[0] = (1.0f + cosw0) / (2.0f * a0);  // b0
    hpCoeffs[1] = -(1.0f + cosw0) / a0;          // b1
    hpCoeffs[2] = (1.0f + cosw0) / (2.0f * a0);  // b2
    hpCoeffs[3] = 1.0f;                          // a0
    hpCoeffs[4] = -2.0f * cosw0 / a0;            // a1
    hpCoeffs[5] = (1.0f - alpha) / a0;           // a2
}

void AudioProcessor::CalculateLowPassCoeffs() {
    float fc = config.lowPassFreq / config.sampleRate;
    float Q = 0.707f;
    
    float w0 = TWO_PI * fc;
    float cosw0 = cosf(w0);
    float sinw0 = sinf(w0);
    float alpha = sinw0 / (2.0f * Q);
    
    float a0 = 1.0f + alpha;
    
    lpCoeffs[0] = (1.0f - cosw0) / (2.0f * a0);  // b0
    lpCoeffs[1] = (1.0f - cosw0) / a0;           // b1
    lpCoeffs[2] = (1.0f - cosw0) / (2.0f * a0);  // b2
    lpCoeffs[3] = 1.0f;                          // a0
    lpCoeffs[4] = -2.0f * cosw0 / a0;            // a1
    lpCoeffs[5] = (1.0f - alpha) / a0;           // a2
}
void AudioProcessor::ProcessBiquad(float* audioData, int samples, const float* coeffs, BiquadState* state, int stride) {
    float b0 = coeffs[0], b1 = coeffs[1], b2 = coeffs[2];
    float a1 = coeffs[4], a2 = coeffs[5];  // Assuming coeffs[3] is a0=1 normalized
    float x1 = state->x1, x2 = state->x2;
    float y1 = state->y1, y2 = state->y2;

    for (int i = 0; i < samples; i++) {
        float input = audioData[i * stride];
        float output = b0 * input + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;

        x2 = x1;
        x1 = input;
        y2 = y1;
        y1 = output;

        audioData[i * stride] = output;
    }

    state->x1 = x1; state->x2 = x2;
    state->y1 = y1; state->y2 = y2;
}

void AudioProcessor::ApplyHighPassFilter(float* audioData, int samples) {
    ProcessBiquad(audioData, samples, hpCoeffs, hpState);
}

void AudioProcessor::ApplyLowPassFilter(float* audioData, int samples) {
    ProcessBiquad(audioData, samples, lpCoeffs, lpState);
}

// ============================================================================
// NOISE GATE (for quiet voice)
// ============================================================================


// ============================================================================
// AUTOMATIC GAIN CONTROL (AGC)
// ============================================================================
void AudioProcessor::ApplyAGC(float* audioData, int samples) {
    float targetLinear = DbToLinear(config.agcTargetLevel);
    float maxGainLinear = DbToLinear(config.agcMaxGain);
    float minGainLinear = DbToLinear(config.agcMinGain);

    float attackCoeff = 1.0f - expf(-1000.0f / (config.agcAttack * config.sampleRate / (samples / config.channels + 1)));
    float releaseCoeff = 1.0f - expf(-1000.0f / (config.agcRelease * config.sampleRate / (samples / config.channels + 1)));

    for (int ch = 0; ch < config.channels; ch++) {
        AGCState* state = &agcState[ch];

        float sum = 0.0f; int count = 0;
        for (int i = ch; i < samples; i += config.channels) {
            sum += audioData[i] * audioData[i];
            count++;
        }
        float rms = sqrtf(sum / (count + 1e-8f));

        if (rms > state->envelope)
            state->envelope += attackCoeff * (rms - state->envelope);
        else
            state->envelope += releaseCoeff * (rms - state->envelope);

        float dynamicTarget = (rms < DbToLinear(-48.0f)) ? DbToLinear(-10.0f) : targetLinear;

        if (state->envelope > 1e-6f) {
            state->targetGain = dynamicTarget / state->envelope;
            state->targetGain = std::max(minGainLinear, std::min(maxGainLinear, state->targetGain));
            state->targetGain = std::min(state->targetGain, DbToLinear(16.0f));   // максимум +16 dB
        }

        state->currentGain += (state->targetGain - state->currentGain) * attackCoeff;
        state->currentGain = std::max(state->currentGain, DbToLinear(-8.0f));   // никогда ниже -8 dB

        for (int i = ch; i < samples; i += config.channels) {
            audioData[i] *= state->currentGain;
        }
    }

    {
        std::lock_guard<std::mutex> lock(statsMutex);
        stats.currentGain = LinearToDb(agcState[0].currentGain);
    }
}
// ============================================================================
// NOISE SUPPRESSION (Spectral Subtraction)
// ============================================================================
void AudioProcessor::ApplyNoiseSuppression(float* audioData, int samples) {
    // Use simple time-domain noise suppression for low latency
    SimpleNoiseSuppression(audioData, samples, 0);
    if (config.channels > 1) {
        SimpleNoiseSuppression(audioData, samples, 1);
    }
}

void AudioProcessor::SimpleNoiseSuppression(float* audioData, int samples, int channel) {
    NSState* state = &nsState[channel];
    
    // Update noise estimate during silence
    UpdateNoiseEstimate(audioData, samples, channel);
    
    float noiseGate = state->noiseEstimate * config.noiseSuppressionLevel;
    
    for (int i = channel; i < samples; i += config.channels) {
        float input = audioData[i];
        float inputAbs = fabsf(input);
        
        // Simple spectral gating
        if (inputAbs < noiseGate * 2.0f) {
            float reduction = (inputAbs - noiseGate) / noiseGate;
            reduction = std::max(0.0f, std::min(1.0f, reduction));
            audioData[i] = input * reduction * reduction;  // Smooth curve
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(statsMutex);
        stats.noiseEstimate = LinearToDb(state->noiseEstimate + 1e-10f);
        stats.suppressionAmount = config.noiseSuppressionLevel;
    }
}

void AudioProcessor::UpdateNoiseEstimate(const float* audioData, int samples, int channel) {
    NSState* state = &nsState[channel];
    
    // Calculate frame energy
    float energy = 0.0f;
    for (int i = channel; i < samples; i += config.channels) {
        energy += audioData[i] * audioData[i];
    }
    energy = sqrtf(energy / (samples / config.channels));
    
    // Track energy history
    state->energyHistory.push_back(energy);
    if (state->energyHistory.size() > 50) {
        state->energyHistory.pop_front();
    }
    
    // Find minimum energy (likely noise)
    if (state->energyHistory.size() >= 30) {
        float minEnergy = *std::min_element(state->energyHistory.begin(), state->energyHistory.end());
        
        // Update noise estimate with smoothing
        float alpha = 0.05f;
        state->noiseEstimate = (1.0f - alpha) * state->noiseEstimate + alpha * minEnergy;
    } else {
        state->noiseEstimate = energy * 0.1f;
    }
}

// ============================================================================
// DE-ESSER (for crackling/harsh sibilance)
// ============================================================================
void AudioProcessor::ApplyDeesser(float* audioData, int samples) {
    float thresholdLinear = DbToLinear(config.deesserThreshold);
    float ratio = config.deesserRatio;
    
    // Simple high-frequency envelope detection
    float attackCoeff = 0.1f;
    float releaseCoeff = 0.01f;
    
    for (int ch = 0; ch < config.channels; ch++) {
        DeesserState* state = &deesserState[ch];
        
        for (int i = ch; i < samples; i += config.channels) {
            float input = audioData[i];
            
            // High-frequency content detection (simple difference)
            float highFreq = input;
            if (i >= config.channels) {
                highFreq = input - audioData[i - config.channels];
            }
            float highFreqAbs = fabsf(highFreq);
            
            // Envelope follower
            if (highFreqAbs > state->envelope) {
                state->envelope += attackCoeff * (highFreqAbs - state->envelope);
            } else {
                state->envelope += releaseCoeff * (highFreqAbs - state->envelope);
            }
            
            // Compression for high frequencies
            float targetGain = 1.0f;
            if (state->envelope > thresholdLinear) {
                float excess = state->envelope - thresholdLinear;
                float compressed = excess / ratio;
                targetGain = (thresholdLinear + compressed) / state->envelope;
            }
            
            // Smooth gain
            state->gain += 0.1f * (targetGain - state->gain);
            
            audioData[i] = input * state->gain;
        }
    }
}

// ============================================================================
// SOFT LIMITER (prevent clipping)
// ============================================================================
void AudioProcessor::ApplyLimiter(float* audioData, int samples) {
    float thresholdLinear = DbToLinear(config.limiterThreshold);
    float releaseCoeff = 1.0f - expf(-1000.0f / (config.limiterRelease * config.sampleRate));
    
    for (int ch = 0; ch < config.channels; ch++) {
        LimiterState* state = &limiterState[ch];
        
        // Find peak in this frame
        float peak = 0.0f;
        for (int i = ch; i < samples; i += config.channels) {
            peak = std::max(peak, fabsf(audioData[i]));
        }
        
        // Update envelope
        if (peak > state->envelope) {
            state->envelope = peak;  // Instant attack
        } else {
            state->envelope += releaseCoeff * (peak - state->envelope);
        }
        
        // Calculate gain
        float targetGain = 1.0f;
        if (state->envelope > thresholdLinear) {
            targetGain = thresholdLinear / state->envelope;
        }
        
        // Smooth gain
        state->gain += 0.5f * (targetGain - state->gain);
        
        // Apply gain
        for (int i = ch; i < samples; i += config.channels) {
            audioData[i] *= state->gain;
        }
    }
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================
float AudioProcessor::DbToLinear(float db) {
    return powf(10.0f, db / 20.0f);
}

float AudioProcessor::LinearToDb(float linear) {
    return 20.0f * log10f(linear + 1e-10f);
}

void AudioProcessor::FloatToInt16(const float* input, int16_t* output, int samples) {
    for (int i = 0; i < samples; i++) {
        float sample = input[i];
        // Soft clipping
        if (sample > 1.0f) sample = 1.0f + tanhf(sample - 1.0f) * 0.1f;
        if (sample < -1.0f) sample = -1.0f + tanhf(sample + 1.0f) * 0.1f;
        
        // Convert to int16
        int32_t intSample = static_cast<int32_t>(sample * 32767.0f);
        intSample = std::max(-32768, std::min(32767, intSample));
        output[i] = static_cast<int16_t>(intSample);
    }
}

void AudioProcessor::Int16ToFloat(const int16_t* input, float* output, int samples) {
    for (int i = 0; i < samples; i++) {
        output[i] = static_cast<float>(input[i]) / 32768.0f;
    }
}

AudioProcessor::ProcessorStats AudioProcessor::GetStats() const {
    std::lock_guard<std::mutex> lock(statsMutex);
    return stats;
}

float AudioProcessor::CalculateRMS(const float* audioData, int samples) {
    float sum = 0.0f;
    for (int i = 0; i < samples; i++) {
        sum += audioData[i] * audioData[i];
    }
    return sqrtf(sum / samples);
}

float AudioProcessor::CalculatePeak(const float* audioData, int samples) {
    float peak = 0.0f;
    for (int i = 0; i < samples; i++) {
        peak = std::max(peak, fabsf(audioData[i]));
    }
    return peak;
}
// ============================================================================
// NOISE GATE (для тихого голоса) - УЛУЧШЕННАЯ ВЕРСИЯ
// ============================================================================
void AudioProcessor::ApplyNoiseGate(float* audioData, int samples) {
    float baseThreshold = DbToLinear(config.noiseGateThreshold);
    float attackCoeff = 1.0f - expf(-1000.0f / (config.noiseGateAttack * config.sampleRate));
    float releaseCoeff = 1.0f - expf(-1000.0f / (config.noiseGateRelease * config.sampleRate));
    int holdSamples = static_cast<int>(config.noiseGateHold * config.sampleRate / 1000.0f);

    for (int ch = 0; ch < config.channels; ch++) {
        NoiseGateState* state = &ngState[ch];

        for (int i = ch; i < samples; i += config.channels) {
            float input = audioData[i];
            float inputAbs = fabsf(input);

            // Обновление огибающей
            if (inputAbs > state->envelope)
                state->envelope += attackCoeff * (inputAbs - state->envelope);
            else
                state->envelope += releaseCoeff * (inputAbs - state->envelope);

            // Адаптивный порог
            state->noiseEstimate = 0.94f * state->noiseEstimate + 0.06f * state->envelope;
            float adaptiveThreshold = baseThreshold * (1.0f + 0.10f * state->noiseEstimate);

            // === УЛУЧШЕННАЯ логика с импульсным блокировщиком ===
            bool isImpulse = (state->envelope > DbToLinear(-8.0f)) &&
                (state->envelope / (state->noiseEstimate + 1e-8f) > 6.5f);

            if (isImpulse) {
                state->isOpen = false;
                state->holdCount = 0;
            }
            else if (state->envelope > adaptiveThreshold) {
                state->isOpen = true;
                state->holdCount = holdSamples;
            }
            else if (state->holdCount > 0) {
                state->holdCount--;
            }
            else {
                state->isOpen = false;
            }

            // Плавное применение усиления
            float targetGain = state->isOpen ? 1.0f : 0.04f;
            state->gain += (targetGain - state->gain) * (state->isOpen ? attackCoeff : releaseCoeff * 0.4f);

            audioData[i] = input * state->gain;
        }
    }

    {
        std::lock_guard<std::mutex> lock(statsMutex);
        stats.noiseGateOpen = ngState[0].isOpen;
    }
}

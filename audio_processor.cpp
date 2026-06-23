//audio_processor.cpp:
#include "audio_processor.h"
#include <cstring>
#include <algorithm>
#include <numeric>
#include <cmath>
#include "advanced_audio_processor.h"
#include "utility.h"


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
    AudioUtils::Int16ToFloat(audioData, floatData.data(), samples);
    
    // Process in float
    ProcessFrame(floatData.data(), samples);
    
    // Convert back to int16
    AudioUtils::FloatToInt16(floatData.data(), audioData, samples);
}

void AudioProcessor::ProcessFrame(float* audioData, int samples) {
    if (!audioData || samples <= 0) return;
    
    int frames = samples / NUM_CHANNELS;
    
    // Calculate input level
    float inputRMS = 0.0f;
    for (int i = 0; i < samples; i++) {
        inputRMS += audioData[i] * audioData[i];
    }
    inputRMS = sqrtf(inputRMS / samples);
    
    {
        std::lock_guard<std::mutex> lock(statsMutex);
        stats.inputLevel = AudioUtils::LinearToDb(inputRMS + 1e-10f);
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
        stats.outputLevel = AudioUtils::LinearToDb(outputRMS + 1e-10f);
    }
}

// ============================================================================
// DC OFFSET REMOVAL
// ============================================================================
void AudioProcessor::ApplyDCRemoval(float* audioData, int samples) {
    for (int ch = 0; ch < NUM_CHANNELS; ch++) {
        float* state = &dcState[ch];
        
        for (int i = ch; i < samples; i += NUM_CHANNELS) {
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
    float fc = config.highPassFreq / SAMPLE_RATE;
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
    float fc = config.lowPassFreq / SAMPLE_RATE;
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
    float targetLinear = AudioUtils::DbToLinear(config.agcTargetLevel);
    // Ограничиваем максимальное усиление +16 dB (40x)
    float maxGainLinear = AudioUtils::DbToLinear(16.0f);
    float minGainLinear = AudioUtils::DbToLinear(-12.0f);

    // Коэффициенты атаки и спада для огибающей RMS
    float attackCoeff = 1.0f - expf(-1000.0f / (config.agcAttack * SAMPLE_RATE / (samples / NUM_CHANNELS + 1)));
    float releaseCoeff = 1.0f - expf(-1000.0f / (config.agcRelease * SAMPLE_RATE / (samples / NUM_CHANNELS + 1)));

    for (int ch = 0; ch < NUM_CHANNELS; ch++) {
        AGCState* state = &agcState[ch];

        // Вычисляем RMS текущего кадра
        float rms = 0.0f;
        int count = 0;
        for (int i = ch; i < samples; i += NUM_CHANNELS) {
            rms += audioData[i] * audioData[i];
            count++;
        }
        rms = sqrtf(rms / (count + 1e-8f));

        // Обновляем огибающую RMS
        if (rms > state->envelope)
            state->envelope += attackCoeff * (rms - state->envelope);
        else
            state->envelope += releaseCoeff * (rms - state->envelope);

        // Динамический целевой уровень: для очень тихих участков усиляем сильнее
        float dynamicTarget = targetLinear;
        if (rms < AudioUtils::DbToLinear(-45.0f)) {
            dynamicTarget = AudioUtils::DbToLinear(-10.0f);   // целевой -10 dB
        }

        // Вычисляем желаемое усиление
        float targetGain = dynamicTarget / (state->envelope + 1e-8f);
        targetGain = std::max(minGainLinear, std::min(maxGainLinear, targetGain));

        // Сглаживание изменения усиления (атака быстрее, спад медленнее)
        float gainSmooth = 0.2f;
        if (targetGain > state->currentGain) {
            // Атака: быстрее поднимаем уровень
            state->currentGain += gainSmooth * (targetGain - state->currentGain);
        }
        else {
            // Спад: очень медленно опускаем, чтобы не создавать щелчков
            state->currentGain += 0.05f * (targetGain - state->currentGain);
        }
        state->currentGain = std::max(minGainLinear, std::min(maxGainLinear, state->currentGain));

        // Применяем усиление
        for (int i = ch; i < samples; i += NUM_CHANNELS) {
            audioData[i] *= state->currentGain;
        }
    }

    {
        std::lock_guard<std::mutex> lock(statsMutex);
        stats.currentGain = AudioUtils::LinearToDb(agcState[0].currentGain);
    }
}
// ============================================================================
// NOISE SUPPRESSION (Spectral Subtraction)
// ============================================================================
void AudioProcessor::ApplyNoiseSuppression(float* audioData, int samples) {
    // Use simple time-domain noise suppression for low latency
    SimpleNoiseSuppression(audioData, samples, 0);
    if (NUM_CHANNELS > 1) {
        SimpleNoiseSuppression(audioData, samples, 1);
    }
}

void AudioProcessor::SimpleNoiseSuppression(float* audioData, int samples, int channel) {
    NSState* state = &nsState[channel];
    
    // Update noise estimate during silence
    UpdateNoiseEstimate(audioData, samples, channel);
    
    float noiseGate = state->noiseEstimate * config.noiseSuppressionLevel;
    
    for (int i = channel; i < samples; i += NUM_CHANNELS) {
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
        stats.noiseEstimate = AudioUtils::LinearToDb(state->noiseEstimate + 1e-10f);
        stats.suppressionAmount = config.noiseSuppressionLevel;
    }
}

void AudioProcessor::UpdateNoiseEstimate(const float* audioData, int samples, int channel) {
    NSState* state = &nsState[channel];
    
    // Calculate frame energy
    float energy = 0.0f;
    for (int i = channel; i < samples; i += NUM_CHANNELS) {
        energy += audioData[i] * audioData[i];
    }
    energy = sqrtf(energy / (samples / NUM_CHANNELS));
    
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
    float thresholdLinear = AudioUtils::DbToLinear(config.deesserThreshold);
    float ratio = config.deesserRatio;            // обычно 2.0 – 4.0
    float attack = 0.1f;     // быстрая атака
    float release = 0.02f;   // медленный спад, чтобы не было свиста

    for (int ch = 0; ch < NUM_CHANNELS; ch++) {
        DeesserState* state = &deesserState[ch];

        // Простой фильтр верхних частот для выделения сибилянтов (~6 кГц)
        // Используем разность между соседними отсчётами (high-pass 1-го порядка)
        float envelope = state->envelope;
        float gain = state->gain;

        for (int i = ch; i < samples; i += NUM_CHANNELS) {
            float input = audioData[i];
            // Эмуляция high-pass: вычитаем предыдущий отсчёт
            float highFreq = input;
            if (i >= NUM_CHANNELS) {
                highFreq = input - audioData[i - NUM_CHANNELS];
            }
            float absHigh = fabsf(highFreq);

            // Огибающая высокочастотной составляющей
            if (absHigh > envelope)
                envelope += attack * (absHigh - envelope);
            else
                envelope += release * (absHigh - envelope);

            // Компрессия
            float targetGain = 1.0f;
            if (envelope > thresholdLinear) {
                float excess = envelope - thresholdLinear;
                float compressed = thresholdLinear + excess / ratio;
                targetGain = compressed / envelope;
                // Ограничение глубины сжатия: не более -6 dB
                targetGain = std::max(0.5f, targetGain);
            }

            // Сглаживание gain
            gain += 0.05f * (targetGain - gain);
            gain = std::max(0.5f, std::min(1.0f, gain));

            audioData[i] = input * gain;
        }

        state->envelope = envelope;
        state->gain = gain;
    }
}

// ============================================================================
// SOFT LIMITER (prevent clipping)
// ============================================================================
void AudioProcessor::ApplyLimiter(float* audioData, int samples) {
    float thresholdLinear = AudioUtils::DbToLinear(config.limiterThreshold);
    float releaseCoeff = 1.0f - expf(-1000.0f / (config.limiterRelease * SAMPLE_RATE));
    
    for (int ch = 0; ch < NUM_CHANNELS; ch++) {
        LimiterState* state = &limiterState[ch];
        
        // Find peak in this frame
        float peak = 0.0f;
        for (int i = ch; i < samples; i += NUM_CHANNELS) {
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
        for (int i = ch; i < samples; i += NUM_CHANNELS) {
            audioData[i] *= state->gain;
        }
    }
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

AudioProcessor::ProcessorStats AudioProcessor::GetStats() const {
    std::lock_guard<std::mutex> lock(statsMutex);
    return stats;
}

// ============================================================================
// NOISE GATE (для тихого голоса) - УЛУЧШЕННАЯ ВЕРСИЯ
// ============================================================================
void AudioProcessor::ApplyNoiseGate(float* audioData, int samples) {
    float baseThreshold = AudioUtils::DbToLinear(config.noiseGateThreshold);
    float attackCoeff = 1.0f - expf(-1000.0f / (config.noiseGateAttack * SAMPLE_RATE));
    float releaseCoeff = 1.0f - expf(-1000.0f / (config.noiseGateRelease * SAMPLE_RATE));
    // Очень плавное закрытие (медленный спад) для устранения щелчков
    float smoothRelease = releaseCoeff * 0.2f;  // в 5 раз медленнее основного релиза
    int holdSamples = static_cast<int>(config.noiseGateHold * SAMPLE_RATE / 1000.0f);

    for (int ch = 0; ch < NUM_CHANNELS; ch++) {
        NoiseGateState* state = &ngState[ch];
        // Дополнительная переменная для плавного затухания
        float envelope = state->envelope;
        float noiseEstimate = state->noiseEstimate;
        int holdCount = state->holdCount;
        bool isOpen = state->isOpen;
        float gain = state->gain;

        for (int i = ch; i < samples; i += NUM_CHANNELS) {
            float input = audioData[i];
            float absInput = fabsf(input);

            // Огибающая с разными постоянными для атаки и спада
            if (absInput > envelope)
                envelope += attackCoeff * (absInput - envelope);
            else
                envelope += releaseCoeff * (absInput - envelope);

            // Оценка фонового шума (медленное обновление)
            noiseEstimate = 0.99f * noiseEstimate + 0.01f * envelope;
            float adaptiveThreshold = baseThreshold * (1.0f + 0.1f * noiseEstimate);

            // Детектор импульсных помех (щелчки клавиатуры и т.п.)
            bool isImpulse = (envelope > AudioUtils::DbToLinear(-12.0f)) &&
                (envelope > adaptiveThreshold * 8.0f) &&
                (envelope / (noiseEstimate + 1e-8f) > 10.0f);

            if (isImpulse) {
                // Импульс – не открываем гейт
                isOpen = false;
                holdCount = 0;
                // Резко уменьшаем усиление, но без щелчка
                gain = 0.05f;
            }
            else {
                if (envelope > adaptiveThreshold) {
                    isOpen = true;
                    holdCount = holdSamples;
                }
                else if (holdCount > 0) {
                    holdCount--;
                    if (holdCount == 0) {
                        isOpen = false;
                    }
                }
                else {
                    isOpen = false;
                }
            }

            // Плавное изменение усиления
            float targetGain = isOpen ? 1.0f : 0.05f;
            if (isOpen) {
                // Атака: быстрее открываем
                gain += attackCoeff * (targetGain - gain);
            }
            else {
                // Закрытие: очень плавное, экспоненциальное затухание
                gain += smoothRelease * (targetGain - gain);
            }
            gain = std::max(0.02f, std::min(1.0f, gain));  // не ниже -34 dB

            audioData[i] = input * gain;
        }

        // Сохраняем состояние
        state->envelope = envelope;
        state->noiseEstimate = noiseEstimate;
        state->holdCount = holdCount;
        state->isOpen = isOpen;
        state->gain = gain;
    }

    {
        std::lock_guard<std::mutex> lock(statsMutex);
        stats.noiseGateOpen = ngState[0].isOpen;
    }
}

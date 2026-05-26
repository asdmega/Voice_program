//advanced_audio_processor.cpp:
#include "advanced_audio_processor.h"
#include <cstring>
#include <algorithm>
#include <numeric>
#include "voice_detector.h"
#include <iostream>

static const float PI = 3.14159265358979323846f;
static const float TWO_PI = 2.0f * PI;

AdvancedAudioProcessor::AdvancedAudioProcessor() {
    voiceDetector_ = std::make_unique<VoiceDetector>();  // уже есть
}

AdvancedAudioProcessor::~AdvancedAudioProcessor() {
}

bool AdvancedAudioProcessor::InitializeAdvanced(const AdvancedConfig& config) {
    advConfig = config;

    if (!Initialize(config)) return false;

    // Принудительная калибровка VAD при старте
    voiceDetector_->StartCalibration();

    // Discord-подобные настройки
    advConfig.noiseGateThreshold = -38.0f;     // чувствительнее
    advConfig.noiseGateAttack = 3.0f;
    advConfig.noiseGateRelease = 120.0f;

    // === НОВОЕ: КОМФОРТНЫЙ ШУМ (чтобы не было полной тишины) ===
    //enableComfortNoise = true;                    // добавь это поле в AdvancedConfig

    // === ИСПРАВЛЕНИЕ: ИНИЦИАЛИЗАЦИЯ VoiceDetector ===
    VoiceDetector::DetectorConfig vadConfig;
    vadConfig.sampleRate = advConfig.sampleRate;
    vadConfig.channels = advConfig.channels;
    vadConfig.frameSize = advConfig.frameSize;
    vadConfig.fftSize = 2048;          // как в твоём коде
    vadConfig.hopSize = 960;           // должно совпадать с frameSize
    vadConfig.voiceHangoverMs = 400.0f;       // твои настройки
    vadConfig.minVoiceConfidence = 0.4f; // ,skj 0.35f; // чуть выше, чтобы не было слишком много ложных срабатываний

    if (!voiceDetector_->Initialize(vadConfig)) {
        std::cerr << "VoiceDetector initialization failed!" << std::endl;
        return false;
    }
    // ================================================

    // Initialize spectral processing
    InitializeSpectralProcessing();

    // Initialize multiband de-esser
    InitializeMultibandDeesser();
    
    // Initialize lookahead limiter
    InitializeLookaheadLimiter();
    
    // Initialize dynamic EQ
    InitializeDynamicEQ();
    
    return true;
}

void AdvancedAudioProcessor::InitializeSpectralProcessing() {
    // Create Hann window
    spectralWindow.resize(SPECTRAL_FFT_SIZE);
    for (int i = 0; i < SPECTRAL_FFT_SIZE; i++) {
        spectralWindow[i] = 0.5f - 0.5f * cosf(TWO_PI * i / (SPECTRAL_FFT_SIZE - 1));
    }
    
    // Initialize buffers
    fftBuffer.resize(SPECTRAL_FFT_SIZE);
    noiseProfile.resize(SPECTRAL_FFT_SIZE / 2 + 1, 0.0f);
    noiseProfileTmp.resize(SPECTRAL_FFT_SIZE / 2 + 1, 0.0f);
    smoothedMagnitude.resize(SPECTRAL_FFT_SIZE / 2 + 1, 0.0f);
    previousGain.resize(SPECTRAL_FFT_SIZE / 2 + 1, 1.0f);
    
    for (int ch = 0; ch < 2; ch++) {
        overlapBuffer[ch].resize(SPECTRAL_FFT_SIZE - SPECTRAL_HOP_SIZE, 0.0f);
    }
}

void AdvancedAudioProcessor::InitializeMultibandDeesser() {
    // Calculate coefficients for three bands
    // Low band: 4-6 kHz
    CalculateBandpassCoeffs(deesserFilters.low, advConfig.deesserFreqLow, 2000.0f, false);
    // Mid band: 6-8 kHz
    CalculateBandpassCoeffs(deesserFilters.mid, advConfig.deesserFreqMid, 2000.0f, false);
    // High band: 8-12 kHz
    CalculateBandpassCoeffs(deesserFilters.high, advConfig.deesserFreqHigh, 4000.0f, false);
}
void AdvancedAudioProcessor::StartCalibration() {
    if (voiceDetector_) {
        voiceDetector_->StartCalibration();
    }
}

bool AdvancedAudioProcessor::IsCalibrated() const {
    if (voiceDetector_) {
        return voiceDetector_->IsCalibrated();
    }
    return false;
}

int AdvancedAudioProcessor::GetCalibrationProgress() const {
    if (voiceDetector_) {
        auto result = voiceDetector_->GetLastResult();
        return result.calibrationProgress;
    }
    return 0;
}

AdvancedAudioProcessor::VoiceDetectorStats AdvancedAudioProcessor::GetVoiceDetectorStats() const {
    VoiceDetectorStats stats;
    if (voiceDetector_) {
        auto result = voiceDetector_->GetLastResult();
        stats.isVoice = result.isVoice;
        stats.confidence = result.confidence;
        stats.noiseFloor = result.noiseFloor;
        stats.voiceLevel = result.voiceLevel;
        stats.isCalibrated = result.isCalibrated;
        stats.calibrationProgress = result.calibrationProgress;
        stats.isImpulseNoise = result.isImpulseNoise;
        stats.isTonalNoise = result.isTonalNoise;
        stats.isFrictionNoise = result.isFrictionNoise;
        stats.isPlosiveNoise = result.isPlosiveNoise;
        stats.rejectionReason = result.rejectionReason;
    }
    return stats;
}

void AdvancedAudioProcessor::CalculateBandpassCoeffs(float* coeffs, float freq, float bandwidth, bool isHighpass) {
    float fc = freq / advConfig.sampleRate;
    float bw = bandwidth / advConfig.sampleRate;
    float Q = freq / bandwidth;
    
    float w0 = TWO_PI * fc;
    float cosw0 = cosf(w0);
    float sinw0 = sinf(w0);
    float alpha = sinw0 / (2.0f * Q);
    
    float a0 = 1.0f + alpha;
    
    if (isHighpass) {
        coeffs[0] = (1.0f + cosw0) / (2.0f * a0);
        coeffs[1] = -(1.0f + cosw0) / a0;
        coeffs[2] = (1.0f + cosw0) / (2.0f * a0);
    } else {
        coeffs[0] = alpha / a0;
        coeffs[1] = 0.0f;
        coeffs[2] = -alpha / a0;
    }
    coeffs[3] = 1.0f;
    coeffs[4] = -2.0f * cosw0 / a0;
    coeffs[5] = (1.0f - alpha) / a0;
}

void AdvancedAudioProcessor::InitializeLookaheadLimiter() {
    for (int ch = 0; ch < 2; ch++) {
        lookaheadBuffer[ch].resize(advConfig.lookaheadSamples, 0.0f);
    }
}

void AdvancedAudioProcessor::InitializeDynamicEQ() {
    // Calculate peaking EQ coefficients (initially flat)
    CalculatePeakingEQCoeffs(deqCoeffs, advConfig.harshFreqCenter, advConfig.harshFreqQ, 0.0f);
}

void AdvancedAudioProcessor::CalculatePeakingEQCoeffs(float* coeffs, float freq, float q, float gainDb) {
    float A = powf(10.0f, gainDb / 40.0f);
    float w0 = TWO_PI * freq / advConfig.sampleRate;
    float cosw0 = cosf(w0);
    float sinw0 = sinf(w0);
    float alpha = sinw0 / (2.0f * q);
    
    float a0 = 1.0f + alpha / A;
    
    coeffs[0] = (1.0f + alpha * A) / a0;
    coeffs[1] = (-2.0f * cosw0) / a0;
    coeffs[2] = (1.0f - alpha * A) / a0;
    coeffs[3] = 1.0f;
    coeffs[4] = (-2.0f * cosw0) / a0;
    coeffs[5] = (1.0f - alpha / A) / a0;
}

// В advanced_audio_processor.cpp, в методе ProcessFrameAdvanced:
bool AdvancedAudioProcessor::ApplyNoiseGateAndReturnState(float* audioData, int samples) {
    float baseThreshold = DbToLinear(advConfig.noiseGateThreshold);  // сейчас будет -38
    float attackCoeff = 1.0f - expf(-3000.0f / (advConfig.noiseGateAttack * advConfig.sampleRate));   // быстрее атака
    float releaseCoeff = 1.0f - expf(-800.0f / (advConfig.noiseGateRelease * advConfig.sampleRate));

    bool anyGateOpen = false;

    for (int ch = 0; ch < advConfig.channels; ch++) {
        auto* state = &ngState[ch];

        for (int i = ch; i < samples; i += advConfig.channels) {
            float absVal = fabsf(audioData[i]);

            if (absVal > state->envelope)
                state->envelope += attackCoeff * (absVal - state->envelope);
            else
                state->envelope += releaseCoeff * (absVal - state->envelope);

            bool open = (state->envelope > baseThreshold * 0.92f);  // чуть мягче
            if (open) anyGateOpen = true;

            float target = open ? 1.0f : 0.025f;
            state->gain += (target - state->gain) * (open ? attackCoeff : releaseCoeff * 0.7f);

            audioData[i] *= state->gain;
        }
    }
    return anyGateOpen;
}

void AdvancedAudioProcessor::AdaptAllParametersAutomatically(const VoiceDetector::DetectionResult& detection,float currentRMS,bool gateOpen)
{
    static int64_t lastVoiceTime = 0;
    auto now = std::chrono::steady_clock::now().time_since_epoch().count() / 1'000'000;

    // Динамический порог Noise Gate
    dynamicNoiseGateThreshold = -38.0f - (currentRMS < DbToLinear(-45.0f) ? 8.0f : 0.0f);

    // Динамический VAD confidence
    dynamicVADConfidence = 0.28f;
    if (detection.voiceLevel < -38.0f) dynamicVADConfidence -= 0.06f;     // тихий голос
    if (now - lastVoiceTime > 800)     dynamicVADConfidence -= 0.04f;     // долго нет голоса

    // Динамический AGC target
    dynamicAGCTargetLevel = (currentRMS < DbToLinear(-42.0f)) ? -12.0f : -20.0f;

    // Динамический уровень шумоподавления
    advConfig.nsReductionAmount = 0.75f + (detection.noiseFloor > -35.0f ? 0.15f : 0.0f);

    // Динамические de-esser пороги
    advConfig.deesserThresholdLow = -19.0f - (currentRMS > DbToLinear(-22.0f) ? 4.0f : 0.0f);
    advConfig.deesserThresholdMid = -23.0f;
    advConfig.deesserThresholdHigh = -27.0f;

    if (detection.isVoice) lastVoiceTime = now;
}

void AdvancedAudioProcessor::ProcessFrameAdvanced(float* audioData, int samples) {
    if (!audioData || samples <= 0 || !voiceDetector_) return;
    
    // 1. Noise Gate (открывается на любой звук)
    bool gateOpen = ApplyNoiseGateAndReturnState(audioData, samples);

    // 2. VoiceDetector
    std::vector<float> frameData(audioData, audioData + samples);
    auto detection = voiceDetector_->ProcessFrame(frameData);

    // 3. Адаптация всех параметров в реальном времени
    float currentRMS = CalculateRMS(audioData, samples);
    AdaptAllParametersAutomatically(detection, currentRMS, gateOpen);

    // 4. ГЛАВНОЕ РЕШЕНИЕ ОТПРАВКИ (твоя логика)
    bool voiceActive = gateOpen &&
        detection.isVoice && 
        detection.confidence >= dynamicVADConfidence &&
        detection.voiceLevel > dynamicVoiceLevelThreshold;

    // Обновляем статистику
    {
        std::lock_guard<std::mutex> lock(advStatsMutex);
        advStats.voiceDetected = detection.isVoice;
        advStats.voiceConfidence = detection.confidence;
        advStats.noiseGateOpen = gateOpen;
        advStats.voiceActive = voiceActive;
    }

    // 5. ЕСЛИ НЕ ГОЛОС — полностью глушим и ВЫХОДИМ (больше пакетов тишины!)
    if (!voiceActive) {
        for (int i = 0; i < samples; i++) audioData[i] *= 0.085f; // почти тишина
        return;
    }

    // 6. Если голос — полный Discord-style процессинг
    if (advConfig.enableDCRemoval)      ApplyDCRemoval(audioData, samples);
    if (advConfig.enableHighPass)       ApplyHighPassFilter(audioData, samples);
    //if (advConfig.enableSpectralNS)     ApplySpectralNoiseSuppression(audioData, samples);
    if (advConfig.enableAGC)            ApplyAGC(audioData, samples);
    if (advConfig.enableMultibandDeesser) ApplyMultibandDeesser(audioData, samples);
    if (advConfig.enableLimiter)        ApplyLookaheadLimiter(audioData, samples);
}

void AdvancedAudioProcessor::ProcessFrameAdvanced(int16_t* audioData, int samples) {
    if (!audioData || samples <= 0) return;
    
    std::vector<float> floatData(samples);
    Int16ToFloat(audioData, floatData.data(), samples);
    
    ProcessFrameAdvanced(floatData.data(), samples);
    
    FloatToInt16(floatData.data(), audioData, samples);
}

void AdvancedAudioProcessor::ApplySpectralNoiseSuppression(float* audioData, int samples) {
    int hop = SPECTRAL_HOP_SIZE;
    int frames = samples / advConfig.channels;

    for (int ch = 0; ch < advConfig.channels; ch++) {
        std::vector<float> frame(SPECTRAL_FFT_SIZE, 0.0f);

        // Копируем с overlap
        int overlapSize = SPECTRAL_FFT_SIZE - hop;
        for (int i = 0; i < overlapSize; i++) {
            frame[i] = overlapBuffer[ch][i];
        }
        for (int i = 0; i < hop && i < frames; i++) {
            frame[overlapSize + i] = audioData[ch + i * advConfig.channels];
        }

        // Окно
        for (int i = 0; i < SPECTRAL_FFT_SIZE; i++) frame[i] *= spectralWindow[i];

        // === FFT ===
        std::vector<std::complex<float>> freq(SPECTRAL_FFT_SIZE);
        ComputeFFT(frame.data(), freq.data(), SPECTRAL_FFT_SIZE);

        // Магнитуда
        std::vector<float> magnitude(SPECTRAL_FFT_SIZE / 2 + 1);
        for (size_t i = 0; i < magnitude.size(); i++) {
            magnitude[i] = std::abs(freq[i]);
        }

        UpdateNoiseProfile(magnitude);
        auto gain = ComputeSpectralGain(magnitude);

        // Применяем подавление
        for (size_t i = 0; i < magnitude.size(); i++) {
            freq[i] *= gain[i];
        }

        // === IFFT ===
        ComputeIFFT(freq.data(), frame.data(), SPECTRAL_FFT_SIZE);

        // Копируем обратно
        for (int i = 0; i < hop && i < frames; i++) {
            int idx = ch + i * advConfig.channels;
            if (idx < samples) audioData[idx] = frame[i + overlapSize];
        }

        // Сохраняем overlap
        for (int i = 0; i < overlapSize; i++) {
            overlapBuffer[ch][i] = frame[hop + i];
        }
    }
}
// ============================================================================
// АВТОМАТИЧЕСКАЯ АДАПТАЦИЯ ПАРАМЕТРОВ ПОД РАССТОЯНИЕ И ГРОМКОСТЬ
// ============================================================================
void AdvancedAudioProcessor::AdaptParametersAutomatically(float currentRMS, bool voiceDetected) {
    if (!advConfig.enableAutoAdaptation) return;

    // === 1. Адаптация Noise Gate (при отдалении повышаем чувствительность) ===
    float targetNG = -38.0f;                    // базовый порог
    if (currentRMS < DbToLinear(-42.0f)) {      // очень тихий голос (далеко)
        targetNG = -48.0f;                      // делаем гейт более чувствительным
    }
    else if (currentRMS > DbToLinear(-25.0f)) { // громкий голос (близко)
        targetNG = -32.0f;
    }

    // Плавная подстройка
    config.noiseGateThreshold = 0.85f * config.noiseGateThreshold + 0.15f * targetNG;

    // === 2. Адаптация AGC Target Level ===
    float targetAGC = -20.0f;
    if (currentRMS < DbToLinear(-40.0f)) {
        targetAGC = -12.0f;                     // сильно усиливаем тихий голос
    }
    else if (currentRMS > DbToLinear(-18.0f)) {
        targetAGC = -24.0f;                     // уменьшаем усиление при громкой речи
    }

    config.agcTargetLevel = 0.82f * config.agcTargetLevel + 0.18f * targetAGC;

    // === 3. Адаптация VAD Threshold ===
    float targetVAD = -36.0f;
    if (currentRMS < DbToLinear(-45.0f)) {
        targetVAD = -48.0f;                     // ловим очень тихий голос на расстоянии
    }
    advConfig.vadThreshold = 0.88f * advConfig.vadThreshold + 0.12f * targetVAD;

    // Обновляем статистику для отображения
    {
        std::lock_guard<std::mutex> lock(advStatsMutex);
        advStats.currentNoiseGateThreshold = config.noiseGateThreshold;
        advStats.currentAGCTarget = config.agcTargetLevel;
    }
}
void AdvancedAudioProcessor::ApplyMultibandDeesser(float* audioData, int samples) {
    float totalGainReduction = 0.0f;
    int reductionCount = 0;
    
    for (int ch = 0; ch < advConfig.channels; ch++) {
        // Process each band
        std::vector<float> lowBand(samples / advConfig.channels);
        std::vector<float> midBand(samples / advConfig.channels);
        std::vector<float> highBand(samples / advConfig.channels);
        
        // Extract bands
        int frameSize = samples / advConfig.channels;
        for (int i = 0; i < frameSize; i++) {
            int idx = ch + i * advConfig.channels;
            lowBand[i] = audioData[idx];
            midBand[i] = audioData[idx];
            highBand[i] = audioData[idx];
        }
        
        // Apply bandpass filters
        ProcessBiquad(lowBand.data(), frameSize, deesserFilters.low, &deesserFilters.lowState[ch]);
        ProcessBiquad(midBand.data(), frameSize, deesserFilters.mid, &deesserFilters.midState[ch]);
        ProcessBiquad(highBand.data(), frameSize, deesserFilters.high, &deesserFilters.highState[ch]);
        
        // Calculate envelope and apply compression per band
        float lowThresh = DbToLinear(advConfig.deesserThresholdLow);
        float midThresh = DbToLinear(advConfig.deesserThresholdMid);
        float highThresh = DbToLinear(advConfig.deesserThresholdHigh);
        
        float lowEnv = 0.0f, midEnv = 0.0f, highEnv = 0.0f;
        float attack = 0.1f, release = 0.01f;
        
        for (int i = 0; i < frameSize; i++) {
            // Update envelopes
            float lowAbs = fabsf(lowBand[i]);
            float midAbs = fabsf(midBand[i]);
            float highAbs = fabsf(highBand[i]);
            
            if (lowAbs > lowEnv) lowEnv += attack * (lowAbs - lowEnv);
            else lowEnv += release * (lowAbs - lowEnv);
            
            if (midAbs > midEnv) midEnv += attack * (midAbs - midEnv);
            else midEnv += release * (midAbs - midEnv);
            
            if (highAbs > highEnv) highEnv += attack * (highAbs - highEnv);
            else highEnv += release * (highAbs - highEnv);
            
            // Calculate gains
            float lowGain = 1.0f;
            float midGain = 1.0f;
            float highGain = 1.0f;
            
            if (lowEnv > lowThresh) {
                lowGain = lowThresh / lowEnv;
                lowGain = 1.0f - (1.0f - lowGain) * 0.85f;   // 85%  // Softer reduction
            }
            if (midEnv > midThresh) {
                midGain = midThresh / midEnv;
                midGain = 1.0f - (1.0f - midGain) * 0.90f;
            }
            if (highEnv > highThresh) {
                highGain = highThresh / highEnv;
                highGain = 1.0f - (1.0f - highGain) * 0.95f;
            }
            
            // Замените все три строки lowGain / midGain / highGain на это:
            lowGain = std::max(0.85f, lowGain);   // минимум 85% громкости
            midGain = std::max(0.80f, midGain);
            highGain = std::max(0.75f, highGain);

            // Combine gains (use minimum for most reduction)
            float combinedGain = std::min({lowGain, midGain, highGain});
            
            // Track gain reduction
            if (combinedGain < 1.0f) {
                totalGainReduction += LinearToDb(combinedGain);
                reductionCount++;
            }
            
            // Apply to original signal
            int idx = ch + i * advConfig.channels;
            audioData[idx] *= combinedGain;
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(advStatsMutex);
        if (reductionCount > 0) {
            advStats.deesserGainReduction = totalGainReduction / reductionCount;
        } else {
            advStats.deesserGainReduction = 0.0f;
        }
    }
}

// === НОВЫЙ: HIGH-FREQUENCY LIMITER — убивает хрип, не трогает голос ===
void AdvancedAudioProcessor::ApplyHighFrequencyLimiter(float* audioData, int samples) {
    const float hfThreshold = DbToLinear(-10.0f);   // только очень громкие высокие частоты
    const float attack = 0.03f;
    const float release = 0.015f;

    for (int ch = 0; ch < advConfig.channels; ch++) {
        float env = 0.0f;
        for (int i = ch; i < samples; i += advConfig.channels) {
            // Выделяем только высокие частоты (хрип)
            float hf = audioData[i];
            if (i + advConfig.channels < samples) {
                hf = hf - 0.7f * audioData[i - advConfig.channels];  // уже было, но явно
            }

            float absHf = fabsf(hf);
            env = std::max(env * (1.0f - release), absHf);   // быстрый envelope

            if (env > hfThreshold) {
                float gain = hfThreshold / (env + 1e-8f);
                audioData[i] *= gain * 0.4f;   // сильно режем именно хрип
            }
        }
    }
}
void AdvancedAudioProcessor::ApplyDynamicEQ(float* audioData, int samples) {
    float thresholdLinear = DbToLinear(advConfig.harshThreshold);
    float attack = 0.2f;
    float release = 0.05f;

    int frames = samples / advConfig.channels;  // Add this

    for (int ch = 0; ch < advConfig.channels; ch++) {
        DynamicEQState* state = &deqState[ch];

        // Filter to get harsh frequency content
        std::vector<float> filtered(frames);  // Use frames
        for (int i = 0; i < frames; i++) {
            filtered[i] = audioData[ch + i * advConfig.channels];
        }

        // Apply peaking filter
        BiquadState tmpState = state->filterState[ch];
        ProcessBiquad(filtered.data(), frames, deqCoeffs, &tmpState, 1);  // stride=1

        // Calculate envelope of harsh frequencies
        float envelope = 0.0f;
        for (float sample : filtered) {
            envelope = std::max(envelope * (1.0f - release), fabsf(sample));
        }

        // Calculate gain reduction
        float targetGain = 1.0f;
        if (envelope > thresholdLinear) {
            float excess = envelope - thresholdLinear;
            targetGain = thresholdLinear / (thresholdLinear + excess * 0.5f);
        }

        // Smooth gain
        state->currentGain += attack * (targetGain - state->currentGain);

        // Update filter coefficients with new gain
        float gainDb = LinearToDb(state->currentGain) - 6.0f;
        CalculatePeakingEQCoeffs(deqCoeffs, advConfig.harshFreqCenter, advConfig.harshFreqQ, gainDb);

        // Apply filter to original signal
        ProcessBiquad(&audioData[ch], frames, deqCoeffs, &state->filterState[ch], advConfig.channels);  // Fix state and add stride
    }

    {
        std::lock_guard<std::mutex> lock(advStatsMutex);
        advStats.dynamicEQGain = LinearToDb(deqState[0].currentGain);
    }
}

void AdvancedAudioProcessor::ApplyVAD(const float* audioData, int samples) {
    for (int ch = 0; ch < advConfig.channels; ch++) {
        VADState* state = &vadState[ch];

        float energy = 0.0f;
        int frameSize = samples / advConfig.channels;
        for (int i = 0; i < frameSize; i++) {
            int idx = ch + i * advConfig.channels;
            if (idx < samples) energy += audioData[idx] * audioData[idx];
        }
        energy = sqrtf(energy / (frameSize + 1e-8f));

        state->energy = 0.68f * state->energy + 0.32f * energy;

        float thresholdLinear = DbToLinear(advConfig.vadThreshold);

        // === БЛОКИРОВКА ГРОМКОГО ШУМА ===
        bool isImpulse = (energy > DbToLinear(-10.0f) &&
            (energy / (state->energy + 1e-8f) > 7.0f));

        if (isImpulse) {
            state->speechActive = false;
            state->hangoverCount = 0;
        }
        else if (energy > thresholdLinear * 0.52f) {   // чувствительность для расстояния
            state->speechActive = true;
            state->hangoverCount = advConfig.vadHangover;
        }
        else if (state->hangoverCount > 0) {
            state->hangoverCount--;
            if (state->hangoverCount <= 4) state->speechActive = false;
        }
        else {
            state->speechActive = false;
        }
    }

    {
        std::lock_guard<std::mutex> lock(advStatsMutex);
        advStats.voiceDetected = vadState[0].speechActive;
    }
}
AdvancedAudioProcessor::AdvancedStats AdvancedAudioProcessor::GetAdvancedStats() const {
    std::lock_guard<std::mutex> lock(advStatsMutex);
    AdvancedStats s = advStats;
    
    // Copy base stats
    s.currentGain = stats.currentGain;
    s.noiseGateOpen = stats.noiseGateOpen;
    s.inputLevel = stats.inputLevel;
    s.outputLevel = stats.outputLevel;
    s.noiseEstimate = stats.noiseEstimate;
    s.suppressionAmount = stats.suppressionAmount;
    
    return s;
}

// FFT implementation (simplified Cooley-Tukey)
void AdvancedAudioProcessor::ComputeFFT(const float* input, std::complex<float>* output, int size) {
    // Copy input
    for (int i = 0; i < size; i++) {
        output[i] = std::complex<float>(input[i], 0.0f);
    }
    
    // Bit-reverse
    BitReverse(output, size);
    
    // FFT stages
    for (int stage = 1; stage < size; stage <<= 1) {
        float angle = -PI / stage;
        std::complex<float> wlen(cosf(angle), sinf(angle));
        
        for (int i = 0; i < size; i += (stage << 1)) {
            std::complex<float> w(1.0f, 0.0f);
            
            for (int j = 0; j < stage; j++) {
                std::complex<float> u = output[i + j];
                std::complex<float> v = output[i + j + stage] * w;
                
                output[i + j] = u + v;
                output[i + j + stage] = u - v;
                
                w *= wlen;
            }
        }
    }
}

void AdvancedAudioProcessor::BitReverse(std::complex<float>* data, int n) {
    int j = 0;
    for (int i = 1; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        
        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }
}
// ============================================================================
// COMPUTE IFFT (обратное FFT) — нужно для спектрального подавления шума
// ============================================================================
void AdvancedAudioProcessor::ComputeIFFT(const std::complex<float>* input, float* output, int size) {
    std::vector<std::complex<float>> data(size);
    for (int i = 0; i < size; i++) {
        data[i] = input[i];
    }

    // Bit-reverse (тот же, что в FFT)
    BitReverse(data.data(), size);

    // IFFT стадии (угол + вместо -)
    for (int stage = 1; stage < size; stage <<= 1) {
        float angle = PI / stage;                     // + вместо -
        std::complex<float> wlen(cosf(angle), sinf(angle));

        for (int i = 0; i < size; i += (stage << 1)) {
            std::complex<float> w(1.0f, 0.0f);

            for (int j = 0; j < stage; j++) {
                std::complex<float> u = data[i + j];
                std::complex<float> v = data[i + j + stage] * w;

                data[i + j] = u + v;
                data[i + j + stage] = u - v;

                w *= wlen;
            }
        }
    }

    // Нормализация + извлечение реальной части
    float scale = 1.0f / size;
    for (int i = 0; i < size; i++) {
        output[i] = data[i].real() * scale;
    }
}

// ============================================================================
// UPDATE NOISE PROFILE — собирает профиль шума в паузах
// ============================================================================
void AdvancedAudioProcessor::UpdateNoiseProfile(const std::vector<float>& magnitude) {
    if (!noiseProfileCollected) {
        noiseProfile = magnitude;
        noiseProfileCollected = true;
        return;
    }

    // Постепенное обновление профиля шума (очень важно!)
    for (size_t i = 0; i < magnitude.size(); i++) {
        noiseProfile[i] = 0.97f * noiseProfile[i] + 0.03f * magnitude[i];  // плавное обновление
    }
}

// ============================================================================
// COMPUTE SPECTRAL GAIN — рассчитывает коэффициенты подавления шума
// ============================================================================
std::vector<float> AdvancedAudioProcessor::ComputeSpectralGain(const std::vector<float>& magnitude) {
    std::vector<float> gain(magnitude.size(), 1.0f);
    // Минимальный уровень подавления (около -16dB). Не глушим в абсолютный ноль!
    // Это сохраняет естественность голоса и убирает артефакты "бульканья".
    const float minGain = 0.15f; // Это -16dB. Шум будет слышен очень тихо, 
    // но голос перестанет быть "рваным".

    for (size_t i = 0; i < magnitude.size(); i++) {
        float noise = noiseProfile[i] * advConfig.nsReductionAmount;
        if (magnitude[i] > noise) {
            float ratio = (magnitude[i] - noise) / (magnitude[i] + 1e-8f);
            gain[i] = std::pow(ratio, advConfig.nsSmoothingFactor);
            gain[i] = std::max(minGain, gain[i]);
        }
        else {
            gain[i] = minGain;
        }
    }
    return gain;
}

// ============================================================================
// APPLY LOOKAHEAD LIMITER — улучшенная версия (без переполнения стека)
// ============================================================================
void AdvancedAudioProcessor::ApplyLookaheadLimiter(float* audioData, int samples) {
    float ceilingLinear = DbToLinear(advConfig.limiterCeiling);
    float releaseCoeff = 1.0f - expf(-1000.0f / (10.0f * advConfig.sampleRate));

    for (int ch = 0; ch < advConfig.channels; ch++) {
        float peakAhead = 0.0f;

        for (int i = ch; i < samples; i += advConfig.channels) {
            // Смотрим вперёд на lookaheadSamples
            peakAhead = fabsf(audioData[i]);
            int look = std::min(advConfig.lookaheadSamples, (samples - i - 1) / advConfig.channels);

            for (int j = 1; j <= look; j++) {
                peakAhead = std::max(peakAhead, fabsf(audioData[i + j * advConfig.channels]));
            }

            // Применяем лимитер
            float target = (peakAhead > ceilingLinear) ? ceilingLinear / peakAhead : 1.0f;

            // Сглаживание
            float& current = lookaheadBuffer[ch][lookaheadIndex % advConfig.lookaheadSamples];
            current += releaseCoeff * (target - current);

            audioData[i] *= current;
        }
    }


    lookaheadIndex = (lookaheadIndex + 1) % advConfig.lookaheadSamples;
}


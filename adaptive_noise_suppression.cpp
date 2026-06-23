#include "adaptive_noise_suppression.h"
#include <cstring>
#include <iostream>
#include <algorithm>
#include "audio_defs.h"
#include "utility.h"


AdaptiveNoiseSuppression::AdaptiveNoiseSuppression() {}

AdaptiveNoiseSuppression::~AdaptiveNoiseSuppression() {}

bool AdaptiveNoiseSuppression::Initialize(const Config& config) {
    config_ = config;

    window_.resize(fftSize);
    for (int i = 0; i < fftSize; i++) {
        float normalized = static_cast<float>(i) / (fftSize - 1);
        window_[i] = 0.5f - 0.5f * cosf(TWO_PI * normalized);
    }

    fftBuffer_.resize(fftSize, 0.0f);
    overlapBuffer_.resize(fftSize - hopSize, 0.0f);
    prevGain.resize(fftSize / 2 + 1, 1.0f);

    noiseProfile_.mean.resize(fftSize / 2 + 1, 1e-4f);
    noiseProfile_.variance.resize(fftSize / 2 + 1, 1e-4f);
    noiseProfile_.min.resize(fftSize / 2 + 1, 1e-4f);
    noiseProfile_.max.resize(fftSize / 2 + 1, 1.0f);

    colaNormFactor_ = WindowUtils::ComputeColaNormFactor(window_, hopSize);

    initialized_ = true;
    return true;
}
float AdaptiveNoiseSuppression::EstimateVoiceConfidence(const float* audioData, int samples) {
    if (!audioData || samples <= 0) return 0.1f;

    float rms = 0.0f;
    for (int i = 0; i < samples; i++) {
        rms += audioData[i] * audioData[i];
    }
    rms = sqrtf(rms / samples);
    float rmsDb = 20.0f * log10f(rms + 1e-10f);

    float confidence = (rmsDb > -38.0f) ? 0.6f : 0.15f;

    if (auto detector = voiceDetector_.lock()) {
        auto result = detector->GetLastResult();
        if (result.isCalibrated) {
            confidence = result.confidence;
        }
    }

    return std::clamp(confidence, 0.0f, 1.0f);
}

void AdaptiveNoiseSuppression::ProcessFrame(float* audioData, int samples) {
    if (!initialized_ || !audioData || samples <= 0) return;

    const size_t numBins = fftSize / 2 + 1;
    std::vector<float> frame(fftSize, 0.0f);
    const int overlapSize = fftSize - hopSize;
    std::copy(overlapBuffer_.begin(), overlapBuffer_.end(), frame.begin());

    for (int i = 0; i < hopSize && i < samples; i++) {
        frame[overlapSize + i] = audioData[i];
    }

    // Окно анализа
    for (int i = 0; i < fftSize; i++) frame[i] *= window_[i];

    std::vector<std::complex<float>> fftOutput(fftSize);
    FFTUtils::ComputeFFT(frame.data(), fftOutput.data());

    std::vector<float> magnitude(numBins);
    for (size_t i = 0; i < numBins; i++) {
        magnitude[i] = std::abs(fftOutput[i]);
    }

    const float voiceConfidence = currentVoiceConfidence_;

    if (voiceConfidence < 0.42f && (++noiseUpdateCounter % 2 == 0)) {
        UpdateNoiseProfile(magnitude, voiceConfidence);
    }

    auto gain = ComputeSpectralGain(magnitude, voiceConfidence);

    // Применяем gain к положительным частотам
    for (size_t i = 0; i < numBins; i++) {
        fftOutput[i] *= gain[i];
    }

    // *** ГЛАВНОЕ ИСПРАВЛЕНИЕ: восстанавливаем эрмитову симметрию ***
    for (size_t i = 1; i < numBins - 1; i++) {
        fftOutput[fftSize - i] = std::conj(fftOutput[i]);
    }
    // DC (i=0) и Найквист (i=numBins-1) остаются как есть (они вещественные)

    FFTUtils::ComputeIFFT(fftOutput.data(), frame.data());

    // *** Исправление COLA: нормировка ***
    for (int i = 0; i < fftSize; i++) {
        frame[i] *= colaNormFactor_;
    }

    // Копируем выходные отсчёты
    for (int i = 0; i < hopSize && i < samples; i++) {
        audioData[i] = frame[overlapSize + i];
    }

    overlapBuffer_.assign(frame.begin() + hopSize, frame.begin() + fftSize);
    frameCounter_++;
}

void AdaptiveNoiseSuppression::UpdateNoiseProfile(const std::vector<float>& magnitude,float voiceConfidence) {
    if (voiceConfidence > 0.45f) return;

    if (magnitude.size() != noiseProfile_.mean.size()) return;

        // Увеличенная базовая скорость обучения + частотная адаптация
        float baseRate = 0.015f;                    // было 0.005–0.008
        float adjustedRate = baseRate * (1.0f - voiceConfidence * 0.88f);
        adjustedRate = std::max(0.004f, std::min(0.045f, adjustedRate));

        for (size_t i = 0; i < magnitude.size(); i++) {
            float m = magnitude[i];
            float mean = noiseProfile_.mean[i];

            float freq = (i * SAMPLE_RATE) / (fftSize * 2.0f);

            // Значительно быстрее адаптируемся к шуму на высоких частотах (тттт, трение, клавиатура)
            float rateBoost = 1.0f;
            if (freq > 6000.0f)      rateBoost = 1.8f;   // очень агрессивно
            else if (freq > 4000.0f) rateBoost = 1.1f;
            else if (freq > 2500.0f) rateBoost = 0.5f;

            float currentRate = adjustedRate * rateBoost;

            // Обновляем среднее
            noiseProfile_.mean[i] = (1.0f - currentRate) * mean + currentRate * m;

            // Обновляем дисперсию
            float diff = m - mean;
            noiseProfile_.variance[i] = (1.0f - currentRate) * noiseProfile_.variance[i] +
                currentRate * (diff * diff);

            // Min/Max обновляем ещё медленнее
            float slowRate = currentRate * 0.18f;

            if (m < noiseProfile_.min[i]) {
                noiseProfile_.min[i] = m;
            }
            else {
                noiseProfile_.min[i] = (1.0f - slowRate) * noiseProfile_.min[i] + slowRate * m;
            }

            if (m > noiseProfile_.max[i]) {
                noiseProfile_.max[i] = m;
            }
            else {
                noiseProfile_.max[i] = (1.0f - slowRate) * noiseProfile_.max[i] + slowRate * m;
            }
        }

        noiseProfile_.updateCount++;
    
}
std::vector<float> AdaptiveNoiseSuppression::ComputeSpectralGain(
    const std::vector<float>& magnitude, float voiceConfidence) {

    const size_t numBins = magnitude.size();
    std::vector<float> gain(numBins, 1.0f);

    // ---------- 1. Базовый минимальный gain (очень консервативно) ----------
    float minGain = 0.25f;  // -12 dB – никогда не давим до нуля
    if (voiceConfidence > 0.8f) {
        minGain = 0.85f;    // почти не трогаем речь
    }
    else if (voiceConfidence < 0.3f) {
        minGain = 0.15f;    // -16 dB – давим шум, но не до нуля
    }

    // ---------- 2. Стабильная оценка шума (только в паузах) ----------
    static std::vector<float> smoothedNoise(numBins, 1e-6f);
    static std::vector<float> noiseVariance(numBins, 0.0f);
    const float NOISE_SMOOTHING = 0.98f;   // очень медленно
    const float VAR_SMOOTHING = 0.99f;

    for (size_t i = 0; i < numBins; i++) {
        float mag = magnitude[i];
        // Обновляем профиль шума только когда уверены, что это не речь
        if (voiceConfidence < 0.3f) {
            smoothedNoise[i] = NOISE_SMOOTHING * smoothedNoise[i] + (1.0f - NOISE_SMOOTHING) * mag;
            float diff = mag - smoothedNoise[i];
            noiseVariance[i] = VAR_SMOOTHING * noiseVariance[i] + (1.0f - VAR_SMOOTHING) * diff * diff;
        }
        // Адаптивный порог: среднее + 1.5 * сигма (было 2.2 – уменьшили)
        float noiseFloor = smoothedNoise[i] + 1.5f * std::sqrt(noiseVariance[i] + 1e-10f);
        noiseFloor = std::max(noiseFloor, 1e-6f);

        // ---------- 3. Мягкое спектральное вычитание ----------
        if (mag > noiseFloor) {
            float ratio = (mag - noiseFloor) / (mag + 1e-10f);   // 0..1
            // Единая экспонента для всех частот (почти линейное подавление)
            const float exponent = 0.92f;
            float rawGain = std::pow(ratio, exponent);
            gain[i] = std::max(minGain, rawGain);
        }
        else {
            gain[i] = minGain;
        }
    }

    // ---------- 4. Временное сглаживание (атака быстро, спад медленно) ----------
    static std::vector<float> prevGain(numBins, 1.0f);
    const float attackSmooth = (voiceConfidence > 0.5f) ? 0.3f : 0.15f;   // быстрое уменьшение gain
    const float releaseSmooth = 0.96f;   // очень медленное восстановление

    for (size_t i = 0; i < numBins; i++) {
        float target = gain[i];
        float current = prevGain[i];
        float alpha = (target < current) ? attackSmooth : releaseSmooth;
        gain[i] = alpha * target + (1.0f - alpha) * current;
        prevGain[i] = gain[i];
    }

    // ---------- 5. Лёгкое частотное сглаживание (окно 3) ----------
    std::vector<float> smoothed(gain.size());
    if (gain.size() > 2) {
        for (size_t i = 1; i < gain.size() - 1; i++) {
            smoothed[i] = (gain[i - 1] + gain[i] + gain[i + 1]) / 3.0f;
        }
        smoothed[0] = gain[0];
        smoothed.back() = gain.back();
    }
    else {
        smoothed = gain;
    }

    return smoothed;
}

void AdaptiveNoiseSuppression::ResetNoiseProfile() {
    std::fill(noiseProfile_.mean.begin(), noiseProfile_.mean.end(), 1e-4f);
    std::fill(noiseProfile_.variance.begin(), noiseProfile_.variance.end(), 1e-4f);
    std::fill(noiseProfile_.min.begin(), noiseProfile_.min.end(), 1e-4f);
    std::fill(noiseProfile_.max.begin(), noiseProfile_.max.end(), 1.0f);
    noiseProfile_.updateCount = 0;
    adaptationRate_ = 0.01f;
    std::fill(prevGain.begin(), prevGain.end(), 1.0f);
    noiseUpdateCounter = 0;   // <-- добавить эту строку
}

float AdaptiveNoiseSuppression::GetNoiseFloorDb() const {
    if (noiseProfile_.mean.empty()) return -60.0f;

    float avgNoise = 0.0f;
    for (float m : noiseProfile_.mean) avgNoise += m;
    avgNoise /= noiseProfile_.mean.size();
    avgNoise = std::max(avgNoise, 1e-10f);
    return 20.0f * std::log10(avgNoise);
}

void AdaptiveNoiseSuppression::ApplyWindow(float* frame, int size) {
    for (int i = 0; i < size && i < static_cast<int>(window_.size()); i++) {
        frame[i] *= window_[i];
    }
}

void AdaptiveNoiseSuppression::ExtractOverlap(const float* input, int samples, 
                                             std::vector<float>& frame) {
    int overlapSize = fftSize - hopSize;

    // This function would be used to extract frames with overlap
    // for batch processing
    frame.resize(fftSize, 0.0f);
    
    for (int i = 0; i < overlapSize && i < static_cast<int>(overlapBuffer_.size()); i++) {
        frame[i] = overlapBuffer_[i];
    }

    for (int i = 0; i < hopSize && i < samples; i++) {
        frame[overlapSize + i] = input[i];
    }
}

void AdaptiveNoiseSuppression::PlaceOverlap(const float* frame, float* output, int samples) {
    int overlapSize = fftSize - hopSize;

    for (int i = 0; i < hopSize && i < samples; i++) {
        output[i] = frame[overlapSize + i];
    }

    overlapBuffer_.assign(
        frame + hopSize,
        frame + fftSize
    );
}

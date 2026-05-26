// voice_detector.cpp - ИСПРАВЛЕННАЯ ВЕРСИЯ
#include "voice_detector.h"
#include <cstring>
#include <complex>
#include <iostream>

// Добавляем отладочный лог если нужно
#ifndef AddDebugLog
#define AddDebugLog(msg) std::cout << msg << std::endl
#endif

static const float PI = 3.14159265358979323846f;
static const float TWO_PI = 2.0f * PI;

VoiceDetector::VoiceDetector() {}

VoiceDetector::~VoiceDetector() {}

bool VoiceDetector::Initialize(const DetectorConfig& config) {
    config_ = config;

    // Создаём окно Ханна для FFT
    fftWindow_.resize(config_.fftSize);
    for (int i = 0; i < config_.fftSize; i++) {
        fftWindow_[i] = 0.5f - 0.5f * cosf(TWO_PI * i / (config_.fftSize - 1));
    }

    fftBuffer_.resize(config_.fftSize);
    fftOutput_.resize(config_.fftSize);
    noiseProfile_.resize(config_.fftSize / 2 + 1, 0.0f);
    smoothedSpectrum_.resize(config_.fftSize / 2 + 1, 0.0f);

    featureHistory_.clear();
    energyHistory_.clear();
    voiceDecisionHistory_.clear();

    return true;
}

void VoiceDetector::StartCalibration() {
    std::lock_guard<std::mutex> lock(mutex_);
    isCalibrating_ = true;
    calibrationFrames_ = 0;
    std::fill(noiseProfile_.begin(), noiseProfile_.end(), 0.0f);
    AddDebugLog("VoiceDetector: Started noise calibration - please be silent for 10 seconds");
}

bool VoiceDetector::IsCalibrated() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastResult_.isCalibrated;
}

void VoiceDetector::ResetNoiseProfile() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::fill(noiseProfile_.begin(), noiseProfile_.end(), 0.0f);
    lastResult_.isCalibrated = false;
    lastResult_.calibrationProgress = 0;
    featureHistory_.clear();
}

VoiceDetector::DetectionResult VoiceDetector::GetLastResult() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastResult_;
}

VoiceDetector::DetectionResult VoiceDetector::ProcessFrame(const std::vector<int16_t>& audioData) {

    std::vector<float> floatData(audioData.size());
    for (size_t i = 0; i < audioData.size(); i++) {
        floatData[i] = audioData[i] / 32768.0f;
    }
    return ProcessFrame(floatData);
}

VoiceDetector::DetectionResult VoiceDetector::ProcessFrame(const std::vector<float>& audioData) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Подготовка буфера FFT с перекрытием
    static std::deque<float> overlapBuffer;
    int hopSize = config_.hopSize;
    int fftSize = config_.fftSize;

    // Добавляем новые данные в буфер перекрытия
    for (float sample : audioData) {
        overlapBuffer.push_back(sample);
    }

    // Обрабатываем, если достаточно данных
    if (overlapBuffer.size() < static_cast<size_t>(fftSize)) {
        return lastResult_;
    }

    // Извлекаем кадр для FFT
    std::vector<float> frame(fftSize);
    for (int i = 0; i < fftSize; i++) {
        frame[i] = overlapBuffer[i] * fftWindow_[i];
    }

    // Удаляем hopSize отсчётов из буфера
    for (int i = 0; i < hopSize && !overlapBuffer.empty(); i++) {
        overlapBuffer.pop_front();
    }

    // Вычисляем FFT
    ComputeFFT(frame, fftOutput_);

    // Получаем спектр мощности
    std::vector<float> spectrum(fftSize / 2 + 1);
    for (int i = 0; i <= fftSize / 2; i++) {
        spectrum[i] = std::abs(fftOutput_[i]);
    }

    // Извлекаем признаки
    VoiceFeatures features = ExtractFeatures(audioData);

    // Обновляем профиль шума или классифицируем
    if (isCalibrating_) {
        UpdateNoiseProfile(features, spectrum);
        calibrationFrames_++;
        lastResult_.calibrationProgress = std::min(100, (calibrationFrames_ * 100) / config_.noiseProfileFrames);

        if (calibrationFrames_ >= config_.noiseProfileFrames) {
            isCalibrating_ = false;
            lastResult_.isCalibrated = true;
            AddDebugLog("VoiceDetector: Calibration complete - noise profile established");
        }
    }
    else {
        // Классификация голос/шум
        float confidence = 0.0f;
        bool isVoice = ClassifyVoice(features, confidence);

        // Применяем hangover для плавного переключения
        int hangoverFrames = static_cast<int>(config_.voiceHangoverMs * config_.sampleRate / 1000.0f / config_.frameSize);

        if (isVoice) {
            voiceHangoverCount_ = hangoverFrames;
            consecutiveVoiceFrames_++;
            consecutiveNoiseFrames_ = 0;
        }
        else {
            if (voiceHangoverCount_ > 0) {
                voiceHangoverCount_--;
                isVoice = true; // Продолжаем считать голосом во время hangover
                confidence *= 0.5f; // Но снижаем уверенность
            }
            else {
                consecutiveVoiceFrames_ = 0;
                consecutiveNoiseFrames_++;
            }
        }

        // Требуем минимум 2 последовательных кадра голоса для уверенности
        if (isVoice && consecutiveVoiceFrames_ < 2) {
            confidence *= 0.3f;
        }

        // Обновляем результат
        lastResult_.isVoice = isVoice && (confidence >= config_.minVoiceConfidence);
        lastResult_.confidence = confidence;
        lastResult_.noiseFloor = LinearToDb(features.totalEnergy * 0.1f + 1e-10f);
        lastResult_.voiceLevel = isVoice ? LinearToDb(features.voiceBandEnergy + 1e-10f) : -100.0f;

        // Обновляем историю для адаптации
        featureHistory_.push_back(features);
        if (featureHistory_.size() > 20) featureHistory_.pop_front();

        voiceDecisionHistory_.push_back(lastResult_.isVoice);
        if (voiceDecisionHistory_.size() > 10) voiceDecisionHistory_.pop_front();
    }

    return lastResult_;
}

VoiceDetector::VoiceFeatures VoiceDetector::ExtractFeatures(const std::vector<float>& frame) {
    VoiceFeatures features;
    if (frame.empty()) return features;

    // 1. Общая энергия и пиковые значения (PAR)
    float totalEnergy = 0.0f;
    float maxAbs = 0.0f;
    for (float s : frame) {
        float absS = std::abs(s);
        if (absS > maxAbs) maxAbs = absS;
        totalEnergy += s * s;
    }
    features.totalEnergy = totalEnergy / frame.size();

    // Peak-to-Average Ratio (PAR) - помогает отличить стук от голоса
    float rms = std::sqrt(features.totalEnergy);
    features.peakToAverageRatio = (rms > 1e-8f) ? (maxAbs / rms) : 0.0f;

    // 2. Частота переходов через ноль (ZCR)
    int zeroCrossings = 0;
    for (size_t i = 1; i < frame.size(); i++) {
        if ((frame[i] > 0) != (frame[i - 1] > 0)) zeroCrossings++;
    }
    features.zeroCrossingRate = static_cast<float>(zeroCrossings) / frame.size();

    // 3. Спектральный анализ (используем уже вычисленный FFT в fftOutput_)
    int fftSize = config_.fftSize;
    int sampleRate = config_.sampleRate;
    int bins = fftSize / 2 + 1;
    float binWidth = static_cast<float>(sampleRate) / fftSize;

    float sumWeightedFreq = 0.0f;
    float sumMagnitude = 0.0f;
    float sumLogMagnitude = 0.0f;

    std::vector<float> spectrum(bins);
    for (int i = 0; i < bins; i++) {
        // Вычисляем амплитуду из комплексного числа (из fftOutput_)
        float mag = std::abs(fftOutput_[i]);
        spectrum[i] = mag;

        float freq = i * binWidth;
        sumWeightedFreq += freq * mag;
        sumMagnitude += mag;
        sumLogMagnitude += logf(mag + 1e-10f);
    }

    // Спектральный центроид (средняя частота спектра)
    features.spectralCentroid = sumMagnitude > 0 ? sumWeightedFreq / sumMagnitude : 0;

    // Спектральная плоскость (Flatness) - у шума близка к 1.0, у голоса к 0.1-0.3
    float geometricMean = expf(sumLogMagnitude / bins);
    float arithmeticMean = sumMagnitude / bins;
    features.spectralFlatness = arithmeticMean > 0 ? geometricMean / arithmeticMean : 1.0f;

    // 4. Энергия в полосе голоса и низкочастотный шум
    float voiceBandEnergy = 0.0f;
    float lowFreqEnergy = 0.0f;
    float totalSpectrumEnergy = 0.0f;

    for (int i = 0; i < bins; i++) {
        float freq = i * binWidth;
        float mag = spectrum[i];
        totalSpectrumEnergy += mag;

        if (freq >= config_.voiceFreqLow && freq <= config_.voiceFreqHigh) {
            voiceBandEnergy += mag;
        }
        if (freq < config_.voiceFreqLow) {
            lowFreqEnergy += mag;
        }
    }

    features.voiceBandEnergy = totalSpectrumEnergy > 0 ? voiceBandEnergy / totalSpectrumEnergy : 0;
    features.lowFreqRatio = totalSpectrumEnergy > 0 ? lowFreqEnergy / totalSpectrumEnergy : 0;

    // 5. Детекция формант (резонансов речевого тракта)
    features.formant1Strength = DetectFormant(spectrum, config_.voiceFormant1, 200.0f);
    features.formant2Strength = DetectFormant(spectrum, config_.voiceFormant2, 300.0f);

    // 6. Гармоническое соотношение (поиск основного тона)
    float fundamental = 100.0f;
    features.harmonicRatio = CalculateHarmonicRatio(spectrum, fundamental);

    // 7. Импульсные шумы (кашель, удары по столу)
    float maxInstantEnergy = 0.0f;
    float avgInstantEnergy = 0.0f;
    const int windowSize = std::max(1, (int)(sampleRate * 0.001f)); // окно 1мс

    for (size_t i = 0; i + windowSize <= frame.size(); i += windowSize) {
        float windowEnergy = 0.0f;
        for (int j = 0; j < windowSize; j++) {
            windowEnergy += frame[i + j] * frame[i + j];
        }
        windowEnergy /= windowSize;
        maxInstantEnergy = std::max(maxInstantEnergy, windowEnergy);
        avgInstantEnergy += windowEnergy;
    }
    avgInstantEnergy /= (frame.size() / (float)windowSize + 1e-8f);
    features.impulseMetric = (maxInstantEnergy + 1e-10f) / (avgInstantEnergy + 1e-10f);

    // 8. Тональные шумы (свист, электро-писк)
    float maxSpectralPeak = 0.0f;
    int peakBin = 0;
    for (int i = 5; i < bins - 5; i++) {
        if (spectrum[i] > maxSpectralPeak) {
            maxSpectralPeak = spectrum[i];
            peakBin = i;
        }
    }
    float halfPeak = maxSpectralPeak * 0.5f;
    int left = peakBin, right = peakBin;
    while (left > 0 && spectrum[left] > halfPeak) left--;
    while (right < bins - 1 && spectrum[right] > halfPeak) right++;

    float peakBandwidth = (right - left) * binWidth;
    float peakFreq = peakBin * binWidth;

    if (peakFreq > 1000.0f && peakBandwidth < 300.0f && maxSpectralPeak > 0.01f) {
        features.tonalityMetric = maxSpectralPeak / (arithmeticMean + 1e-8f);
    }
    else {
        features.tonalityMetric = 0.0f;
    }

    // 9. Детекция трения (шуршание микрофона об одежду)
    float hfEnergy = 0.0f;
    float hfLogSum = 0.0f;
    int hfBins = 0;
    for (int i = 0; i < bins; i++) {
        if (i * binWidth > 4500.0f) {
            hfEnergy += spectrum[i];
            hfLogSum += logf(spectrum[i] + 1e-10f);
            hfBins++;
        }
    }
    if (hfBins > 0 && hfEnergy > 0.001f) {
        float hfFlatness = expf(hfLogSum / hfBins) / (hfEnergy / hfBins + 1e-10f);
        if (hfFlatness > 0.65f) {
            features.frictionMetric = hfEnergy / (sumMagnitude + 1e-8f);
        }
    }

    // 10. Скорость модуляции и временная согласованность
    if (!featureHistory_.empty()) {
        const auto& prev = featureHistory_.back();

        // Скорость изменения энергии
        features.modulationRate = std::abs(features.totalEnergy - prev.totalEnergy) / (prev.totalEnergy + 1e-10f);

        // Согласованность спектра
        float energyDiff = std::abs(features.totalEnergy - prev.totalEnergy) / (prev.totalEnergy + 1e-10f);
        float centroidDiff = std::abs(features.spectralCentroid - prev.spectralCentroid) / (prev.spectralCentroid + 1e-10f);
        features.temporalConsistency = 1.0f - std::min(1.0f, (energyDiff + centroidDiff) * 0.5f);
    }
    else {
        features.modulationRate = 0.0f;
        features.temporalConsistency = 0.5f;
    }
    // === РАСЧЕТ TEMPORAL CONSISTENCY ===
    if (!featureHistory_.empty()) {
        const auto& prev = featureHistory_.back();

        // Считаем разницу энергии и центра масс спектра между текущим и прошлым кадром
        float energyDiff = fabsf(features.totalEnergy - prev.totalEnergy) / (prev.totalEnergy + 1e-10f);
        float centroidDiff = fabsf(features.spectralCentroid - prev.spectralCentroid) / (prev.spectralCentroid + 1e-10f);

        // Если звук почти не изменился - консистентность высокая (1.0)
        // Если произошел резкий скачок - консистентность низкая (0.0)
        features.temporalConsistency = 1.0f - std::min(1.0f, (energyDiff + centroidDiff) * 0.5f);
    }
    else {
        features.temporalConsistency = 0.5f; // Начальное значение
    }
    return features;
}
void VoiceDetector::UpdateNoiseProfile(const VoiceFeatures& features, const std::vector<float>& spectrum) {
    float alpha = config_.noiseAdaptationSpeed;

    for (size_t i = 0; i < noiseProfile_.size() && i < spectrum.size(); i++) {
        noiseProfile_[i] = (1.0f - alpha) * noiseProfile_[i] + alpha * spectrum[i];
    }

    for (size_t i = 0; i < smoothedSpectrum_.size() && i < spectrum.size(); i++) {
        smoothedSpectrum_[i] = 0.8f * smoothedSpectrum_[i] + 0.2f * spectrum[i];
    }
}

bool VoiceDetector::ClassifyVoice(const VoiceFeatures& features, float& confidence) {
    // === НОВЫЕ КОЭФФИЦИЕНТЫ (гораздо мягче) ===
    float voiceScore = features.voiceBandEnergy * 0.65f;
    float harmonicScore = features.harmonicRatio * 0.25f;
    float formantScore = (features.formant1Strength + features.formant2Strength) * 0.20f;
    float consistency = features.temporalConsistency * 0.15f;

    confidence = voiceScore + harmonicScore + formantScore + consistency;

    // Гистерезис (чтобы не рвало слова)
    float threshold = (consecutiveVoiceFrames_ > 3) ? 0.28f : 0.38f;

    // Блокировка только очень сильных импульсов
    if (features.peakToAverageRatio > 18.0f) {
        lastResult_.isImpulseNoise = true;
        return false;
    }

    return (confidence >= threshold);
}

float VoiceDetector::DetectFormant(const std::vector<float>& spectrum, float centerFreq, float bandwidth) {
    int fftSize = config_.fftSize;
    int sampleRate = config_.sampleRate;
    float binWidth = static_cast<float>(sampleRate) / fftSize;

    int centerBin = static_cast<int>(centerFreq / binWidth);
    int bandwidthBins = static_cast<int>(bandwidth / binWidth / 2);

    int startBin = std::max(0, centerBin - bandwidthBins);
    int endBin = std::min(static_cast<int>(spectrum.size()) - 1, centerBin + bandwidthBins);

    float peak = 0.0f;
    float surrounding = 0.0f;
    int surroundingCount = 0;

    for (int i = startBin; i <= endBin; i++) {
        if (spectrum[i] > peak) peak = spectrum[i];
    }

    int widerStart = std::max(0, centerBin - bandwidthBins * 2);
    int widerEnd = std::min(static_cast<int>(spectrum.size()) - 1, centerBin + bandwidthBins * 2);

    for (int i = widerStart; i <= widerEnd; i++) {
        if (i < startBin || i > endBin) {
            surrounding += spectrum[i];
            surroundingCount++;
        }
    }

    float avgSurrounding = surroundingCount > 0 ? surrounding / surroundingCount : 1e-10f;
    return peak / avgSurrounding;
}

float VoiceDetector::CalculateHarmonicRatio(const std::vector<float>& spectrum, float fundamental) {
    int fftSize = config_.fftSize;
    int sampleRate = config_.sampleRate;
    float binWidth = static_cast<float>(sampleRate) / fftSize;

    int fundBin = static_cast<int>(fundamental / binWidth);
    if (fundBin <= 0 || fundBin >= static_cast<int>(spectrum.size())) return 0.0f;

    float harmonicSum = 0.0f;
    float totalSum = 0.0f;
    int harmonicsFound = 0;

    for (int h = 1; h <= 6; h++) {
        int harmonicBin = fundBin * h;
        if (harmonicBin < static_cast<int>(spectrum.size())) {
            float peak = 0.0f;
            for (int j = std::max(0, harmonicBin - 2);
                j <= std::min(static_cast<int>(spectrum.size()) - 1, harmonicBin + 2); j++) {
                peak = std::max(peak, spectrum[j]);
            }
            harmonicSum += peak;
            if (peak > spectrum[fundBin] * 0.1f) harmonicsFound++;
        }
    }

    for (float s : spectrum) totalSum += s;

    float ratio = totalSum > 0 ? harmonicSum / totalSum : 0.0f;
    float harmonicQuality = static_cast<float>(harmonicsFound) / 6.0f;

    return ratio * harmonicQuality;
}

void VoiceDetector::ComputeFFT(const std::vector<float>& input, std::vector<std::complex<float>>& output) {
    int n = input.size();
    output.resize(n);

    for (int i = 0; i < n; i++) {
        output[i] = std::complex<float>(input[i], 0.0f);
    }

    BitReverse(output);

    for (int stage = 1; stage < n; stage <<= 1) {
        float angle = -PI / stage;
        std::complex<float> wlen(cosf(angle), sinf(angle));

        for (int i = 0; i < n; i += (stage << 1)) {
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

void VoiceDetector::BitReverse(std::vector<std::complex<float>>& data) {
    int n = data.size();
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

float VoiceDetector::LinearToDb(float linear) const {
    return 20.0f * log10f(linear + 1e-10f);
}

float VoiceDetector::DbToLinear(float db) const {
    return powf(10.0f, db / 20.0f);
}

// voice_detector.cpp - ИСПРАВЛЕННАЯ ВЕРСИЯ
#include "voice_detector.h"
#include "audio_defs.h"
#include <cstring>
#include <complex>
#include <iostream>
#include "utility.h"


VoiceDetector::VoiceDetector() {}

VoiceDetector::~VoiceDetector() {}

bool VoiceDetector::Initialize(const DetectorConfig& config) {
    config_ = config;
    // В Initialize и StartCalibration:
    featureStats = std::array<FeatureStats, 9>();  // теперь 9 признаков
    std::fill(featureStats.begin(), featureStats.end(), FeatureStats{});
    // Создаём окно Ханна для FFT
    fftWindow_.resize(fftSize);
    for (int i = 0; i < fftSize; i++) {
        fftWindow_[i] = 0.5f - 0.5f * cosf(TWO_PI * i / (fftSize - 1));
    }

    fftBuffer_.resize(fftSize);
    fftOutput_.resize(fftSize);
    noiseProfile_.resize(fftSize / 2 + 1, 0.0f);
    smoothedSpectrum_.resize(fftSize / 2 + 1, 0.0f);

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
    for (auto& fs : featureStats) fs = FeatureStats{}; // <-- добавить
}

bool VoiceDetector::IsCalibrated() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastResult_.isCalibrated;
}

void VoiceDetector::ResetNoiseProfile() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::fill(noiseProfile_.begin(), noiseProfile_.end(), 0.0f);
    for (auto& fs : featureStats) fs = FeatureStats{};
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
#define _CRT_SECURE_NO_WARNINGS
VoiceDetector::DetectionResult VoiceDetector::ProcessFrame(const std::vector<float>& audioData) {
    std::lock_guard<std::mutex> lock(mutex_);

    

    // Подготовка буфера FFT с перекрытием
    static std::deque<float> overlapBuffer;

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
    FFTUtils::ComputeFFT(frame.data(), fftOutput_.data());

    // Получаем спектр мощности
    std::vector<float> spectrum(fftSize / 2 + 1);
    for (int i = 0; i <= fftSize / 2; i++) {
        spectrum[i] = std::abs(fftOutput_[i]);
    }

    // Извлекаем признаки
    VoiceFeatures features = ExtractFeatures(audioData);

    float totalNoiseEnergy = 0.0f;
    for (float n : noiseProfile_) totalNoiseEnergy += n;
    totalNoiseEnergy /= noiseProfile_.size();
    totalNoiseEnergy = std::max(totalNoiseEnergy, 1e-6f);

    bool isNoiseFrame = (features.totalEnergy < totalNoiseEnergy * 2.0f);
    UpdateFeatureStats(features, isNoiseFrame);

    // Обновляем профиль шума или классифицируем
    if (isCalibrating_) {
        UpdateNoiseProfile(features, spectrum);
        calibrationFrames_++;
        lastResult_.calibrationProgress = std::min(100, (calibrationFrames_ * 100) / config_.noiseProfileFrames);

        if (calibrationFrames_ >= config_.noiseProfileFrames) {
            isCalibrating_ = false;
            lastResult_.isCalibrated = true;
        }
    }
    else {
        // Классификация голос/шум
        float confidence = 0.0f;
        bool isVoice = ClassifyVoice(features, confidence);

        // Применяем hangover для плавного переключения
        int hangoverFrames = static_cast<int>(config_.voiceHangoverMs * SAMPLE_RATE / 1000.0f / frameSize);

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
        lastResult_.noiseFloor = AudioUtils::LinearToDb(features.totalEnergy * 0.1f + 1e-10f);
        lastResult_.voiceLevel = isVoice ? AudioUtils::LinearToDb(features.voiceBandEnergy + 1e-10f) : -100.0f;

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

    float rms = std::sqrt(features.totalEnergy);
    features.peakToAverageRatio = (rms > 1e-8f) ? (maxAbs / rms) : 0.0f;

    // 2. Частота переходов через ноль (ZCR)
    int zeroCrossings = 0;
    for (size_t i = 1; i < frame.size(); i++) {
        if ((frame[i] > 0) != (frame[i - 1] > 0)) zeroCrossings++;
    }
    features.zeroCrossingRate = static_cast<float>(zeroCrossings) / frame.size();

    // 3. Спектральный анализ (используем fftOutput_)
    int bins = fftSize / 2 + 1;
    float binWidth = static_cast<float>(SAMPLE_RATE) / fftSize;

    float sumWeightedFreq = 0.0f;
    float sumMagnitude = 0.0f;
    float sumLogMagnitude = 0.0f;
    std::vector<float> spectrum(bins);

    for (int i = 0; i < bins; i++) {
        float mag = std::abs(fftOutput_[i]);
        spectrum[i] = mag;
        float freq = i * binWidth;
        sumWeightedFreq += freq * mag;
        sumMagnitude += mag;
        sumLogMagnitude += logf(mag + 1e-10f);
    }

    features.spectralCentroid = (sumMagnitude > 0) ? sumWeightedFreq / sumMagnitude : 0.0f;

    float geometricMean = expf(sumLogMagnitude / bins);
    float arithmeticMean = sumMagnitude / bins;
    features.spectralFlatness = (arithmeticMean > 0) ? geometricMean / arithmeticMean : 1.0f;

    // 4. Энергия в полосе голоса и низкочастотный шум
    float voiceBandEnergy = 0.0f;
    float lowFreqEnergy = 0.0f;
    float totalSpectrumEnergy = 0.0f;

    for (int i = 0; i < bins; i++) {
        float freq = i * binWidth;
        float mag = spectrum[i];
        totalSpectrumEnergy += mag;
        if (freq >= config_.voiceFreqLow && freq <= config_.voiceFreqHigh)
            voiceBandEnergy += mag;
        if (freq < config_.voiceFreqLow)
            lowFreqEnergy += mag;
    }

    features.voiceBandEnergy = (totalSpectrumEnergy > 0) ? voiceBandEnergy / totalSpectrumEnergy : 0.0f;
    features.lowFreqRatio = (totalSpectrumEnergy > 0) ? lowFreqEnergy / totalSpectrumEnergy : 0.0f;

    // 5. Детекция формант (сила и ширина)
    features.formant1Strength = DetectFormant(spectrum, config_.voiceFormant1, 200.0f);
    features.formant2Strength = DetectFormant(spectrum, config_.voiceFormant2, 300.0f);

    // ---- Пункт 5: ширина формантных пиков (-3dB) ----
    auto computePeakWidth = [&](float centerFreq) -> float {
        int centerBin = static_cast<int>(centerFreq / binWidth);
        if (centerBin < 5 || centerBin >= bins - 5) return 200.0f; // значение по умолчанию

        // Находим реальный локальный максимум в окрестности
        float peakMag = spectrum[centerBin];
        int peakBin = centerBin;
        for (int offset = -3; offset <= 3; ++offset) {
            int idx = centerBin + offset;
            if (idx >= 0 && idx < bins && spectrum[idx] > peakMag) {
                peakMag = spectrum[idx];
                peakBin = idx;
            }
        }

        float halfMag = peakMag * 0.5f;   // -3 dB
        int left = peakBin, right = peakBin;
        while (left > 0 && spectrum[left] > halfMag) left--;
        while (right < bins - 1 && spectrum[right] > halfMag) right++;

        float widthHz = (right - left) * binWidth;
        return std::max(20.0f, widthHz);   // ширина не может быть меньше 20 Гц
        };

    features.formant1Width = computePeakWidth(config_.voiceFormant1);
    features.formant2Width = computePeakWidth(config_.voiceFormant2);
    // -------------------------------------------------

    // 6. Гармоническое соотношение
    //float fundamental = 100.0f;
    features.harmonicRatio = CalculateHarmonicRatio(spectrum, binWidth);
    //features.harmonicRatio = CalculateHarmonicRatio(spectrum, fundamental);

    // 7. Импульсные шумы (кашель, удары)
    float maxInstantEnergy = 0.0f;
    float avgInstantEnergy = 0.0f;
    const int windowSize = std::max(1, static_cast<int>(SAMPLE_RATE * 0.001f)); // 1 мс

    for (size_t i = 0; i + windowSize <= frame.size(); i += windowSize) {
        float windowEnergy = 0.0f;
        for (int j = 0; j < windowSize; j++) {
            windowEnergy += frame[i + j] * frame[i + j];
        }
        windowEnergy /= windowSize;
        maxInstantEnergy = std::max(maxInstantEnergy, windowEnergy);
        avgInstantEnergy += windowEnergy;
    }
    avgInstantEnergy /= (frame.size() / static_cast<float>(windowSize) + 1e-8f);
    features.impulseMetric = (maxInstantEnergy + 1e-10f) / (avgInstantEnergy + 1e-10f);

    // 8. Тональные шумы (свист, звон)
    float maxSpectralPeak = 0.0f;
    int peakBinIdx = 0;
    for (int i = 5; i < bins - 5; i++) {
        if (spectrum[i] > maxSpectralPeak) {
            maxSpectralPeak = spectrum[i];
            peakBinIdx = i;
        }
    }
    float halfPeak = maxSpectralPeak * 0.5f;
    int leftIdx = peakBinIdx, rightIdx = peakBinIdx;
    while (leftIdx > 0 && spectrum[leftIdx] > halfPeak) leftIdx--;
    while (rightIdx < bins - 1 && spectrum[rightIdx] > halfPeak) rightIdx++;

    float peakBandwidth = (rightIdx - leftIdx) * binWidth;
    float peakFreq = peakBinIdx * binWidth;

    if (peakFreq > 1000.0f && peakBandwidth < 300.0f && maxSpectralPeak > 0.01f) {
        features.tonalityMetric = maxSpectralPeak / (arithmeticMean + 1e-8f);
    }
    else {
        features.tonalityMetric = 0.0f;
    }

    // 9. Детекция трения (шуршание микрофона)
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

    // 10. Скорость модуляции и временная согласованность (логарифмическая шкала)
    if (!featureHistory_.empty()) {
        const auto& prev = featureHistory_.back();

        // Переводим энергию в dB (логарифмическая шкала)
        float prevEnergyDB = 10.0f * log10f(prev.totalEnergy + 1e-10f);
        float currEnergyDB = 10.0f * log10f(features.totalEnergy + 1e-10f);
        float energyDiffDB = std::abs(currEnergyDB - prevEnergyDB);
        // Нормируем на 20 dB (типичный перепад громкости речи)
        float normEnergyDiff = std::min(1.0f, energyDiffDB / 20.0f);
        features.modulationRate = normEnergyDiff;      // теперь в диапазоне 0..1

        // Центроид в Гц, нормируем на 2000 Гц
        float centroidDiff = std::abs(features.spectralCentroid - prev.spectralCentroid);
        float normCentroidDiff = std::min(1.0f, centroidDiff / 2000.0f);

        // Временная согласованность: 1.0 – сигнал стабилен, 0.0 – резко изменился
        features.temporalConsistency = 1.0f - (normEnergyDiff + normCentroidDiff) * 0.5f;
    }
    else {
        features.modulationRate = 0.0f;
        features.temporalConsistency = 0.5f;   // нейтральное начальное значение
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
void VoiceDetector::UpdateFeatureStats(const VoiceFeatures& f, bool isNoiseFrame) {
    if (!isNoiseFrame) return;
    // признаки: voiceBandEnergy, harmonicRatio, formant1Strength, formant2Strength, temporalConsistency, spectralFlatness
    /*std::vector<float> values = {
        f.voiceBandEnergy, f.harmonicRatio, f.formant1Strength, f.formant2Strength,
        f.temporalConsistency, f.spectralFlatness
    };
    for (size_t idx = 0; idx < values.size(); ++idx) {
        auto& s = featureStats[idx];
        s.count++;
        float delta = values[idx] - s.mean;
        s.mean += delta / s.count;
        float delta2 = values[idx] - s.mean;
        s.variance += delta * delta2;
    }*/
    std::vector<float> values = {
           f.voiceBandEnergy, f.harmonicRatio, f.formant1Strength, f.formant2Strength,
           f.temporalConsistency, f.spectralFlatness,
           f.lowFreqRatio, f.spectralCentroid, f.zeroCrossingRate
    };
    for (size_t idx = 0; idx < values.size(); ++idx) {
        auto& s = featureStats[idx];
        s.count++;
        float delta = values[idx] - s.mean;
        s.mean += delta / s.count;
        float delta2 = values[idx] - s.mean;
        s.variance += delta * delta2;
    }
}

bool VoiceDetector::ClassifyVoice(const VoiceFeatures& features, float& confidence) {
    // Получаем нормализованные значения
    float normVoiceEnergy = featureStats[0].Normalize(features.voiceBandEnergy);
    float normHarmonic = featureStats[1].Normalize(features.harmonicRatio);
    float normFormant1 = featureStats[2].Normalize(features.formant1Strength);
    float normFormant2 = featureStats[3].Normalize(features.formant2Strength);
    float normConsistency = featureStats[4].Normalize(features.temporalConsistency);
    //float normFlatness = std::max(-3.0f, std::min(3.0f, featureStats[5].Normalize(1.0f - features.spectralFlatness))); // чем ниже flatness, тем больше похоже на речь
    float normFlatness = featureStats[5].Normalize(features.spectralFlatness);
    float normLowFreq = featureStats[6].Normalize(features.lowFreqRatio);
    float normCentroid = featureStats[7].Normalize(features.spectralCentroid);
    float normZcr = featureStats[8].Normalize(features.zeroCrossingRate);
    float widthPenalty = 0.0f;
    float minWidth = config_.formantPeakMinWidthHz;  // 100 Гц вместо 150
    if (features.formant2Width < minWidth) {
        // Плавный штраф: если ширина 50 Гц → штраф 0.4; если 100 Гц → 0.0
        float penalty = (minWidth - features.formant2Width) / minWidth;
        widthPenalty = std::min(0.4f, penalty * 0.8f);
    }
    // Линейная комбинация с весами
    // Веса можно подобрать экспериментально, здесь даны разумные значения
    float w_energy = 0.25f;
    float w_harmonic = 0.15f;   // было 0.20
    float w_formant = 0.15f;
    float w_consistency = 0.15f;   // повысить, так как теперь стабильна
    float w_flatness = -0.20f;  // ослабить
    float w_lowfreq = -0.15f;  // ослабить
    float w_centroid = 0.20f;   // повысить
    float w_zcr = 0.10f;

    float z =
        w_energy * normVoiceEnergy +
        w_harmonic * normHarmonic +
        w_formant * ((normFormant1 + normFormant2) * 0.5f) +
        w_consistency * normConsistency +
        w_flatness * normFlatness +
        w_lowfreq * normLowFreq +
        w_centroid * normCentroid +
        w_zcr * normZcr;

    // Применяем штраф за узкий пик
    z -= widthPenalty;

    // Сигмоида
    const float steepness = 2.5f;   // чуть круче для чёткого порога
    confidence = 1.0f / (1.0f + expf(-steepness * z));

    // Небольшой гистерезис (фильтруем)
    float threshold = config_.minVoiceConfidence;
    if (consecutiveVoiceFrames_ > 3) threshold -= 0.05f;

    return (confidence >= threshold);
}
float VoiceDetector::DetectFormant(const std::vector<float>& spectrum, float centerFreq, float bandwidth) {
    float binWidth = static_cast<float>(SAMPLE_RATE) / fftSize;

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

float VoiceDetector::CalculateHarmonicRatio(const std::vector<float>& spectrum, float binWidth) {
    // Ищем максимум в диапазоне 80-400 Гц
    int minBin = 80 / binWidth;
    int maxBin = 400 / binWidth;
    float maxPeak = 0.0f;
    int fundBin = minBin;
    for (int i = minBin; i <= maxBin && i < (int)spectrum.size(); i++) {
        if (spectrum[i] > maxPeak) { maxPeak = spectrum[i]; fundBin = i; }
    }
    if (maxPeak < 0.001f) return 0.0f;
    float f0 = fundBin * binWidth;
    float harmonicSum = 0.0f, total = 0.0f;
    for (int h = 1; h <= 4; h++) {
        int bin = fundBin * h;
        if (bin < (int)spectrum.size()) {
            harmonicSum += spectrum[bin];
            total += spectrum[bin];
            for (int j = -1; j <= 1; j++) {
                if (bin + j > 0 && bin + j < (int)spectrum.size())
                    total += spectrum[bin + j];
            }
        }
    }
    return harmonicSum / (total + 1e-8f);
}


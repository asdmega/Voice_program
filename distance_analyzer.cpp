#include "distance_analyzer.h"
#include <cstring>
#include <iostream>
#include "utility.h"

DistanceAnalyzer::DistanceAnalyzer()
    : currentDistance_(Dist_NORMAL),
      confidenceCounter_(0),
      averageSignalDb_(-60.0f),
      averageNoiseDb_(-40.0f) {}

DistanceAnalyzer::~DistanceAnalyzer() {}

bool DistanceAnalyzer::Initialize(const Config& config) {
    config_ = config;
    
    signalLevelHistory_.clear();
    noiseFloorHistory_.clear();
    distanceCategoryHistory_.clear();

    // Инициализируем с параметрами для нормального расстояния
    currentDistance_ = Dist_NORMAL;
    UpdateAdaptiveParameters();

    return true;
}

DistanceAnalyzer::DistanceProfile DistanceAnalyzer::AnalyzeFrame(
    const float* audioData, int samples,
    float voiceConfidence, float noiseFloor) {

    if (!audioData || samples <= 0) {
        DistanceProfile profile;
        profile.estimatedDistanceCategory = currentDistance_;
        profile.confidence = 0.0f;
        profile.estimatedDistanceM = 1.0f;
        return profile;
    }

    // Рассчитываем RMS
    float rms = 0.0f;
    for (int i = 0; i < samples; i++) {
        rms += audioData[i] * audioData[i];
    }
    rms = std::sqrt(rms / samples);
    float signalDb = AudioUtils::LinearToDb(rms);

    // Обновляем историю
    signalLevelHistory_.push_back(signalDb);
    noiseFloorHistory_.push_back(noiseFloor);
    if (signalLevelHistory_.size() > HISTORY_SIZE) {
        signalLevelHistory_.pop_front();
        noiseFloorHistory_.pop_front();
    }

    // Расчет экспоненциального скользящего среднего
    float alpha = 0.15f;
    averageSignalDb_ = (1.0f - alpha) * averageSignalDb_ + alpha * signalDb;
    averageNoiseDb_ = (1.0f - alpha) * averageNoiseDb_ + alpha * noiseFloor;

    // Определяем расстояние
    DistanceCategory newDistance = EstimateDistanceFromLevel(averageSignalDb_, averageNoiseDb_);
    
    // Для стабильности требуем несколько согласованных измерений
    distanceCategoryHistory_.push_back(newDistance);
    if (distanceCategoryHistory_.size() > 5) {
        distanceCategoryHistory_.pop_front();
    }

    // Выбираем наиболее часто встречающуюся категорию
    int voteCounts[5] = {0};
    for (auto cat : distanceCategoryHistory_) {
        voteCounts[static_cast<int>(cat)]++;
    }

    int maxVotes = 0;
    DistanceCategory consensusDistance = currentDistance_;
    for (int i = 0; i < 5; i++) {
        if (voteCounts[i] > maxVotes) {
            maxVotes = voteCounts[i];
            consensusDistance = static_cast<DistanceCategory>(i);
        }
    }

    // Обновляем текущее расстояние только если достаточно уверены
    if (maxVotes >= 3 || distanceCategoryHistory_.size() >= 5) {
        if (consensusDistance != currentDistance_) {
            confidenceCounter_ = 0;  // Сброс счетчика при изменении
        }
        currentDistance_ = consensusDistance;
    }

    confidenceCounter_++;

    // Обновляем адаптивные параметры
    UpdateAdaptiveParameters();

    // Подготавливаем результат
    DistanceProfile profile;
    profile.estimatedDistanceCategory = currentDistance_;
    profile.confidence = std::min(1.0f, confidenceCounter_ / 10.0f);  // Уверенность растет со временем
    profile.signalLevelDb = averageSignalDb_;
    profile.noiseFloorDb = averageNoiseDb_;
    profile.snrDb = averageSignalDb_ - averageNoiseDb_;

    // Приблизительная оценка расстояния в метрах
    switch (currentDistance_) {
        case Dist_VERY_CLOSE: profile.estimatedDistanceM = 0.05f; break;  // 5cm
        case Dist_CLOSE: profile.estimatedDistanceM = 0.30f; break;       // 30cm
        case Dist_NORMAL: profile.estimatedDistanceM = 1.0f; break;       // 1m
        case Dist_FAR: profile.estimatedDistanceM = 2.5f; break;          // 2.5m
        case Dist_VERY_FAR: profile.estimatedDistanceM = 5.0f; break;     // 5m
    }

    return profile;
}

DistanceAnalyzer::AdaptiveParameters DistanceAnalyzer::GetAdaptiveParameters() const {
    return currentParameters_;
}

void DistanceAnalyzer::Reset() {
    signalLevelHistory_.clear();
    noiseFloorHistory_.clear();
    distanceCategoryHistory_.clear();
    confidenceCounter_ = 0;
    currentDistance_ = Dist_NORMAL;
    averageSignalDb_ = -60.0f;
    averageNoiseDb_ = -40.0f;
    UpdateAdaptiveParameters();
}

DistanceAnalyzer::DistanceCategory DistanceAnalyzer::EstimateDistanceFromLevel(
    float signalDb, float noiseDb) {

    // Логика определения расстояния:
    // Очень близко: высокий уровень сигнала (> -15 dB)
    // Близко: средне-высокий уровень (-15 to -20 dB)
    // Нормально: средний уровень (-20 to -32 dB)
    // Далеко: низкий уровень (-32 to -42 dB)
    // Очень далеко: очень низкий уровень (< -42 dB)

    if (signalDb >= -15.0f) {
        return Dist_VERY_CLOSE;
    }
    else if (signalDb >= -20.0f) {
        return Dist_CLOSE;
    }
    else if (signalDb >= -32.0f) {
        return Dist_NORMAL;
    }
    else if (signalDb >= -42.0f) {
        return Dist_FAR;
    }
    else {
        return Dist_VERY_FAR;
    }
}

float DistanceAnalyzer::CalculateConfidence() {
    // Уверенность основана на согласованности истории
    if (distanceCategoryHistory_.size() < 3) {
        return 0.0f;
    }

    // Проверяем сколько последних измерений совпадают
    int lastCategory = distanceCategoryHistory_.back();
    int matches = 0;
    for (int i = std::max(0, static_cast<int>(distanceCategoryHistory_.size()) - 5);
         i < static_cast<int>(distanceCategoryHistory_.size()); i++) {
        if (distanceCategoryHistory_[i] == lastCategory) {
            matches++;
        }
    }

    float confidence = matches / 5.0f;  // Максимум 5 согласованных измерений

    // Также учитываем SNR - большой SNR дает большую уверенность
    float snr = averageSignalDb_ - averageNoiseDb_;
    if (snr < 5.0f) {
        confidence *= 0.5f;  // Низкий SNR уменьшает уверенность
    } else if (snr > 20.0f) {
        confidence = std::min(1.0f, confidence * 1.2f);  // Высокий SNR увеличивает
    }

    return std::clamp(confidence, 0.0f, 1.0f);
}

void DistanceAnalyzer::UpdateAdaptiveParameters() {
    // Параметры адаптируются в зависимости от расстояния
    
    switch (currentDistance_) {
        case Dist_VERY_CLOSE:  // 0-10cm
            currentParameters_.voiceConfidenceThreshold = 0.25f;  // Мягче
            currentParameters_.noiseGateThreshold = -35.0f;       // Выше (выше порог)
            currentParameters_.agcTargetLevel = -12.0f;           // Тихше (много усиления)
            currentParameters_.voiceHangoverMs = 200.0f;          // Короче
            currentParameters_.noiseSuppression = 0.55f;          // Меньше подавления
            currentParameters_.minGainDb = -4.4f;                 // Минимум потери
            currentParameters_.noiseAdaptationRate = 0.12f;       // Быстрая адаптация
            currentParameters_.impulseThreshold = 6.0f;           // Чувствительнее
            currentParameters_.hopSize = 512;
            break;

        case Dist_CLOSE:  // 10-50cm
            currentParameters_.voiceConfidenceThreshold = 0.28f;
            currentParameters_.noiseGateThreshold = -38.0f;
            currentParameters_.agcTargetLevel = -16.0f;
            currentParameters_.voiceHangoverMs = 300.0f;
            currentParameters_.noiseSuppression = 0.60f;
            currentParameters_.minGainDb = -9.0f;
            currentParameters_.noiseAdaptationRate = 0.08f;
            currentParameters_.impulseThreshold = 7.0f;
            currentParameters_.hopSize = 512;
            break;

        case Dist_NORMAL:  // 50cm-1.5m (по умолчанию)
            currentParameters_.voiceConfidenceThreshold = 0.32f;
            currentParameters_.noiseGateThreshold = -38.0f;
            currentParameters_.agcTargetLevel = -18.0f;
            currentParameters_.voiceHangoverMs = 400.0f;
            currentParameters_.noiseSuppression = 0.65f;
            currentParameters_.minGainDb = -16.5f;
            currentParameters_.noiseAdaptationRate = 0.05f;
            currentParameters_.impulseThreshold = 8.0f;
            currentParameters_.hopSize = 512;
            break;

        case Dist_FAR:  // 1.5m-4m
            currentParameters_.voiceConfidenceThreshold = 0.38f;  // Строже
            currentParameters_.noiseGateThreshold = -45.0f;       // Ниже (ниже порог)
            currentParameters_.agcTargetLevel = -12.0f;           // Громче (много усиления)
            currentParameters_.voiceHangoverMs = 600.0f;          // Дольше
            currentParameters_.noiseSuppression = 0.70f;          // Больше подавления
            currentParameters_.minGainDb = -20.0f;                // Больше потери
            currentParameters_.noiseAdaptationRate = 0.03f;       // Медленнее адаптируется
            currentParameters_.impulseThreshold = 10.0f;          // Менее чувствительно
            currentParameters_.hopSize = 512;
            break;

        case Dist_VERY_FAR:  // 4m+
            currentParameters_.voiceConfidenceThreshold = 0.42f;  // Очень строго
            currentParameters_.noiseGateThreshold = -50.0f;       // Низко
            currentParameters_.agcTargetLevel = -8.0f;            // Очень громко
            currentParameters_.voiceHangoverMs = 800.0f;          // Очень долго
            currentParameters_.noiseSuppression = 0.75f;          // Максимально
            currentParameters_.minGainDb = -24.0f;                // Максимум потери
            currentParameters_.noiseAdaptationRate = 0.01f;       // Очень медленно
            currentParameters_.impulseThreshold = 12.0f;          // Минимально чувствительно
            currentParameters_.hopSize = 512;
            break;
    }
}


#pragma once

#include <vector>
#include <deque>
#include <cmath>
#include <algorithm>
#include "audio_defs.h"

// ============================================================================
// Distance Analyzer - автоматическое определение расстояния до микрофона
// и адаптация всех параметров без жестких констант
// ============================================================================


class DistanceAnalyzer {
public:
    enum DistanceCategory {
        Dist_VERY_CLOSE = 0,  // 0-10cm   - микрофон у рта (гарнитура)
        Dist_CLOSE = 1,       // 10-50cm  - нормальная позиция микрофона
        Dist_NORMAL = 2,      // 50cm-1.5m - стандартное расстояние
        Dist_FAR = 3,         // 1.5m-4m  - несколько метров
        Dist_VERY_FAR = 4     // 4m+      - большое расстояние
    };

    struct DistanceProfile {
        DistanceCategory estimatedDistanceCategory;
        float confidence;           // 0.0-1.0, насколько уверены в определении
        float estimatedDistanceM;   // Приблизительное расстояние в метрах
        float signalLevelDb;        // Текущий уровень сигнала
        float noiseFloorDb;         // Оценка уровня шума
        float snrDb;                // Signal-to-Noise Ratio
    };

    struct AdaptiveParameters {
        float voiceConfidenceThreshold;  // Порог уверенности для VAD
        float noiseGateThreshold;       // Порог noise gate
        float agcTargetLevel;           // Целевой уровень для AGC
        float voiceHangoverMs;          // Время удержания речи
        float noiseSuppression;         // Коэффициент подавления шума (0-1)
        float minGainDb;                // Минимальное усиление для NS
        float noiseAdaptationRate;      // Скорость адаптации шумового профиля
        float impulseThreshold;         // Порог для детектора импульсов
        int hopSize;                    // Размер шага для FFT
    };

    struct Config {
        int SAMPLE_RATE = 48000;
        int updateIntervalMs = 100;
    };

    DistanceAnalyzer();
    ~DistanceAnalyzer();

    // Инициализация
    bool Initialize(const Config& config);

    // Анализ входного сигнала и определение расстояния
    DistanceProfile AnalyzeFrame(const float* audioData, int samples, 
                                 float voiceConfidence, float noiseFloor);

    // Получить адаптивные параметры для текущего расстояния
    AdaptiveParameters GetAdaptiveParameters() const;

    // Получить текущую категорию расстояния
    DistanceCategory GetDistanceCategory() const { return currentDistance_; }

    // Сбросить анализ (при смене окружения)
    void Reset();

private:
    Config config_;
    DistanceCategory currentDistance_;
    int confidenceCounter_;          // Счетчик кадров для уверенности

    // История сигналов для более надежного определения
    std::deque<float> signalLevelHistory_;  // Последние N значений RMS
    std::deque<float> noiseFloorHistory_;   // История шумового уровня
    std::deque<int> distanceCategoryHistory_;  // История определений

    static constexpr int HISTORY_SIZE = 30;  // Около 300ms при 48kHz

    
    // Определение расстояния по RMS уровню и шуму
    DistanceCategory EstimateDistanceFromLevel(float signalDb, float noiseDb);
    
    // Расчет уверенности в определении
    float CalculateConfidence();
    
    // Адаптивный расчет параметров
    void UpdateAdaptiveParameters();

    // Внутреннее состояние
    AdaptiveParameters currentParameters_;
    float averageSignalDb_;
    float averageNoiseDb_;
};

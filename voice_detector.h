// voice_detector.h - ИСПРАВЛЕННАЯ ВЕРСИЯ для плавной передачи голоса
#pragma once
#include <vector>
#include <deque>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <mutex>
#include <complex>
#include <array>
// === КЛЮЧЕВЫЕ ИЗМЕНЕНИЯ для устранения прерывистой речи ===
// 1. minVoiceConfidence снижен с 0.55 до 0.35 (не обрывает речь)
// 2. voiceHangoverMs увеличен с 200 до 400 мс (дольше держим голос)
// 3. voiceFreqHigh расширен до 4000 Гц (весь речевой спектр)
// 4. Пороги шумов снижены для лучшей фильтрации

class VoiceDetector {
public:
    struct DetectorConfig {
        // РАСШИРЕННЫЙ диапазон голоса - весь спектр речи
        float voiceFreqLow = 70.0f;      // Было 85
        float voiceFreqHigh = 4000.0f;   // Было 255 (!!!) - ключевое изменение
        float voiceFormant1 = 500.0f;
        float voiceFormant2 = 1500.0f;
        float voiceFormant3 = 2500.0f;

        // Быстрая адаптация и длинный hangover
        float noiseAdaptationSpeed = 0.05f;  // Было 0.02
        int noiseProfileFrames = 30;         // Было 50
        float voiceHangoverMs = 400.0f;        // Было 200 - ДОЛЬШЕ держим голос

        // === СНИЖЕННЫЙ порог confidence - ключ к непрерывной речи ===
        float minVoiceConfidence = 0.35f;    // Было 0.55 - МЯГЧЕ!
        float maxNoiseLevel = -15.0f;          // Было -20

        // УСИЛЕННАЯ фильтрация шумов (пороги снижены = строже)
        float impulseThreshold = 8.0f;       // Было 15 - ловим кашель/стуки
        float tonalityThreshold = 4.0f;      // Было 8 - ловим свист/звон
        float frictionThreshold = 0.25f;     // Было 0.4 - ловим трение
        float plosiveThreshold = 6.0f;       // Было 12 - ловим щелчки


        float weightLowFreqRatio = -0.25f;     // отрицательный – штраф за низкочастотный гул
        float weightSpectralCentroid = 0.20f;  // чем выше центроид, тем больше похоже на речь
        float weightZeroCrossing = 0.15f;      // прямая связь

        // Порог для определения «узкого» пика
        float formantPeakMinWidthHz = 100.0f;   // пик шире 150 Гц – кандидат в форманту
    };

    struct VoiceFeatures {
        float totalEnergy = 0.0f;
        float voiceBandEnergy = 0.0f;
        float lowFreqRatio = 0.0f;
        float spectralCentroid = 0.0f;
        float spectralFlatness = 0.0f;
        float peakToAverageRatio = 0.0f; // Для отсечения ударов
        float harmonicRatio = 0.0f;
        float formant1Strength = 0.0f;
        float formant2Strength = 0.0f;
        float zeroCrossingRate = 0.0f;

        // Пункт 5: ширина формантных пиков (на -3dB)
        float formant1Width = 0.0f;   // ширина пика вокруг formant1 (в Гц)
        float formant2Width = 0.0f;   // ширина пика вокруг formant2

        // Метрики для подавления шума
        float impulseMetric = 0.0f;      // Стук, кашель
        float tonalityMetric = 0.0f;     // Свист
        float frictionMetric = 0.0f;     // Шуршание ткани
        float plosiveMetric = 0.0f;      // Взрывные звуки (П, Б)
        float modulationRate = 0.0f;     // Скорость изменения громкости
        float temporalConsistency = 0.0f; // Стабильность звука (Голос стабилен, шум - нет)
    };

    struct DetectionResult {
        bool isVoice = false;
        float confidence = 0.0f;
        float noiseFloor = -100.0f;
        float voiceLevel = -100.0f;
        bool isCalibrated = false;
        int calibrationProgress = 0;

        // НОВЫЕ: флаги обнаруженных шумов
        bool isImpulseNoise = false;       // Кашель, удары
        bool isTonalNoise = false;         // Свист, писк
        bool isFrictionNoise = false;      // Трение, шуршание
        bool isPlosiveNoise = false;       // Щелчки, пластик

        std::string rejectionReason;       // Почему отклонено
    };
    // Структура для скользящей статистики (метод Уэлфорда)
    struct FeatureStats {
        float mean = 0.0f;
        float variance = 0.0f;
        int count = 0;

        void Update(float x) {
            count++;
            float delta = x - mean;
            mean += delta / count;
            float delta2 = x - mean;
            variance += delta * delta2;
        }

        float GetStdDev() const {
            if (count < 2) return 1.0f;
            return std::sqrt(variance / (count - 1) + 1e-8f);
        }

        float Normalize(float x) const {
            float sd = GetStdDev();
            if (sd < 1e-6f) return 0.0f;
            return (x - mean) / sd;
        }
    };

    std::array<FeatureStats, 9> featureStats; // для каждого используемого признака


    VoiceDetector();
    ~VoiceDetector();

    bool Initialize(const DetectorConfig& config);

    // Обработка кадра аудио
    DetectionResult ProcessFrame(const std::vector<int16_t>& audioData);
    DetectionResult ProcessFrame(const std::vector<float>& audioData);

    // Принудительная калибровка шума (10 секунд тишины)
    void StartCalibration();
    bool IsCalibrated() const;

    // Сброс профиля шума
    void ResetNoiseProfile();

    // Получение текущей статистики
    DetectionResult GetLastResult() const;
    
    void SetThreshold(float threshold) {
        std::lock_guard<std::mutex> lock(mutex_);
        config_.minVoiceConfidence = std::max(0.3f, std::min(0.9f, threshold));
    }

private:
    DetectorConfig config_;

    // Буферы и состояние
    std::vector<float> fftWindow_;
    std::vector<float> fftBuffer_;
    std::vector<std::complex<float>> fftOutput_;  // <-- Используем std::complex<float>
    std::vector<float> noiseProfile_;
    std::vector<float> smoothedSpectrum_;

    // История для анализа
    std::deque<VoiceFeatures> featureHistory_;
    std::deque<float> energyHistory_;
    std::deque<bool> voiceDecisionHistory_;

    // Состояние
    mutable std::mutex mutex_;
    DetectionResult lastResult_;
    bool isCalibrating_ = false;
    int calibrationFrames_ = 0;
    int voiceHangoverCount_ = 0;
    int consecutiveNoiseFrames_ = 0;
    int consecutiveVoiceFrames_ = 0;

    // Извлечение признаков
    VoiceFeatures ExtractFeatures(const std::vector<float>& frame);

    // Обновление профиля шума
    void UpdateNoiseProfile(const VoiceFeatures& features, const std::vector<float>& spectrum);

    void UpdateFeatureStats(const VoiceFeatures& f, bool isNoiseFrame);

    // Принятие решения
    bool ClassifyVoice(const VoiceFeatures& features, float& confidence);

   
    float CalculateHarmonicRatio(const std::vector<float>& spectrum, float fundamental);
    float DetectFormant(const std::vector<float>& spectrum, float centerFreq, float bandwidth);
    
    float EstimateFundamental(const std::vector<float>& spectrum, float binWidth);

};


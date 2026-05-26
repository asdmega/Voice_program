//optimized_settings.h:
#pragma once
#include <atomic>
#include <algorithm>
#include <utility> // For std::move if needed

struct OptimizedSettings {
    // Video optimizations
    std::atomic<bool> enableVideoScaling{true};
    std::atomic<bool> enableAdvancedCompression{true};
    std::atomic<bool> enableAdaptiveBitrate{true};
    std::atomic<bool> enableFrameSkipping{false};
    // Audio optimizations
    std::atomic<bool> enableOpusOptimization{true};
    std::atomic<bool> enableEchoCancellation{false};
    // Network optimizations
    std::atomic<bool> enableNetworkOptimization{true};
    std::atomic<bool> enableCongestionControl{true};

    // Target parameters (non-atomic)
    int targetBitrate = 45000000;
    int maxBitrate = 50000000;
    int minBitrate = 20000000;
    int videoQuality = 8;
    int audioQuality = 9;
    bool useDynamicScaling = true;
    float scalingFactor = 1.0f;

    // Stats (non-atomic)
    struct Stats {
        int framesSent = 0;
        int framesSkipped = 0;
        int bytesSent = 0;
        double averageBitrate = 0;
        double networkQuality = 1.0;
        int droppedFrames = 0;
        int framesEncoded = 0;
        int totalBytesSent = 0;
    } stats;

    // ============ Constructors ============
    // Default constructor
    OptimizedSettings() = default;
    
    // Destructor
    ~OptimizedSettings() = default;

    // Copy constructor
    OptimizedSettings(const OptimizedSettings& other) {
        enableVideoScaling.store(other.enableVideoScaling.load(std::memory_order_relaxed));
        enableAdvancedCompression.store(other.enableAdvancedCompression.load(std::memory_order_relaxed));
        enableAdaptiveBitrate.store(other.enableAdaptiveBitrate.load(std::memory_order_relaxed));
        enableFrameSkipping.store(other.enableFrameSkipping.load(std::memory_order_relaxed));
        enableOpusOptimization.store(other.enableOpusOptimization.load(std::memory_order_relaxed));
        enableEchoCancellation.store(other.enableEchoCancellation.load(std::memory_order_relaxed));
        enableNetworkOptimization.store(other.enableNetworkOptimization.load(std::memory_order_relaxed));
        enableCongestionControl.store(other.enableCongestionControl.load(std::memory_order_relaxed));

        targetBitrate = other.targetBitrate;
        maxBitrate = other.maxBitrate;
        minBitrate = other.minBitrate;
        videoQuality = other.videoQuality;
        audioQuality = other.audioQuality;
        useDynamicScaling = other.useDynamicScaling;
        scalingFactor = other.scalingFactor;
        stats = other.stats;
    }

    // Copy assignment
    OptimizedSettings& operator=(const OptimizedSettings& other) {
        if (this != &other) {
            enableVideoScaling.store(other.enableVideoScaling.load(std::memory_order_relaxed));
            enableAdvancedCompression.store(other.enableAdvancedCompression.load(std::memory_order_relaxed));
            enableAdaptiveBitrate.store(other.enableAdaptiveBitrate.load(std::memory_order_relaxed));
            enableFrameSkipping.store(other.enableFrameSkipping.load(std::memory_order_relaxed));
            enableOpusOptimization.store(other.enableOpusOptimization.load(std::memory_order_relaxed));
            enableEchoCancellation.store(other.enableEchoCancellation.load(std::memory_order_relaxed));
            enableNetworkOptimization.store(other.enableNetworkOptimization.load(std::memory_order_relaxed));
            enableCongestionControl.store(other.enableCongestionControl.load(std::memory_order_relaxed));

            targetBitrate = other.targetBitrate;
            maxBitrate = other.maxBitrate;
            minBitrate = other.minBitrate;
            videoQuality = other.videoQuality;
            audioQuality = other.audioQuality;
            useDynamicScaling = other.useDynamicScaling;
            scalingFactor = other.scalingFactor;
            stats = other.stats;
        }
        return *this;
    }

    // Move constructor (effectively copies since atomics can't move)
    OptimizedSettings(OptimizedSettings&& other) noexcept {
        enableVideoScaling.store(other.enableVideoScaling.load(std::memory_order_relaxed));
        enableAdvancedCompression.store(other.enableAdvancedCompression.load(std::memory_order_relaxed));
        enableAdaptiveBitrate.store(other.enableAdaptiveBitrate.load(std::memory_order_relaxed));
        enableFrameSkipping.store(other.enableFrameSkipping.load(std::memory_order_relaxed));
        enableOpusOptimization.store(other.enableOpusOptimization.load(std::memory_order_relaxed));
        enableEchoCancellation.store(other.enableEchoCancellation.load(std::memory_order_relaxed));
        enableNetworkOptimization.store(other.enableNetworkOptimization.load(std::memory_order_relaxed));
        enableCongestionControl.store(other.enableCongestionControl.load(std::memory_order_relaxed));

        targetBitrate = other.targetBitrate;
        maxBitrate = other.maxBitrate;
        minBitrate = other.minBitrate;
        videoQuality = other.videoQuality;
        audioQuality = other.audioQuality;
        useDynamicScaling = other.useDynamicScaling;
        scalingFactor = other.scalingFactor;
        stats = other.stats;

        // Optionally reset source (not required but can be done)
        other.targetBitrate = 0;
        // etc.
    }

    // Move assignment
    OptimizedSettings& operator=(OptimizedSettings&& other) noexcept {
        if (this != &other) {
            enableVideoScaling.store(other.enableVideoScaling.load(std::memory_order_relaxed));
            enableAdvancedCompression.store(other.enableAdvancedCompression.load(std::memory_order_relaxed));
            enableAdaptiveBitrate.store(other.enableAdaptiveBitrate.load(std::memory_order_relaxed));
            enableFrameSkipping.store(other.enableFrameSkipping.load(std::memory_order_relaxed));
            enableOpusOptimization.store(other.enableOpusOptimization.load(std::memory_order_relaxed));
            enableEchoCancellation.store(other.enableEchoCancellation.load(std::memory_order_relaxed));
            enableNetworkOptimization.store(other.enableNetworkOptimization.load(std::memory_order_relaxed));
            enableCongestionControl.store(other.enableCongestionControl.load(std::memory_order_relaxed));

            targetBitrate = other.targetBitrate;
            maxBitrate = other.maxBitrate;
            minBitrate = other.minBitrate;
            videoQuality = other.videoQuality;
            audioQuality = other.audioQuality;
            useDynamicScaling = other.useDynamicScaling;
            scalingFactor = other.scalingFactor;
            stats = other.stats;

            // Optionally reset source
            other.targetBitrate = 0;
            // etc.
        }
        return *this;
    }

    // Other methods remain the same...
    void SetVideoScaling(bool enabled) { enableVideoScaling.store(enabled, std::memory_order_relaxed); }
    void SetAdvancedCompression(bool enabled) { enableAdvancedCompression = enabled; }
    void SetAdaptiveBitrate(bool enabled) { enableAdaptiveBitrate = enabled; }
    void SetFrameSkipping(bool enabled) { enableFrameSkipping = enabled; }
    void SetOpusOptimization(bool enabled) { enableOpusOptimization = enabled; }
    void SetEchoCancellation(bool enabled) { enableEchoCancellation = enabled; }
    void SetNetworkOptimization(bool enabled) { enableNetworkOptimization = enabled; }
    void SetCongestionControl(bool enabled) { enableCongestionControl = enabled; }

    void SetScalingFactor(float factor) {
        scalingFactor = std::max(0.5f, std::min(1.0f, factor));
    }

    void SetTargetBitrate(int bitrate) {
        targetBitrate = std::max(minBitrate, std::min(maxBitrate, bitrate));
    }

    void SetVideoQuality(int quality) {
        videoQuality = std::max(1, std::min(10, quality));
    }

    void SetAudioQuality(int quality) {
        audioQuality = std::max(1, std::min(10, quality));
    }

    // ��������� � �������� ����
    void AdaptToNetworkQuality(double quality) {
        Stats main;
        main.networkQuality = quality;

        if (enableAdaptiveBitrate) {
            if (quality > 0.8) {
                // �������� �������� ���� - ����� ��������� ��������
                SetTargetBitrate(45000000);
                SetVideoQuality(9);
                SetScalingFactor(1.0f);
            }
            else if (quality > 0.6) {
                // ������� ��������
                SetTargetBitrate(35000000);
                SetVideoQuality(8);
                SetScalingFactor(1.0f);
            }
            else if (quality > 0.4) {
                // ������� ��������
                SetTargetBitrate(30000000);
                SetVideoQuality(7);
                SetScalingFactor(0.9f);
            }
            else if (quality > 0.2) {
                // ������ ��������
                SetTargetBitrate(25000000);
                SetVideoQuality(6);
                SetScalingFactor(0.8f);
            }
            else {
                // ����� ������ ��������
                SetTargetBitrate(20000000);
                SetVideoQuality(5);
                SetScalingFactor(0.7f);
            }
        }
    }

    // �������� ����������� ������ ����� � ������ ���������������
    void GetOptimalSize(int& width, int& height) const {
        if (enableVideoScaling && scalingFactor < 1.0f) {
            width = static_cast<int>(1920 * scalingFactor);
            height = static_cast<int>(1080 * scalingFactor);
            // ������ ������� 16 ��� ������ ������������� �������
            width = (width + 15) & ~15;
            height = (height + 15) & ~15;
        }
        else {
            width = 1920;
            height = 1080;
        }
    }

    // �������� ����������� FPS � ������ �������� ������
    int GetOptimalFPS() {
        Stats main;
        if (enableFrameSkipping && main.networkQuality < 0.5) {
            return 30; // ���������� ������ ������ ����
        }
        return 60;
    }

    // �������� ����������� �������
    int GetOptimalBitrate()const {
        if (enableAdaptiveBitrate) {
            return targetBitrate;
        }
        return 45000000; // ������������� 45 Mbps
    }

    // �������� ������� ������ (0.0 - 1.0, ��� 1.0 = ��� ������)
    float GetCompressionLevel() const {
        if (!enableAdvancedCompression) {
            return 0.5f; // ������� ������ 50%
        }

        // ����������� ������ �� ������ �������� ��������
        int optimalBitrate = GetOptimalBitrate();
        float rawBitrate = 1920.0f * 1080.0f * 4.0f * 60.0f * 8.0f / 1000000.0f; // ~3981 Mbps
        float compressionRatio = optimalBitrate / (rawBitrate * 1000000.0f);

        return std::max(0.02f, std::min(1.0f, compressionRatio)); // ������� 2% �� ���������
    }
};

// ���������� ������ ��������
extern OptimizedSettings gOptimizedSettings;

//advanced_video_codec.h:
#pragma once

#include <vector>
#include <memory>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <chrono>

#ifdef USE_FFMPEG
extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
}
#endif

#include "optimized_settings.h"
#include <WinSock2.h>

// ����������� ����� ����� � ���������� ��������������� � �����������
class AdvancedVideoCodec {
public:
    enum class ScalingMode {
        NONE,           // ��� ���������������
        BILINEAR,       // ���������� ������������
        BICUBIC,        // ������������ ������������
        LANCZOS,        // ������ ������������
        DYNAMIC         // ������������ ���������������
    };

    struct VideoConfig {
        int inputWidth = 1920;
        int inputHeight = 1080;
        int outputWidth = 1920;
        int outputHeight = 1080;
        int fps = 60;
        int targetBitrate = 45000000; // 45 Mbps
        int maxBitrate = 50000000;    // 50 Mbps
        int minBitrate = 20000000;    // 20 Mbps
        ScalingMode scalingMode = ScalingMode::DYNAMIC;
        bool enableAdaptiveQuality = true;
        int quality = 8; // 1-10 scale
    };

    struct EncodedFrame {
        std::vector<uint8_t> data;
        int64_t timestamp;
        bool isKeyFrame;
        int width;
        int height;
        int originalWidth;
        int originalHeight;
        float scalingFactor;
        int qualityLevel;
    };

    struct FrameMetadata {
        int originalWidth;
        int originalHeight;
        int encodedWidth;
        int encodedHeight;
        float scalingFactor;
        int quality;
        bool isKeyFrame;
        int64_t timestamp;
    };

    AdvancedVideoCodec();
    ~AdvancedVideoCodec();

    // ������������� ������
    bool Initialize(const VideoConfig& config);

    // ����������� ����� � ������ ��������
    void EncodeFrame(const std::vector<uint8_t>& rawFrame, const OptimizedSettings& settings);

    // ������������� �����
    std::vector<uint8_t> DecodeFrame(const EncodedFrame& encodedFrame);

    // ��������������� �����
    bool ScaleFrame(
        const std::vector<uint8_t>& inputFrame,
        std::vector<uint8_t>& outputFrame,
        int inputWidth, int inputHeight,
        int outputWidth, int outputHeight,
        ScalingMode mode
    );

    // ��������� ����������
    struct Statistics {
        double averageBitrate;
        double currentFPS;
        int encodedFrames;
        int scaledFrames;
        int skippedFrames;
        double encodingTimeMs;
        double scalingTimeMs;
        double averageScalingFactor;
        int qualityChanges;
    };

    Statistics GetStatistics() const;

    // ���������� ��������� � �������� �������
    void SetTargetBitrate(int bitrate);
    void SetFPS(int fps);
    void SetQuality(int quality);
    void SetScalingMode(ScalingMode mode);
    void ForceKeyFrame();

    // ��������� � ��������
    void AdaptToNetworkConditions(double networkQuality, int availableBandwidth);
    void AdaptToPerformance(double cpuUsage, double memoryUsage);

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

// ����������� ��������� ��� ������������ ������
class AsyncAdvancedVideoProcessor {
public:
    AsyncAdvancedVideoProcessor();
    ~AsyncAdvancedVideoProcessor();

    void Start();
    void Stop();

    void SubmitFrame(
        const std::vector<uint8_t>& rawFrame,
        const OptimizedSettings& settings
    );

    bool GetEncodedFrame(AdvancedVideoCodec::EncodedFrame& frame);

    void SetConfig(const AdvancedVideoCodec::VideoConfig& config);

    AdvancedVideoCodec::Statistics GetStatistics() const;

private:
    void ProcessingLoop();

    std::atomic<bool> running_;
    std::thread processingThread_;
    // ============ Async Video Processing ============
    struct InputFrame {
        std::vector<uint8_t> data;
        OptimizedSettings settings;
        int64_t timestamp;

        InputFrame() = default;
        ~InputFrame() = default;

        // Move semantics
        InputFrame(InputFrame&&) = default;
        InputFrame& operator=(InputFrame&&) = default;

        // Copy semantics now defaulted (no delete)
        InputFrame(const InputFrame&) = default;
        InputFrame& operator=(const InputFrame&) = default;
    };

    std::queue<InputFrame> inputBuffer_;
    std::queue<AdvancedVideoCodec::EncodedFrame> outputBuffer_;

    std::mutex inputMutex_;
    std::mutex outputMutex_;
    std::condition_variable inputCV_;
    std::condition_variable outputCV_;

    std::unique_ptr<AdvancedVideoCodec> codec_;
    AdvancedVideoCodec::VideoConfig config_;

    static const size_t MAX_INPUT_BUFFER = 10;
    static const size_t MAX_OUTPUT_BUFFER = 20;
};

// �������� ������������ ����� ������
class AdvancedVideoStreamManager {
public:
    AdvancedVideoStreamManager();
    ~AdvancedVideoStreamManager();

    bool Start(
        const std::string& targetIP,
        int port,
        const OptimizedSettings& settings
    );

    void Stop();

    void SubmitRawFrame(const std::vector<uint8_t>& frame);

    bool GetEncodedFrame(AdvancedVideoCodec::EncodedFrame& frame);

    void UpdateSettings(const OptimizedSettings& settings);

    AdvancedVideoCodec::Statistics GetStatistics() const;

    bool IsRunning() const { return isRunning_.load(); }

private:
    void StreamLoop();

    std::atomic<bool> isRunning_;
    std::thread streamThread_;

    std::unique_ptr<AsyncAdvancedVideoProcessor> processor_;

    // ������� ����������
    SOCKET socket_;
    sockaddr_in targetAddr_;

    // ������� ���������
    OptimizedSettings currentSettings_;

    // ����������
    struct StreamStats {
        std::atomic<int> framesSent;
        std::atomic<int> bytesSent;
        std::atomic<int> packetsSent;
        std::atomic<int> retransmissions;
    } stats_;
};

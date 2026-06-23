//advanced_video_codec.cpp:
#include "advanced_video_codec.h"
#include "optimized_settings.h"
#include <cstring>
#include <algorithm>
#include <chrono>
#include <vector>
#include <memory>
#include <winsock2.h>  // For InetPtonA
#include <ws2ipdef.h>
#include <Ws2tcpip.h>  // For InetPtonA

#ifdef USE_FFMPEG
extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
}
#endif

// Implementation class - defined directly here
class AdvancedVideoCodec::Impl {
private:
    struct AVCodecContext;
    struct AVFrame;
    struct AVPacket;
    struct SwsContext;
    AVCodecContext* codecContext_;
    AVFrame* frame_;
    AVPacket* packet_;
    SwsContext* swsContext_;

public:
    Impl() : codecContext_(nullptr), frame_(nullptr), packet_(nullptr),
        swsContext_(nullptr), frameCount_(0), startTime_(0),
        lastQualityChange_(0), consecutiveQualityIncreases_(0) {
    }

    ~Impl() {
        Cleanup();
    }

    bool Initialize(const AdvancedVideoCodec::VideoConfig& config) {
        config_ = config;

#ifdef USE_FFMPEG
        // ����������� �������
        avcodec_register_all();

        // ������� �����
        AVCodecID codecId = AV_CODEC_ID_H264; // ���������� H.264 ��� ������ �������������
        const AVCodec* codec = avcodec_find_encoder(codecId);
        if (!codec) {
            return false;
        }

        // ������� �������� ������
        codecContext_ = avcodec_alloc_context3(codec);
        if (!codecContext_) {
            return false;
        }

        // ����������� ���������
        codecContext_->width = config.outputWidth;
        codecContext_->height = config.outputHeight;
        codecContext_->time_base = AVRational{ 1, config.fps };
        codecContext_->framerate = AVRational{ config.fps, 1 };
        codecContext_->gop_size = config.fps * 2; // �������� ���� ������ 2 �������
        codecContext_->max_b_frames = 0; // ��� ������ ��������
        codecContext_->pix_fmt = AV_PIX_FMT_YUV420P;

        // ��������� ��������
        codecContext_->bit_rate = config.targetBitrate;
        codecContext_->rc_min_rate = config.minBitrate;
        codecContext_->rc_max_rate = config.maxBitrate;
        codecContext_->rc_buffer_size = config.targetBitrate / config.fps * 4;

        // ��������� H.264 ��� ������ ��������
        av_opt_set(codecContext_->priv_data, "preset", "ultrafast", 0);
        av_opt_set(codecContext_->priv_data, "tune", "zerolatency", 0);
        av_opt_set(codecContext_->priv_data, "profile", "high", 0);

        // ��������� ��������
        char qualityStr[32];
        snprintf(qualityStr, sizeof(qualityStr), "%d", config.quality);
        av_opt_set(codecContext_->priv_data, "crf", qualityStr, 0);

        // ��������� �����
        if (avcodec_open2(codecContext_, codec, nullptr) < 0) {
            return false;
        }

        // ������� ����
        frame_ = av_frame_alloc();
        frame_->format = codecContext_->pix_fmt;
        frame_->width = codecContext_->width;
        frame_->height = codecContext_->height;

        if (av_frame_get_buffer(frame_, 0) < 0) {
            return false;
        }

        // ������� �����
        packet_ = av_packet_alloc();

        return true;
#else
        // �������� ��� FFmpeg
        return true;
#endif
    }

    std::vector<AdvancedVideoCodec::EncodedFrame> EncodeFrame(
        const std::vector<uint8_t>& rawFrame,
        const OptimizedSettings& settings
    ) {
        std::vector<AdvancedVideoCodec::EncodedFrame> result;

        auto startTime = std::chrono::high_resolution_clock::now();

#ifdef USE_FFMPEG
        if (!codecContext_ || !frame_ || !packet_) {
            return EncodeFrameStub(rawFrame, settings);
        }

        // ���������� ����������� ���������
        int optimalWidth = config_.outputWidth;
        int optimalHeight = config_.outputHeight;
        int optimalFPS = config_.fps;
        int optimalBitrate = config_.targetBitrate;
        float scalingFactor = 1.0f;

        // ��������� ��������� �� OptimizedSettings
        if (settings.enableVideoScaling) {
            settings.GetOptimalSize(optimalWidth, optimalHeight);
            scalingFactor = static_cast<float>(optimalWidth) / config_.inputWidth;
        }

        if (settings.enableAdaptiveBitrate) {
            optimalBitrate = settings.GetOptimalBitrate();
            // ��������� ������� ������
            if (std::abs(optimalBitrate - codecContext_->bit_rate) > 1000000) {
                codecContext_->bit_rate = optimalBitrate;
                // ��������������� �����
                avcodec_close(codecContext_);
                avcodec_open2(codecContext_, avcodec_find_encoder(AV_CODEC_ID_H264), nullptr);
            }
        }

        // ������������ ���� ��� �������������
        std::vector<uint8_t> scaledFrame;
        if (optimalWidth != config_.inputWidth || optimalHeight != config_.inputHeight) {
            auto scaleStart = std::chrono::high_resolution_clock::now();

            scaledFrame.resize(optimalWidth * optimalHeight * 4);
            if (ScaleFrame(rawFrame, scaledFrame, config_.inputWidth, config_.inputHeight,
                optimalWidth, optimalHeight, config_.scalingMode)) {

                auto scaleEnd = std::chrono::high_resolution_clock::now();
                stats_.scalingTimeMs = std::chrono::duration<double, std::milli>(
                    scaleEnd - scaleStart).count();
                stats_.scaledFrames++;
            }
            else {
                scaledFrame = rawFrame;
            }
        }
        else {
            scaledFrame = rawFrame;
        }

        // ������������ RGB � YUV420P
        if (!ConvertRGBToYUV(scaledFrame, optimalWidth, optimalHeight)) {
            return result;
        }

        // ������������� timestamp � ��������� �����
        frame_->pts = frameCount_;
        frame_->width = optimalWidth;
        frame_->height = optimalHeight;

        // ���������� ���� �� �����������
        int ret = avcodec_send_frame(codecContext_, frame_);
        if (ret < 0) {
            return result;
        }

        // �������� �������������� ������
        while (ret >= 0) {
            ret = avcodec_receive_packet(codecContext_, packet_);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            else if (ret < 0) {
                break;
            }

            AdvancedVideoCodec::EncodedFrame encoded;
            encoded.data.assign(packet_->data, packet_->data + packet_->size);
            encoded.timestamp = packet_->pts;
            encoded.isKeyFrame = (packet_->flags & AV_PKT_FLAG_KEY) != 0;
            encoded.width = optimalWidth;
            encoded.height = optimalHeight;
            encoded.originalWidth = config_.inputWidth;
            encoded.originalHeight = config_.inputHeight;
            encoded.scalingFactor = scalingFactor;
            encoded.qualityLevel = settings.videoQuality;

            result.push_back(encoded);

            av_packet_unref(packet_);
        }

        frameCount_++;
        stats_.encodedFrames++;

        auto endTime = std::chrono::high_resolution_clock::now();
        stats_.encodingTimeMs = std::chrono::duration<double, std::milli>(
            endTime - startTime).count();

#else
        // �������� ��� FFmpeg
        return EncodeFrameStub(rawFrame, settings);
#endif

        return result;
    }

    std::vector<uint8_t> DecodeFrame(const AdvancedVideoCodec::EncodedFrame& encodedFrame) {
#ifdef USE_FFMPEG
        // ���������� �������������
        return encodedFrame.data;
#else
        return DecodeFrameStub(encodedFrame.data);
#endif
    }

    bool ScaleFrame(
        const std::vector<uint8_t>& inputFrame,
        std::vector<uint8_t>& outputFrame,
        int inputWidth, int inputHeight,
        int outputWidth, int outputHeight,
        AdvancedVideoCodec::ScalingMode mode
    ) {
        if (inputWidth == outputWidth && inputHeight == outputHeight) {
            outputFrame = inputFrame;
            return true;
        }

#ifdef USE_FFMPEG
        if (!swsContext_) {
            swsContext_ = sws_getContext(
                inputWidth, inputHeight, AV_PIX_FMT_BGRA,
                outputWidth, outputHeight, AV_PIX_FMT_BGRA,
                GetScalingFlags(mode), nullptr, nullptr, nullptr);

            if (!swsContext_) {
                return false;
            }
        }

        const uint8_t* srcSlice[] = { inputFrame.data() };
        int srcStride[] = { inputWidth * 4 };

        uint8_t* dstPlanes[] = { outputFrame.data() };
        int dstStride[] = { outputWidth * 4 };

        sws_scale(swsContext_, srcSlice, srcStride, 0, inputHeight,
            dstPlanes, dstStride);

        return true;
#else
        // ������� ��������������� ��� FFmpeg
        return SimpleScale(inputFrame, outputFrame, inputWidth, inputHeight,
            outputWidth, outputHeight);
#endif
    }

    AdvancedVideoCodec::Statistics GetStatistics() const {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        double elapsedSeconds = (now - startTime_) / 1000.0;

        AdvancedVideoCodec::Statistics stats = stats_;
        stats.currentFPS = stats_.encodedFrames / elapsedSeconds;
        stats.averageBitrate = (stats_.encodedFrames * config_.outputWidth *
            config_.outputHeight * 4) / elapsedSeconds;

        return stats;
    }

    void SetTargetBitrate(int bitrate) {
        config_.targetBitrate = (config_.minBitrate,
            min(config_.maxBitrate, bitrate));
#ifdef USE_FFMPEG
        if (codecContext_) {
            codecContext_->bit_rate = config_.targetBitrate;
        }
#endif
    }

    void SetFPS(int fps) {
        config_.fps = fps;
#ifdef USE_FFMPEG
        if (codecContext_) {
            codecContext_->time_base = AVRational{ 1, fps };
            codecContext_->framerate = AVRational{ fps, 1 };
        }
#endif
    }

    void SetQuality(int quality) {
        config_.quality = quality;
        if (std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count() -
            lastQualityChange_ > 2) { // �� ���� ��� ��� � 2 �������

#ifdef USE_FFMPEG
            if (codecContext_) {
                char qualityStr[32];
                snprintf(qualityStr, sizeof(qualityStr), "%d", quality);
                av_opt_set(codecContext_->priv_data, "crf", qualityStr, 0);
            }
#endif
            lastQualityChange_ = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        }
    }

    void SetScalingMode(AdvancedVideoCodec::ScalingMode mode) {
        config_.scalingMode = mode;
        // ������� �������� ��������������� ��� ������������
        if (swsContext_) {
            #ifdef USE_FFMPEG
            sws_freeContext(swsContext_);
            #endif
            swsContext_ = nullptr;
        }
    }

    void ForceKeyFrame() {
#ifdef USE_FFMPEG
        if (codecContext_) {
            frame_->pict_type = AV_PICTURE_TYPE_I;
        }
#endif
    }

    void AdaptToNetworkConditions(double networkQuality, int availableBandwidth) {
        if (!config_.enableAdaptiveQuality) return;

        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        // ���������� ������ � ������� �� ������ ���������
        if (now - lastQualityChange_ < 1000) return; // �� ���� 1 ���� � �������

        if (networkQuality > 0.8 && availableBandwidth > 40000000) {
            // �������� ������� - ����� �������� ��������
            if (consecutiveQualityIncreases_ < 3) {
                SetQuality(min(10, config_.quality + 1));
                consecutiveQualityIncreases_++;
            }
        }
        else if (networkQuality < 0.4 || availableBandwidth < 25000000) {
            // ������ ������� - ����� ������� ��������
            SetQuality(max(1, config_.quality - 1));
            consecutiveQualityIncreases_ = 0;
        }

        lastQualityChange_ = now;
    }

    void AdaptToPerformance(double cpuUsage, double memoryUsage) {
        if (cpuUsage > 80.0 || memoryUsage > 80.0) {
            // ������� �������� - ������� ��������
            SetQuality(max(1, config_.quality - 1));
        }
    }

private:
    void Cleanup() {
#ifdef USE_FFMPEG
        if (swsContext_) sws_freeContext(swsContext_);
        if (codecContext_) avcodec_free_context(&codecContext_);
        if (frame_) {
            av_frame_free(&frame_);
        }
        if (packet_) {
            av_packet_free(&packet_);
        }
#endif
    }

#ifdef USE_FFMPEG
    int GetScalingFlags(ScalingMode mode) {
        switch (mode) {
        case ScalingMode::BILINEAR: return SWS_BILINEAR;
        case ScalingMode::BICUBIC: return SWS_BICUBIC;
        case ScalingMode::LANCZOS: return SWS_LANCZOS;
        case ScalingMode::DYNAMIC: return SWS_BILINEAR; // ������� �� ���������
        default: return SWS_BILINEAR;
        }
    }

    bool ConvertRGBToYUV(const std::vector<uint8_t>& rgbData, int width, int height) {
        if (!swsContext_) {
            swsContext_ = sws_getContext(
                width, height, AV_PIX_FMT_BGRA,
                width, height, AV_PIX_FMT_YUV420P,
                SWS_BILINEAR, nullptr, nullptr, nullptr);

            if (!swsContext_) {
                return false;
            }
        }

        const uint8_t* srcSlice[] = { rgbData.data() };
        int srcStride[] = { width * 4 };

        uint8_t* dstPlanes[] = {
            frame_->data[0], frame_->data[1], frame_->data[2]
        };
        int dstStride[] = {
            frame_->linesize[0], frame_->linesize[1], frame_->linesize[2]
        };

        sws_scale(swsContext_, srcSlice, srcStride, 0, height, dstPlanes, dstStride);

        return true;
    }
#endif

    std::vector<AdvancedVideoCodec::EncodedFrame> EncodeFrameStub(
        const std::vector<uint8_t>& rawFrame,
        const OptimizedSettings& settings
    ) {
        std::vector<AdvancedVideoCodec::EncodedFrame> result;

        // ������� ���������� ��� ������������ ��� FFmpeg
        int optimalWidth = config_.outputWidth;
        int optimalHeight = config_.outputHeight;
        float scalingFactor = 1.0f;

        if (settings.enableVideoScaling) {
            settings.GetOptimalSize(optimalWidth, optimalHeight);
            scalingFactor = static_cast<float>(optimalWidth) / config_.inputWidth;
        }

        // ������������ ���� ��� �������������
        std::vector<uint8_t> scaledFrame;
        if (optimalWidth != config_.inputWidth || optimalHeight != config_.inputHeight) {
            auto scaleStart = std::chrono::high_resolution_clock::now();

            scaledFrame.resize(optimalWidth * optimalHeight * 4);
            SimpleScale(rawFrame, scaledFrame, config_.inputWidth, config_.inputHeight,
                optimalWidth, optimalHeight);

            auto scaleEnd = std::chrono::high_resolution_clock::now();
            stats_.scalingTimeMs = std::chrono::duration<double, std::milli>(
                scaleEnd - scaleStart).count();
            stats_.scaledFrames++;
        }
        else {
            scaledFrame = rawFrame;
        }

        // ������� ������ �� ������ ��������
        float compressionLevel = settings.GetCompressionLevel();
        std::vector<uint8_t> compressed = SimpleCompress(scaledFrame, compressionLevel);

        AdvancedVideoCodec::EncodedFrame encoded;
        encoded.data = compressed;
        encoded.timestamp = frameCount_;
        encoded.isKeyFrame = (frameCount_ % (config_.fps * 2) == 0); // �������� ���� ������ 2 �������
        encoded.width = optimalWidth;
        encoded.height = optimalHeight;
        encoded.originalWidth = config_.inputWidth;
        encoded.originalHeight = config_.inputHeight;
        encoded.scalingFactor = scalingFactor;
        encoded.qualityLevel = settings.videoQuality;

        result.push_back(encoded);

        frameCount_++;
        stats_.encodedFrames++;

        auto endTime = std::chrono::high_resolution_clock::now();
        stats_.encodingTimeMs = std::chrono::duration<double, std::milli>(
            endTime - std::chrono::high_resolution_clock::now()).count();

        return result;
    }

    std::vector<uint8_t> DecodeFrameStub(const std::vector<uint8_t>& encodedData) {
        return SimpleDecompress(encodedData);
    }

    bool SimpleScale(
        const std::vector<uint8_t>& inputFrame,
        std::vector<uint8_t>& outputFrame,
        int inputWidth, int inputHeight,
        int outputWidth, int outputHeight
    ) {
        // ������� ���������� ���������������
        float xRatio = static_cast<float>(inputWidth) / outputWidth;
        float yRatio = static_cast<float>(inputHeight) / outputHeight;

        for (int y = 0; y < outputHeight; y++) {
            for (int x = 0; x < outputWidth; x++) {
                int srcX = static_cast<int>(x * xRatio);
                int srcY = static_cast<int>(y * yRatio);

                int srcIndex = (srcY * inputWidth + srcX) * 4;
                int dstIndex = (y * outputWidth + x) * 4;

                if (srcIndex + 3 < inputFrame.size() && dstIndex + 3 < outputFrame.size()) {
                    outputFrame[dstIndex] = inputFrame[srcIndex];     // B
                    outputFrame[dstIndex + 1] = inputFrame[srcIndex + 1]; // G
                    outputFrame[dstIndex + 2] = inputFrame[srcIndex + 2]; // R
                    outputFrame[dstIndex + 3] = inputFrame[srcIndex + 3]; // A
                }
            }
        }

        return true;
    }

    std::vector<uint8_t> SimpleCompress(
        const std::vector<uint8_t>& data,
        float compressionLevel
    ) {
        // ������� ������ - ����� ������ n-� ������� � ����������� �� ������ ������
        int step = static_cast<int>(1.0f / compressionLevel);
        step = max(1, min(16, step));

        std::vector<uint8_t> compressed;
        compressed.reserve(data.size() / step);

        for (size_t i = 0; i < data.size(); i += step * 4) {
            if (i + 4 <= data.size()) {
                compressed.insert(compressed.end(), data.begin() + i, data.begin() + i + 4);
            }
        }

        return compressed;
    }

    std::vector<uint8_t> SimpleDecompress(const std::vector<uint8_t>& compressed) {
        // ������� ������������ - ��������� �������
        std::vector<uint8_t> decompressed;
        decompressed.reserve(compressed.size() * 4); // ��������� ��������������

        for (size_t i = 0; i < compressed.size(); i += 4) {
            if (i + 4 <= compressed.size()) {
                // ��������� ������� ��� ����������
                for (int j = 0; j < 4; j++) {
                    decompressed.insert(decompressed.end(),
                        compressed.begin() + i, compressed.begin() + i + 4);
                }
            }
        }

        return decompressed;
    }

    AdvancedVideoCodec::VideoConfig config_;
    AdvancedVideoCodec::Statistics stats_;

#ifdef USE_FFMPEG
    AVCodecContext* codecContext_;
    AVFrame* frame_;
    AVPacket* packet_;
    struct SwsContext* swsContext_; // �������� struct
#endif

    int64_t frameCount_;
    int64_t startTime_;
    int64_t lastQualityChange_;
    int consecutiveQualityIncreases_;

}; // End of AdvancedVideoCodec::Impl class

// AdvancedVideoCodec implementation
AdvancedVideoCodec::AdvancedVideoCodec() : pImpl(std::make_unique<Impl>()) {}
AdvancedVideoCodec::~AdvancedVideoCodec() = default;

bool AdvancedVideoCodec::Initialize(const VideoConfig& config) {
    return pImpl->Initialize(config);
}

void AdvancedVideoCodec::EncodeFrame(
    const std::vector<uint8_t>& rawFrame,
    const OptimizedSettings& settings
) {
    // Internal method returns vector but public API doesn't expose it
    // Call internal implementation directly if needed for return value
    if (pImpl) {
        // Get the encoded frames from implementation
        pImpl->EncodeFrame(rawFrame, settings);
    }
}

std::vector<uint8_t> AdvancedVideoCodec::DecodeFrame(const EncodedFrame& encodedFrame) {
    return pImpl->DecodeFrame(encodedFrame);
}

bool AdvancedVideoCodec::ScaleFrame(
    const std::vector<uint8_t>& inputFrame,
    std::vector<uint8_t>& outputFrame,
    int inputWidth, int inputHeight,
    int outputWidth, int outputHeight,
    ScalingMode mode
) {
    return pImpl->ScaleFrame(inputFrame, outputFrame, inputWidth, inputHeight,
        outputWidth, outputHeight, mode);
}

AdvancedVideoCodec::Statistics AdvancedVideoCodec::GetStatistics() const {
    return pImpl->GetStatistics();
}

void AdvancedVideoCodec::SetTargetBitrate(int bitrate) {
    pImpl->SetTargetBitrate(bitrate);
}

void AdvancedVideoCodec::SetFPS(int fps) {
    pImpl->SetFPS(fps);
}

void AdvancedVideoCodec::SetQuality(int quality) {
    pImpl->SetQuality(quality);
}

void AdvancedVideoCodec::SetScalingMode(ScalingMode mode) {
    pImpl->SetScalingMode(mode);
}

void AdvancedVideoCodec::ForceKeyFrame() {
    pImpl->ForceKeyFrame();
}

void AdvancedVideoCodec::AdaptToNetworkConditions(double networkQuality, int availableBandwidth) {
    pImpl->AdaptToNetworkConditions(networkQuality, availableBandwidth);
}

void AdvancedVideoCodec::AdaptToPerformance(double cpuUsage, double memoryUsage) {
    pImpl->AdaptToPerformance(cpuUsage, memoryUsage);
}

// ���������� AsyncAdvancedVideoProcessor
AsyncAdvancedVideoProcessor::AsyncAdvancedVideoProcessor() : running_(false) {}

AsyncAdvancedVideoProcessor::~AsyncAdvancedVideoProcessor() {
    Stop();
}

void AsyncAdvancedVideoProcessor::Start() {
    if (!running_) {
        running_ = true;
        codec_ = std::make_unique<AdvancedVideoCodec>();
        codec_->Initialize(config_);
        processingThread_ = std::thread(&AsyncAdvancedVideoProcessor::ProcessingLoop, this);
    }
}

void AsyncAdvancedVideoProcessor::Stop() {
    if (running_) {
        running_ = false;
        inputCV_.notify_all();
        outputCV_.notify_all();

        if (processingThread_.joinable()) {
            processingThread_.join();
        }

        codec_.reset();
    }
}

void AsyncAdvancedVideoProcessor::SubmitFrame(
    const std::vector<uint8_t>& rawFrame,
    const OptimizedSettings& settings
) {
    std::unique_lock<std::mutex> lock(inputMutex_);

    if (inputBuffer_.size() >= MAX_INPUT_BUFFER) {
        // ������� ������ ����� ��� �������������� ������������
        inputBuffer_.pop();
    }

    InputFrame frame{};
    frame.data = rawFrame;
    frame.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    // Copy atomic settings one by one since assignment is deleted
    frame.settings.enableVideoScaling.store(settings.enableVideoScaling.load());
    frame.settings.enableAdvancedCompression.store(settings.enableAdvancedCompression.load());
    frame.settings.enableAdaptiveBitrate.store(settings.enableAdaptiveBitrate.load());
    frame.settings.enableFrameSkipping.store(settings.enableFrameSkipping.load());

    inputBuffer_.push(std::move(frame));
    lock.unlock();

    inputCV_.notify_one();
}

bool AsyncAdvancedVideoProcessor::GetEncodedFrame(AdvancedVideoCodec::EncodedFrame& frame) {
    std::unique_lock<std::mutex> lock(outputMutex_);

    if (outputBuffer_.empty()) {
        return false;
    }

    frame = std::move(outputBuffer_.front());
    outputBuffer_.pop();

    return true;
}

void AsyncAdvancedVideoProcessor::SetConfig(const AdvancedVideoCodec::VideoConfig& config) {
    config_ = config;
    if (codec_) {
        codec_->SetTargetBitrate(config.targetBitrate);
        codec_->SetFPS(config.fps);
        codec_->SetQuality(config.quality);
    }
}

AdvancedVideoCodec::Statistics AsyncAdvancedVideoProcessor::GetStatistics() const {
    if (codec_) {
        return codec_->GetStatistics();
    }
    return {};
}

void AsyncAdvancedVideoProcessor::ProcessingLoop() {
    while (running_) {
        std::vector<uint8_t> frameData;
        OptimizedSettings frameSettings;
        int64_t frameTimestamp = 0;
        bool hasFrame = false;

        {
            std::unique_lock<std::mutex> lock(inputMutex_);
            inputCV_.wait(lock, [this] { return !inputBuffer_.empty() || !running_; });

            if (!running_) break;

            if (!inputBuffer_.empty()) {
                auto& queuedFrame = inputBuffer_.front();
                frameData = std::move(queuedFrame.data);
                frameTimestamp = queuedFrame.timestamp;
                
                // Copy OptimizedSettings members one by one
                frameSettings.enableVideoScaling.store(queuedFrame.settings.enableVideoScaling.load());
                frameSettings.enableAdvancedCompression.store(queuedFrame.settings.enableAdvancedCompression.load());
                frameSettings.enableAdaptiveBitrate.store(queuedFrame.settings.enableAdaptiveBitrate.load());
                frameSettings.enableFrameSkipping.store(queuedFrame.settings.enableFrameSkipping.load());
                frameSettings.enableOpusOptimization.store(queuedFrame.settings.enableOpusOptimization.load());
                frameSettings.enableEchoCancellation.store(queuedFrame.settings.enableEchoCancellation.load());
                frameSettings.enableNetworkOptimization.store(queuedFrame.settings.enableNetworkOptimization.load());
                frameSettings.enableCongestionControl.store(queuedFrame.settings.enableCongestionControl.load());
                frameSettings.targetBitrate = queuedFrame.settings.targetBitrate;
                frameSettings.maxBitrate = queuedFrame.settings.maxBitrate;
                frameSettings.minBitrate = queuedFrame.settings.minBitrate;
                frameSettings.videoQuality = queuedFrame.settings.videoQuality;
                frameSettings.audioQuality = queuedFrame.settings.audioQuality;
                frameSettings.useDynamicScaling = queuedFrame.settings.useDynamicScaling;
                frameSettings.scalingFactor = queuedFrame.settings.scalingFactor;
                
                inputBuffer_.pop();
                hasFrame = true;
            }
        }

        // Process frame
        auto encodedFrames = std::vector<AdvancedVideoCodec::EncodedFrame>();
        if (codec_ && hasFrame) {
            codec_->EncodeFrame(frameData, frameSettings);
            // Create a simple placeholder frame since EncodeFrame returns void
            AdvancedVideoCodec::EncodedFrame frame;
            frame.data = frameData;
            frame.timestamp = frameTimestamp;
            encodedFrames.push_back(frame);
        }

        // Submit to output buffer
        {
            std::unique_lock<std::mutex> lock(outputMutex_);

            if (outputBuffer_.size() >= MAX_OUTPUT_BUFFER) {
                // Drop oldest frame
                outputBuffer_.pop();
            }

            for (auto& frame : encodedFrames) {
                outputBuffer_.push(std::move(frame));
            }
        }

        outputCV_.notify_all();
    }
}

// ���������� AdvancedVideoStreamManager
AdvancedVideoStreamManager::AdvancedVideoStreamManager() : isRunning_(false), socket_(INVALID_SOCKET) {}

AdvancedVideoStreamManager::~AdvancedVideoStreamManager() {
    Stop();
}

bool AdvancedVideoStreamManager::Start(
    const std::string& targetIP,
    int port,
    const OptimizedSettings& settings
) {
    if (isRunning_) {
        return false;
    }

    // ������� �����
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == INVALID_SOCKET) {
        return false;
    }

    // ��������� ������
    int bufferSize = 2 * 1024 * 1024; // 2MB ������
    setsockopt(socket_, SOL_SOCKET, SO_SNDBUF, (char*)&bufferSize, sizeof(bufferSize));
    setsockopt(socket_, SOL_SOCKET, SO_RCVBUF, (char*)&bufferSize, sizeof(bufferSize));

    // ������������� �����
    u_long nonBlocking = 1;
    ioctlsocket(socket_, FIONBIO, &nonBlocking);

    // ����������� �����
    targetAddr_.sin_family = AF_INET;
    targetAddr_.sin_port = htons(port);
    
    // Use InetPton instead of deprecated inet_addr
    if (InetPtonA(AF_INET, targetIP.c_str(), &targetAddr_.sin_addr) <= 0) {
        targetAddr_.sin_addr.s_addr = INADDR_ANY;
    }

    // �������������� ���������
    AdvancedVideoCodec::VideoConfig config;
    config.inputWidth = 1920;
    config.inputHeight = 1080;
    config.outputWidth = 1920;
    config.outputHeight = 1080;
    config.fps = 60;
    config.targetBitrate = settings.targetBitrate;
    config.maxBitrate = settings.maxBitrate;
    config.minBitrate = settings.minBitrate;
    config.scalingMode = AdvancedVideoCodec::ScalingMode::DYNAMIC;
    config.enableAdaptiveQuality = settings.enableAdaptiveBitrate;
    config.quality = settings.videoQuality;

    processor_ = std::make_unique<AsyncAdvancedVideoProcessor>();
    processor_->SetConfig(config);
    processor_->Start();

    // Copy settings member by member due to deleted assignment operator
    currentSettings_.enableVideoScaling.store(settings.enableVideoScaling.load());
    currentSettings_.enableAdvancedCompression.store(settings.enableAdvancedCompression.load());
    currentSettings_.enableAdaptiveBitrate.store(settings.enableAdaptiveBitrate.load());
    currentSettings_.enableFrameSkipping.store(settings.enableFrameSkipping.load());
    currentSettings_.enableOpusOptimization.store(settings.enableOpusOptimization.load());
    currentSettings_.enableEchoCancellation.store(settings.enableEchoCancellation.load());
    currentSettings_.enableNetworkOptimization.store(settings.enableNetworkOptimization.load());
    currentSettings_.enableCongestionControl.store(settings.enableCongestionControl.load());
    currentSettings_.targetBitrate = settings.targetBitrate;
    currentSettings_.maxBitrate = settings.maxBitrate;
    currentSettings_.minBitrate = settings.minBitrate;
    currentSettings_.videoQuality = settings.videoQuality;
    currentSettings_.scalingFactor = settings.scalingFactor;
    
    isRunning_ = true;

    // ��������� ����� ��������
    streamThread_ = std::thread(&AdvancedVideoStreamManager::StreamLoop, this);

    return true;
}

void AdvancedVideoStreamManager::Stop() {
    if (isRunning_) {
        isRunning_ = false;

        if (streamThread_.joinable()) {
            streamThread_.join();
        }

        processor_->Stop();
        processor_.reset();

        if (socket_ != INVALID_SOCKET) {
            closesocket(socket_);
            socket_ = INVALID_SOCKET;
        }
    }
}

void AdvancedVideoStreamManager::SubmitRawFrame(const std::vector<uint8_t>& frame) {
    if (isRunning_ && processor_) {
        processor_->SubmitFrame(frame, currentSettings_);
    }
}

bool AdvancedVideoStreamManager::GetEncodedFrame(AdvancedVideoCodec::EncodedFrame& frame) {
    if (processor_) {
        return processor_->GetEncodedFrame(frame);
    }
    return false;
}

void AdvancedVideoStreamManager::UpdateSettings(const OptimizedSettings& settings) {
    // Copy settings member by member due to deleted assignment operator
    currentSettings_.enableVideoScaling.store(settings.enableVideoScaling.load());
    currentSettings_.enableAdvancedCompression.store(settings.enableAdvancedCompression.load());
    currentSettings_.enableAdaptiveBitrate.store(settings.enableAdaptiveBitrate.load());
    currentSettings_.enableFrameSkipping.store(settings.enableFrameSkipping.load());
    currentSettings_.enableOpusOptimization.store(settings.enableOpusOptimization.load());
    currentSettings_.enableEchoCancellation.store(settings.enableEchoCancellation.load());
    currentSettings_.enableNetworkOptimization.store(settings.enableNetworkOptimization.load());
    currentSettings_.enableCongestionControl.store(settings.enableCongestionControl.load());
    currentSettings_.targetBitrate = settings.targetBitrate;
    currentSettings_.maxBitrate = settings.maxBitrate;
    currentSettings_.minBitrate = settings.minBitrate;
    currentSettings_.videoQuality = settings.videoQuality;
    currentSettings_.scalingFactor = settings.scalingFactor;
}

AdvancedVideoCodec::Statistics AdvancedVideoStreamManager::GetStatistics() const {
    if (processor_) {
        return processor_->GetStatistics();
    }
    return {};
}

void AdvancedVideoStreamManager::StreamLoop() {
    while (isRunning_) {
        AdvancedVideoCodec::EncodedFrame encodedFrame;

        if (GetEncodedFrame(encodedFrame)) {
            // ���������� ���� �� ����
            if (socket_ != INVALID_SOCKET) {
                // ��������� ����� � ����������
                struct VideoPacketHeader {
                    uint32_t signature;
                    uint32_t timestamp;
                    uint16_t originalWidth;
                    uint16_t originalHeight;
                    uint16_t encodedWidth;
                    uint16_t encodedHeight;
                    uint8_t isKeyFrame;
                    uint8_t qualityLevel;
                    float scalingFactor;
                    uint8_t reserved[3];
                } header;

                header.signature = 0x56494445; // "VIDE"
                header.timestamp = static_cast<uint32_t>(encodedFrame.timestamp);
                header.originalWidth = static_cast<uint16_t>(encodedFrame.originalWidth);
                header.originalHeight = static_cast<uint16_t>(encodedFrame.originalHeight);
                header.encodedWidth = static_cast<uint16_t>(encodedFrame.width);
                header.encodedHeight = static_cast<uint16_t>(encodedFrame.height);
                header.isKeyFrame = encodedFrame.isKeyFrame ? 1 : 0;
                header.qualityLevel = encodedFrame.qualityLevel;
                header.scalingFactor = encodedFrame.scalingFactor;
                memset(header.reserved, 0, 3);

                // ���������� ���������
                sendto(socket_, (char*)&header, sizeof(header), 0,
                    (sockaddr*)&targetAddr_, sizeof(targetAddr_));

                // ���������� ������ �����
                if (!encodedFrame.data.empty()) {
                    // ��������� �� ��������� ���� �����
                    const size_t maxFragmentSize = 1400;
                    size_t offset = 0;
                    uint16_t fragmentIndex = 0;

                    while (offset < encodedFrame.data.size()) {
                        size_t fragmentSize = min(maxFragmentSize,
                            encodedFrame.data.size() - offset);

                        struct FragmentHeader {
                            uint32_t signature;
                            uint16_t fragmentIndex;
                            uint16_t totalFragments;
                            uint32_t fragmentOffset;
                        } fragHeader;

                        fragHeader.signature = 0x46524147; // "FRAG"
                        fragHeader.fragmentIndex = fragmentIndex++;
                        fragHeader.totalFragments = static_cast<uint16_t>(
                            (encodedFrame.data.size() + maxFragmentSize - 1) / maxFragmentSize);
                        fragHeader.fragmentOffset = static_cast<uint32_t>(offset);

                        // ��������� �����
                        std::vector<uint8_t> packet;
                        packet.reserve(sizeof(fragHeader) + fragmentSize);
                        packet.insert(packet.end(),
                            reinterpret_cast<uint8_t*>(&fragHeader),
                            reinterpret_cast<uint8_t*>(&fragHeader) + sizeof(fragHeader));
                        packet.insert(packet.end(),
                            encodedFrame.data.begin() + offset,
                            encodedFrame.data.begin() + offset + fragmentSize);

                        int sent = sendto(socket_, (char*)packet.data(), packet.size(), 0,
                            (sockaddr*)&targetAddr_, sizeof(targetAddr_));

                        if (sent > 0) {
                            stats_.bytesSent += sent;
                            stats_.packetsSent++;
                        }
                        else {
                            stats_.retransmissions++;
                        }

                        offset += fragmentSize;
                    }
                }

                stats_.framesSent++;
            }
        }

        // ��������� ����� ��� �������������� �������� �������� �� CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}


#pragma once
#include <vector>
#include <memory>
#include "modules/audio_processing/include/audio_processing.h"

class WebRtcAudioProcessor {
public:
    WebRtcAudioProcessor();
    ~WebRtcAudioProcessor();

    bool Initialize();  // 48kHz, mono, 10ms frames

    // Обработка перед отправкой (микрофон → сеть)
    std::vector<int16_t> ProcessCapture(const std::vector<int16_t>& input);

    // Обработка после приёма (сеть → динамики)
    std::vector<int16_t> ProcessRender(const std::vector<int16_t>& input);

    void SetAgcGain(int gain_dB);   // от 0 до 30 (рекомендую 12-18)

private:
    std::unique_ptr<webrtc::AudioProcessing> apm_;
    webrtc::StreamConfig stream_config_;
};

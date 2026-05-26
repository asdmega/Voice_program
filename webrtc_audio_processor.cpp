#include "webrtc_audio_processor.h"
#include <iostream>

WebRtcAudioProcessor::WebRtcAudioProcessor() {
    webrtc::AudioProcessing::Config config;
    config.echo_canceller.enabled = true;
    config.echo_canceller.mobile_mode = false;
    config.noise_suppression.enabled = true;
    config.noise_suppression.level = webrtc::NoiseSuppression::kHigh;
    config.gain_controller1.enabled = true;
    config.gain_controller1.mode = webrtc::GainControl::kAdaptiveDigital;
    config.gain_controller2.enabled = true;
    config.gain_controller2.fixed_gain_db = 18;        // ? основное усиление
    config.voice_detection.enabled = true;
    config.high_pass_filter.enabled = true;

    apm_ = webrtc::AudioProcessing::Create(config);
    stream_config_ = webrtc::StreamConfig(48000, 1);  // 48kHz mono
}

WebRtcAudioProcessor::~WebRtcAudioProcessor() = default;

bool WebRtcAudioProcessor::Initialize() {
    return apm_ != nullptr;
}

std::vector<int16_t> WebRtcAudioProcessor::ProcessCapture(const std::vector<int16_t>& input) {
    if (!apm_ || input.empty()) return input;

    webrtc::AudioFrame frame;
    frame.UpdateFrame(0, input.data(), input.size(), 48000, webrtc::AudioFrame::kMono, webrtc::AudioFrame::kVadActive);

    if (apm_->ProcessStream(&frame) == webrtc::AudioProcessing::kNoError) {
        std::vector<int16_t> output(input.size());
        memcpy(output.data(), frame.data(), input.size() * sizeof(int16_t));
        return output;
    }
    return input;
}

std::vector<int16_t> WebRtcAudioProcessor::ProcessRender(const std::vector<int16_t>& input) {
    if (!apm_ || input.empty()) return input;

    webrtc::AudioFrame frame;
    frame.UpdateFrame(0, input.data(), input.size(), 48000, webrtc::AudioFrame::kMono, webrtc::AudioFrame::kVadActive);

    if (apm_->ProcessReverseStream(&frame) == webrtc::AudioProcessing::kNoError) {
        std::vector<int16_t> output(input.size());
        memcpy(output.data(), frame.data(), input.size() * sizeof(int16_t));
        return output;
    }
    return input;
}

void WebRtcAudioProcessor::SetAgcGain(int gain_dB) {
    if (apm_) {
        apm_->gain_controller2()->set_fixed_gain_db(gain_dB);
    }
}

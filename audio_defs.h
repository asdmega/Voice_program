#pragma once
constexpr int SAMPLE_RATE = 48000;
constexpr int NUM_CHANNELS = 2;
constexpr int BITS_PER_SAMPLE = 16;
constexpr int BUFFER_DURATION_MS = 60;
constexpr int frameSize = SAMPLE_RATE * BUFFER_DURATION_MS / 1000;
constexpr int fftSize = 2048;
constexpr int hopSize = 1024;

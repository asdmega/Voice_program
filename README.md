# VoiceChat - Secure Real-Time Audio Communication

A professional voice communication application for local networks with advanced audio processing, end-to-end encryption, and adaptive jitter buffering.

## Features

- **Low-latency audio capture & playback** using Windows Wave API (48 kHz, stereo, 16-bit)
- **Opus codec** with VBR, FEC, and DTX support
- **Secure channel** – AES-256-GCM authenticated encryption
- **Reliable transport** – packet fragmentation, checksum verification, and reassembly
- **Adaptive jitter buffer** – dynamically adjusts to network delays and packet loss
- **Advanced audio processing** (all implemented in pure C++):
  - Real-time voice activity detection (VAD) with spectral analysis
  - Adaptive spectral noise suppression
  - Automatic gain control (AGC)
  - Multiband de-esser (3 bands)
  - Lookahead limiter
  - High-pass filter & DC removal
  - Dynamic parameter adaptation based on voice level and background noise
- **Greeting protocol** – encrypted handshake for peer discovery and connection setup
- **ImGui-based UI** with device selection and real-time statistics

## Requirements

- Windows 10 / 11 (x64)
- Visual Studio 2022 (or later) with C++ development tools
- [Opus](https://opus-codec.org/) library
- [OpenSSL](https://openssl.org/) (libcrypto, libssl)
- [ImGui](https://github.com/ocornut/imgui) + DX11 backend
- DirectX 11 SDK

## Building

1. Clone the repository.
2. Install dependencies (Opus, OpenSSL) and place headers/libraries in appropriate paths.
3. Open the solution in Visual Studio.
4. Set configuration to `Release x64`.
5. Build the project.

Make sure the following libraries are linked:
```
ws2_32.lib winmm.lib d3d11.lib d3dcompiler.lib iphlpapi.lib opus.lib libcrypto.lib libssl.lib
```

## Usage

1. Run the executable.
2. Select your microphone and headphones from the dropdown menus.
3. Enter the target IP address (or wait for an incoming call).
4. Click **Connect** – the application will send a secured greeting.
5. Accept incoming calls when prompted.
6. Use **Mute** to disable microphone transmission.
7. Adjust audio processing parameters in the **Audio Processing** panel.

The debug log shows connection status, packet statistics, and audio processor state.

## Architecture Overview

- `CaptureAudio` thread reads microphone PCM → audio processing → Opus encode → fragment → encrypt → UDP send.
- `PlayAudio` thread receives UDP → decrypt → reassemble → jitter buffer → Opus decode → playback.
- `GreetingListener` / `GreetingSender` handle encrypted handshake on a separate port.
- `AdvancedAudioProcessor` integrates VAD, noise suppression, AGC, de-esser, limiter.
- `SecureChannel` provides AES-256-GCM encryption.
- `ReliableTransport` manages fragmentation, checksums, and packet reassembly.

## License

This project is for educational purposes. Use at your own risk.

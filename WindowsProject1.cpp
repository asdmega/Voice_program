// ============================================================================
// WindowsProject1.cpp - ПОЛНАЯ ВЕРСИЯ С АУДИО ОБРАБОТКОЙ
// ============================================================================

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <iostream>
#include <windows.h>
#include <winsock2.h>
#include <wincrypt.h>
#include <fstream>
#include <string>
#include <mmsystem.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <thread>
#include <atomic>
#include <vector>
#include <algorithm>
#include <map>
#include <iphlpapi.h>
#include <random>
#include <icmpapi.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include <chrono>
#include <deque>
#include <condition_variable>

// ImGui
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <shellscalingapi.h>

// Project headers
#include "optimized_settings.h"
#include "advanced_audio_codec.h"
#include "secure_channel.h"
//#include "reliable_transport.h"
#include "adaptive_bitrate.h"
#include "network_monitor.h"
#include "low_latency_buffer.h"
#include "voice_framework.h"
#include "audio_processor.h"
#include "advanced_audio_processor.h"
#include "advanced_video_codec.h"

#include "voice_detector.h"
#include <tchar.h>
#include <ws2ipdef.h>
#include <WS2tcpip.h>
#include <opus/opus.h>
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "opus.lib")
#pragma comment(lib, "libcrypto.lib")
#pragma comment(lib, "libssl.lib")


// ============================================================================
// НАСТРОЙКИ
// ============================================================================
const int PORT = 12345;
const int VIDEO_PORT = 12346;
const int GREETING_PORT = 12347;
const int CONNECTION_TIMEOUT = 30;

// Оптимальные параметры для стерео 48кГц с шифрованием
const int SAMPLE_RATE = 48000; //opus Sampling rates from 8 to 48 kHz
const int NUM_CHANNELS = 2; 
const int BITS_PER_SAMPLE = 16;
const int BUFFER_DURATION_MS = 60;  // 10ms = 960 samples; opus x>2.5 && x<60 40 щк 60

// Расчёт размера аудиоданных
const int AUDIO_DATA_SIZE = SAMPLE_RATE * BUFFER_DURATION_MS / 1000 * (BITS_PER_SAMPLE / 8) * NUM_CHANNELS;
const int ENCRYPTION_OVERHEAD = 64;
const int MAX_PACKET_SIZE = AUDIO_DATA_SIZE + ENCRYPTION_OVERHEAD;

// Приветственные сообщения
const char* GREETING_MESSAGE = "VOICE_CHAT_HELLO_2024";
const char* GREETING_RESPONSE = "VOICE_CHAT_ACCEPT2024";
const int GREETING_LENGTH = 21;

const int OPTIMIZED_VIDEO_WIDTH = 1920;
const int OPTIMIZED_VIDEO_HEIGHT = 1080;

// ============================================================================
// СТАТУСЫ ПОДКЛЮЧЕНИЯ
// ============================================================================
enum ConnectionStatus {
    STATUS_DISCONNECTED,
    STATUS_CONNECTING,
    STATUS_CONNECTED,
    STATUS_NO_RESPONSE,
    STATUS_WAITING_GREETING,
    STATUS_WAITING_RESPONSE,
    STATUS_MIC_MUTED
};

// ============================================================================
// ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ
// ============================================================================
extern OptimizedSettings gOptimizedSettings;
std::unique_ptr<AdvancedVideoStreamManager> gAdvancedVideoStream;
bool showOptimizationSettings = false;

std::mutex g_audioMutex;

std::atomic<bool> isRunning{ false };
std::atomic<bool> isConnected{ false };
std::atomic<bool> isMuted{ false };
std::atomic<bool> isSharingScreen{ false };
std::atomic<bool> isReceivingScreen{ false };
std::atomic<bool> isFullscreenVideo{ false };
std::atomic<int> connectionStatus{ STATUS_WAITING_GREETING };
std::atomic<bool> showConnectionError{ false };
std::atomic<bool> showConnectionRequest{ false };
std::string connectionErrorMsg;
std::string pendingConnectionIP;
std::mutex connectionMutex;
std::atomic<bool> isProcessingConnection{ false };
std::atomic<bool> isRequestDialogOpen{ false };
std::atomic<DWORD> lastGreetingTime{ 0 };
const DWORD MIN_GREETING_INTERVAL = 3000;
const DWORD SOCKET_TIMEOUT = 200;
std::atomic<bool> g_applicationClosing{ false };
std::mutex g_threadManagementMutex;

bool g_useOptimizedCodecs = true;
std::unique_ptr<AdvancedAudioCodec> g_optimizedAudioCodec;
std::unique_ptr<AdvancedVideoCodec> g_optimizedVideoCodec;

// === NEW: AUDIO PROCESSOR ===
std::unique_ptr<VoiceApplicationFramework> g_voiceFramework;
std::shared_ptr<AdvancedAudioProcessor> g_audioProcessor = nullptr;
std::atomic<bool> showAudioSettings{ false };
std::atomic<bool> showQualityPanel{ false };

// Настройки аудио обработки
struct AudioProcessingSettings {
    std::atomic<bool> enabled{ true };
    std::atomic<bool> noiseGateEnabled{ true };
    std::atomic<bool> agcEnabled{ true };
    std::atomic<bool> noiseSuppressionEnabled{ true };
    std::atomic<bool> deesserEnabled{ true };
    std::atomic<bool> limiterEnabled{ true };

    // === КЛЮЧЕВЫЕ: Мягкие пороги ===
    std::atomic<float> noiseGateThreshold{ -42.0f };  // Было -45, мягче
    std::atomic<float> agcTargetLevel{ -18.0f };      // Чуть громче
    std::atomic<float> noiseSuppressionLevel{ 0.5f }; // Было 0.7, мягче
    std::atomic<float> deesserThreshold{ -15.0f };   // Было -25, мягче
} g_audioSettings;

// === UI ===
float globalUIScale = 1.0f;
ImVec4 backgroundColor = ImVec4(0.08f, 0.08f, 0.10f, 1.0f);
ImVec4 textColor = ImVec4(1.00f, 1.00f, 1.00f, 1.0f);
ImVec4 interfaceColor = ImVec4(0.12f, 0.12f, 0.15f, 1.0f);
bool showSettings = false;
float fontSizeScale = 1.0f;
std::string selectedFont = "Default";

// Цветовые пресеты
struct ColorPreset {
    std::string name;
    ImVec4 background;
    ImVec4 text;
    ImVec4 interfacee;
};

std::vector<ColorPreset> colorPresets = {
    {"White", ImVec4(0.95f, 0.95f, 0.95f, 1.0f), ImVec4(0.1f, 0.1f, 0.1f, 1.0f), ImVec4(0.85f, 0.85f, 0.85f, 1.0f)},
    {"Gray", ImVec4(0.5f, 0.5f, 0.5f, 1.0f), ImVec4(0.9f, 0.9f, 0.9f, 1.0f), ImVec4(0.4f, 0.4f, 0.4f, 1.0f)},
    {"Black", ImVec4(0.08f, 0.08f, 0.10f, 1.0f), ImVec4(1.0f, 1.0f, 1.0f, 1.0f), ImVec4(0.12f, 0.12f, 0.15f, 1.0f)},
    {"Ultra Black", ImVec4(0.02f, 0.02f, 0.02f, 1.0f), ImVec4(0.7f, 0.7f, 0.7f, 1.0f), ImVec4(0.05f, 0.05f, 0.05f, 1.0f)}
};

// Сокеты
SOCKET udpSocket = INVALID_SOCKET;
SOCKET videoSocket = INVALID_SOCKET;
SOCKET greetingSocket = INVALID_SOCKET;
sockaddr_in targetAddr;

// Потоки
std::thread captureThread, playThread, networkThread, screenShareThread, videoReceiveThread, greetingListenerThread, greetingSenderThread;

// Переменные для устройств
UINT selectedMicrophoneId = (UINT)-1;
UINT selectedHeadphonesId = (UINT)-1;
std::vector<std::string> microphoneDevices;
std::vector<std::string> headphoneDevices;
std::vector<std::string> discoveredIPs;
std::vector<std::string> localIPs;
std::vector<std::string> manualIPs;
int selectedMicrophoneIndex = 0;
int selectedHeadphoneIndex = 0;

// Переменные ImGui
char ipInput[16] = "127.0.0.1";
bool showMicrophoneSelection = false;
bool showHeadphoneSelection = false;
int selectedIPIndex = 0;
bool useManualIP = false;

// Флаги для управления потоками приветствий
std::atomic<bool> stopGreetingSender{ false };
std::atomic<bool> isWaitingForResponse{ false };

// DirectX переменные
ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// Текстура для видео
ID3D11Texture2D* videoTexture = nullptr;
ID3D11ShaderResourceView* videoTextureSRV = nullptr;
int videoTextureWidth = 0;
int videoTextureHeight = 0;

// Полноэкранное окно для видео
HWND videoHwnd = nullptr;
IDXGISwapChain* videoSwapChain = nullptr;
ID3D11RenderTargetView* videoRenderTargetView = nullptr;
std::atomic<bool> videoWindowActive{ false };

// Отладочный лог
std::vector<std::string> debugLog;
bool showDebugLog = true;
std::mutex debugLogMutex;

// Определяем типы данных для специализации шаблона
struct AudioDataType {};
struct VideoDataType {};
struct GreetingDataType {};

// ============================================================================
// ПРОТОТИПЫ ФУНКЦИЙ
// ============================================================================
void AddDebugLog(const std::string& message);
bool InitializeAudioProcessor();
void ShutdownAudioProcessor();
void ProcessAudioCapture(std::vector<int16_t>& audioData);
void UpdateAudioProcessingSettings();
void RenderAudioSettingsPanel();
void RenderQualityPanel();

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void InitializeSecureStreams();
void StartConnection();
void StopConnection();
void CaptureAudio();
void PlayAudio();
void EnumerateAudioDevices();
void RenderUI();
std::string WideStringToUTF8(const std::wstring& wstr);
void DiscoverNetworkIPs();
bool IsValidIP(const std::string& ip);
void StartGreetingListener();
void StopGreetingListener();
void GreetingListener();
void SendGreeting(const std::string& targetIP);
void StartGreetingSender();
void StopGreetingSender();
void GreetingSender();
void AcceptConnection();
void RejectConnection();
bool CheckTargetReachable(const std::string& ip);
void CleanupSecureStreams();
void SafeStopAllThreads();
void SendGreetingResponse(const std::string& targetIP);

// ============================================================================
// SecureStream (оставляем для совместимости)
// ============================================================================
template<typename T>
class SecureStream {
private:
    EVP_CIPHER_CTX* encrypt_ctx = nullptr;
    EVP_CIPHER_CTX* decrypt_ctx = nullptr;
    std::vector<uint8_t> hmac_key;
    bool use_compression = false;
    SOCKET* target_socket = nullptr;
    sockaddr_in* target_addr = nullptr;

    std::vector<uint8_t> calculate_hmac(const std::vector<uint8_t>& data, const std::vector<uint8_t>& key) {
        std::vector<uint8_t> result(EVP_MAX_MD_SIZE);
        unsigned int len = 0;
        HMAC(EVP_sha256(), key.data(), key.size(), data.data(), data.size(), result.data(), &len);
        result.resize(len);
        return result;
    }

public:
    sockaddr_in* get_target_addr() const { return target_addr; }
    
    SecureStream(SOCKET* socket, sockaddr_in* addr, bool compress = false)
        : use_compression(compress), target_socket(socket), target_addr(addr) {
        encrypt_ctx = EVP_CIPHER_CTX_new();
        decrypt_ctx = EVP_CIPHER_CTX_new();

        unsigned char key[32] = { 0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,
                                 0xfe,0xdc,0xba,0x98,0x76,0x54,0x32,0x10,
                                 0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
                                 0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x00 };
        unsigned char iv[16] = { 0x12,0x34,0x56,0x78,0x9a,0xbc,0xde,0xf0,
                                0x0f,0xed,0xcb,0xa9,0x87,0x65,0x43,0x21 };

        EVP_EncryptInit_ex(encrypt_ctx, EVP_aes_256_cbc(), NULL, key, iv);
        EVP_DecryptInit_ex(decrypt_ctx, EVP_aes_256_cbc(), NULL, key, iv);
        hmac_key.resize(32, 0xAA);
    }

    ~SecureStream() {
        if (encrypt_ctx) EVP_CIPHER_CTX_free(encrypt_ctx);
        if (decrypt_ctx) EVP_CIPHER_CTX_free(decrypt_ctx);
    }

    sockaddr_in* get_target_addr() { return target_addr; }
    void set_target_addr(sockaddr_in* new_addr) { target_addr = new_addr; }

    void send_secure(const std::vector<uint8_t>& data) {
        if (!target_socket || *target_socket == INVALID_SOCKET) return;
        try {
            if (data.size() != AUDIO_DATA_SIZE) {
                AddDebugLog("[SecureStream] Warning: unexpected data size");
            }

            std::vector<uint8_t> encrypted(data.size() + EVP_MAX_BLOCK_LENGTH);
            int len = 0, final_len = 0;
            EVP_EncryptUpdate(encrypt_ctx, encrypted.data(), &len, data.data(), data.size());
            EVP_EncryptFinal_ex(encrypt_ctx, encrypted.data() + len, &final_len);
            encrypted.resize(len + final_len);

            if (encrypted.size() > MAX_PACKET_SIZE - 36) {
                AddDebugLog("[SecureStream] Warning: encrypted data too large");
            }

            auto hmac = calculate_hmac(encrypted, hmac_key);
            uint32_t data_size = htonl(encrypted.size());
            std::vector<uint8_t> packet;
            packet.reserve(sizeof(data_size) + encrypted.size() + hmac.size());
            packet.insert(packet.end(), (uint8_t*)&data_size, (uint8_t*)&data_size + sizeof(data_size));
            packet.insert(packet.end(), encrypted.begin(), encrypted.end());
            packet.insert(packet.end(), hmac.begin(), hmac.end());

            if (packet.size() > 1450) {
                AddDebugLog("[SecureStream] Warning: packet size exceeds safe MTU");
            }

            sendto(*target_socket, (const char*)packet.data(), packet.size(), 0,
                (sockaddr*)target_addr, sizeof(*target_addr));
        }
        catch (...) {
            AddDebugLog("[SecureStream] send_secure exception");
        }
    }

    std::vector<uint8_t> receive_secure(const std::vector<uint8_t>& packet) {
        if (packet.size() < sizeof(uint32_t) + 32) {
            AddDebugLog("[SecureStream] receive_secure: packet too small");
            return {};
        }

        try {
            uint32_t data_size;
            memcpy(&data_size, packet.data(), sizeof(data_size));
            data_size = ntohl(data_size);

            if (data_size > packet.size() - sizeof(uint32_t) - 32 ||
                data_size > AUDIO_DATA_SIZE + 32) {
                AddDebugLog("[SecureStream] receive_secure: invalid data size");
                return {};
            }

            std::vector<uint8_t> encrypted(packet.begin() + sizeof(uint32_t),
                packet.begin() + sizeof(uint32_t) + data_size);
            std::vector<uint8_t> hmac(packet.begin() + sizeof(uint32_t) + data_size, packet.end());

            if (calculate_hmac(encrypted, hmac_key) != hmac) {
                AddDebugLog("[SecureStream] HMAC verification failed");
                return {};
            }

            std::vector<uint8_t> decrypted(encrypted.size() + EVP_MAX_BLOCK_LENGTH);
            int len = 0, final_len = 0;
            EVP_DecryptUpdate(decrypt_ctx, decrypted.data(), &len, encrypted.data(), encrypted.size());
            EVP_DecryptFinal_ex(decrypt_ctx, decrypted.data() + len, &final_len);
            decrypted.resize(len + final_len);

            if (decrypted.size() != AUDIO_DATA_SIZE) {
                AddDebugLog("[SecureStream] Warning: decrypted size mismatch");
            }

            return decrypted;
        }
        catch (...) {
            AddDebugLog("[SecureStream] receive_secure exception");
            return {};
        }
    }
};

SecureStream<AudioDataType>* secureAudioStream = nullptr;
SecureStream<VideoDataType>* secureVideoStream = nullptr;
SecureStream<GreetingDataType>* secureGreetingStream = nullptr;

void InitializeSecureStreams() {
    if (!secureAudioStream) secureAudioStream = new SecureStream<AudioDataType>(&udpSocket, &targetAddr, true);
    if (!secureVideoStream) secureVideoStream = new SecureStream<VideoDataType>(&videoSocket, &targetAddr, true);
    if (!secureGreetingStream) secureGreetingStream = new SecureStream<GreetingDataType>(&greetingSocket, &targetAddr, false);
}

void CleanupSecureStreams() {
    delete secureAudioStream; secureAudioStream = nullptr;
    delete secureVideoStream; secureVideoStream = nullptr;
    delete secureGreetingStream; secureGreetingStream = nullptr;
}

// ============================================================================
// ОТЛАДОЧНОЕ ЛОГИРОВАНИЕ
// ============================================================================
void AddDebugLog(const std::string& message) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char timeStr[32];
    sprintf_s(timeStr, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    std::string fullMsg = timeStr + message;
    {
        std::lock_guard<std::mutex> lock(debugLogMutex);
        debugLog.push_back(fullMsg);
        if (debugLog.size() > 2000) debugLog.erase(debugLog.begin(), debugLog.begin() + 500);
    }
    std::cout << fullMsg << std::endl;
}

// ============================================================================
// АУДИО ПРОЦЕССОР (НОВОЕ)
// ============================================================================
bool InitializeAudioProcessor() {
    AddDebugLog("Initializing audio processor...");
    g_audioProcessor = std::make_unique<AdvancedAudioProcessor>();
    
    AdvancedAudioProcessor::AdvancedConfig config;
    config.sampleRate = SAMPLE_RATE;
    config.channels = NUM_CHANNELS;
    config.frameSize = SAMPLE_RATE * BUFFER_DURATION_MS / 1000;
    config.enableNoiseGate = g_audioSettings.noiseGateEnabled.load();
    config.noiseGateThreshold = g_audioSettings.noiseGateThreshold.load();
    config.noiseGateAttack = 5.0f;
    config.noiseGateRelease = 100.0f;
    config.enableAGC = g_audioSettings.agcEnabled.load();
    config.agcTargetLevel = g_audioSettings.agcTargetLevel.load();
    config.agcMaxGain = 30.0f;
    config.enableNoiseSuppression = g_audioSettings.noiseSuppressionEnabled.load();
    config.noiseSuppressionLevel = g_audioSettings.noiseSuppressionLevel.load();
    config.enableSpectralNS = true;
    config.enableDeesser = g_audioSettings.deesserEnabled.load();
    config.deesserThreshold = g_audioSettings.deesserThreshold.load();
    config.enableMultibandDeesser = true;
    config.enableLimiter = g_audioSettings.limiterEnabled.load();
    config.limiterThreshold = -3.0f;
    config.enableLookaheadLimiter = true;
    config.enableDCRemoval = true;
    config.enableHighPass = true;
    config.highPassFreq = 80.0f;
    config.enableVAD = true;
    config.enableDynamicEQ = true;
    
    if (g_audioProcessor->InitializeAdvanced(config)) {
        AddDebugLog("Audio processor initialized");
        return true;
    }
    g_audioProcessor.reset();
    return false;
}

void ShutdownAudioProcessor() {
    if (g_audioProcessor) {
        AddDebugLog("Shutting down audio processor...");
        g_audioProcessor.reset();
    }
}

void ProcessAudioCapture(std::vector<short>& audioData) {
    // Обязательно проверяйте глобальный указатель перед использованием
    if (g_audioProcessor == nullptr) {
        return;
    }

    g_audioProcessor->ProcessFrameAdvanced(audioData.data(), (int)audioData.size());
}

void UpdateAudioProcessingSettings() {
    ShutdownAudioProcessor();
    InitializeAudioProcessor();
}

// ============================================================================
// UI ПАНЕЛИ (НОВОЕ)
// ============================================================================
void RenderAudioSettingsPanel() {
    if (!showAudioSettings) return;

    ImGui::SetNextWindowSize(ImVec2(520 * globalUIScale, 720 * globalUIScale), ImGuiCond_FirstUseEver);
    bool open = showAudioSettings;   // убираем atomic проблему
    if (ImGui::Begin("Audio Processing Settings", &open)) {
        showAudioSettings = open;

        if (g_audioProcessor) {
            auto& cfg = g_audioProcessor->advConfig;
            auto& stats = g_audioProcessor->advStats;

            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.4f, 1.0f), "ДИНАМИЧЕСКАЯ СИСТЕМА (всё подстраивается само)");
            ImGui::Separator();

            // Слайдеры (теперь работают)
            ImGui::SliderFloat("Base Noise Gate (dB)", &cfg.noiseGateThreshold, -55.0f, -25.0f, "%.1f");
            ImGui::SliderFloat("Base VAD Confidence", &cfg.vadThreshold, 0.20f, 0.55f, "%.2f");
            ImGui::SliderFloat("Base AGC Target (dB)", &cfg.agcTargetLevel, -30.0f, -10.0f, "%.1f");
            ImGui::SliderFloat("Noise Suppression", &cfg.nsReductionAmount, 0.3f, 1.0f, "%.2f");

            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "ТЕКУЩИЕ ДИНАМИЧЕСКИЕ ЗНАЧЕНИЯ:");

            ImGui::Text("Noise Gate: %.1f dB", cfg.dynamicNoiseGateThreshold);
            ImGui::Text("VAD Confidence: %.2f", cfg.dynamicVADConfidence);
            ImGui::Text("AGC Target: %.1f dB", cfg.dynamicAGCTargetLevel);
            ImGui::Text("Voice Active: %s", stats.voiceActive ? "ДА (отправка)" : "НЕТ");
            ImGui::Text("Voice Confidence: %.2f", stats.voiceConfidence);
            ImGui::Text("Noise Gate Open: %s", stats.noiseGateOpen ? "ОТКРЫТ" : "ЗАКРЫТ");
        }
        else {
            ImGui::Text("Audio processor not initialized yet");
        }

        ImGui::End();
    }
}
void RenderQualityPanel() {
    if (!showQualityPanel.load()) return;
    
    ImGui::SetNextWindowSize(ImVec2(300 * globalUIScale, 250 * globalUIScale));
    bool isOpen = showQualityPanel.load();
    if (ImGui::Begin("Quality Monitor", &isOpen, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
        showQualityPanel.store(isOpen);
        
        if (g_voiceFramework) {
            auto report = g_voiceFramework->GetQualityReport();
            ImGui::Text("Network Quality");
            ImGui::Separator();
            ImGui::Text("Latency: %.1f ms", report.currentLatency);
            ImGui::Text("Packet Loss: %.2f%%", report.packetLossRate * 100.0);
            ImGui::Text("Jitter: %.1f ms", report.jitter);
            ImGui::Text("Bitrate: %d kbps", report.currentBitrate / 1000);
            ImGui::Text("Grade: %s", report.networkGrade.c_str());
            
            ImGui::Separator();
            ImGui::Text("Audio Processing");
            ImGui::Separator();
            ImGui::Text("Input Level: %.1f dB", report.inputLevel);
            ImGui::Text("Output Level: %.1f dB", report.outputLevel);
            ImGui::Text("AGC Gain: %.1f dB", report.currentGain);
            ImGui::Text("Noise Gate: %s", report.noiseGateOpen ? "OPEN" : "CLOSED");
            ImGui::Text("Voice Detected: %s", report.voiceDetected ? "YES" : "NO");
            
            ImGui::Separator();
            ImGui::Text("Recommendations:");
            ImGui::TextWrapped("%s", report.recommendations.c_str());
        }
        ImGui::End();
    }
}

// ============================================================================
// ОСТАЛЬНЫЕ ФУНКЦИИ (из оригинального файла)
// ============================================================================
void SafeStopAllThreads() {
    std::lock_guard<std::mutex> lock(g_threadManagementMutex);
    // Убираем: g_applicationClosing = true; — этот флаг теперь только для полного выхода
    isRunning = false;
    isConnected = false;
    isSharingScreen = false;
    isReceivingScreen = false;

    AddDebugLog("=== Safe stopping connection threads ===");

    // Закрываем сокеты соединения, но НЕ трогаем greetingSocket
    if (udpSocket != INVALID_SOCKET) {
        shutdown(udpSocket, SD_BOTH);
        closesocket(udpSocket);
        udpSocket = INVALID_SOCKET;
    }
    if (videoSocket != INVALID_SOCKET) {
        shutdown(videoSocket, SD_BOTH);
        closesocket(videoSocket);
        videoSocket = INVALID_SOCKET;
    }
    // greetingSocket оставляем работать!

    Sleep(300);

    auto SafeJoin = [](std::thread& th, const std::string& name) {
        if (th.joinable()) {
            try {
                AddDebugLog("Joining thread: " + name);
                th.join();
                AddDebugLog("Thread joined: " + name);
            }
            catch (const std::exception& e) {
                AddDebugLog("Error joining " + name + ": " + e.what());
                th.detach();
            }
        }
        };

    // Завершаем только потоки текущего соединения
    SafeJoin(screenShareThread, "ScreenShare");
    SafeJoin(videoReceiveThread, "VideoReceive");
    SafeJoin(captureThread, "CaptureAudio");
    SafeJoin(playThread, "PlayAudio");
    SafeJoin(networkThread, "NetworkHandler");
    // greetingSender останавливается отдельно, если нужно
    SafeJoin(greetingSenderThread, "GreetingSender");

    // НЕ трогаем greetingListenerThread – он продолжает слушать
    AddDebugLog("=== Connection threads stopped ===");
}


bool IsValidIP(const std::string& ip) {
    int parts[4];
    int count = sscanf_s(ip.c_str(), "%d.%d.%d.%d", &parts[0], &parts[1], &parts[2], &parts[3]);
    if (count != 4) return false;
    for (int i = 0; i < 4; i++) if (parts[i] < 0 || parts[i] > 255) return false;
    return inet_addr(ip.c_str()) != INADDR_NONE;
}

bool CheckTargetReachable(const std::string& ip) {
    if (!IsValidIP(ip)) return false;
    HANDLE hIcmpFile = IcmpCreateFile();
    if (hIcmpFile == INVALID_HANDLE_VALUE) return false;
    IPAddr destIp = inet_addr(ip.c_str());
    char sendData[32] = "PingTest";
    DWORD replySize = sizeof(ICMP_ECHO_REPLY) + sizeof(sendData) + 8;
    LPVOID replyBuffer = malloc(replySize);
    if (replyBuffer == nullptr) { IcmpCloseHandle(hIcmpFile); return false; }
    DWORD result = IcmpSendEcho(hIcmpFile, destIp, sendData, sizeof(sendData), nullptr, replyBuffer, replySize, 1000);
    free(replyBuffer);
    IcmpCloseHandle(hIcmpFile);
    return (result != 0);
}

void StartGreetingListener() {
    AddDebugLog("Starting greeting listener on port " + std::to_string(GREETING_PORT));
    greetingSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (greetingSocket == INVALID_SOCKET) {
        AddDebugLog("ERROR: Failed to create greeting socket");
        return;
    }
    sockaddr_in localAddr;
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = htons(GREETING_PORT);
    localAddr.sin_addr.s_addr = INADDR_ANY;
    int reuse = 1;
    setsockopt(greetingSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));
    if (bind(greetingSocket, (sockaddr*)&localAddr, sizeof(localAddr)) == SOCKET_ERROR) {
        AddDebugLog("ERROR: Failed to bind greeting socket");
        closesocket(greetingSocket);
        greetingSocket = INVALID_SOCKET;
        return;
    }
    u_long nonBlocking = 1;
    ioctlsocket(greetingSocket, FIONBIO, &nonBlocking);
    greetingListenerThread = std::thread(GreetingListener);
    AddDebugLog("Greeting listener started successfully");
}

void StopGreetingListener() {
    isRequestDialogOpen = false;
    showConnectionRequest = false;
    if (greetingSocket != INVALID_SOCKET) {
        shutdown(greetingSocket, SD_BOTH);
        Sleep(150);
        closesocket(greetingSocket);
        greetingSocket = INVALID_SOCKET;
        AddDebugLog("Greeting socket closed gracefully");
    }
    if (greetingListenerThread.joinable()) {
        try {
            AddDebugLog("Waiting for greeting listener thread...");
            greetingListenerThread.join();
            AddDebugLog("Greeting listener thread finished");
        }
        catch (...) {
            if (greetingListenerThread.joinable()) greetingListenerThread.detach();
        }
    }
    AddDebugLog("Greeting listener stopped");
}

void GreetingListener() {
    AddDebugLog("Secure greeting listener thread started");
    ULONGLONG currentTime = GetTickCount64();
    sockaddr_in fromAddr;
    int fromLen = sizeof(fromAddr);
    InitializeSecureStreams();
    const int secureBufferSize = 1024;
    std::vector<uint8_t> buffer(secureBufferSize);
    DWORD timeout = 500;
    ULONGLONG lastLogTime = 0;
    setsockopt(greetingSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

    while (greetingSocket != INVALID_SOCKET) {
        if (g_applicationClosing) break;
        sockaddr_in fromAddr;
        int fromLen = sizeof(fromAddr);
        int bytesReceived = recvfrom(greetingSocket, reinterpret_cast<char*>(buffer.data()), secureBufferSize, 0,
            (sockaddr*)&fromAddr, &fromLen);

        if (bytesReceived > 0) {
            std::string fromIP = inet_ntoa(fromAddr.sin_addr);
            std::vector<uint8_t> packet(buffer.begin(), buffer.begin() + bytesReceived);
            std::vector<uint8_t> greeting_data = secureGreetingStream->receive_secure(packet);
            AddDebugLog("Decrypted greeting size: " + std::to_string(greeting_data.size()));
            if (!greeting_data.empty() && greeting_data.size() == GREETING_LENGTH) {
                AddDebugLog("Greeting content: " + std::string(greeting_data.begin(), greeting_data.end()));
            }
            if (!greeting_data.empty() && greeting_data.size() == GREETING_LENGTH) {
                if (memcmp(greeting_data.data(), GREETING_MESSAGE, greeting_data.size()) == 0) {
                    if (currentTime - lastGreetingTime < MIN_GREETING_INTERVAL) {
                        if (currentTime - lastLogTime > 5000) {
                            AddDebugLog("Ignoring frequent greeting from " + fromIP);
                            lastLogTime = currentTime;
                        }
                        continue;
                    }
                    lastGreetingTime = currentTime;
                    if (isRequestDialogOpen.load()) {
                        AddDebugLog("Request dialog already open, ignoring greeting from " + fromIP);
                        continue;
                    }
                    AddDebugLog("Received greeting from " + fromIP);
                    if (!isRunning && !isWaitingForResponse) {
                        pendingConnectionIP = fromIP;
                        showConnectionRequest = true;
                        isRequestDialogOpen = true;
                    }
                }
                else if (memcmp(greeting_data.data(), GREETING_RESPONSE, GREETING_LENGTH) == 0) {
                    AddDebugLog("Received VALID secure greeting response from " + fromIP);
                    if (isWaitingForResponse && fromIP == std::string(ipInput)) {
                        StopGreetingSender();
                        Sleep(300);
                        StartConnection();
                    }
                }
            }
        }
        else if (bytesReceived == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error == WSAENOTSOCK || error == 10058) break;
            if (error == 10054) continue;
            if (error != WSAETIMEDOUT && error != WSAEWOULDBLOCK) break;
        }
        Sleep(50);
    }
    isRequestDialogOpen = false;
    AddDebugLog("Greeting listener thread stopped");
}

void StartGreetingSender() {
    stopGreetingSender = false;
    isWaitingForResponse = true;
    connectionStatus = STATUS_WAITING_RESPONSE;
    greetingSenderThread = std::thread(GreetingSender);
    AddDebugLog("Started greeting sender");
}

void StopGreetingSender() {
    stopGreetingSender = true;
    isWaitingForResponse = false;
    if (greetingSenderThread.joinable()) greetingSenderThread.join();
    AddDebugLog("Stopped greeting sender");
}

void GreetingSender() {
    AddDebugLog("Greeting sender thread started");
    int attempt = 0;
    const int maxAttempts = 120;
    while (!stopGreetingSender && attempt < maxAttempts) {
        SendGreeting(ipInput);
        attempt++;
        for (int i = 0; i < 50 && !stopGreetingSender; i++) Sleep(100);
    }
    if (attempt >= maxAttempts && !stopGreetingSender) {
        AddDebugLog("Greeting sender timeout - no response received");
        connectionErrorMsg = "No response from target after 10 minutes";
        showConnectionError = true;
        connectionStatus = STATUS_WAITING_GREETING;
    }
    AddDebugLog("Greeting sender thread stopped");
}

void SendGreeting(const std::string& targetIP) {
    if (greetingSocket == INVALID_SOCKET) return;
    sockaddr_in destAddr;
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(GREETING_PORT);
    destAddr.sin_addr.s_addr = inet_addr(targetIP.c_str());
    if (destAddr.sin_addr.s_addr == INADDR_NONE) {
        AddDebugLog("ERROR: Invalid target IP for greeting: " + targetIP);
        return;
    }
    InitializeSecureStreams();
    std::vector<uint8_t> greeting_data(reinterpret_cast<const uint8_t*>(GREETING_MESSAGE),
        reinterpret_cast<const uint8_t*>(GREETING_MESSAGE) + GREETING_LENGTH);
    sockaddr_in* original_addr = secureGreetingStream->get_target_addr();
    sockaddr_in temp_addr = *original_addr;
    secureGreetingStream->set_target_addr(&destAddr);
    secureGreetingStream->send_secure(greeting_data);
    secureGreetingStream->set_target_addr(&temp_addr);
    AddDebugLog("Secure greeting sent to " + targetIP);
}

void SendGreetingResponse(const std::string& targetIP) {
    if (greetingSocket == INVALID_SOCKET) return;
    sockaddr_in destAddr{};
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(GREETING_PORT);
    destAddr.sin_addr.s_addr = inet_addr(targetIP.c_str());
    if (destAddr.sin_addr.s_addr == INADDR_NONE) {
        AddDebugLog("ERROR: Invalid IP for greeting response");
        return;
    }
    InitializeSecureStreams();
    std::vector<uint8_t> response_data(reinterpret_cast<const uint8_t*>(GREETING_RESPONSE),
        reinterpret_cast<const uint8_t*>(GREETING_RESPONSE) + GREETING_LENGTH);
    sockaddr_in* original_addr = secureGreetingStream->get_target_addr();
    sockaddr_in temp_addr = *original_addr;
    secureGreetingStream->set_target_addr(&destAddr);
    secureGreetingStream->send_secure(response_data);
    secureGreetingStream->set_target_addr(&temp_addr);
    AddDebugLog("Secure greeting RESPONSE sent to " + targetIP);
}

void AcceptConnection() {
    isRequestDialogOpen = false;
    showConnectionRequest = false;
    SendGreetingResponse(pendingConnectionIP);
    Sleep(300);
    strcpy_s(ipInput, pendingConnectionIP.c_str());
    StartConnection();
}

void RejectConnection() {
    isRequestDialogOpen = false;
    showConnectionRequest = false;
    pendingConnectionIP.clear();
}

void DiscoverNetworkIPs() {
    localIPs.clear();
    ULONG bufferSize = 0;
    GetAdaptersAddresses(AF_INET, 0, nullptr, nullptr, &bufferSize);
    std::vector<uint8_t> buffer(bufferSize);
    PIP_ADAPTER_ADDRESSES adapterAddresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
    if (GetAdaptersAddresses(AF_INET, 0, nullptr, adapterAddresses, &bufferSize) == NO_ERROR) {
        for (PIP_ADAPTER_ADDRESSES adapter = adapterAddresses; adapter != nullptr; adapter = adapter->Next) {
            if (adapter->OperStatus == IfOperStatusUp) {
                for (PIP_ADAPTER_UNICAST_ADDRESS unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
                    sockaddr_in* addr = reinterpret_cast<sockaddr_in*>(unicast->Address.lpSockaddr);
                    char ipStr[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &addr->sin_addr, ipStr, INET_ADDRSTRLEN);
                    if (strcmp(ipStr, "127.0.0.1") != 0) {
                        localIPs.push_back(ipStr);
                        AddDebugLog("Found local IP: " + std::string(ipStr));
                    }
                }
            }
        }
    }
    if (localIPs.empty()) localIPs.push_back("127.0.0.1");
}

void EnumerateAudioDevices() {
    microphoneDevices.clear();
    headphoneDevices.clear();
    UINT numInDevices = waveInGetNumDevs();
    for (UINT i = 0; i < numInDevices; i++) {
        WAVEINCAPSW wic;
        if (waveInGetDevCapsW(i, &wic, sizeof(wic)) == MMSYSERR_NOERROR) {
            microphoneDevices.push_back(WideStringToUTF8(wic.szPname));
        }
    }
    UINT numOutDevices = waveOutGetNumDevs();
    for (UINT i = 0; i < numOutDevices; i++) {
        WAVEOUTCAPSW woc;
        if (waveOutGetDevCapsW(i, &woc, sizeof(woc)) == MMSYSERR_NOERROR) {
            headphoneDevices.push_back(WideStringToUTF8(woc.szPname));
        }
    }
    if (!microphoneDevices.empty()) selectedMicrophoneId = 0;
    if (!headphoneDevices.empty()) selectedHeadphonesId = 0;
}

std::string WideStringToUTF8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], size, nullptr, nullptr);
    return result;
}

// ============================================================================
// ЗАХВАТ И ВОСПРОИЗВЕДЕНИЕ АУДИО (с обработкой)
// ============================================================================
void CaptureAudio() {
    AddDebugLog("Audio capture thread started");

    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = NUM_CHANNELS;
    wfx.nSamplesPerSec = SAMPLE_RATE;
    wfx.wBitsPerSample = BITS_PER_SAMPLE;
    wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    HWAVEIN hWaveIn = nullptr;
    HANDLE hBufferEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    waveInOpen(&hWaveIn, selectedMicrophoneId, &wfx, (DWORD_PTR)hBufferEvent, 0, CALLBACK_EVENT);

    const int NUM_BUFFERS = 8;
    WAVEHDR waveHeaders[NUM_BUFFERS];
    std::vector<std::vector<int16_t>> buffers(NUM_BUFFERS, std::vector<int16_t>(AUDIO_DATA_SIZE / sizeof(int16_t)));

    for (int i = 0; i < NUM_BUFFERS; i++) {
        waveHeaders[i] = {};
        waveHeaders[i].lpData = reinterpret_cast<LPSTR>(buffers[i].data());
        waveHeaders[i].dwBufferLength = AUDIO_DATA_SIZE;
        waveInPrepareHeader(hWaveIn, &waveHeaders[i], sizeof(WAVEHDR));
        waveInAddBuffer(hWaveIn, &waveHeaders[i], sizeof(WAVEHDR));
    }

    waveInStart(hWaveIn);

    // СОСТОЯНИЕ ДЛЯ УМНОЙ ОТПРАВКИ
    int bufferIndex = 0;
    int hangoverFrames = 0;
    const int MAX_HANGOVER = 20; // Удержание канала открытым ~200мс после последнего слова
    std::deque<std::vector<int16_t>> preBuffer; // Чтобы не "съедались" первые буквы

    while (isRunning && !g_applicationClosing) {
        if (WaitForSingleObject(hBufferEvent, 50) != WAIT_OBJECT_0) continue;

        WAVEHDR* currentHeader = &waveHeaders[bufferIndex];
        if (!(currentHeader->dwFlags & WHDR_DONE)) continue;

        if (!isMuted) {
            std::vector<int16_t> audioData(buffers[bufferIndex]);

            // Обработка (VAD, Шумодав, AGC)
            ProcessAudioCapture(audioData);

            bool isVoice = false;
            float confidence = 0.0f;
            bool gateOpen = false;

            if (g_audioProcessor) {
                auto vd = g_audioProcessor->GetVoiceDetectorStats();
                auto st = g_audioProcessor->GetAdvancedStats();
                isVoice = vd.isVoice;
                confidence = vd.confidence; // ТЕПЕРЬ ОПРЕДЕЛЕНО
                gateOpen = st.noiseGateOpen;
            }

            // ЛОГИКА АКТИВАЦИИ (VAD)
            // Порог 0.45 отсекает трение микрофона, но ловит тихий голос
            bool hasRealVoice = (isVoice && confidence > 0.45f);

            if (hasRealVoice) {
                hangoverFrames = MAX_HANGOVER;
            }
            else if (hangoverFrames > 0) {
                hangoverFrames--;
            }

            // ПРИНЯТИЕ РЕШЕНИЯ ОБ ОТПРАВКЕ ПАКЕТА
            bool shouldSend = (hangoverFrames > 0);

            if (shouldSend) {
                // Если это начало фразы - досылаем пре-буфер (первые буквы)
                while (!preBuffer.empty()) {
                    auto pb = preBuffer.front();
                    if (secureAudioStream) {
                        std::vector<uint8_t> data(pb.size() * sizeof(int16_t));
                        memcpy(data.data(), pb.data(), data.size());
                        secureAudioStream->send_secure(data);
                    }
                    preBuffer.pop_front();
                }

                if (secureAudioStream) {
                    std::vector<uint8_t> data(audioData.size() * sizeof(int16_t));
                    memcpy(data.data(), audioData.data(), data.size());
                    secureAudioStream->send_secure(data);
                }
            }
            else {
                // Голоса нет — пакеты не шлем, копим пре-буфер для следующей фразы
                preBuffer.push_back(audioData);
                if (preBuffer.size() > 3) preBuffer.pop_front();
            }
        }

        waveInUnprepareHeader(hWaveIn, currentHeader, sizeof(WAVEHDR));
        currentHeader->dwFlags = 0;
        waveInPrepareHeader(hWaveIn, currentHeader, sizeof(WAVEHDR));
        waveInAddBuffer(hWaveIn, currentHeader, sizeof(WAVEHDR));
        bufferIndex = (bufferIndex + 1) % NUM_BUFFERS;
    }
    waveInStop(hWaveIn);
    waveInReset(hWaveIn);
    for (int i = 0; i < NUM_BUFFERS; i++) {
        waveInUnprepareHeader(hWaveIn, &waveHeaders[i], sizeof(WAVEHDR));
    }
    waveInClose(hWaveIn);
    CloseHandle(hBufferEvent);

    AddDebugLog("Audio capture stopped");
}

void PlayAudio() {
    AddDebugLog("Audio playback thread started");

    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = NUM_CHANNELS;
    wfx.nSamplesPerSec = SAMPLE_RATE;
    wfx.wBitsPerSample = BITS_PER_SAMPLE;
    wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    HWAVEOUT hWaveOut = nullptr;
    MMRESULT result = waveOutOpen(&hWaveOut, selectedHeadphonesId, &wfx, 0, 0, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR) {
        AddDebugLog("Failed to open headphones: " + std::to_string(result));
        return;
    }

    const int NUM_BUFFERS = 6;
    WAVEHDR waveHeaders[NUM_BUFFERS];
    std::vector<std::vector<int16_t>> buffers(NUM_BUFFERS,
        std::vector<int16_t>(AUDIO_DATA_SIZE / sizeof(int16_t)));

    for (int i = 0; i < NUM_BUFFERS; i++) {
        waveHeaders[i] = {};
        waveHeaders[i].lpData = reinterpret_cast<LPSTR>(buffers[i].data());
        waveHeaders[i].dwBufferLength = AUDIO_DATA_SIZE;
        waveOutPrepareHeader(hWaveOut, &waveHeaders[i], sizeof(WAVEHDR));
    }

    // === JITTER BUFFER ===
    std::deque<std::vector<int16_t>> jitterBuffer;
    const size_t TARGET_SIZE = 4;   // 200ms буфер
    const size_t MAX_SIZE = 10;     // Максимум 500ms
    bool primed = false;

    int bufferIndex = 0;
    int packetsReceived = 0;
    int packetsPlayed = 0;
    int underruns = 0;
    auto lastStats = std::chrono::steady_clock::now();

    AddDebugLog("Playback with jitter buffer started");

    while (isRunning && !g_applicationClosing) {
        // === ПРИЁМ: неблокирующий ===
        sockaddr_in fromAddr;
        int fromLen = sizeof(fromAddr);
        std::vector<uint8_t> recvBuffer(MAX_PACKET_SIZE);

        // Неблокирующий приём
        u_long nonBlock = 1;
        ioctlsocket(udpSocket, FIONBIO, &nonBlock);

        int recvLen = recvfrom(udpSocket, reinterpret_cast<char*>(recvBuffer.data()),
            recvBuffer.size(), 0, (sockaddr*)&fromAddr, &fromLen);

        if (recvLen > 0) {
            packetsReceived++;
            std::vector<uint8_t> packet(recvBuffer.begin(), recvBuffer.begin() + recvLen);
            std::vector<uint8_t> decrypted = secureAudioStream->receive_secure(packet);

            if (!decrypted.empty() && decrypted.size() >= 64) {  // Минимум 32 сэмпла
                std::vector<int16_t> audio(decrypted.size() / sizeof(int16_t));
                memcpy(audio.data(), decrypted.data(), decrypted.size());

                // Добавляем в jitter buffer
                jitterBuffer.push_back(audio);

                // Ограничиваем размер
                while (jitterBuffer.size() > MAX_SIZE) {
                    jitterBuffer.pop_front();
                }
            }
        }

        // === ВОСПРОИЗВЕДЕНИЕ ===
        WAVEHDR* currentHeader = &waveHeaders[bufferIndex];

        if (!(currentHeader->dwFlags & WHDR_INQUEUE)) {
            // Накопили достаточно?
            if (!primed) {
                if (jitterBuffer.size() >= TARGET_SIZE) {
                    primed = true;
                    AddDebugLog("Jitter buffer primed, size=" + std::to_string(jitterBuffer.size()));
                }
            }

            if (primed && !jitterBuffer.empty()) {
                // Воспроизводим
                memcpy(buffers[bufferIndex].data(), jitterBuffer.front().data(),
                    jitterBuffer.front().size() * sizeof(int16_t));
                currentHeader->dwBufferLength = jitterBuffer.front().size() * sizeof(int16_t);

                MMRESULT res = waveOutWrite(hWaveOut, currentHeader, sizeof(WAVEHDR));
                if (res == MMSYSERR_NOERROR) {
                    packetsPlayed++;
                }

                jitterBuffer.pop_front();
                bufferIndex = (bufferIndex + 1) % NUM_BUFFERS;
            }
            else {
                // Нет данных — underrun
                underruns++;
                primed = false;  // Снова накапливаем
            }
        }

        // === СТАТИСТИКА ===
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastStats).count() >= 3) {
            AddDebugLog(std::string("PLAYBACK: recv=") + std::to_string(packetsReceived) +
                " play=" + std::to_string(packetsPlayed) +
                " underr=" + std::to_string(underruns) +
                " jitter=" + std::to_string(jitterBuffer.size()) +
                "/" + std::to_string(TARGET_SIZE));
            packetsReceived = 0;
            packetsPlayed = 0;
            underruns = 0;
            lastStats = now;
        }

        Sleep(1);
    }

    waveOutReset(hWaveOut);
    for (int i = 0; i < NUM_BUFFERS; i++) {
        waveOutUnprepareHeader(hWaveOut, &waveHeaders[i], sizeof(WAVEHDR));
    }
    waveOutClose(hWaveOut);

    AddDebugLog("Playback stopped");
}

// ============================================================================
// ПОДКЛЮЧЕНИЕ
// ============================================================================
void StartConnection() {
    AddDebugLog("Starting connection...");
    g_applicationClosing = false;   // <-- ВАЖНО: снимаем флаг полного завершения
    isRunning = true;
    isConnected = false;
    connectionStatus = STATUS_CONNECTING;

    // Инициализация сокетов...
    udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in localAddr;
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = htons(PORT);
    localAddr.sin_addr.s_addr = INADDR_ANY;
    bind(udpSocket, (sockaddr*)&localAddr, sizeof(localAddr));

    targetAddr.sin_family = AF_INET;
    targetAddr.sin_port = htons(PORT);
    targetAddr.sin_addr.s_addr = inet_addr(ipInput);

    InitializeSecureStreams();
    InitializeAudioProcessor();

    captureThread = std::thread(CaptureAudio);
    playThread = std::thread(PlayAudio);

    connectionStatus = STATUS_CONNECTED;
    isConnected = true;
    AddDebugLog("Connection started");
}

void StopConnection() {
    AddDebugLog("Stopping connection...");
    isRunning = false;
    isConnected = false;
    connectionStatus = STATUS_DISCONNECTED;
    SafeStopAllThreads();
    ShutdownAudioProcessor();
    CleanupSecureStreams();
    AddDebugLog("Connection stopped");
}

// ============================================================================
// UI РЕНДЕРИНГ (ПОЛНЫЙ)
// ============================================================================
void RenderUI() {
    // Main menu bar
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Settings")) {
            ImGui::MenuItem("General Settings", nullptr, &showSettings);
            ImGui::MenuItem("Audio Processing", nullptr, &showAudioSettings);
            ImGui::MenuItem("Optimization Settings", nullptr, &showOptimizationSettings);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Debug Log", nullptr, &showDebugLog);
            ImGui::MenuItem("Quality Monitor", nullptr, &showQualityPanel);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    
    // Main window
    ImGui::SetNextWindowPos(ImVec2(0, ImGui::GetFrameHeight()));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y - ImGui::GetFrameHeight()));
    ImGui::Begin("MainWindow", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    
    // Connection controls
    ImGui::Text("Connection");
    ImGui::Separator();
    ImGui::InputText("IP Address", ipInput, sizeof(ipInput));
    
    if (!isConnected) {
        if (ImGui::Button("Connect", ImVec2(120 * globalUIScale, 30 * globalUIScale))) {
            StartConnection();
        }
    } else {
        if (ImGui::Button("Disconnect", ImVec2(120 * globalUIScale, 30 * globalUIScale))) {
            StopConnection();
        }
    }
    
    ImGui::SameLine();
    bool muted = isMuted.load();
    if (muted) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button(muted ? "Unmute" : "Mute", ImVec2(100 * globalUIScale, 30 * globalUIScale))) {
        isMuted.store(!muted);
    }
    if (muted) ImGui::PopStyleColor();
    
    ImGui::Separator();
    
    // Audio Processing Quick Status
    ImGui::Text("Audio Processing");
    ImGui::Separator();
    bool audioProcEnabled = g_audioSettings.enabled.load();
    if (ImGui::Checkbox("Enable Processing", &audioProcEnabled)) {
        g_audioSettings.enabled.store(audioProcEnabled);
        UpdateAudioProcessingSettings();
    }
    
    if (g_audioProcessor && g_audioSettings.enabled.load()) {
        auto stats = g_audioProcessor->GetAdvancedStats();
        ImGui::Text("Input: %.1f dB | Output: %.1f dB | Gain: %.1f dB", stats.inputLevel, stats.outputLevel, stats.currentGain);
        ImGui::Text("Noise Gate: %s | Voice: %s", stats.noiseGateOpen ? "OPEN" : "CLOSED", stats.voiceDetected ? "YES" : "NO");
    }
    
    ImGui::Separator();
    
    // Device selection
    ImGui::Text("Audio Devices");
    ImGui::Separator();
    
    if (ImGui::Button("Select Microphone", ImVec2(150 * globalUIScale, 25 * globalUIScale))) {
        showMicrophoneSelection = true;
    }
    ImGui::SameLine();
    ImGui::Text("%s", selectedMicrophoneIndex < microphoneDevices.size() ? microphoneDevices[selectedMicrophoneIndex].c_str() : "Default");
    
    if (ImGui::Button("Select Headphones", ImVec2(150 * globalUIScale, 25 * globalUIScale))) {
        showHeadphoneSelection = true;
    }
    ImGui::SameLine();
    ImGui::Text("%s", selectedHeadphoneIndex < headphoneDevices.size() ? headphoneDevices[selectedHeadphoneIndex].c_str() : "Default");
    
    ImGui::Separator();
    
    // Local IPs
    ImGui::Text("Local IPs:");
    for (const auto& ip : localIPs) {
        ImGui::Text("  %s", ip.c_str());
    }
    
    ImGui::End();
    
    // Render panels
    RenderAudioSettingsPanel();
    RenderQualityPanel();
    
    // Device selection dialogs
    if (showMicrophoneSelection) {
        ImGui::Begin("Select Microphone", &showMicrophoneSelection);
        for (int i = 0; i < microphoneDevices.size(); i++) {
            if (ImGui::Selectable(microphoneDevices[i].c_str(), selectedMicrophoneIndex == i)) {
                selectedMicrophoneIndex = i;
                selectedMicrophoneId = i;
                showMicrophoneSelection = false;
            }
        }
        ImGui::End();
    }
    
    if (showHeadphoneSelection) {
        ImGui::Begin("Select Headphones", &showHeadphoneSelection);
        for (int i = 0; i < headphoneDevices.size(); i++) {
            if (ImGui::Selectable(headphoneDevices[i].c_str(), selectedHeadphoneIndex == i)) {
                selectedHeadphoneIndex = i;
                selectedHeadphonesId = i;
                showHeadphoneSelection = false;
            }
        }
        ImGui::End();
    }
    
    // Connection request dialog
    if (showConnectionRequest) {
        ImGui::OpenPopup("Connection Request");
        if (ImGui::BeginPopupModal("Connection Request", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Incoming connection from:");
            ImGui::Text("%s", pendingConnectionIP.c_str());
            if (ImGui::Button("Accept", ImVec2(120, 30))) AcceptConnection();
            ImGui::SameLine();
            if (ImGui::Button("Reject", ImVec2(120, 30))) RejectConnection();
            ImGui::EndPopup();
        }
    }
    
    // Debug log window
    if (showDebugLog) {
        ImGui::Begin("Debug Log", &showDebugLog);
        std::lock_guard<std::mutex> lock(debugLogMutex);
        for (const auto& msg : debugLog) {
            ImGui::TextUnformatted(msg.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
        ImGui::End();
    }
}

// ============================================================================
// DIRECTX (упрощенно)
// ============================================================================
bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray,
        2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ============================================================================
// ТОЧКА ВХОДА
// ============================================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    //CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    EnumerateAudioDevices();
    DiscoverNetworkIPs();
    StartGreetingListener();

    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, _T("VoiceChat"), nullptr };
    RegisterClassEx(&wc);
    HWND hwnd = CreateWindow(wc.lpszClassName, _T("Voice Chat with Audio Processing"), WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    ImVec4 clear_color = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        RenderUI();

        ImGui::Render();
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, (float*)&clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }
    g_applicationClosing = true;
    StopConnection();
    StopGreetingListener();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);

    WSACleanup();
    CoUninitialize();
    return 0;
}

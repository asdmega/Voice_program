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
#include "adaptive_bitrate.h"
#include "network_monitor.h"
#include "low_latency_buffer.h"
#include "voice_framework.h"
#include "audio_processor.h"
#include "advanced_audio_processor.h"
#include "advanced_video_codec.h"
// После существующих глобальных переменных
#include "reliable_transport.h"

#include "voice_detector.h"
#include <tchar.h>
#include <ws2ipdef.h>
#include <WS2tcpip.h>
#include <opus/opus.h>
#include "audio_defs.h"
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
std::unique_ptr<SecureChannel> g_secureChannel;
std::mutex g_audioMutex;
std::unique_ptr<SecureChannel> g_greetingChannel;

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

    std::atomic<float> noiseGateThreshold{ -48.0f };  // изменено
    std::atomic<float> agcTargetLevel{ -16.0f };
    std::atomic<float> noiseSuppressionLevel{ 0.55f }; // изменено
    std::atomic<float> deesserThreshold{ -20.0f };     // изменено
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



std::unique_ptr<ReliableTransport> g_reliableTransport;
std::unique_ptr<AdaptiveJitterBuffer> g_adaptiveJitterBuffer;
std::unique_ptr<AdaptiveBitrateController> g_bitrateController;
std::unique_ptr<AdvancedAudioCodec> g_audioCodec;   // для кодирования/декодирования

// Очередь исходящих пакетов (потокобезопасная)
//std::queue<ReliableTransport::NetworkPacket> g_sendQueue;
std::queue<std::vector<uint8_t>> g_sendQueue;
std::mutex g_sendQueueMutex;
std::condition_variable g_sendQueueCV;
std::thread g_networkSenderThread;
std::atomic<bool> g_networkSenderRunning{ false };

// Статистика для адаптивного битрейта
std::chrono::steady_clock::time_point g_lastBitrateUpdate;

// ============================================================================
// ПРОТОТИПЫ ФУНКЦИЙ
// ============================================================================
void AddDebugLog(const std::string& message);
bool InitializeAudioProcessor();
void ShutdownAudioProcessor();
void ProcessAudioCapture(std::vector<int16_t>& audioData);
void UpdateAudioProcessingSettings();
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
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
void InitializeGreetingChannel();
void SafeStopAllThreads();
void SendGreetingResponse(const std::string& targetIP);


void InitializeGreetingChannel() {
    g_greetingChannel = std::make_unique<SecureChannel>();
    SecureChannel::ChannelConfig cfg;
    cfg.algorithm = SecureChannel::EncryptionAlgorithm::AES_256_GCM;
    cfg.preSharedKey = "VoiceApp2024SecureLocalNetwork!!"; // Используем тот же ключ, что и для аудио
    if (!g_greetingChannel->Initialize(cfg)) {
        AddDebugLog("ERROR: Failed to initialize greeting secure channel");
    }
    else {
        AddDebugLog("Greeting secure channel initialized (AES-256-GCM)");
    }
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
    config.enableNoiseGate = g_audioSettings.noiseGateEnabled.load();
    config.noiseGateThreshold = g_audioSettings.noiseGateThreshold.load();
    config.noiseGateAttack = 5.0f;
    config.noiseGateRelease = 250.0f;
    config.enableAGC = g_audioSettings.agcEnabled.load();
    config.agcTargetLevel = g_audioSettings.agcTargetLevel.load();
    config.agcMaxGain = 100.0f;
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
    // Инициализируем SecureChannel для greeting (GCM)
    InitializeGreetingChannel();
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
    ULONGLONG lastLogTime = 0;   // <-- ДОБАВИТЬ ЭТУ СТРОКУ
    const int secureBufferSize = 1024;
    std::vector<uint8_t> buffer(secureBufferSize);
    DWORD timeout = 500;
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

            // Минимальная длина: nonce (12) + tag (16) = 28 байт
            if (packet.size() < 12 + 16) {
                AddDebugLog("Greeting packet too small, ignoring");
                continue;
            }

            SecureChannel::EncryptedPacket encPkt;
            encPkt.nonce.assign(packet.begin(), packet.begin() + 12);
            encPkt.ciphertext.assign(packet.begin() + 12, packet.end() - 16);
            encPkt.tag.assign(packet.end() - 16, packet.end());
            encPkt.timestamp = 0;
            encPkt.sequenceNumber = 0;

            if (!g_greetingChannel) {
                AddDebugLog("Greeting channel not initialized");
                continue;
            }

            std::vector<uint8_t> greeting_data = g_greetingChannel->Decrypt(encPkt);
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
        connectionErrorMsg = "No response from target after 2 minutes";
        showConnectionError = true;
        connectionStatus = STATUS_WAITING_GREETING;
    }
    AddDebugLog("Greeting sender thread stopped");
}

void SendGreeting(const std::string& targetIP) {
    if (greetingSocket == INVALID_SOCKET) return;
    if (!g_greetingChannel) {
        AddDebugLog("Greeting channel not initialized");
        return;
    }

    sockaddr_in destAddr;
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(GREETING_PORT);
    destAddr.sin_addr.s_addr = inet_addr(targetIP.c_str());
    if (destAddr.sin_addr.s_addr == INADDR_NONE) {
        AddDebugLog("ERROR: Invalid target IP for greeting: " + targetIP);
        return;
    }

    std::vector<uint8_t> plaintext(reinterpret_cast<const uint8_t*>(GREETING_MESSAGE),
        reinterpret_cast<const uint8_t*>(GREETING_MESSAGE) + GREETING_LENGTH);
    auto encrypted = g_greetingChannel->Encrypt(plaintext);
    if (encrypted.ciphertext.empty()) {
        AddDebugLog("Encryption failed for greeting");
        return;
    }

    // Формируем UDP-пакет: nonce (12) + ciphertext + tag (16)
    std::vector<uint8_t> udpPacket;
    udpPacket.reserve(encrypted.nonce.size() + encrypted.ciphertext.size() + encrypted.tag.size());
    udpPacket.insert(udpPacket.end(), encrypted.nonce.begin(), encrypted.nonce.end());
    udpPacket.insert(udpPacket.end(), encrypted.ciphertext.begin(), encrypted.ciphertext.end());
    udpPacket.insert(udpPacket.end(), encrypted.tag.begin(), encrypted.tag.end());

    sendto(greetingSocket, (const char*)udpPacket.data(), udpPacket.size(), 0,
        (sockaddr*)&destAddr, sizeof(destAddr));
    AddDebugLog("Secure greeting sent to " + targetIP);
}

void SendGreetingResponse(const std::string& targetIP) {
    if (greetingSocket == INVALID_SOCKET) return;
    if (!g_greetingChannel) {
        AddDebugLog("Greeting channel not initialized");
        return;
    }

    sockaddr_in destAddr{};
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(GREETING_PORT);
    destAddr.sin_addr.s_addr = inet_addr(targetIP.c_str());
    if (destAddr.sin_addr.s_addr == INADDR_NONE) {
        AddDebugLog("ERROR: Invalid IP for greeting response");
        return;
    }

    std::vector<uint8_t> plaintext(reinterpret_cast<const uint8_t*>(GREETING_RESPONSE),
        reinterpret_cast<const uint8_t*>(GREETING_RESPONSE) + GREETING_LENGTH);
    auto encrypted = g_greetingChannel->Encrypt(plaintext);
    if (encrypted.ciphertext.empty()) {
        AddDebugLog("Encryption failed for greeting response");
        return;
    }

    std::vector<uint8_t> udpPacket;
    udpPacket.reserve(encrypted.nonce.size() + encrypted.ciphertext.size() + encrypted.tag.size());
    udpPacket.insert(udpPacket.end(), encrypted.nonce.begin(), encrypted.nonce.end());
    udpPacket.insert(udpPacket.end(), encrypted.ciphertext.begin(), encrypted.ciphertext.end());
    udpPacket.insert(udpPacket.end(), encrypted.tag.begin(), encrypted.tag.end());

    sendto(greetingSocket, (const char*)udpPacket.data(), udpPacket.size(), 0,
        (sockaddr*)&destAddr, sizeof(destAddr));
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
    if (waveInOpen(&hWaveIn, selectedMicrophoneId, &wfx, (DWORD_PTR)hBufferEvent, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        AddDebugLog("ERROR: Failed to open microphone");
        return;
    }

    const int NUM_BUFFERS = 8;
    const int SAMPLES_PER_FRAME = AUDIO_DATA_SIZE / sizeof(int16_t);
    WAVEHDR waveHeaders[NUM_BUFFERS];
    std::vector<std::vector<int16_t>> buffers(NUM_BUFFERS, std::vector<int16_t>(SAMPLES_PER_FRAME));

    for (int i = 0; i < NUM_BUFFERS; i++) {
        waveHeaders[i] = {};
        waveHeaders[i].lpData = reinterpret_cast<LPSTR>(buffers[i].data());
        waveHeaders[i].dwBufferLength = AUDIO_DATA_SIZE;
        waveInPrepareHeader(hWaveIn, &waveHeaders[i], sizeof(WAVEHDR));
        waveInAddBuffer(hWaveIn, &waveHeaders[i], sizeof(WAVEHDR));
    }

    waveInStart(hWaveIn);
    int bufferIndex = 0;
    int hangoverFrames = 0;
    const int HANGOVER_FRAMES = 12;   // удержание после потери голоса
    int test = 0;
    while (isRunning && !g_applicationClosing) {
        if (WaitForSingleObject(hBufferEvent, 50) != WAIT_OBJECT_0) continue;
        
        WAVEHDR* currentHeader = &waveHeaders[bufferIndex];
        if (!(currentHeader->dwFlags & WHDR_DONE)) continue;

        if (!isMuted) {
            std::vector<int16_t> audioData(buffers[bufferIndex]);
            
            // Обработка (шумоподавление, AGC, VAD)
            ProcessAudioCapture(audioData);

            bool voiceActive = false;
            if (g_audioProcessor) {
                voiceActive = g_audioProcessor->GetAdvancedStats().voiceActive;
            }

            if (voiceActive) {
                hangoverFrames = HANGOVER_FRAMES;
            }
            else if (hangoverFrames > 0) {
                hangoverFrames--;
            }

            if (hangoverFrames > 0) {
                std::vector<uint8_t> encoded = g_audioCodec->Encode(audioData);
                if (!encoded.empty()) {
                    auto packets = g_reliableTransport->PreparePackets(encoded, ReliableTransport::PacketType::DATA);
                    for (auto& pkt : packets) {
                        auto serialized = pkt.Serialize();
                        auto encryptedPacket = g_secureChannel->Encrypt(serialized);
                        if (!encryptedPacket.ciphertext.empty()) {
                            // Формируем UDP-датаграмму: nonce (12 байт) + ciphertext + tag (16 байт)
                            std::vector<uint8_t> udpPacket;
                            udpPacket.reserve(encryptedPacket.nonce.size() + encryptedPacket.ciphertext.size() + encryptedPacket.tag.size());
                            udpPacket.insert(udpPacket.end(), encryptedPacket.nonce.begin(), encryptedPacket.nonce.end());
                            udpPacket.insert(udpPacket.end(), encryptedPacket.ciphertext.begin(), encryptedPacket.ciphertext.end());
                            udpPacket.insert(udpPacket.end(), encryptedPacket.tag.begin(), encryptedPacket.tag.end());

                            // Отправляем напрямую через udpSocket
                            sendto(udpSocket, (const char*)udpPacket.data(), udpPacket.size(), 0,
                                (sockaddr*)&targetAddr, sizeof(targetAddr));
                        }
                    }
                }
            }
            // Если голоса нет, ничего не отправляем (экономия трафика)
        }

        // Переиспользование буфера
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
    AddDebugLog("Audio playback thread started with dynamic jitter buffer");

    // Инициализация WaveOut
    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = NUM_CHANNELS;
    wfx.nSamplesPerSec = SAMPLE_RATE;
    wfx.wBitsPerSample = BITS_PER_SAMPLE;
    wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    HWAVEOUT hWaveOut = nullptr;
    if (waveOutOpen(&hWaveOut, selectedHeadphonesId, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        AddDebugLog("ERROR: Failed to open headphones for playback");
        return;
    }

    const int NUM_BUFFERS = 6;
    WAVEHDR waveHeaders[NUM_BUFFERS];
    std::vector<std::vector<int16_t>> buffers(NUM_BUFFERS, std::vector<int16_t>(AUDIO_DATA_SIZE / sizeof(int16_t)));

    for (int i = 0; i < NUM_BUFFERS; i++) {
        waveHeaders[i] = {};
        waveHeaders[i].lpData = reinterpret_cast<LPSTR>(buffers[i].data());
        waveHeaders[i].dwBufferLength = AUDIO_DATA_SIZE;
        waveOutPrepareHeader(hWaveOut, &waveHeaders[i], sizeof(WAVEHDR));
    }

    // Jitter buffer
    std::deque<std::vector<int16_t>> jitterBuffer;
    const size_t MAX_BUFFER_FRAMES = 20;    // максимум 20 кадров (~1.2 сек)
    const size_t MIN_BUFFER_FRAMES = 2;     // минимум 2 кадра (~120 мс)

    // Статистика для адаптации
    std::deque<double> interarrivalMs;      // межпакетные интервалы в мс
    double avgDelay = 0.0, delayVar = 0.0;
    auto lastPktTime = std::chrono::steady_clock::now();
    bool firstPkt = true;
    double targetFrames = 4.0;              // плавная цель (в кадрах)
    double smoothTarget = 4.0;
    const double ALPHA = 0.2;               // сглаживание цели
    const double SAFETY_FACTOR = 2.0;       // target = avg + factor * stddev

    // Состояние воспроизведения
    bool primed = false;
    int consecutiveUnderflows = 0;
    int bufferIndex = 0;
    uint64_t packetsReceived = 0, packetsLost = 0;
    auto lastAdaptTime = std::chrono::steady_clock::now();

    // Основной цикл
    while (isRunning && !g_applicationClosing) {
        // --- 1. Приём UDP-пакета ---
        sockaddr_in fromAddr;
        int fromLen = sizeof(fromAddr);
        std::vector<uint8_t> recvBuffer(MAX_PACKET_SIZE);
        u_long nonBlock = 1;
        ioctlsocket(udpSocket, FIONBIO, &nonBlock);
        int recvLen = recvfrom(udpSocket, (char*)recvBuffer.data(), recvBuffer.size(), 0,
                               (sockaddr*)&fromAddr, &fromLen);

        if (recvLen > 0) {
            packetsReceived++;
            std::vector<uint8_t> packet(recvBuffer.begin(), recvBuffer.begin() + recvLen);

            // Расшифровка
            if (packet.size() < 12 + 16) continue;
            SecureChannel::EncryptedPacket encPkt;
            encPkt.nonce.assign(packet.begin(), packet.begin() + 12);
            encPkt.ciphertext.assign(packet.begin() + 12, packet.end() - 16);
            encPkt.tag.assign(packet.end() - 16, packet.end());
            encPkt.timestamp = 0;
            encPkt.sequenceNumber = 0;
            std::vector<uint8_t> decrypted = g_secureChannel->Decrypt(encPkt);
            if (!decrypted.empty()) {
                auto netPkt = ReliableTransport::NetworkPacket::Deserialize(decrypted);
                if (g_reliableTransport->VerifyChecksum(netPkt)) {
                    g_reliableTransport->ProcessPacket(netPkt);
                }
            }

            // Обновление статистики межпакетных интервалов
            auto now = std::chrono::steady_clock::now();
            if (!firstPkt) {
                double dt = std::chrono::duration<double, std::milli>(now - lastPktTime).count();
                interarrivalMs.push_back(dt);
                if (interarrivalMs.size() > 100) interarrivalMs.pop_front();

                // Вычисляем среднее и дисперсию
                double sum = 0.0, sumSq = 0.0;
                for (double t : interarrivalMs) {
                    sum += t;
                    sumSq += t * t;
                }
                avgDelay = sum / interarrivalMs.size();
                delayVar = (sumSq / interarrivalMs.size()) - avgDelay * avgDelay;
                if (delayVar < 0) delayVar = 0;
                double stddev = sqrt(delayVar);

                // Целевой размер буфера (в кадрах) на основе статистики
                double frameDurationMs = (double)BUFFER_DURATION_MS;
                double optimal = (avgDelay + SAFETY_FACTOR * stddev) / frameDurationMs;
                targetFrames = std::clamp(optimal, (double)MIN_BUFFER_FRAMES, (double)MAX_BUFFER_FRAMES);
                smoothTarget = ALPHA * targetFrames + (1.0 - ALPHA) * smoothTarget;
            }
            firstPkt = false;
            lastPktTime = std::chrono::steady_clock::now();
        }

        // --- 2. Получение собранных данных от ReliableTransport ---
        std::vector<uint8_t> completeData = g_reliableTransport->ReceiveCompleteData();
        if (!completeData.empty()) {
            std::vector<int16_t> pcm = g_audioCodec->Decode(completeData);
            if (!pcm.empty()) {
                jitterBuffer.push_back(std::move(pcm));
                while (jitterBuffer.size() > MAX_BUFFER_FRAMES * 2)
                    jitterBuffer.pop_front();
            }
        }

        // --- 3. Адаптация по потерям (дополнительный механизм) ---
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastAdaptTime).count() >= 5) {
            float lossRate = (packetsReceived > 0) ? (float)packetsLost / (packetsReceived + packetsLost) : 0.0f;
            // Если потери высоки – увеличиваем buffer (осторожно, не более +2)
            if (lossRate > 0.03f && smoothTarget < MAX_BUFFER_FRAMES - 2) {
                smoothTarget += 0.5;
            } else if (lossRate < 0.005f && smoothTarget > MIN_BUFFER_FRAMES + 1) {
                smoothTarget -= 0.25;
            }
            smoothTarget = std::clamp(smoothTarget, (double)MIN_BUFFER_FRAMES, (double)MAX_BUFFER_FRAMES);
            packetsReceived = packetsLost = 0;
            lastAdaptTime = now;
            AddDebugLog("Jitter: target=" + std::to_string(smoothTarget) + " frames, avgDelay=" + std::to_string(avgDelay) + " ms");
        }

        // --- 4. Воспроизведение ---
        WAVEHDR* currentHeader = &waveHeaders[bufferIndex];
        if (!(currentHeader->dwFlags & WHDR_INQUEUE)) {
            // Если буфер ещё не наполнен до целевого размера – ждём
            if (!primed && jitterBuffer.size() >= (size_t)smoothTarget) {
                primed = true;
                AddDebugLog("Jitter buffer primed: size="+ std::to_string(jitterBuffer.size()) +" % zu, target ="+std::to_string(smoothTarget));
            }

            if (primed && !jitterBuffer.empty()) {
                size_t samplesToCopy = jitterBuffer.front().size();
                if (samplesToCopy * sizeof(int16_t) <= currentHeader->dwBufferLength) {
                    memcpy(buffers[bufferIndex].data(), jitterBuffer.front().data(),
                           samplesToCopy * sizeof(int16_t));
                    currentHeader->dwBufferLength = (DWORD)(samplesToCopy * sizeof(int16_t));
                } else {
                    currentHeader->dwBufferLength = AUDIO_DATA_SIZE;
                }
                waveOutWrite(hWaveOut, currentHeader, sizeof(WAVEHDR));
                jitterBuffer.pop_front();
                bufferIndex = (bufferIndex + 1) % NUM_BUFFERS;
                consecutiveUnderflows = 0;
            } else {
                // Нет данных – увеличиваем счётчик underflow, но не сбрасываем primed сразу
                consecutiveUnderflows++;
                if (consecutiveUnderflows > 10) {
                    primed = false;
                    consecutiveUnderflows = 0;
                    AddDebugLog("Jitter buffer underflow – reset primed");
                }
                packetsLost++;
                // Небольшая задержка для снижения нагрузки CPU
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
    }

    // Очистка
    waveOutReset(hWaveOut);
    for (int i = 0; i < NUM_BUFFERS; i++) {
        waveOutUnprepareHeader(hWaveOut, &waveHeaders[i], sizeof(WAVEHDR));
    }
    waveOutClose(hWaveOut);
    AddDebugLog("Playback thread stopped");
}

//void NetworkSenderThread() {
//    AddDebugLog("Network sender thread started");
//    while (g_networkSenderRunning && !g_applicationClosing) {
//        std::vector<std::vector<uint8_t>> packetsToSend;
//
//        // Получаем сериализованные пакеты из очереди отправки
//        {
//            std::unique_lock<std::mutex> lock(g_sendQueueMutex);
//            if (g_sendQueueCV.wait_for(lock, std::chrono::milliseconds(20),
//                [] { return !g_sendQueue.empty() || !g_networkSenderRunning; })) {
//                while (!g_sendQueue.empty()) {
//                    packetsToSend.push_back(std::move(g_sendQueue.front()));
//                    g_sendQueue.pop();
//                }
//            }
//        }
//
//        // Получаем пакеты для ретрансляции от ReliableTransport (тип NetworkPacket)
//        auto retransPackets = g_reliableTransport->GetPacketsToSend();
//        for (auto& pkt : retransPackets) {
//            packetsToSend.push_back(pkt.Serialize());
//        }
//
//        // Отправляем все пакеты через зашифрованный канал
//        for (auto& rawPacket : packetsToSend) {
//            if (secureAudioStream) {
//                secureAudioStream->send_secure(rawPacket);
//            }
//        }
//
//        // Обновляем статистику для адаптивного битрейта (раз в секунду)
//        static auto lastStatsUpdate = std::chrono::steady_clock::now();
//        auto now = std::chrono::steady_clock::now();
//        if (now - lastStatsUpdate > std::chrono::seconds(1)) {
//            auto transportStats = g_reliableTransport->GetStatistics();
//            AdaptiveBitrateController::NetworkMetrics metrics;
//            metrics.packetLossRate = transportStats.packetLossRate;
//            metrics.roundTripTime = transportStats.averageLatency;
//            metrics.jitter = 0.0; // можно оценить из истории RTT
//            metrics.bandwidth = 0;
//            g_bitrateController->UpdateMetrics(metrics);
//            int newBitrate = g_bitrateController->GetRecommendedBitrate();
//            g_audioCodec->SetBitrate(newBitrate);
//            lastStatsUpdate = now;
//        }
//    }
//    AddDebugLog("Network sender thread stopped");
//}
// ============================================================================
// ПОДКЛЮЧЕНИЕ
// ============================================================================
    void StartConnection() {
        AddDebugLog("Starting connection with reliable transport...");
        g_applicationClosing = false;
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

    SecureChannel::ChannelConfig channelConfig;
    channelConfig.algorithm = SecureChannel::EncryptionAlgorithm::AES_256_GCM;
    channelConfig.useAuthentication = true;
    channelConfig.preSharedKey = "VoiceApp2024SecureLocalNetwork!!"; // ровно 32 символа
    g_secureChannel = std::make_unique<SecureChannel>();
    if (!g_secureChannel->Initialize(channelConfig)) {
        AddDebugLog("ERROR: Failed to initialize SecureChannel");
        return;
    }

    InitializeAudioProcessor();
    // === Инициализация Audio Codec (Opus) ===
    AdvancedAudioCodec::AudioConfig audioCfg;
    audioCfg.targetBitrate = 32000;
    audioCfg.complexity = 9;
    audioCfg.enableFEC = true;
    audioCfg.enableDTX = true;
    audioCfg.enableCBR = false;
    g_audioCodec = std::make_unique<AdvancedAudioCodec>();
    if (!g_audioCodec->Initialize(audioCfg)) {
        AddDebugLog("ERROR: Failed to initialize Opus codec");
        return;
    }

    // === ReliableTransport ===
    ReliableTransport::ReliabilityConfig relCfg;
    relCfg.maxRetries = 3;
    relCfg.retryTimeoutMs = 100;
    relCfg.ackTimeoutMs = 50;
    relCfg.packetSize = 1200;          // меньше MTU, оставляем место для шифрования
    relCfg.enableFEC = true;
    relCfg.fecRedundancy = 2;
    g_reliableTransport = std::make_unique<ReliableTransport>();
    if (!g_reliableTransport->Initialize(relCfg)) {
        AddDebugLog("ERROR: Failed to initialize ReliableTransport");
        return;
    }

    // === Adaptive Jitter Buffer ===
    AdaptiveJitterBuffer::JitterConfig jitCfg;
    jitCfg.minBufferMs = 30;
    jitCfg.maxBufferMs = 200;
    jitCfg.targetBufferMs = 60;
    g_adaptiveJitterBuffer = std::make_unique<AdaptiveJitterBuffer>();
    g_adaptiveJitterBuffer->Initialize(jitCfg);

    // === Adaptive Bitrate Controller ===
    AdaptiveBitrateController::BitrateConfig brCfg;
    brCfg.minBitrate = 16000;
    brCfg.maxBitrate = 64000;
    brCfg.targetBitrate = 32000;
    brCfg.startingBitrate = 32000;
    brCfg.strategy = AdaptiveBitrateController::Strategy::BALANCED;
    g_bitrateController = std::make_unique<AdaptiveBitrateController>();
    g_bitrateController->Initialize(brCfg);

    // Запуск потока сетевого отправителя
    /*g_networkSenderRunning = true;
    g_networkSenderThread = std::thread(NetworkSenderThread);*/

    // Запуск потоков захвата и воспроизведения
    captureThread = std::thread(CaptureAudio);
    playThread = std::thread(PlayAudio);

    connectionStatus = STATUS_CONNECTED;
    isConnected = true;
    AddDebugLog("Connection started with reliable transport, Opus, adaptive bitrate");
    }

    void StopConnection() {
        AddDebugLog("Stopping connection and reliable transport...");
        isRunning = false;
        isConnected = false;
        connectionStatus = STATUS_DISCONNECTED;

        g_networkSenderRunning = false;
        g_sendQueueCV.notify_all();
        if (g_networkSenderThread.joinable()) g_networkSenderThread.join();

        SafeStopAllThreads();
        ShutdownAudioProcessor();

        g_reliableTransport.reset();
        g_adaptiveJitterBuffer.reset();
        g_bitrateController.reset();
        g_audioCodec.reset();
        g_secureChannel.reset();
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
    //ImGui::Separator();
    //if (g_audioProcessor) {
    //    auto& cfg = g_audioProcessor->advConfig;
    //    auto& stats = g_audioProcessor->advStats;
    //    auto test = g_audioProcessor->GetVoiceDetectorStats();
    //    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.4f, 1.0f), "ДИНАМИЧЕСКАЯ СИСТЕМА (всё подстраивается само)");
    //    ImGui::Separator();

    //    // Слайдеры
    //    ImGui::SliderFloat("Base Noise Gate (dB)", &cfg.noiseGateThreshold, -55.0f, -25.0f, "%.1f");
    //    ImGui::SliderFloat("Base VAD Confidence", &cfg.vadThreshold, 0.20f, 0.55f, "%.2f");
    //    ImGui::SliderFloat("Base AGC Target (dB)", &cfg.agcTargetLevel, -30.0f, -10.0f, "%.1f");
    //    ImGui::SliderFloat("Noise Suppression", &cfg.nsReductionAmount, 0.3f, 1.0f, "%.2f");

    //    ImGui::Separator();
    //    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "ТЕКУЩИЕ ДИНАМИЧЕСКИЕ ЗНАЧЕНИЯ:");

    //    ImGui::Text("Noise Gate: %.1f dB", cfg.dynamicNoiseGateThreshold);
    //    ImGui::Text("VAD Confidence: %.2f", cfg.dynamicVADConfidence);
    //    ImGui::Text("AGC Target: %.1f dB", cfg.dynamicAGCTargetLevel);
    //    ImGui::Text("Voice Active: %s", stats.voiceActive ? "ДА (отправка)" : "НЕТ");
    //    ImGui::Text("Voice Confidence: %.2f", stats.voiceConfidence);
    //    ImGui::Text("Voice Confidence: %.2f", test.confidence);
    //    ImGui::Text("Noise Gate Open: %s", stats.noiseGateOpen ? "ОТКРЫТ" : "ЗАКРЫТ");
    //}
    //else {
    //    ImGui::Text("Audio processor not initialized yet");
    //}
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
    
    // Debug Log – с явной позицией и флагом, чтобы не прятался
    if (showDebugLog) {
        static bool firstFrame = true;
        if (firstFrame) {
            ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
            firstFrame = false;
        }
        ImGui::Begin("Debug Log", &showDebugLog, ImGuiWindowFlags_NoFocusOnAppearing);
        if (ImGui::Button("Copy to Clipboard")) {
            std::string fullLog;
            {
                std::lock_guard<std::mutex> lock(debugLogMutex);
                for (const auto& line : debugLog) fullLog += line + "\n";
            }
            ImGui::SetClipboardText(fullLog.c_str());
        }
        ImGui::SameLine();
        ImGui::Text("Lines: %zu", debugLog.size());
        ImGui::Separator();
        ImGui::BeginChild("LogScrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        {
            std::lock_guard<std::mutex> lock(debugLogMutex);
            for (const auto& line : debugLog) ImGui::TextUnformatted(line.c_str());
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
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

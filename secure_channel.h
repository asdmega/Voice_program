//secure_channel.h:
#pragma once

#include <vector>
#include <memory>
#include <cstdint>
#include <string>
#include <array>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>

// Secure channel for encrypted voice/video communication
class SecureChannel {
public:
    // Encryption algorithms
    enum class EncryptionAlgorithm {
        AES_256_GCM = 0,    // Recommended: AEAD cipher, authenticated
        CHACHA20_POLY1305 = 1  // Alternative: faster on some hardware
    };

    struct ChannelConfig {
        EncryptionAlgorithm algorithm = EncryptionAlgorithm::AES_256_GCM;
        bool useAuthentication = true;
        bool useCompression = true;
        std::string preSharedKey = "";  // For initial handshake
    };

    struct EncryptedPacket {
        std::vector<uint8_t> ciphertext;
        std::vector<uint8_t> nonce;          // IV for encryption
        std::vector<uint8_t> tag;            // Authentication tag for GCM
        std::vector<uint8_t> additionalData; // AAD for authentication
        int64_t timestamp;
        uint32_t sequenceNumber;
    };

    SecureChannel();
    //~SecureChannel();

    // Initialize secure channel with handshake
    bool Initialize(const ChannelConfig& config);

    // Key exchange and establishment (PSK mode for local network)
    //bool EstablishKeyWithPSK(const std::string& presharedKey);

    // Encrypt data with authentication
    EncryptedPacket Encrypt(const std::vector<uint8_t>& plaintext);

    // Decrypt and verify data
    std::vector<uint8_t> Decrypt(const EncryptedPacket& packet);

    // Generate new session keys
    //void RegenerateKeys();

    // Get current security status
    struct SecurityStatus {
        bool isEstablished = false;
        bool isKeyExchangeComplete = false;
        int64_t uptime = 0;
        uint64_t packetsEncrypted = 0;
        uint64_t packetsDecrypted = 0;
        uint64_t authenticationFailures = 0;
    };

    SecurityStatus GetStatus() const;

private:
    ChannelConfig config;
    EVP_CIPHER_CTX* encryptContext = nullptr;
    EVP_CIPHER_CTX* decryptContext = nullptr;

    // Encryption keys
    std::array<uint8_t, 32> encryptionKey;      // 256-bit key
    std::array<uint8_t, 32> authenticationKey;  // For HMAC
    
    // Nonce management (prevents replay attacks)
    uint64_t nonceCounter = 0;
    uint32_t sequenceNumber = 0;

    // Status tracking
    mutable SecurityStatus status;

    // Helper functions
    const EVP_CIPHER* GetCipherAlgorithm() const;
    bool DeriveKeys(const std::string& baseKey);
    std::vector<uint8_t> GenerateNonce();
};

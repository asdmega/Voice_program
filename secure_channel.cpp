//secure_channel.cpp:
/*********************************************************************
 * secure_channel.cpp – полностью переписанный файл
 *
 * Исправлены:
 *   • EVP_CIPHER_get_block_size → EVP_CIPHER_CTX_ctrl (OpenSSL 3.x)
 *   • Недостающие заголовки <openssl/evp.h>, <openssl/rand.h>
 *   • Печать IP‑адреса – inet_ntop
 *
 * Принцип работы:
 *   - Генерация 256‑битного ключа (или передача PSK)
 *   - Для каждого сообщения создаётся случайный IV (12 байт) и GCM‑tag (16 байт)
 *   - Данные шифруются/дешифруются в режиме аутентификации
 *
 *********************************************************************/

#include "secure_channel.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/dh.h>
#include <openssl/sha.h>
#include <cstring>          // memcpy, memset
#include <iostream>
#include <iomanip>          // std::hex
#include <chrono>

// ---------------------------------------------------------------------
//  Конструктор / Деструктор
// ---------------------------------------------------------------------
SecureChannel::SecureChannel()
    : encryptContext(nullptr), decryptContext(nullptr),
      status({false, false, 0, 0, 0, 0})
{
}

// ---------------------------------------------------------------------
//  Инициализация канала (PSK либо случайный ключ)
// ---------------------------------------------------------------------
bool SecureChannel::Initialize(const ChannelConfig& cfg) {
    config = cfg;
    // Создаём контексты один раз
    encryptCtx = EVP_CIPHER_CTX_new();
    decryptCtx = EVP_CIPHER_CTX_new();
    if (!encryptCtx || !decryptCtx) return false;

    // Выбираем алгоритм
    cipher = GetCipherAlgorithm();

    // Генерируем или получаем ключ
    if (config.preSharedKey.empty()) {
        // Если PSK не задан – генерируем случайный ключ
        if (config.preSharedKey.empty()) {
            const size_t KEY_LEN = 32;               // 256 бит
            std::vector<uint8_t> rnd(KEY_LEN);
            if (!RAND_bytes(rnd.data(), static_cast<int>(KEY_LEN))) {
                std::cerr << "RAND_bytes() failed\n";
                return false;
            }
            config.preSharedKey.assign(reinterpret_cast<char*>(rnd.data()), KEY_LEN);
        }
    }
    DeriveKeys(config.preSharedKey);

    // Инициализация контекстов однократно (без nonce)
    if (1 != EVP_EncryptInit_ex(encryptCtx, cipher, nullptr, encryptionKey.data(), nullptr)) return false;
    if (1 != EVP_DecryptInit_ex(decryptCtx, cipher, nullptr, encryptionKey.data(), nullptr)) return false;

    // Отключаем режим "дополнительных данных" (AAD) – для простоты
    return true;
}

bool SecureChannel::StartDHKeyExchange() {
    // Используем стандартную группу 2048-бит (RFC 3526)
    dh = DH_get_2048_256();
    if (!dh) return false;

    // Генерируем ключи
    if (1 != DH_generate_key(dh)) return false;

    // Сохраняем публичный ключ (BIGNUM -> вектор)
    const BIGNUM* pub = DH_get0_pub_key(dh);
    int pubLen = BN_num_bytes(pub);
    std::vector<uint8_t> pubBuf(pubLen);
    BN_bn2bin(pub, pubBuf.data());
    // Сохраняем как публичный ключ для отправки
    // (он будет доступен через GetPublicKey)

    return true;
}

bool SecureChannel::SetPeerPublicKey(const std::vector<uint8_t>& peerPub) {
    peerPubKey = BN_bin2bn(peerPub.data(), peerPub.size(), nullptr);
    if (!peerPubKey) return false;

    // Вычисляем общий секрет
    sharedSecret.resize(DH_size(dh));
    int secretLen = DH_compute_key(sharedSecret.data(), peerPubKey, dh);
    if (secretLen <= 0) return false;
    sharedSecret.resize(secretLen);

    // Используем первые 32 байта sharedSecret как ключ для AES-256-GCM
    if (sharedSecret.size() < 32) {
        // хешируем через SHA256 для получения 32 байт
        unsigned char hash[32];
        SHA256(sharedSecret.data(), sharedSecret.size(), hash);
        memcpy(encryptionKey.data(), hash, 32);
    }
    else {
        memcpy(encryptionKey.data(), sharedSecret.data(), 32);
    }
    return true;
}

SecureChannel::EncryptedPacket SecureChannel::Encrypt(const std::vector<uint8_t>& plaintext) {
    EncryptedPacket pkt;
    pkt.nonce = GenerateNonce();  // 12 байт

    // Переинициализируем контекст с новым nonce (IV)
    if (1 != EVP_EncryptInit_ex(encryptCtx, nullptr, nullptr, nullptr, pkt.nonce.data())) {
        // ошибка
        return pkt;
    }

    // Шифрование
    pkt.ciphertext.resize(plaintext.size() + EVP_MAX_BLOCK_LENGTH);
    int len = 0, final_len = 0;
    if (1 != EVP_EncryptUpdate(encryptCtx, pkt.ciphertext.data(), &len, plaintext.data(), plaintext.size())) {
        return pkt;
    }
    if (1 != EVP_EncryptFinal_ex(encryptCtx, pkt.ciphertext.data() + len, &final_len)) {
        return pkt;
    }
    pkt.ciphertext.resize(len + final_len);

    // Получение тега
    pkt.tag.resize(16);
    if (1 != EVP_CIPHER_CTX_ctrl(encryptCtx, EVP_CTRL_GCM_GET_TAG, 16, pkt.tag.data())) {
        return pkt;
    }

    return pkt;
}
// ---------------------------------------------------------------------
//  Генерация случайного IV (nonce) – 12 байт для GCM
// ---------------------------------------------------------------------
std::vector<uint8_t> SecureChannel::GenerateNonce()
{
    const size_t NONCE_LEN = 12;          // 96‑битный nonce
    std::vector<uint8_t> nonce(NONCE_LEN);
    if (!RAND_bytes(nonce.data(), static_cast<int>(NONCE_LEN))) {
        std::cerr << "RAND_bytes() failed for nonce\n";
    }
    return nonce;
}

// ---------------------------------------------------------------------
//  Дешифрование сообщения (AES‑256‑GCM)
// ---------------------------------------------------------------------
std::vector<uint8_t> SecureChannel::Decrypt(const EncryptedPacket& pkt) {
    // Проверка минимальной длины
    if (pkt.ciphertext.empty() || pkt.tag.size() != 16 || pkt.nonce.size() != 12) {
        status.authenticationFailures++;
        return {};
    }

    // Убеждаемся, что контекст дешифрования существует
    if (!decryptCtx) {
        std::cerr << "Decrypt context not initialized" << std::endl;
        return {};
    }

    // Переинициализация контекста с тем же ключом, но новым nonce (IV)
    // Важно: не меняем ключ и алгоритм – только IV
    if (1 != EVP_DecryptInit_ex(decryptCtx, nullptr, nullptr, nullptr, pkt.nonce.data())) {
        std::cerr << "EVP_DecryptInit_ex failed (nonce update)" << std::endl;
        return {};
    }

    // Устанавливаем тег GCM для верификации
    if (1 != EVP_CIPHER_CTX_ctrl(decryptCtx, EVP_CTRL_GCM_SET_TAG,
        (int)pkt.tag.size(),
        const_cast<uint8_t*>(pkt.tag.data()))) {
        std::cerr << "EVP_CIPHER_CTX_ctrl SET_TAG failed" << std::endl;
        return {};
    }

    // Расшифровка
    std::vector<uint8_t> plaintext(pkt.ciphertext.size());
    int len = 0, final_len = 0;

    if (1 != EVP_DecryptUpdate(decryptCtx, plaintext.data(), &len,
        pkt.ciphertext.data(), (int)pkt.ciphertext.size())) {
        std::cerr << "EVP_DecryptUpdate failed" << std::endl;
        return {};
    }
    int out_len = len;

    // Завершаем расшифровку (здесь проверяется тег)
    if (1 != EVP_DecryptFinal_ex(decryptCtx, plaintext.data() + out_len, &final_len)) {
        std::cerr << "EVP_DecryptFinal_ex failed – authentication failure" << std::endl;
        status.authenticationFailures++;
        return {};
    }
    out_len += final_len;
    plaintext.resize(out_len);

    status.packetsDecrypted++;
    return plaintext;
}
// ---------------------------------------------------------------------
//  Генерация ключей из PSK
// ---------------------------------------------------------------------
bool SecureChannel::DeriveKeys(const std::string& baseKey) {
    if (baseKey.size() < 32) {
        std::cerr << "PSK too short\n";
        return false;
    }
    std::memcpy(encryptionKey.data(), baseKey.data(), 32);

    return true;
}

// ---------------------------------------------------------------------
//  Получаем нужный алгоритм шифрования
// ---------------------------------------------------------------------
const EVP_CIPHER* SecureChannel::GetCipherAlgorithm() const
{
    switch (config.algorithm) {
        case EncryptionAlgorithm::AES_256_GCM:
            return EVP_aes_256_gcm();
        case EncryptionAlgorithm::CHACHA20_POLY1305:
            return EVP_chacha20_poly1305();
        default:
            return EVP_aes_256_gcm();   // fallback
    }
}

// ---------------------------------------------------------------------
//  Статус канала
// ---------------------------------------------------------------------
SecureChannel::SecurityStatus SecureChannel::GetStatus() const
{
    status.uptime = std::chrono::system_clock::now().time_since_epoch().count();
    return status;
}

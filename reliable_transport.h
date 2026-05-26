//reliable_transport.h:
#pragma once

#include <vector>
#include <queue>
#include <map>
#include <memory>
#include <cstdint>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>

// Reliable transport layer with packet recovery and FEC
class ReliableTransport {
public:
    // Packet types for protocol
    enum class PacketType : uint8_t {
        DATA = 0,           // Regular data packet
        ACK = 1,            // Acknowledgment
        NACK = 2,           // Negative acknowledgment (packet lost)
        FEC = 3,            // Forward Error Correction
        HEARTBEAT = 4       // Keep-alive
    };

    struct NetworkPacket {
        PacketType type = PacketType::DATA;
        uint32_t sequenceNumber = 0;
        uint32_t groupNumber = 0;      // For FEC grouping
        uint8_t packetIndex = 0;       // Index within group
        std::vector<uint8_t> payload;
        int64_t timestamp = 0;
        uint16_t checksum = 0;
        int priority = 0;              // 0=low, 1=normal, 2=high
        
        // Serialized header size (32 bytes)
        static constexpr int HEADER_SIZE = 32;
    };

    struct ReliabilityConfig {
        int maxRetries = 3;
        int retryTimeoutMs = 100;
        int ackTimeoutMs = 50;
        int packetSize = 1472;         // MTU - IP/UDP headers
        bool enableFEC = true;
        int fecRedundancy = 2;         // 2 = 50% extra packets
        bool enableCompression = true;
        int bufferTimeMs = 100;        // Jitter buffer
    };

    ReliableTransport();
    ~ReliableTransport();

    bool Initialize(const ReliabilityConfig& config);

    // Fragment and send data reliably
    std::vector<NetworkPacket> PreparePackets(const std::vector<uint8_t>& data, PacketType type);

    // Process received packet and handle ACK/retransmit
    void ProcessPacket(const NetworkPacket& packet);

    // Get packets ready to send (with retransmissions)
    std::vector<NetworkPacket> GetPacketsToSend();

    // Receive complete data when all packets arrive
    std::vector<uint8_t> ReceiveCompleteData();

    // Detect and mark lost packets, trigger retransmission
    void DetectPacketLoss();

    // FEC encoding - create redundant packets
    NetworkPacket GenerateFECPacket(
        const std::vector<NetworkPacket>& groupPackets,
        uint32_t groupNumber,
        uint8_t fecIndex
    );

    // FEC decoding - recover lost packets
    bool RecoverFromFEC(
        const std::vector<NetworkPacket>& fecPackets,
        const std::vector<NetworkPacket>& receivedPackets,
        std::vector<NetworkPacket>& recoveredPackets
    );

    // Statistics
    struct TransportStatistics {
        uint64_t packetsSent = 0;
        uint64_t packetsReceived = 0;
        uint64_t packetsLost = 0;
        uint64_t packetsRetransmitted = 0;
        uint64_t packetsRecoveredByFEC = 0;
        double averageLatency = 0.0;
        double packetLossRate = 0.0;
        int activeRetransmissions = 0;
    };

    TransportStatistics GetStatistics() const;

private:
    ReliabilityConfig config;
    uint32_t sequenceNumberCounter = 0;
    uint32_t groupNumberCounter = 0;

    // Packet tracking
    struct PacketEntry {
        NetworkPacket packet;
        int64_t sentTime = 0;
        int retryCount = 0;
        bool acknowledged = false;
        std::vector<int64_t> timestamps;  // For latency calculation
    };

    // Send buffer: pending packets to send
    std::map<uint32_t, PacketEntry> sendBuffer;
    std::queue<NetworkPacket> retransmissionQueue;

    // Receive buffer: incoming packets
    std::map<uint32_t, PacketEntry> receiveBuffer;
    std::map<uint32_t, std::vector<NetworkPacket>> packetGroups;  // Grouped by sequence

    // Thread synchronization
    mutable std::mutex bufferMutex;
    std::condition_variable dataAvailable;

    // Statistics
    mutable std::mutex statsMutex;
    TransportStatistics statistics;

    // Helper functions
    uint16_t CalculateChecksum(const NetworkPacket& packet);
    bool VerifyChecksum(const NetworkPacket& packet);
    NetworkPacket SerializeData(
        const std::vector<uint8_t>& data,
        uint32_t sequenceNumber,
        uint32_t groupNumber,
        uint8_t packetIndex,
        PacketType type
    );
};

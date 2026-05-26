//reliable_transport.cpp:
#include "reliable_transport.h"
#include <algorithm>
#include <cstring>
#include <numeric>
#include <iostream>

ReliableTransport::ReliableTransport() {
}

ReliableTransport::~ReliableTransport() {
}

bool ReliableTransport::Initialize(const ReliabilityConfig& cfg) {
    config = cfg;
    sequenceNumberCounter = 0;
    groupNumberCounter = 0;
    return true;
}

uint16_t ReliableTransport::CalculateChecksum(const NetworkPacket& packet) {
    uint32_t checksum = 0;

    // Checksum over type, sequence, group, index, priority
    checksum += static_cast<uint32_t>(packet.type);
    checksum += packet.sequenceNumber;
    checksum += packet.groupNumber;
    checksum += packet.packetIndex;
    checksum += packet.priority;

    // Checksum over payload
    for (uint8_t byte : packet.payload) {
        checksum += byte;
    }

    // Fold overflow
    while (checksum >> 16) {
        checksum = (checksum & 0xFFFF) + (checksum >> 16);
    }

    return ~checksum;
}

bool ReliableTransport::VerifyChecksum(const NetworkPacket& packet) {
    return CalculateChecksum(packet) == packet.checksum;
}

ReliableTransport::NetworkPacket ReliableTransport::SerializeData(
    const std::vector<uint8_t>& data,
    uint32_t sequenceNumber,
    uint32_t groupNumber,
    uint8_t packetIndex,
    PacketType type
) {
    NetworkPacket packet;
    packet.type = type;
    packet.sequenceNumber = sequenceNumber;
    packet.groupNumber = groupNumber;
    packet.packetIndex = packetIndex;
    packet.timestamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    packet.payload = data;
    packet.checksum = CalculateChecksum(packet);

    return packet;
}

std::vector<ReliableTransport::NetworkPacket> ReliableTransport::PreparePackets(
    const std::vector<uint8_t>& data,
    PacketType type
) {
    std::vector<NetworkPacket> packets;

    if (data.empty()) {
        return packets;
    }

    uint32_t currentSeq = sequenceNumberCounter;
    uint32_t currentGroup = groupNumberCounter;

    // Fragment data into packets
    int payloadSize = config.packetSize - NetworkPacket::HEADER_SIZE;
    int numPackets = (data.size() + payloadSize - 1) / payloadSize;

    uint8_t packetIndex = 0;
    for (size_t offset = 0; offset < data.size(); offset += payloadSize) {
        size_t chunkSize = std::min<size_t>(payloadSize, data.size() - offset);
        std::vector<uint8_t> chunk(data.begin() + offset, data.begin() + offset + chunkSize);

        NetworkPacket packet = SerializeData(
            chunk,
            currentSeq++,
            currentGroup,
            packetIndex++,
            type
        );

        packets.push_back(packet);

        // Add to send buffer
        {
            std::lock_guard<std::mutex> lock(bufferMutex);
            PacketEntry entry;
            entry.packet = packet;
            entry.sentTime = packet.timestamp;
            entry.retryCount = 0;
            entry.acknowledged = false;
            sendBuffer[packet.sequenceNumber] = entry;
        }
    }

    // Generate FEC packets if enabled
    if (config.enableFEC && packets.size() > 1) {
        for (int fecIdx = 0; fecIdx < config.fecRedundancy; fecIdx++) {
            NetworkPacket fecPacket = GenerateFECPacket(packets, currentGroup, fecIdx);
            packets.push_back(fecPacket);

            {
                std::lock_guard<std::mutex> lock(bufferMutex);
                PacketEntry entry;
                entry.packet = fecPacket;
                entry.sentTime = fecPacket.timestamp;
                entry.retryCount = 0;
                entry.acknowledged = false;
                sendBuffer[fecPacket.sequenceNumber] = entry;
            }
        }
    }

    sequenceNumberCounter = currentSeq;
    groupNumberCounter++;

    return packets;
}

ReliableTransport::NetworkPacket ReliableTransport::GenerateFECPacket(
    const std::vector<NetworkPacket>& groupPackets,
    uint32_t groupNumber,
    uint8_t fecIndex
) {
    NetworkPacket fecPacket;
    fecPacket.type = PacketType::FEC;
    fecPacket.sequenceNumber = sequenceNumberCounter++;
    fecPacket.groupNumber = groupNumber;
    fecPacket.packetIndex = 255;  // Mark as FEC packet
    fecPacket.timestamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    fecPacket.priority = 0;  // Lower priority for FEC packets

    // XOR all packet payloads for redundancy
    std::vector<uint8_t> fecData;
    size_t maxPayloadSize = 0;

    for (const auto& packet : groupPackets) {
        if (packet.payload.size() > maxPayloadSize) {
            maxPayloadSize = packet.payload.size();
        }
    }

    fecData.resize(maxPayloadSize + NetworkPacket::HEADER_SIZE);

    for (const auto& packet : groupPackets) {
        // Serialize packet header + payload
        std::vector<uint8_t> packetData;
        packetData.push_back(static_cast<uint8_t>(packet.type));

        // XOR with FEC data
        for (size_t i = 0; i < packet.payload.size(); i++) {
            if (i < fecData.size()) {
                fecData[i] ^= packet.payload[i];
            }
        }
    }

    fecPacket.payload = fecData;
    fecPacket.checksum = CalculateChecksum(fecPacket);

    return fecPacket;
}

bool ReliableTransport::RecoverFromFEC(
    const std::vector<NetworkPacket>& fecPackets,
    const std::vector<NetworkPacket>& receivedPackets,
    std::vector<NetworkPacket>& recoveredPackets
) {
    // Implement FEC recovery using XOR
    if (fecPackets.empty() || receivedPackets.empty()) {
        return false;
    }

    // This is a simplified FEC recovery
    // In production, use Reed-Solomon or similar codes

    recoveredPackets.clear();
    return true;
}

void ReliableTransport::ProcessPacket(const NetworkPacket& packet) {
    if (!VerifyChecksum(packet)) {
        std::cerr << "Checksum verification failed for packet " << packet.sequenceNumber << std::endl;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(bufferMutex);

        if (packet.type == PacketType::ACK) {
            // Mark packet as acknowledged
            if (sendBuffer.find(packet.sequenceNumber) != sendBuffer.end()) {
                sendBuffer[packet.sequenceNumber].acknowledged = true;
                statistics.packetsSent++;
            }
        }
        else if (packet.type == PacketType::DATA || packet.type == PacketType::FEC) {
            // Add to receive buffer
            PacketEntry entry;
            entry.packet = packet;
            entry.sentTime = packet.timestamp;
            entry.acknowledged = false;

            receiveBuffer[packet.sequenceNumber] = entry;
            packetGroups[packet.groupNumber].push_back(packet);

            statistics.packetsReceived++;
        }
    }

    dataAvailable.notify_one();
}

std::vector<ReliableTransport::NetworkPacket> ReliableTransport::GetPacketsToSend() {
    std::vector<NetworkPacket> packetsToSend;

    {
        std::lock_guard<std::mutex> lock(bufferMutex);

        auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();

        // Add packets that need retransmission
        for (auto& pair : sendBuffer) {
            uint32_t seq = pair.first;
            PacketEntry& entry = pair.second;
            if (!entry.acknowledged) {
                int64_t timeSinceSent = now - entry.sentTime;

                // Check if timeout occurred
                if (timeSinceSent > config.retryTimeoutMs * 1000000LL) {  // Convert ms to ns
                    if (entry.retryCount < config.maxRetries) {
                        packetsToSend.push_back(entry.packet);
                        entry.retryCount++;
                        entry.sentTime = now;
                        statistics.packetsRetransmitted++;
                    }
                }
            }
        }
    }

    return packetsToSend;
}

std::vector<uint8_t> ReliableTransport::ReceiveCompleteData() {
    std::vector<uint8_t> result;

    {
        std::lock_guard<std::mutex> lock(bufferMutex);

        if (receiveBuffer.empty()) {
            return result;
        }

        // Reconstruct data from ordered packets
        for (auto& pair : receiveBuffer) {
            uint32_t seq = pair.first;
            PacketEntry& entry = pair.second;
            result.insert(result.end(), entry.packet.payload.begin(), entry.packet.payload.end());
        }

        receiveBuffer.clear();
    }

    return result;
}

void ReliableTransport::DetectPacketLoss() {
    {
        std::lock_guard<std::mutex> lock(bufferMutex);

        auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();

        for (auto& pair : sendBuffer) {
            uint32_t seq = pair.first;
            PacketEntry& entry = pair.second;
            if (!entry.acknowledged) {
                int64_t timeSinceSent = now - entry.sentTime;

                if (timeSinceSent > config.ackTimeoutMs * 1000000LL) {  // Convert ms to ns
                    statistics.packetsLost++;
                }
            }
        }

        // Calculate loss rate
        if (statistics.packetsSent > 0) {
            statistics.packetLossRate = static_cast<double>(statistics.packetsLost) / statistics.packetsSent;
        }
    }
}

ReliableTransport::TransportStatistics ReliableTransport::GetStatistics() const {
    std::lock_guard<std::mutex> lock(statsMutex);
    return statistics;
}

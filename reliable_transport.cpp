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
    fecPacket.packetIndex = 255;
    fecPacket.timestamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    fecPacket.priority = 0;

    // Определяем максимальный размер полезной нагрузки среди пакетов группы
    size_t maxPayloadSize = 0;
    for (const auto& packet : groupPackets) {
        maxPayloadSize = std::max(maxPayloadSize, packet.payload.size());
    }

    // Создаём буфер для XOR, заполненный нулями
    std::vector<uint8_t> fecData(maxPayloadSize, 0);

    for (const auto& packet : groupPackets) {
        // Для каждого пакета копируем его payload в локальный буфер, дополняя нулями до maxPayloadSize
        std::vector<uint8_t> paddedPayload = packet.payload;
        paddedPayload.resize(maxPayloadSize, 0);
        // XOR
        for (size_t i = 0; i < maxPayloadSize; i++) {
            fecData[i] ^= paddedPayload[i];
        }
    }

    fecPacket.payload = std::move(fecData);
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
// reliable_transport.cpp

std::vector<uint8_t> ReliableTransport::NetworkPacket::Serialize() const {
    std::vector<uint8_t> result;
    result.reserve(HEADER_SIZE + payload.size());

    auto append = [&result](const void* data, size_t size) {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
        result.insert(result.end(), bytes, bytes + size);
        };

    append(&type, sizeof(type));
    append(&sequenceNumber, sizeof(sequenceNumber));
    append(&groupNumber, sizeof(groupNumber));
    append(&packetIndex, sizeof(packetIndex));
    append(&timestamp, sizeof(timestamp));
    append(&checksum, sizeof(checksum));
    append(&priority, sizeof(priority));
    result.insert(result.end(), payload.begin(), payload.end());

    return result;
}

ReliableTransport::NetworkPacket ReliableTransport::NetworkPacket::Deserialize(const std::vector<uint8_t>& data) {
    NetworkPacket pkt;
    if (data.size() < HEADER_SIZE) return pkt;

    size_t offset = 0;
    auto read = [&data, &offset](void* dest, size_t size) -> bool {
        if (offset + size > data.size()) return false;
        memcpy(dest, data.data() + offset, size);
        offset += size;
        return true;
        };

    if (!read(&pkt.type, sizeof(pkt.type))) return pkt;
    if (!read(&pkt.sequenceNumber, sizeof(pkt.sequenceNumber))) return pkt;
    if (!read(&pkt.groupNumber, sizeof(pkt.groupNumber))) return pkt;
    if (!read(&pkt.packetIndex, sizeof(pkt.packetIndex))) return pkt;
    if (!read(&pkt.timestamp, sizeof(pkt.timestamp))) return pkt;
    if (!read(&pkt.checksum, sizeof(pkt.checksum))) return pkt;
    if (!read(&pkt.priority, sizeof(pkt.priority))) return pkt;

    if (offset < data.size()) {
        pkt.payload.assign(data.begin() + offset, data.end());
    }

    return pkt;
}

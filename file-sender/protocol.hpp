#ifndef FILE_SENDER_PROTOCOL_HPP
#define FILE_SENDER_PROTOCOL_HPP

#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <cstdint>
#include <vector>


enum class PacketType : uint8_t {
    Data = 0x01,
    Ack = 0x02,
    Nack = 0x03,
    Start = 0x04,
    End = 0x05,
};

enum class Operation : uint8_t {
    None = 0x00,
    Add = 0x01,
    Mul = 0x02,
    Avg = 0x03,
};

constexpr uint32_t TRANSFER_ID = 1;
constexpr size_t HEADER_SIZE = 28;
constexpr size_t MAX_PAYLOAD = 484;
constexpr int WINDOW_SIZE = 8;
constexpr int TIMEOUT_MS = 500;
constexpr int MAX_RETRIES = 5;

struct Packet {
    PacketType type = PacketType::Data;
    uint32_t seq = 0;
    uint32_t ack = 0;
    uint32_t transfer_id = 0;
    uint32_t fragment = 0;
    uint32_t total_fragments = 0;
    Operation operation = Operation::None;
    uint16_t payload_size = 0;
    uint32_t crc = 0;
    std::vector<uint8_t> payload;
};

inline void write4bytes(std::vector<uint8_t>& buffer, size_t offset, uint32_t value) {
    buffer[offset] = static_cast<uint8_t>((value >> 24) & 0xFF);
    buffer[offset + 1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    buffer[offset + 2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    buffer[offset + 3] = static_cast<uint8_t>(value & 0xFF);
}

inline void write2bytes(std::vector<uint8_t>& buffer, size_t offset, uint16_t value) {
    buffer[offset] = static_cast<uint8_t>((value >> 8) & 0xFF);
    buffer[offset + 1] = static_cast<uint8_t>(value & 0xFF);
}

inline uint32_t read4bytes(const uint8_t* buffer, size_t offset) {
    return (static_cast<uint32_t>(buffer[offset]) << 24) |
           (static_cast<uint32_t>(buffer[offset + 1]) << 16) |
           (static_cast<uint32_t>(buffer[offset + 2]) << 8) |
           static_cast<uint32_t>(buffer[offset + 3]);
}

inline uint16_t read2bytes(const uint8_t* buffer, size_t offset) {
    return static_cast<uint16_t>((buffer[offset] << 8) | buffer[offset + 1]);
}

inline uint32_t crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

inline std::vector<uint8_t> serialize_packet(const Packet& packet) {
    std::vector<uint8_t> buffer(HEADER_SIZE + packet.payload.size(), 0);
    buffer[0] = static_cast<uint8_t>(packet.type);
    write4bytes(buffer, 1, packet.seq);
    write4bytes(buffer, 5, packet.ack);
    write4bytes(buffer, 9, packet.transfer_id);
    write4bytes(buffer, 13, packet.fragment);
    write4bytes(buffer, 17, packet.total_fragments);
    buffer[21] = static_cast<uint8_t>(packet.operation);
    write2bytes(buffer, 22, static_cast<uint16_t>(packet.payload.size()));
    write4bytes(buffer, 24, 0);
    std::copy(packet.payload.begin(), packet.payload.end(), buffer.begin() + HEADER_SIZE);

    // CRC uses the packet with the CRC field temporarily set to zero.
    write4bytes(buffer, 24, crc32(buffer.data(), buffer.size()));
    return buffer;
}

inline bool parse_packet(const uint8_t* data, size_t length, Packet& packet) {
    if (length < HEADER_SIZE) {
        return false;
    }

    const uint16_t payload_size = read2bytes(data, 22);
    if (payload_size > MAX_PAYLOAD || length != HEADER_SIZE + payload_size) {
        return false;
    }

    std::vector<uint8_t> copy(data, data + length);
    const uint32_t received_crc = read4bytes(copy.data(), 24);
    write4bytes(copy, 24, 0);
    if (crc32(copy.data(), copy.size()) != received_crc) {
        return false;
    }

    packet.type = static_cast<PacketType>(data[0]);
    packet.seq = read4bytes(data, 1);
    packet.ack = read4bytes(data, 5);
    packet.transfer_id = read4bytes(data, 9);
    packet.fragment = read4bytes(data, 13);
    packet.total_fragments = read4bytes(data, 17);
    packet.operation = static_cast<Operation>(data[21]);
    packet.payload_size = payload_size;
    packet.crc = received_crc;
    packet.payload.assign(data + HEADER_SIZE, data + HEADER_SIZE + payload_size);
    return packet.transfer_id == TRANSFER_ID;
}

inline bool send_packet(int sock, const sockaddr_in& address, const Packet& packet) {
    const std::vector<uint8_t> bytes = serialize_packet(packet);
    const ssize_t sent = sendto(sock, bytes.data(), bytes.size(), 0,
                                reinterpret_cast<const sockaddr*>(&address),
                                sizeof(address));
    return sent == static_cast<ssize_t>(bytes.size());
}

inline bool same_address(const sockaddr_in& a, const sockaddr_in& b) {
    return a.sin_family == b.sin_family &&
           a.sin_port == b.sin_port &&
           a.sin_addr.s_addr == b.sin_addr.s_addr;
}


#endif

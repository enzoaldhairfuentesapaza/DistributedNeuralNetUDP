#ifndef DNN_UDP_PROTOCOL_HPP
#define DNN_UDP_PROTOCOL_HPP

#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

enum class PacketType : uint8_t {
    Data = 0x01,
    Ack = 0x02,
    Start = 0x03,
    End = 0x04,
};

enum class ObjectType : uint8_t {
    None = 0x00,
    WorkAssignment = 0x01,
    GradientResult = 0x02,
    Control = 0x03,
};

constexpr uint16_t MASTER_ID = 0;
constexpr uint16_t FIRST_WORKER_ID = 1;
constexpr uint16_t LAST_WORKER_ID = 10;

constexpr uint32_t ACK_NONE = 0xFFFFFFFFu;
constexpr size_t DATAGRAM_SIZE = 512;
constexpr size_t HEADER_SIZE = 36;
constexpr size_t MAX_PAYLOAD = DATAGRAM_SIZE - HEADER_SIZE;
constexpr int WINDOW_SIZE = 8;
constexpr int TIMEOUT_MS = 500;
constexpr int MAX_RETRIES = 5;

static_assert(MAX_PAYLOAD == 476, "Protocol payload size must match the manual");

struct Packet {
    PacketType type = PacketType::Data;
    uint32_t seq = 0;
    uint32_t ack = ACK_NONE;
    uint32_t transfer_id = 0;
    uint16_t sender_id = 0;
    uint16_t receiver_id = 0;
    ObjectType object_type = ObjectType::None;
    uint32_t object_size = 0;
    uint32_t fragment = 0;
    uint32_t total_fragments = 0;
    uint16_t payload_size = 0;
    uint32_t crc = 0;
    std::vector<uint8_t> payload;
};

inline uint32_t assignment_transfer_id(uint16_t worker_id) {
    return 1000u + worker_id;
}

inline uint32_t gradient_transfer_id(uint16_t worker_id) {
    return 2000u + worker_id;
}

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
    return static_cast<uint16_t>((static_cast<uint16_t>(buffer[offset]) << 8) |
                                 static_cast<uint16_t>(buffer[offset + 1]));
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

inline bool valid_packet_type(uint8_t value) {
    return value == static_cast<uint8_t>(PacketType::Data) ||
           value == static_cast<uint8_t>(PacketType::Ack) ||
           value == static_cast<uint8_t>(PacketType::Start) ||
           value == static_cast<uint8_t>(PacketType::End);
}

inline bool valid_object_type(uint8_t value) {
    return value == static_cast<uint8_t>(ObjectType::None) ||
           value == static_cast<uint8_t>(ObjectType::WorkAssignment) ||
           value == static_cast<uint8_t>(ObjectType::GradientResult) ||
           value == static_cast<uint8_t>(ObjectType::Control);
}

inline std::vector<uint8_t> serialize_packet(const Packet& packet) {
    const size_t payload_size = packet.payload.size();
    std::vector<uint8_t> buffer(HEADER_SIZE + payload_size, 0);

    buffer[0] = static_cast<uint8_t>(packet.type);
    write4bytes(buffer, 1, packet.seq);
    write4bytes(buffer, 5, packet.ack);
    write4bytes(buffer, 9, packet.transfer_id);
    write2bytes(buffer, 13, packet.sender_id);
    write2bytes(buffer, 15, packet.receiver_id);
    buffer[17] = static_cast<uint8_t>(packet.object_type);
    write4bytes(buffer, 18, packet.object_size);
    write4bytes(buffer, 22, packet.fragment);
    write4bytes(buffer, 26, packet.total_fragments);
    write2bytes(buffer, 30, static_cast<uint16_t>(payload_size));
    write4bytes(buffer, 32, 0);

    std::copy(packet.payload.begin(), packet.payload.end(), buffer.begin() + HEADER_SIZE);

    write4bytes(buffer, 32, crc32(buffer.data(), buffer.size()));
    return buffer;
}

inline bool parse_packet(const uint8_t* data, size_t length, Packet& packet) {
    if (length < HEADER_SIZE) {
        return false;
    }

    const uint8_t raw_type = data[0];
    const uint8_t raw_object_type = data[17];
    const uint16_t payload_size = read2bytes(data, 30);
    if (!valid_packet_type(raw_type) ||
        !valid_object_type(raw_object_type) ||
        payload_size > MAX_PAYLOAD ||
        length != HEADER_SIZE + payload_size) {
        return false;
    }

    std::vector<uint8_t> copy(data, data + length);
    const uint32_t received_crc = read4bytes(copy.data(), 32);
    write4bytes(copy, 32, 0);
    if (crc32(copy.data(), copy.size()) != received_crc) {
        return false;
    }

    packet.type = static_cast<PacketType>(raw_type);
    packet.seq = read4bytes(data, 1);
    packet.ack = read4bytes(data, 5);
    packet.transfer_id = read4bytes(data, 9);
    packet.sender_id = read2bytes(data, 13);
    packet.receiver_id = read2bytes(data, 15);
    packet.object_type = static_cast<ObjectType>(raw_object_type);
    packet.object_size = read4bytes(data, 18);
    packet.fragment = read4bytes(data, 22);
    packet.total_fragments = read4bytes(data, 26);
    packet.payload_size = payload_size;
    packet.crc = received_crc;
    packet.payload.assign(data + HEADER_SIZE, data + HEADER_SIZE + payload_size);
    return true;
}

inline bool send_packet(int sock, const sockaddr_in& address, const Packet& packet) {
    if (packet.payload.size() > MAX_PAYLOAD) {
        return false;
    }

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

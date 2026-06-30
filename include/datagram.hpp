#ifndef DNN_UDP_DATAGRAM_HPP
#define DNN_UDP_DATAGRAM_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

enum class DatagramType : uint8_t {
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

constexpr uint32_t ACK_NONE = 0xFFFFFFFFu;
constexpr size_t DATAGRAM_SIZE = 512;
constexpr size_t HEADER_SIZE = 36;
constexpr size_t MAX_PAYLOAD = DATAGRAM_SIZE - HEADER_SIZE;

static_assert(MAX_PAYLOAD == 476, "Datagram payload size must match the manual");

struct Datagram {
    DatagramType type = DatagramType::Data;
    uint32_t seq = 0;
    uint32_t ack = 0;
    uint32_t transfer_id = 0;
    uint16_t sender_id = 0;
    uint16_t receiver_id = 0;
    ObjectType object_type = ObjectType::None;
    uint32_t object_size = 0;
    uint32_t fragment = 0;
    uint32_t total_fragments = 0;
    std::vector<uint8_t> payload;
};

inline bool matches_transfer(const Datagram& datagram,
                             uint32_t transfer_id,
                             uint16_t expected_sender,
                             uint16_t expected_receiver,
                             ObjectType object_type) {
    return datagram.transfer_id == transfer_id &&
           datagram.sender_id == expected_sender &&
           datagram.receiver_id == expected_receiver &&
           datagram.object_type == object_type;
}

inline Datagram make_datagram(DatagramType type,
                              uint32_t transfer_id,
                              uint16_t sender_id,
                              uint16_t receiver_id,
                              ObjectType object_type,
                              uint32_t object_size,
                              uint32_t total_fragments) {
    Datagram datagram;
    datagram.type = type;
    datagram.ack = 0;
    datagram.transfer_id = transfer_id;
    datagram.sender_id = sender_id;
    datagram.receiver_id = receiver_id;
    datagram.object_type = object_type;
    datagram.object_size = object_size;
    datagram.total_fragments = total_fragments;
    return datagram;
}

inline Datagram make_ack_datagram(uint32_t transfer_id,
                                  uint16_t sender_id,
                                  uint16_t receiver_id,
                                  uint32_t ack_value) {
    Datagram datagram = make_datagram(DatagramType::Ack,
                                      transfer_id,
                                      sender_id,
                                      receiver_id,
                                      ObjectType::None,
                                      0,
                                      0);
    datagram.ack = ack_value;
    return datagram;
}

inline Datagram make_data_datagram(const std::vector<uint8_t>& object,
                                   uint32_t transfer_id,
                                   uint16_t sender_id,
                                   uint16_t receiver_id,
                                   ObjectType object_type,
                                   uint32_t index,
                                   uint32_t total_fragments) {
    Datagram datagram = make_datagram(DatagramType::Data,
                                      transfer_id,
                                      sender_id,
                                      receiver_id,
                                      object_type,
                                      static_cast<uint32_t>(object.size()),
                                      total_fragments);
    datagram.seq = index;
    datagram.fragment = index;

    const size_t start = static_cast<size_t>(index) * MAX_PAYLOAD;
    const size_t end = std::min(start + MAX_PAYLOAD, object.size());
    datagram.payload.assign(object.begin() + start, object.begin() + end);
    return datagram;
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

inline bool valid_datagram_type(uint8_t value) {
    return value == static_cast<uint8_t>(DatagramType::Data) ||
           value == static_cast<uint8_t>(DatagramType::Ack) ||
           value == static_cast<uint8_t>(DatagramType::Start) ||
           value == static_cast<uint8_t>(DatagramType::End);
}

inline bool valid_object_type(uint8_t value) {
    return value == static_cast<uint8_t>(ObjectType::None) ||
           value == static_cast<uint8_t>(ObjectType::WorkAssignment) ||
           value == static_cast<uint8_t>(ObjectType::GradientResult) ||
           value == static_cast<uint8_t>(ObjectType::Control);
}

inline std::vector<uint8_t> serialize_datagram(const Datagram& datagram) {
    const size_t payload_size = datagram.payload.size();
    std::vector<uint8_t> buffer(HEADER_SIZE + payload_size, 0);

    buffer[0] = static_cast<uint8_t>(datagram.type);
    write4bytes(buffer, 1, datagram.seq);
    write4bytes(buffer, 5, datagram.ack);
    write4bytes(buffer, 9, datagram.transfer_id);
    write2bytes(buffer, 13, datagram.sender_id);
    write2bytes(buffer, 15, datagram.receiver_id);
    buffer[17] = static_cast<uint8_t>(datagram.object_type);
    write4bytes(buffer, 18, datagram.object_size);
    write4bytes(buffer, 22, datagram.fragment);
    write4bytes(buffer, 26, datagram.total_fragments);
    write2bytes(buffer, 30, static_cast<uint16_t>(payload_size));
    write4bytes(buffer, 32, 0);

    std::copy(datagram.payload.begin(), datagram.payload.end(), buffer.begin() + HEADER_SIZE);

    write4bytes(buffer, 32, crc32(buffer.data(), buffer.size()));
    return buffer;
}

inline bool parse_datagram(const uint8_t* data, size_t length, Datagram& datagram) {
    if (length < HEADER_SIZE) {
        return false;
    }

    const uint8_t raw_type = data[0];
    const uint8_t raw_object_type = data[17];
    const uint16_t payload_size = read2bytes(data, 30);
    if (!valid_datagram_type(raw_type) ||
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

    datagram.type = static_cast<DatagramType>(raw_type);
    datagram.seq = read4bytes(data, 1);
    datagram.ack = read4bytes(data, 5);
    datagram.transfer_id = read4bytes(data, 9);
    datagram.sender_id = read2bytes(data, 13);
    datagram.receiver_id = read2bytes(data, 15);
    datagram.object_type = static_cast<ObjectType>(raw_object_type);
    datagram.object_size = read4bytes(data, 18);
    datagram.fragment = read4bytes(data, 22);
    datagram.total_fragments = read4bytes(data, 26);
    datagram.payload.assign(data + HEADER_SIZE, data + HEADER_SIZE + payload_size);
    return true;
}

#endif

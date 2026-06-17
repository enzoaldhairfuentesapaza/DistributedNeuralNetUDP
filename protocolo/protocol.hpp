#ifndef DNN_UDP_PROTOCOL_HPP
#define DNN_UDP_PROTOCOL_HPP

#include "datagram.hpp"

#include <netinet/in.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

constexpr uint16_t MASTER_ID = 0;
constexpr uint16_t FIRST_WORKER_ID = 1;
constexpr uint16_t LAST_WORKER_ID = 10;

constexpr int WINDOW_SIZE = 8;
constexpr int TIMEOUT_MS = 2000;
constexpr int MAX_RETRIES = 5;

inline uint32_t worker_payload_transfer_id(uint16_t worker_id) {
    return 1000u + worker_id;
}

inline uint32_t master_payload_transfer_id(uint16_t worker_id) {
    return 2000u + worker_id;
}

bool receive_datagram(int sock, Datagram& datagram, sockaddr_in& from, bool& corrupt);

bool send_datagram(int sock, const sockaddr_in& address, const Datagram& datagram);

bool same_address(const sockaddr_in& a, const sockaddr_in& b);

class RdtSender {
public:
    RdtSender(const sockaddr_in& address,
              std::vector<uint8_t> object,
              uint32_t transfer_id,
              uint16_t sender_id,
              uint16_t receiver_id,
              ObjectType object_type);

    void pump(int sock);
    void handle_ack(const Datagram& datagram, const sockaddr_in& from);

    bool done() const;
    bool failed() const;
    const std::string& error() const;

private:
    enum class Phase {
        Start,
        Data,
        End,
        Done,
        Failed,
    };

    void fail(const std::string& reason);
    void send_control(int sock, DatagramType type);
    void send_window(int sock);

    sockaddr_in address_ {};
    std::vector<uint8_t> object_;
    uint32_t transfer_id_ = 0;
    uint16_t sender_id_ = 0;
    uint16_t receiver_id_ = 0;
    ObjectType object_type_ = ObjectType::None;
    uint32_t total_fragments_ = 0;
    uint32_t base_ = 0;
    uint32_t next_ = 0;
    int retries_ = 0;
    bool waiting_ = false;
    Phase phase_ = Phase::Start;
    std::chrono::steady_clock::time_point last_send_ = std::chrono::steady_clock::now();
    std::string error_;
};

class RdtReceiver {
public:
    RdtReceiver(uint32_t transfer_id, uint16_t expected_sender, uint16_t expected_receiver,
                ObjectType object_type);

    void handle_transfer_datagram(int sock, const Datagram& datagram, const sockaddr_in& from);
    void handle_corrupt(int sock, const sockaddr_in& from);

    bool receiving() const;
    bool done() const;
    std::vector<uint8_t> take_data();
    const sockaddr_in& peer_address() const;

private:
    void send_ack(int sock, uint32_t ack) const;
    uint32_t last_ack() const;

    uint32_t transfer_id_ = 0;
    uint16_t expected_sender_ = 0;
    uint16_t expected_receiver_ = 0;
    ObjectType object_type_ = ObjectType::None;
    bool receiving_ = false;
    bool done_ = false;
    std::chrono::steady_clock::time_point wait_until_ {};
    uint32_t expected_seq_ = 0;
    uint32_t total_fragments_ = 0;
    uint32_t object_size_ = 0;
    sockaddr_in peer_address_ {};
    std::vector<uint8_t> data_;
};

bool send_object(int sock, const sockaddr_in& address, const std::vector<uint8_t>& object,
                 uint32_t transfer_id, uint16_t sender_id, uint16_t receiver_id,
                 ObjectType object_type);

bool receive_object(int sock, uint32_t transfer_id, uint16_t expected_sender,
                    uint16_t expected_receiver, ObjectType object_type, std::vector<uint8_t>& object,
                    sockaddr_in& peer_address);

#endif

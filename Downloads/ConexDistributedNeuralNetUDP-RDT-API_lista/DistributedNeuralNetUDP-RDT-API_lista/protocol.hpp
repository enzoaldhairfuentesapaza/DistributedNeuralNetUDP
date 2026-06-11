#ifndef DNN_UDP_PROTOCOL_HPP
#define DNN_UDP_PROTOCOL_HPP

#include "datagram.hpp"

#include <netinet/in.h>

#include <cstdint>
#include <vector>

constexpr uint16_t MASTER_ID = 0;
constexpr uint16_t FIRST_WORKER_ID = 1;
constexpr uint16_t LAST_WORKER_ID = 10;

constexpr int WINDOW_SIZE = 8;
constexpr int TIMEOUT_MS = 500;
constexpr int MAX_RETRIES = 5;

inline uint32_t assignment_transfer_id(uint16_t worker_id) {
    return 1000u + worker_id;
}

inline uint32_t gradient_transfer_id(uint16_t worker_id) {
    return 2000u + worker_id;
}

bool receive_datagram(int sock, Datagram& datagram, sockaddr_in& from, bool& corrupt);

bool send_datagram(int sock, const sockaddr_in& address, const Datagram& datagram);

bool same_address(const sockaddr_in& a, const sockaddr_in& b);

bool send_object(int sock,
                 const sockaddr_in& address,
                 const std::vector<uint8_t>& object,
                 uint32_t transfer_id,
                 uint16_t sender_id,
                 uint16_t receiver_id,
                 ObjectType object_type);

bool receive_object(int sock,
                    uint32_t transfer_id,
                    uint16_t expected_sender,
                    uint16_t expected_receiver,
                    ObjectType object_type,
                    std::vector<uint8_t>& object,
                    sockaddr_in& peer_address);

#endif

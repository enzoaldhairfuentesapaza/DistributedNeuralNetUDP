#include "protocol.hpp"

#include <errno.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

bool read_file(const string& path, vector<uint8_t>& data) {
    ifstream file(path, ios::binary);
    if (!file) {
        return false;
    }
    data.assign(istreambuf_iterator<char>(file), istreambuf_iterator<char>());
    return true;
}

bool write_file(const string& path, const vector<uint8_t>& data) {
    ofstream file(path, ios::binary);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<streamsize>(data.size()));
    return file.good();
}

bool wait_for_packet(int sock, Packet& packet, sockaddr_in& from) {
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(sock, &read_set);

    timeval timeout {};
    timeout.tv_sec = TIMEOUT_MS / 1000;
    timeout.tv_usec = (TIMEOUT_MS % 1000) * 1000;

    const int ready = select(sock + 1, &read_set, nullptr, nullptr, &timeout);
    if (ready <= 0) {
        return false;
    }

    uint8_t buffer[DATAGRAM_SIZE];
    socklen_t from_len = sizeof(from);
    const ssize_t received = recvfrom(sock, buffer, sizeof(buffer), 0,
                                      reinterpret_cast<sockaddr*>(&from), &from_len);
    if (received < 0) {
        return false;
    }

    return parse_packet(buffer, static_cast<size_t>(received), packet);
}

bool matches_transfer(const Packet& packet,
                      uint32_t transfer_id,
                      uint16_t expected_sender,
                      uint16_t expected_receiver,
                      ObjectType object_type) {
    return packet.transfer_id == transfer_id &&
           packet.sender_id == expected_sender &&
           packet.receiver_id == expected_receiver &&
           packet.object_type == object_type;
}

Packet make_packet(PacketType type,
                   uint32_t transfer_id,
                   uint16_t sender_id,
                   uint16_t receiver_id,
                   ObjectType object_type,
                   uint32_t object_size,
                   uint32_t total_fragments) {
    Packet packet;
    packet.type = type;
    packet.transfer_id = transfer_id;
    packet.sender_id = sender_id;
    packet.receiver_id = receiver_id;
    packet.object_type = object_type;
    packet.object_size = object_size;
    packet.total_fragments = total_fragments;
    return packet;
}

Packet make_ack(const Packet& received, uint32_t ack_value) {
    Packet packet = make_packet(PacketType::Ack,
                                received.transfer_id,
                                received.receiver_id,
                                received.sender_id,
                                received.object_type,
                                received.object_size,
                                received.total_fragments);
    packet.ack = ack_value;
    return packet;
}

Packet make_data_packet(const vector<uint8_t>& object,
                        uint32_t transfer_id,
                        uint16_t sender_id,
                        uint16_t receiver_id,
                        ObjectType object_type,
                        uint32_t index,
                        uint32_t total_fragments) {
    Packet packet = make_packet(PacketType::Data,
                                transfer_id,
                                sender_id,
                                receiver_id,
                                object_type,
                                static_cast<uint32_t>(object.size()),
                                total_fragments);
    packet.seq = index;
    packet.fragment = index;

    const size_t start = static_cast<size_t>(index) * MAX_PAYLOAD;
    const size_t end = min(start + MAX_PAYLOAD, object.size());
    packet.payload.assign(object.begin() + start, object.begin() + end);
    return packet;
}

bool receive_object(int sock,
                    uint32_t transfer_id,
                    uint16_t expected_sender,
                    uint16_t expected_receiver,
                    ObjectType object_type,
                    vector<uint8_t>& object,
                    sockaddr_in& peer_address) {
    bool receiving = false;
    uint32_t expected_seq = 0;
    uint32_t total_fragments = 0;
    uint32_t object_size = 0;
    vector<uint8_t> received_data;

    while (true) {
        Packet packet;
        sockaddr_in from {};
        if (!wait_for_packet(sock, packet, from)) {
            continue;
        }

        if (!matches_transfer(packet, transfer_id, expected_sender, expected_receiver, object_type)) {
            continue;
        }

        if (!receiving) {
            peer_address = from;
        } else if (!same_address(from, peer_address)) {
            continue;
        }

        if (packet.type == PacketType::Start) {
            receiving = true;
            peer_address = from;
            expected_seq = 0;
            total_fragments = packet.total_fragments;
            object_size = packet.object_size;
            received_data.clear();
            send_packet(sock, peer_address, make_ack(packet, ACK_NONE));
            continue;
        }

        if (!receiving) {
            continue;
        }

        if (packet.type == PacketType::Data) {
            if (packet.seq == expected_seq &&
                packet.fragment == expected_seq &&
                packet.total_fragments == total_fragments &&
                packet.object_size == object_size) {
                received_data.insert(received_data.end(), packet.payload.begin(), packet.payload.end());
                send_packet(sock, peer_address, make_ack(packet, expected_seq));
                ++expected_seq;
            } else {
                const uint32_t last_ack = expected_seq == 0 ? ACK_NONE : expected_seq - 1;
                send_packet(sock, peer_address, make_ack(packet, last_ack));
            }
            continue;
        }

        if (packet.type == PacketType::End) {
            if (expected_seq == total_fragments &&
                packet.seq == total_fragments &&
                received_data.size() == object_size) {
                send_packet(sock, peer_address, make_ack(packet, packet.seq));
                object = received_data;
                return true;
            }

            const uint32_t last_ack = expected_seq == 0 ? ACK_NONE : expected_seq - 1;
            send_packet(sock, peer_address, make_ack(packet, last_ack));
        }
    }
}

bool wait_for_ack(int sock,
                  const sockaddr_in& address,
                  uint32_t transfer_id,
                  uint16_t expected_sender,
                  uint16_t expected_receiver,
                  ObjectType object_type,
                  uint32_t& ack_value) {
    Packet packet;
    sockaddr_in from {};
    if (!wait_for_packet(sock, packet, from) || !same_address(from, address)) {
        return false;
    }

    if (packet.type != PacketType::Ack ||
        !matches_transfer(packet, transfer_id, expected_sender, expected_receiver, object_type)) {
        return false;
    }

    ack_value = packet.ack;
    return true;
}

bool send_control_with_ack(int sock,
                           const sockaddr_in& address,
                           const Packet& packet,
                           uint32_t expected_ack) {
    for (int attempt = 0; attempt <= MAX_RETRIES; ++attempt) {
        if (!send_packet(sock, address, packet)) {
            return false;
        }

        uint32_t ack = ACK_NONE;
        if (wait_for_ack(sock,
                         address,
                         packet.transfer_id,
                         packet.receiver_id,
                         packet.sender_id,
                         packet.object_type,
                         ack) &&
            ack == expected_ack) {
            return true;
        }
    }
    return false;
}

bool send_object(int sock,
                 const sockaddr_in& address,
                 const vector<uint8_t>& object,
                 uint32_t transfer_id,
                 uint16_t sender_id,
                 uint16_t receiver_id,
                 ObjectType object_type) {
    const uint32_t total_fragments =
        max<uint32_t>(1, static_cast<uint32_t>((object.size() + MAX_PAYLOAD - 1) / MAX_PAYLOAD));
    const uint32_t object_size = static_cast<uint32_t>(object.size());

    Packet start = make_packet(PacketType::Start,
                               transfer_id,
                               sender_id,
                               receiver_id,
                               object_type,
                               object_size,
                               total_fragments);
    start.ack = ACK_NONE;

    if (!send_control_with_ack(sock, address, start, ACK_NONE)) {
        return false;
    }

    uint32_t base = 0;
    uint32_t next = 0;
    int retries = 0;

    while (base < total_fragments) {
        while (next < total_fragments && next < base + WINDOW_SIZE) {
            Packet data = make_data_packet(object,
                                           transfer_id,
                                           sender_id,
                                           receiver_id,
                                           object_type,
                                           next,
                                           total_fragments);
            if (!send_packet(sock, address, data)) {
                return false;
            }
            ++next;
        }

        uint32_t ack = ACK_NONE;
        if (wait_for_ack(sock, address, transfer_id, receiver_id, sender_id, object_type, ack)) {
            if (ack == ACK_NONE) {
                base = 0;
                next = 0;
            } else if (ack >= base && ack < total_fragments) {
                base = ack + 1;
                retries = 0;
            }
            continue;
        }

        ++retries;
        if (retries > MAX_RETRIES) {
            return false;
        }
        next = base;
    }

    Packet end = make_packet(PacketType::End,
                             transfer_id,
                             sender_id,
                             receiver_id,
                             object_type,
                             object_size,
                             total_fragments);
    end.seq = total_fragments;
    end.ack = ACK_NONE;
    return send_control_with_ack(sock, address, end, total_fragments);
}

bool valid_worker_id(int worker_id) {
    return worker_id >= FIRST_WORKER_ID && worker_id <= LAST_WORKER_ID;
}

int main(int argc, char* argv[]) {
    if (argc != 3 && argc != 4) {
        cerr << "usage: ./worker <worker_id> <gradient_result_file> [assignment_output_file]\n";
        return 1;
    }

    const int parsed_worker_id = atoi(argv[1]);
    if (!valid_worker_id(parsed_worker_id)) {
        cerr << "worker_id must be between 1 and 10\n";
        return 1;
    }

    const uint16_t worker_id = static_cast<uint16_t>(parsed_worker_id);
    const uint16_t port = static_cast<uint16_t>(9000 + worker_id);
    const string gradient_path = argv[2];
    const bool save_assignment = argc == 4;
    const string assignment_output_path = save_assignment ? argv[3] : "";

    const int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        cerr << "could not create UDP socket\n";
        return 1;
    }

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in listen_address {};
    listen_address.sin_family = AF_INET;
    listen_address.sin_addr.s_addr = htonl(INADDR_ANY);
    listen_address.sin_port = htons(port);

    if (::bind(sock, reinterpret_cast<const sockaddr*>(&listen_address), sizeof(listen_address)) < 0) {
        cerr << "could not bind UDP port " << port << ": " << strerror(errno) << "\n";
        close(sock);
        return 1;
    }

    cout << "Worker " << worker_id << " listening on 127.0.0.1:" << port << "\n";

    vector<uint8_t> assignment;
    sockaddr_in master_address {};
    if (!receive_object(sock,
                        assignment_transfer_id(worker_id),
                        MASTER_ID,
                        worker_id,
                        ObjectType::WorkAssignment,
                        assignment,
                        master_address)) {
        cerr << "Worker " << worker_id << ": failed to receive WORK_ASSIGNMENT\n";
        close(sock);
        return 1;
    }

    cout << "Worker " << worker_id << ": received WORK_ASSIGNMENT ("
         << assignment.size() << " bytes)\n";

    if (save_assignment && !write_file(assignment_output_path, assignment)) {
        cerr << "Worker " << worker_id << ": could not write " << assignment_output_path << "\n";
        close(sock);
        return 1;
    }

    vector<uint8_t> gradient_result;
    if (!read_file(gradient_path, gradient_result)) {
        cerr << "Worker " << worker_id << ": could not read " << gradient_path << "\n";
        close(sock);
        return 1;
    }

    cout << "Worker " << worker_id << ": sending GRADIENT_RESULT ("
         << gradient_result.size() << " bytes)\n";
    if (!send_object(sock,
                     master_address,
                     gradient_result,
                     gradient_transfer_id(worker_id),
                     worker_id,
                     MASTER_ID,
                     ObjectType::GradientResult)) {
        cerr << "Worker " << worker_id << ": failed to send GRADIENT_RESULT\n";
        close(sock);
        return 1;
    }

    cout << "Worker " << worker_id << ": transfer complete\n";
    close(sock);
    return 0;
}

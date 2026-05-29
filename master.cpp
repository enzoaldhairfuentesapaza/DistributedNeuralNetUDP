#include "protocol.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

struct WorkerAddress {
    const char* host;
    uint16_t port;
    uint16_t id;
};

const WorkerAddress WORKERS[] = {
    {"127.0.0.1", 9001, 1},
    {"127.0.0.1", 9002, 2},
    {"127.0.0.1", 9003, 3},
    {"127.0.0.1", 9004, 4},
    {"127.0.0.1", 9005, 5},
    {"127.0.0.1", 9006, 6},
    {"127.0.0.1", 9007, 7},
    {"127.0.0.1", 9008, 8},
    {"127.0.0.1", 9009, 9},
    {"127.0.0.1", 9010, 10},
};

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

bool ensure_directory(const string& path) {
    struct stat info {};
    if (stat(path.c_str(), &info) == 0) {
        return S_ISDIR(info.st_mode);
    }
    return mkdir(path.c_str(), 0755) == 0;
}

string worker_file_path(const string& directory, uint16_t worker_id) {
    return directory + "/worker_" + to_string(worker_id) + ".bin";
}

bool make_address(const WorkerAddress& worker, sockaddr_in& address) {
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(worker.port);
    return inet_pton(AF_INET, worker.host, &address.sin_addr) == 1;
}

bool wait_for_packet(int sock, const sockaddr_in& expected_address, Packet& packet) {
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
    sockaddr_in from {};
    socklen_t from_len = sizeof(from);
    const ssize_t received = recvfrom(sock, buffer, sizeof(buffer), 0,
                                      reinterpret_cast<sockaddr*>(&from), &from_len);
    if (received < 0 || !same_address(from, expected_address)) {
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

bool wait_for_ack(int sock,
                  const sockaddr_in& address,
                  uint32_t transfer_id,
                  uint16_t expected_sender,
                  uint16_t expected_receiver,
                  ObjectType object_type,
                  uint32_t& ack_value) {
    Packet packet;
    if (!wait_for_packet(sock, address, packet)) {
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

bool receive_object(int sock,
                    const sockaddr_in& address,
                    uint32_t transfer_id,
                    uint16_t expected_sender,
                    uint16_t expected_receiver,
                    ObjectType object_type,
                    vector<uint8_t>& object) {
    bool receiving = false;
    uint32_t expected_seq = 0;
    uint32_t total_fragments = 0;
    uint32_t object_size = 0;
    vector<uint8_t> received_data;
    int idle_timeouts = 0;

    while (true) {
        Packet packet;
        if (!wait_for_packet(sock, address, packet)) {
            ++idle_timeouts;
            if (idle_timeouts > MAX_RETRIES) {
                return false;
            }
            continue;
        }
        idle_timeouts = 0;

        if (!matches_transfer(packet, transfer_id, expected_sender, expected_receiver, object_type)) {
            continue;
        }

        if (packet.type == PacketType::Start) {
            receiving = true;
            expected_seq = 0;
            total_fragments = packet.total_fragments;
            object_size = packet.object_size;
            received_data.clear();
            send_packet(sock, address, make_ack(packet, ACK_NONE));
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
                send_packet(sock, address, make_ack(packet, expected_seq));
                ++expected_seq;
            } else {
                const uint32_t last_ack = expected_seq == 0 ? ACK_NONE : expected_seq - 1;
                send_packet(sock, address, make_ack(packet, last_ack));
            }
            continue;
        }

        if (packet.type == PacketType::End) {
            if (expected_seq == total_fragments &&
                packet.seq == total_fragments &&
                received_data.size() == object_size) {
                send_packet(sock, address, make_ack(packet, packet.seq));
                object = received_data;
                return true;
            }

            const uint32_t last_ack = expected_seq == 0 ? ACK_NONE : expected_seq - 1;
            send_packet(sock, address, make_ack(packet, last_ack));
        }
    }
}

bool process_worker(int sock,
                    const WorkerAddress& worker,
                    const string& assignments_dir,
                    const string& results_dir) {
    sockaddr_in address {};
    if (!make_address(worker, address)) {
        cerr << "Worker " << worker.id << ": invalid address\n";
        return false;
    }

    vector<uint8_t> assignment;
    const string assignment_path = worker_file_path(assignments_dir, worker.id);
    if (!read_file(assignment_path, assignment)) {
        cerr << "Worker " << worker.id << ": could not read " << assignment_path << "\n";
        return false;
    }

    cout << "Worker " << worker.id << ": sending WORK_ASSIGNMENT ("
         << assignment.size() << " bytes)\n";
    if (!send_object(sock,
                     address,
                     assignment,
                     assignment_transfer_id(worker.id),
                     MASTER_ID,
                     worker.id,
                     ObjectType::WorkAssignment)) {
        cerr << "Worker " << worker.id << ": failed to send WORK_ASSIGNMENT\n";
        return false;
    }

    cout << "Worker " << worker.id << ": waiting for GRADIENT_RESULT\n";
    vector<uint8_t> gradient_result;
    if (!receive_object(sock,
                        address,
                        gradient_transfer_id(worker.id),
                        worker.id,
                        MASTER_ID,
                        ObjectType::GradientResult,
                        gradient_result)) {
        cerr << "Worker " << worker.id << ": failed to receive GRADIENT_RESULT\n";
        return false;
    }

    const string result_path = worker_file_path(results_dir, worker.id);
    if (!write_file(result_path, gradient_result)) {
        cerr << "Worker " << worker.id << ": could not write " << result_path << "\n";
        return false;
    }

    cout << "Worker " << worker.id << ": wrote " << gradient_result.size()
         << " bytes to " << result_path << "\n";
    return true;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "usage: ./server <assignments_dir> <results_dir>\n";
        return 1;
    }

    const string assignments_dir = argv[1];
    const string results_dir = argv[2];

    if (!ensure_directory(results_dir)) {
        cerr << "could not create or access results directory: " << results_dir << "\n";
        return 1;
    }

    const int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        cerr << "could not create UDP socket\n";
        return 1;
    }

    bool all_ok = true;
    for (const WorkerAddress& worker : WORKERS) {
        all_ok = process_worker(sock, worker, assignments_dir, results_dir) && all_ok;
    }

    close(sock);
    return all_ok ? 0 : 1;
}

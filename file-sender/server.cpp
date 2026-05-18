#include "protocol.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
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
    char* host;
    uint16_t port;
};

const WorkerAddress WORKERS[] = {
    {"127.0.0.1", 9001},
    {"127.0.0.1", 9002},
    {"127.0.0.1", 9003},
    {"127.0.0.1", 9004},
};

bool wait_for_packet(int sock, const sockaddr_in& expected_address, Packet& packet) {
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(sock, &read_set);

    timeval timeout{};
    timeout.tv_sec = TIMEOUT_MS / 1000;
    timeout.tv_usec = (TIMEOUT_MS % 1000) * 1000;

    const int ready = select(sock + 1, &read_set, nullptr, nullptr, &timeout);
    if (ready <= 0) {
        return false;
    }

    uint8_t buffer[HEADER_SIZE + MAX_PAYLOAD];
    sockaddr_in from{};
    socklen_t from_len = sizeof(from);
    const ssize_t received = recvfrom(sock, buffer, sizeof(buffer), 0,
                                      reinterpret_cast<sockaddr*>(&from), &from_len);
    if (received < 0 || !same_address(from, expected_address)) {
        return false;
    }

    return parse_packet(buffer, static_cast<size_t>(received), packet);
}

Packet make_control_packet(PacketType type, uint32_t seq, uint32_t ack, uint32_t total_fragments) {
    Packet packet;
    packet.type = type;
    packet.seq = seq;
    packet.ack = ack;
    packet.transfer_id = TRANSFER_ID;
    packet.total_fragments = total_fragments;
    packet.operation = Operation::None;
    return packet;
}

Packet make_data_packet(const vector<uint8_t>& file, uint32_t index, uint32_t total_fragments) {
    Packet packet;
    packet.type = PacketType::Data;
    packet.seq = index;
    packet.transfer_id = TRANSFER_ID;
    packet.fragment = index;
    packet.total_fragments = total_fragments;
    packet.operation = Operation::None;

    const size_t start = static_cast<size_t>(index) * MAX_PAYLOAD;
    const size_t end = min(start + MAX_PAYLOAD, file.size());
    packet.payload.assign(file.begin() + start, file.begin() + end);
    return packet;
}

bool wait_for_ack(int sock, const sockaddr_in& address, uint32_t& ack_value, bool& got_nack) {
    Packet packet;
    if (!wait_for_packet(sock, address, packet)) {
        return false;
    }
    if (packet.type == PacketType::Ack) {
        ack_value = packet.ack;
        got_nack = false;
        return true;
    }
    if (packet.type == PacketType::Nack) {
        ack_value = packet.ack;
        got_nack = true;
        return true;
    }
    return false;
}

bool send_with_ack(int sock, const sockaddr_in& address, const Packet& packet, uint32_t expected_ack) {
    for (int attempt = 0; attempt <= MAX_RETRIES; ++attempt) {
        if (!send_packet(sock, address, packet)) {
            return false;
        }

        uint32_t ack = 0;
        bool got_nack = false;
        if (wait_for_ack(sock, address, ack, got_nack) && !got_nack && ack == expected_ack) {
            return true;
        }
    }
    return false;
}

bool send_file_to_worker(int sock, const sockaddr_in& address, const vector<uint8_t>& file, int worker_number) {
    const uint32_t total_fragments =
        static_cast<uint32_t>((file.size() + MAX_PAYLOAD - 1) / MAX_PAYLOAD);

    cout << "Worker " << worker_number << ": START\n";
    if (!send_with_ack(sock, address, make_control_packet(PacketType::Start, 0, 0, total_fragments), 0)) {
        cerr << "Worker " << worker_number << ": START failed\n";
        return false;
    }

    uint32_t base = 0;
    uint32_t next = 0;
    int retries = 0;

    while (base < total_fragments) {
        while (next < total_fragments && next < base + WINDOW_SIZE) {
            if (!send_packet(sock, address, make_data_packet(file, next, total_fragments))) {
                return false;
            }
            ++next;
        }

        uint32_t ack = 0;
        bool got_nack = false;
        if (wait_for_ack(sock, address, ack, got_nack)) {
            if (!got_nack && ack >= base && ack < total_fragments) {
                base = ack + 1;
                retries = 0;
            } else if (got_nack) {
                next = base;
            }
            continue;
        }

        ++retries;
        if (retries > MAX_RETRIES) {
            cerr << "Worker " << worker_number << ": DATA failed after retries\n";
            return false;
        }
        next = base;
    }

    cout << "Worker " << worker_number << ": END\n";
    const uint32_t end_seq = total_fragments;
    if (!send_with_ack(sock, address, make_control_packet(PacketType::End, end_seq, 0, total_fragments), end_seq)) {
        cerr << "Worker " << worker_number << ": END failed\n";
        return false;
    }

    cout << "Worker " << worker_number << ": transfer complete\n";
    return true;
}

bool read_file(const string& path, vector<uint8_t>& data) {
    ifstream file(path, ios::binary);
    if (!file) {
        return false;
    }
    data.assign(istreambuf_iterator<char>(file), istreambuf_iterator<char>());
    return true;
}

bool make_address(const WorkerAddress& worker, sockaddr_in& address) {
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(worker.port);
    return inet_pton(AF_INET, worker.host, &address.sin_addr) == 1;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "worng paramethers: ./server <file_path>\n";
        return 1;
    }

    vector<uint8_t> file;
    if (!read_file(argv[1], file)) {
        cerr << "could not read file: " << argv[1] << "\n";
        return 1;
    }

    const int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        cerr << "cant create UDP socket\n";
        return 1;
    }

    bool all_ok = true;
    for (size_t i = 0; i < sizeof(WORKERS) / sizeof(WORKERS[0]); ++i) {
        sockaddr_in address{};
        if (!make_address(WORKERS[i], address)) {
            cerr << "invalid worker address\n";
            all_ok = false;
            continue;
        }
        all_ok = send_file_to_worker(sock, address, file, static_cast<int>(i + 1)) && all_ok;
    }

    close(sock);
    return all_ok ? 0 : 1;
}

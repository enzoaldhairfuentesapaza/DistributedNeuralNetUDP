#include "protocol.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

uint16_t WORKER_PORTS[] = {9001, 9002, 9003, 9004};

Packet make_ack(PacketType type, uint32_t ack, uint32_t total_fragments) {
    Packet packet;
    packet.type = type;
    packet.ack = ack;
    packet.transfer_id = TRANSFER_ID;
    packet.total_fragments = total_fragments;
    packet.operation = Operation::None;
    return packet;
}

bool write_file(string& path, vector<uint8_t>& data) {
    ofstream file(path, ios::binary);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<char*>(data.data()), static_cast<streamsize>(data.size()));
    return file.good();
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Faltan parametros ./worker <worker_id> <output_path>\n";
        return 1;
    }

    int worker_id = atoi(argv[1]);
    if (worker_id < 1 || worker_id > 4) {
        cerr << "worker_id must be between 1 and 4\n";
        return 1;
    }

    uint16_t port = WORKER_PORTS[worker_id - 1];
    string output_path = argv[2];

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        cerr << "Could not create UDP socket\n";
        return 1;
    }

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in listen_address{};
    listen_address.sin_family = AF_INET;
    listen_address.sin_addr.s_addr = htonl(INADDR_ANY);
    listen_address.sin_port = htons(port);

    if (::bind(sock, reinterpret_cast<const sockaddr*>(&listen_address), sizeof(listen_address)) < 0) {
        cerr << "Could not bind UDP port" << port << ": " << strerror(errno) << "\n";
        close(sock);
        return 1;
    }

    cout << "Worker " << worker_id << " listening on 127.0.0.1:" << port << "\n";

    bool receiving = false;
    sockaddr_in server_address{};
    uint32_t expected_seq = 0;
    uint32_t total_fragments = 0;
    vector<uint8_t> received_data;

    while (true) {
        uint8_t buffer[HEADER_SIZE + MAX_PAYLOAD];
        sockaddr_in from{};
        socklen_t from_len = sizeof(from);
        ssize_t received = recvfrom(sock, buffer, sizeof(buffer), 0,
                                          reinterpret_cast<sockaddr*>(&from), &from_len);
        if (received < 0)  continue;

        Packet packet;
        if (!parse_packet(buffer, static_cast<size_t>(received), packet)) {
            if (receiving && same_address(from, server_address)) {
                uint32_t last_ack = expected_seq == 0 ? 0 : expected_seq - 1;
                send_packet(sock, server_address, make_ack(PacketType::Nack, last_ack, total_fragments));
            }
            continue;
        }

        if (packet.type == PacketType::Start) {
            receiving = true;
            server_address = from;
            expected_seq = 0;
            total_fragments = packet.total_fragments;
            received_data.clear();
            send_packet(sock, server_address, make_ack(PacketType::Ack, 0, total_fragments));
            cout << "Worker " << worker_id << ": START received\n";
            continue;
        }

        if (!receiving || !same_address(from, server_address)) {
            continue;
        }

        if (packet.type == PacketType::Data) {
            if (packet.seq == expected_seq &&
                packet.fragment == expected_seq &&
                packet.total_fragments == total_fragments) {
                received_data.insert(received_data.end(), packet.payload.begin(), packet.payload.end());
                send_packet(sock, server_address, make_ack(PacketType::Ack, expected_seq, total_fragments));
                ++expected_seq;
            } else {
                // Go-Back-N accepts only the next expected packet.
                if (expected_seq == 0) {
                    send_packet(sock, server_address, make_ack(PacketType::Nack, 0, total_fragments));
                } else {
                    send_packet(sock, server_address, make_ack(PacketType::Ack, expected_seq - 1, total_fragments));
                }
            }
            continue;
        }

        if (packet.type == PacketType::End) {
            if (expected_seq == total_fragments) {
                if (!write_file(output_path, received_data)) {
                    cerr << "Could not write output file: " << output_path << "\n";
                    close(sock);
                    return 1;
                }
                send_packet(sock, server_address, make_ack(PacketType::Ack, packet.seq, total_fragments));
                cout << "Worker " << worker_id << ": wrote " << received_data.size()
                     << " bytes to " << output_path << "\n";
                close(sock);
                return 0;
            }

            uint32_t last_ack = expected_seq == 0 ? 0 : expected_seq - 1;
            send_packet(sock, server_address, make_ack(PacketType::Nack, last_ack, total_fragments));
        }
    }
}

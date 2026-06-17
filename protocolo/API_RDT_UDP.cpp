#include "API_RDT_UDP.hpp"

#include "protocol.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>


using Clock = std::chrono::steady_clock;

constexpr int SESSION_POLL_MS = 50;
constexpr int MASTER_PAYLOAD_START_TIMEOUT_SECONDS = 60;

struct SocketHandle {
    int fd = -1;

    explicit SocketHandle(int socket_fd) : fd(socket_fd) {}

    ~SocketHandle() {
        if (fd >= 0) {
            close(fd);
        }
    }
};

struct WorkerSessionResult {
    Bytes master_payload;
    std::string error;
};

bool make_address(const std::string& host, uint16_t port, sockaddr_in& address) {
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    return inet_pton(AF_INET, host.c_str(), &address.sin_addr) == 1;
}

bool wait_for_socket(int sock) {
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(sock, &read_set);

    timeval timeout {};
    timeout.tv_sec = 0;
    timeout.tv_usec = SESSION_POLL_MS * 1000;
    return select(sock + 1, &read_set, nullptr, nullptr, &timeout) > 0;
}

WorkerSessionResult run_worker_session(const WorkerEndpoint& worker,
                                       const Bytes& worker_payload) {
    WorkerSessionResult result;

    sockaddr_in address {};
    if (!make_address(worker.host, worker.port, address)) {
        result.error = "Worker " + std::to_string(worker.id) + ": invalid address";
        return result;
    }

    SocketHandle sock(socket(AF_INET, SOCK_DGRAM, 0));
    if (sock.fd < 0) {
        result.error = "Worker " + std::to_string(worker.id) + ": could not create UDP socket";
        return result;
    }

    RdtSender sender(address, worker_payload, worker_payload_transfer_id(worker.id),
                     MASTER_ID, worker.id, ObjectType::WorkerPayload);
    RdtReceiver receiver(master_payload_transfer_id(worker.id), worker.id, MASTER_ID,
                         ObjectType::MasterPayload);

    bool master_payload_deadline_started = false;
    Clock::time_point master_payload_deadline {};

    while (!sender.failed() && (!sender.done() || !receiver.done())) {
        sender.pump(sock.fd);

        if (sender.done() && !master_payload_deadline_started) {
            master_payload_deadline_started = true;
            master_payload_deadline = Clock::now() + std::chrono::seconds(MASTER_PAYLOAD_START_TIMEOUT_SECONDS);
        }

        if (master_payload_deadline_started &&
            !receiver.receiving() &&
            Clock::now() >= master_payload_deadline) {
            result.error = "Worker " + std::to_string(worker.id) +
                           ": master payload did not start within 60 seconds";
            return result;
        }

        if (!wait_for_socket(sock.fd)) {
            continue;
        }

        Datagram datagram;
        sockaddr_in from {};
        bool corrupt = false;
        if (!receive_datagram(sock.fd, datagram, from, corrupt)) {
            if (corrupt) {
                receiver.handle_corrupt(sock.fd, from);
            }
            continue;
        }

        if (!same_address(from, address) ||
            datagram.sender_id != worker.id ||
            datagram.receiver_id != MASTER_ID) {
            continue;
        }

        sender.handle_ack(datagram, from);
        receiver.handle_transfer_datagram(sock.fd, datagram, from);
    }

    if (sender.failed()) {
        result.error = "Worker " + std::to_string(worker.id) + ": " + sender.error();
        return result;
    }

    result.master_payload = receiver.take_data();
    return result;
}

void join_threads(std::vector<std::thread>& threads) {
    for (std::thread& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

void validate_exchange(const std::vector<WorkerEndpoint>& workers,
                       const std::map<uint16_t, Bytes>& worker_payloads) {
    if (workers.size() != LAST_WORKER_ID) {
        throw std::runtime_error("master requires exactly 10 workers");
    }

    bool seen[LAST_WORKER_ID + 1] {};
    for (const WorkerEndpoint& worker : workers) {
        if (worker.id < FIRST_WORKER_ID || worker.id > LAST_WORKER_ID || seen[worker.id]) {
            throw std::runtime_error("workers must have unique ids from 1 to 10");
        }
        if (worker_payloads.find(worker.id) == worker_payloads.end()) {
            throw std::runtime_error("missing worker payload for worker " + std::to_string(worker.id));
        }
        seen[worker.id] = true;
    }
}


std::vector<WorkerEndpoint> default_workers() {
    return {
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
}

MasterTransport::MasterTransport(std::vector<WorkerEndpoint> workers)
    : workers_(std::move(workers)) {}

std::map<uint16_t, Bytes> MasterTransport::exchange(const std::map<uint16_t, Bytes>& worker_payloads) const {
    validate_exchange(workers_, worker_payloads);

    std::vector<WorkerSessionResult> session_results(LAST_WORKER_ID + 1);
    std::vector<std::thread> threads;
    threads.reserve(workers_.size());

    try {
        for (const WorkerEndpoint& worker : workers_) {
            threads.emplace_back([&worker_payloads, &session_results, worker]() {
                try {
                    session_results[worker.id] = run_worker_session(worker, worker_payloads.at(worker.id));
                } catch (const std::exception& error) {
                    session_results[worker.id].error = error.what();
                } catch (...) {
                    session_results[worker.id].error = "unknown session error";
                }
            });
        }
    } catch (...) {
        join_threads(threads);
        throw;
    }

    join_threads(threads);

    std::map<uint16_t, Bytes> master_payloads;
    std::string errors;
    for (uint16_t worker_id = FIRST_WORKER_ID; worker_id <= LAST_WORKER_ID; ++worker_id) {
        WorkerSessionResult& session = session_results[worker_id];
        if (session.error.empty()) {
            master_payloads[worker_id] = std::move(session.master_payload);
        } else {
            if (!errors.empty()) {
                errors += '\n';
            }
            errors += session.error;
        }
    }

    if (!errors.empty()) {
        throw std::runtime_error(errors);
    }
    return master_payloads;
}

WorkerTransport::WorkerTransport(uint16_t worker_id) : worker_id_(worker_id) {
    if (worker_id < FIRST_WORKER_ID || worker_id > LAST_WORKER_ID) {
        throw std::runtime_error("worker_id must be between 1 and 10");
    }

    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) {
        throw std::runtime_error("could not create UDP socket");
    }

    int reuse = 1;
    setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in listen_address {};
    listen_address.sin_family = AF_INET;
    listen_address.sin_addr.s_addr = htonl(INADDR_ANY);
    listen_address.sin_port = htons(static_cast<uint16_t>(9000 + worker_id_));

    if (::bind(sock_, reinterpret_cast<const sockaddr*>(&listen_address), sizeof(listen_address)) < 0) {
        close(sock_);
        sock_ = -1;
        throw std::runtime_error("could not bind UDP port " + std::to_string(9000 + worker_id_));
    }
}

WorkerTransport::~WorkerTransport() {
    if (sock_ >= 0) {
        close(sock_);
    }
}

Bytes WorkerTransport::receive_worker_payload() {
    Bytes worker_payload;
    sockaddr_in master_address {};
    if (!receive_object(sock_, worker_payload_transfer_id(worker_id_), MASTER_ID,
                        worker_id_, ObjectType::WorkerPayload, worker_payload, master_address)) {
        throw std::runtime_error("failed to receive WORKER_PAYLOAD");
    }

    master_address_ = master_address;
    has_master_address_ = true;
    return worker_payload;
}

void WorkerTransport::send_master_payload(const Bytes& master_payload) {
    if (!has_master_address_) {
        throw std::runtime_error("cannot send MASTER_PAYLOAD before receiving WORKER_PAYLOAD");
    }

    if (!send_object(sock_, master_address_, master_payload, master_payload_transfer_id(worker_id_),
            worker_id_, MASTER_ID, ObjectType::MasterPayload)) {
        throw std::runtime_error("failed to send MASTER_PAYLOAD");
    }
}

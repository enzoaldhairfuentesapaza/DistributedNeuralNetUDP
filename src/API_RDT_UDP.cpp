#include "API_RDT_UDP.hpp"

#include "protocol.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using Clock = std::chrono::steady_clock;


struct SocketHandle {
    int fd = -1;

    explicit SocketHandle(int socket_fd) : fd(socket_fd) {}

    ~SocketHandle() {
        if (fd >= 0) {
            close(fd);
        }
    }
};

enum class SendPhase {
    Start,
    Data,
    End,
    Done,
    Failed,
};

struct SendState {
    API_RDT_UDP::Bytes object;
    uint32_t transfer_id = 0;
    uint16_t receiver_id = 0;
    uint32_t object_size = 0;
    uint32_t total_fragments = 0;
    uint32_t base = 0;
    uint32_t next = 0;
    int retries = 0;
    bool waiting = false;
    SendPhase phase = SendPhase::Start;
    Clock::time_point last_send = Clock::now();
    std::string error;
};

struct ReceiveState {
    uint32_t transfer_id = 0;
    bool receiving = false;
    bool done = false;
    uint32_t expected_seq = 0;
    uint32_t total_fragments = 0;
    uint32_t object_size = 0;
    API_RDT_UDP::Bytes data;
};

struct WorkerSession {
    API_RDT_UDP::WorkerEndpoint worker {};
    sockaddr_in address {};
    SendState assignment;
    ReceiveState result;
};

bool make_address(const std::string& host, uint16_t port, sockaddr_in& address) {
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    return inet_pton(AF_INET, host.c_str(), &address.sin_addr) == 1;
}

void fail_session(WorkerSession& session, const std::string& reason) {
    session.assignment.phase = SendPhase::Failed;
    session.assignment.error = "Worker " + std::to_string(session.worker.id) + ": " + reason;
}

bool send_control_datagram(int sock, WorkerSession& session, DatagramType type) {
    SendState& state = session.assignment;
    Datagram datagram = make_datagram(type,
                                      state.transfer_id,
                                      MASTER_ID,
                                      state.receiver_id,
                                      ObjectType::WorkAssignment,
                                      state.object_size,
                                      state.total_fragments);
    if (type == DatagramType::End) {
        datagram.seq = state.total_fragments;
    }

    if (!send_datagram(sock, session.address, datagram)) {
        fail_session(session, type == DatagramType::Start ? "failed to send START" : "failed to send END");
        return false;
    }

    state.waiting = true;
    state.last_send = Clock::now();
    return true;
}

bool send_data_window(int sock, WorkerSession& session) {
    SendState& state = session.assignment;
    while (state.next < state.total_fragments && state.next < state.base + WINDOW_SIZE) {
        Datagram data = make_data_datagram(state.object,
                                           state.transfer_id,
                                           MASTER_ID,
                                           state.receiver_id,
                                           ObjectType::WorkAssignment,
                                           state.next,
                                           state.total_fragments);
        if (!send_datagram(sock, session.address, data)) {
            fail_session(session, "failed to send DATA");
            return false;
        }
        ++state.next;
    }

    state.waiting = state.base < state.next;
    state.last_send = Clock::now();
    return true;
}

void pump_assignment_sender(int sock, WorkerSession& session) {
    SendState& state = session.assignment;
    if (state.phase == SendPhase::Done || state.phase == SendPhase::Failed) {
        return;
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - state.last_send);
    if (state.waiting && elapsed.count() >= TIMEOUT_MS) {
        ++state.retries;
        if (state.retries > MAX_RETRIES) {
            fail_session(session, "transfer timed out");
            return;
        }

        if (state.phase == SendPhase::Data) {
            state.next = state.base;
        }
        state.waiting = false;
    }

    if (state.phase == SendPhase::Failed || state.waiting) {
        return;
    }

    if (state.phase == SendPhase::Start) {
        send_control_datagram(sock, session, DatagramType::Start);
    } else if (state.phase == SendPhase::Data) {
        send_data_window(sock, session);
    } else if (state.phase == SendPhase::End) {
        send_control_datagram(sock, session, DatagramType::End);
    }
}

void handle_assignment_ack(WorkerSession& session, const Datagram& datagram) {
    SendState& state = session.assignment;
    if (state.phase == SendPhase::Done || state.phase == SendPhase::Failed) {
        return;
    }

    if (state.phase == SendPhase::Start) {
        if (datagram.ack == ACK_NONE) {
            state.phase = SendPhase::Data;
            state.waiting = false;
            state.retries = 0;
        }
        return;
    }

    if (state.phase == SendPhase::Data) {
        if (datagram.ack == ACK_NONE) {
            state.base = 0;
            state.next = 0;
            state.waiting = false;
            return;
        }

        if (datagram.ack >= state.base && datagram.ack < state.total_fragments) {
            state.base = datagram.ack + 1;
            state.retries = 0;
            if (state.base == state.total_fragments) {
                state.phase = SendPhase::End;
                state.waiting = false;
            } else if (state.base == state.next) {
                state.waiting = false;
            } else {
                state.last_send = Clock::now();
            }
        }
        return;
    }

    if (state.phase == SendPhase::End && datagram.ack == state.total_fragments) {
        state.phase = SendPhase::Done;
        state.waiting = false;
        state.retries = 0;
    }
}

void handle_result_datagram(int sock, WorkerSession& session, const Datagram& datagram) {
    ReceiveState& state = session.result;
    if (datagram.type == DatagramType::End)
    {
        std::cout
            << "Worker "
            << session.worker.id
            << ": received GRADIENT_RESULT ("
            << state.data.size()
            << " bytes)"
            << std::endl;
    }

    if (state.done) {
        if (datagram.type == DatagramType::End) {
            send_datagram(sock,
                          session.address,
                          make_ack_datagram(datagram.transfer_id,
                                            datagram.receiver_id,
                                            datagram.sender_id,
                                            datagram.seq));
        }
        return;
    }

    if (datagram.transfer_id != state.transfer_id ||
        datagram.object_type != ObjectType::GradientResult) {
        return;
    }

    if (datagram.type == DatagramType::Start) {
        state.receiving = true;
        state.expected_seq = 0;
        state.total_fragments = datagram.total_fragments;
        state.object_size = datagram.object_size;
        state.data.clear();
        send_datagram(sock,
                      session.address,
                      make_ack_datagram(datagram.transfer_id,
                                        datagram.receiver_id,
                                        datagram.sender_id,
                                        ACK_NONE));
        return;
    }

    if (!state.receiving) {
        return;
    }

    if (datagram.type == DatagramType::Data) {
        if (datagram.seq == state.expected_seq && datagram.fragment == state.expected_seq) {
            state.data.insert(state.data.end(), datagram.payload.begin(), datagram.payload.end());
            send_datagram(sock,
                          session.address,
                          make_ack_datagram(datagram.transfer_id,
                                            datagram.receiver_id,
                                            datagram.sender_id,
                                            state.expected_seq));
            ++state.expected_seq;
        } else {
            const uint32_t last_ack = state.expected_seq == 0 ? ACK_NONE : state.expected_seq - 1;
            send_datagram(sock,
                          session.address,
                          make_ack_datagram(datagram.transfer_id,
                                            datagram.receiver_id,
                                            datagram.sender_id,
                                            last_ack));
        }
        return;
    }

    if (datagram.type == DatagramType::End) {
        if (state.expected_seq == state.total_fragments &&
            datagram.seq == state.total_fragments &&
            state.data.size() == state.object_size) {
            send_datagram(sock,
                          session.address,
                          make_ack_datagram(datagram.transfer_id,
                                            datagram.receiver_id,
                                            datagram.sender_id,
                                            datagram.seq));
            state.done = true;
            return;
        }

        const uint32_t last_ack = state.expected_seq == 0 ? ACK_NONE : state.expected_seq - 1;
        send_datagram(sock,
                      session.address,
                      make_ack_datagram(datagram.transfer_id,
                                        datagram.receiver_id,
                                        datagram.sender_id,
                                        last_ack));
    }
}

WorkerSession* find_session(std::vector<WorkerSession>& sessions,
                            const sockaddr_in& from,
                            const Datagram& datagram) {
    for (WorkerSession& session : sessions) {
        if (same_address(session.address, from) &&
            datagram.sender_id == session.worker.id &&
            datagram.receiver_id == MASTER_ID) {
            return &session;
        }
    }
    return nullptr;
}

WorkerSession* find_session_by_address(std::vector<WorkerSession>& sessions, const sockaddr_in& from) {
    for (WorkerSession& session : sessions) {
        if (same_address(session.address, from)) {
            return &session;
        }
    }
    return nullptr;
}

bool all_done(const std::vector<WorkerSession>& sessions) {
    for (const WorkerSession& session : sessions) {
        if (session.assignment.phase != SendPhase::Done || !session.result.done) {
            return false;
        }
    }
    return true;
}

std::string first_failure(const std::vector<WorkerSession>& sessions) {
    for (const WorkerSession& session : sessions) {
        if (session.assignment.phase == SendPhase::Failed) {
            return session.assignment.error.empty() ? "transfer failed" : session.assignment.error;
        }
    }
    return "";
}

bool run_event_loop(int sock, std::vector<WorkerSession>& sessions) {
    while (!all_done(sessions)) {
        if (!first_failure(sessions).empty()) {
            return false;
        }

        for (WorkerSession& session : sessions) {
            pump_assignment_sender(sock, session);
        }

        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(sock, &read_set);

        timeval timeout {};
        timeout.tv_sec = 0;
        timeout.tv_usec = 50000;

        const int ready = select(sock + 1, &read_set, nullptr, nullptr, &timeout);
        if (ready <= 0) {
            continue;
        }

        while (true) {
            Datagram datagram;
            sockaddr_in from {};
            bool corrupt = false;
            if (!receive_datagram(sock, datagram, from, corrupt)) {
                if (corrupt) {
                    WorkerSession* session = find_session_by_address(sessions, from);
                    if (session != nullptr) {
                        ReceiveState& result = session->result;
                        const uint32_t last_ack =
                            result.expected_seq == 0 ? ACK_NONE : result.expected_seq - 1;
                        send_datagram(sock,
                                      session->address,
                                      make_ack_datagram(result.transfer_id,
                                                        MASTER_ID,
                                                        session->worker.id,
                                                        last_ack));
                    }
                }
                break;
            }

            WorkerSession* session = find_session(sessions, from, datagram);
            if (session != nullptr) {
                if (datagram.type == DatagramType::Ack &&
                    datagram.transfer_id == session->assignment.transfer_id &&
                    datagram.sender_id == session->worker.id &&
                    datagram.receiver_id == MASTER_ID) {
                    handle_assignment_ack(*session, datagram);
                } else {
                    handle_result_datagram(sock, *session, datagram);
                }
            }

            FD_ZERO(&read_set);
            FD_SET(sock, &read_set);
            timeval drain_timeout {};
            const int more = select(sock + 1, &read_set, nullptr, nullptr, &drain_timeout);
            if (more <= 0) {
                break;
            }
        }
    }
    return true;
}


namespace API_RDT_UDP {

std::vector<WorkerEndpoint> build_workers(uint16_t num_workers)
{
    if (num_workers < 1 || num_workers > 10)
    {
        throw std::runtime_error("Number of workers must be between 1 and 10.");
    }
    std::vector<WorkerEndpoint> workers;
    for (uint16_t id = 1; id <= num_workers; ++id)
    {
        workers.push_back(
        {
            "127.0.0.1",
            static_cast<uint16_t>(9000 + id),
            id
        });
    }
    return workers;
}

MasterTransport::MasterTransport(std::vector<WorkerEndpoint> workers)
    : workers_(std::move(workers)) {}

std::map<uint16_t, Bytes> MasterTransport::exchange(const std::map<uint16_t, Bytes>& assignments) const {
    std::vector<WorkerSession> sessions;

    for (const WorkerEndpoint& worker : workers_) {
        if (worker.id < FIRST_WORKER_ID || worker.id > LAST_WORKER_ID) {
            throw std::runtime_error("worker id out of range: " + std::to_string(worker.id));
        }

        auto assignment = assignments.find(worker.id);
        if (assignment == assignments.end()) {
            throw std::runtime_error("missing assignment for worker " + std::to_string(worker.id));
        }

        WorkerSession session;
        session.worker = worker;
        if (!make_address(worker.host, worker.port, session.address)) {
            throw std::runtime_error("invalid address for worker " + std::to_string(worker.id));
        }

        session.assignment.object = assignment->second;
        session.assignment.transfer_id = assignment_transfer_id(worker.id);
        session.assignment.receiver_id = worker.id;
        session.assignment.object_size = static_cast<uint32_t>(session.assignment.object.size());
        session.assignment.total_fragments =
            std::max<uint32_t>(1, static_cast<uint32_t>(
                                      (session.assignment.object.size() + MAX_PAYLOAD - 1) / MAX_PAYLOAD));
        session.result.transfer_id = gradient_transfer_id(worker.id);
        sessions.push_back(std::move(session));
    }

    SocketHandle sock(socket(AF_INET, SOCK_DGRAM, 0));
    if (sock.fd < 0) {
        throw std::runtime_error("could not create UDP socket");
    }

    if (!run_event_loop(sock.fd, sessions)) {
        const std::string reason = first_failure(sessions);
        throw std::runtime_error(reason.empty() ? "master exchange failed" : reason);
    }

    std::map<uint16_t, Bytes> results;
    for (const WorkerSession& session : sessions) {
        results[session.worker.id] = session.result.data;
    }
    return results;
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

Bytes WorkerTransport::receive_assignment() {
    Bytes assignment;
    sockaddr_in master_address {};
    if (!receive_object(sock_,
                        assignment_transfer_id(worker_id_),
                        MASTER_ID,
                        worker_id_,
                        ObjectType::WorkAssignment,
                        assignment,
                        master_address)) {
        throw std::runtime_error("failed to receive WORK_ASSIGNMENT");
    }

    master_address_ = master_address;
    has_master_address_ = true;
    return assignment;
}

void WorkerTransport::send_gradient(const Bytes& gradient_result) {
    if (!has_master_address_) {
        throw std::runtime_error("cannot send GRADIENT_RESULT before receiving WORK_ASSIGNMENT");
    }

    if (!send_object(sock_,
                     master_address_,
                     gradient_result,
                     gradient_transfer_id(worker_id_),
                     worker_id_,
                     MASTER_ID,
                     ObjectType::GradientResult)) {
        throw std::runtime_error("failed to send GRADIENT_RESULT");
    }
}

}  // namespace API_RDT_UDP

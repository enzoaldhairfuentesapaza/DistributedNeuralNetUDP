#include "protocol.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace std;
using Clock = chrono::steady_clock;

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

enum class SendPhase {
    Start,
    Data,
    End,
    Done,
    Failed,
};

struct SendState {
    vector<uint8_t> object;
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
};

struct ReceiveState {
    uint32_t transfer_id = 0;
    bool receiving = false;
    bool done = false;
    uint32_t expected_seq = 0;
    uint32_t total_fragments = 0;
    uint32_t object_size = 0;
    vector<uint8_t> data;
};

struct WorkerSession {
    WorkerAddress worker {};
    sockaddr_in address {};
    SendState assignment;
    ReceiveState result;
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

bool read_datagram(int sock, Datagram& datagram, sockaddr_in& from, bool& corrupt) {
    corrupt = false;

    uint8_t buffer[DATAGRAM_SIZE];
    socklen_t from_len = sizeof(from);
    const ssize_t received = recvfrom(sock, buffer, sizeof(buffer), 0,
                                      reinterpret_cast<sockaddr*>(&from), &from_len);
    if (received < 0) {
        return false;
    }

    if (!parse_datagram(buffer, static_cast<size_t>(received), datagram)) {
        corrupt = true;
        return false;
    }
    return true;
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
        cerr << "Worker " << session.worker.id << ": failed to send "
             << (type == DatagramType::Start ? "START" : "END") << "\n";
        state.phase = SendPhase::Failed;
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
            cerr << "Worker " << session.worker.id << ": failed to send DATA\n";
            state.phase = SendPhase::Failed;
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

    const auto elapsed = chrono::duration_cast<chrono::milliseconds>(Clock::now() - state.last_send);
    if (state.waiting && elapsed.count() >= TIMEOUT_MS) {
        ++state.retries;
        if (state.retries > MAX_RETRIES) {
            cerr << "Worker " << session.worker.id << ": transfer timed out\n";
            state.phase = SendPhase::Failed;
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
            cout << "Worker " << session.worker.id << ": START acknowledged\n";
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
                cout << "Worker " << session.worker.id << ": DATA acknowledged\n";
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
        cout << "Worker " << session.worker.id << ": WORK_ASSIGNMENT sent\n";
    }
}

void handle_result_datagram(int sock, WorkerSession& session, const Datagram& datagram) {
    ReceiveState& state = session.result;
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
        cout << "Worker " << session.worker.id << ": receiving GRADIENT_RESULT ("
             << state.object_size << " bytes)\n";
        return;
    }

    if (!state.receiving) {
        return;
    }

    if (datagram.type == DatagramType::Data) {
        if (datagram.seq == state.expected_seq &&
            datagram.fragment == state.expected_seq) {
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
            cout << "Worker " << session.worker.id << ": GRADIENT_RESULT received\n";
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

WorkerSession* find_session(vector<WorkerSession>& sessions, const sockaddr_in& from, const Datagram& datagram) {
    for (WorkerSession& session : sessions) {
        if (same_address(session.address, from) &&
            datagram.sender_id == session.worker.id &&
            datagram.receiver_id == MASTER_ID) {
            return &session;
        }
    }
    return nullptr;
}

WorkerSession* find_session_by_address(vector<WorkerSession>& sessions, const sockaddr_in& from) {
    for (WorkerSession& session : sessions) {
        if (same_address(session.address, from)) {
            return &session;
        }
    }
    return nullptr;
}

bool all_done(const vector<WorkerSession>& sessions) {
    for (const WorkerSession& session : sessions) {
        if (session.assignment.phase != SendPhase::Done || !session.result.done) {
            return false;
        }
    }
    return true;
}

bool any_failed(const vector<WorkerSession>& sessions) {
    for (const WorkerSession& session : sessions) {
        if (session.assignment.phase == SendPhase::Failed) {
            return true;
        }
    }
    return false;
}

bool load_sessions(const string& assignments_dir, vector<WorkerSession>& sessions) {
    for (const WorkerAddress& worker : WORKERS) {
        WorkerSession session;
        session.worker = worker;

        memset(&session.address, 0, sizeof(session.address));
        session.address.sin_family = AF_INET;
        session.address.sin_port = htons(worker.port);
        if (inet_pton(AF_INET, worker.host, &session.address.sin_addr) != 1) {
            cerr << "Worker " << worker.id << ": invalid address\n";
            return false;
        }

        const string assignment_path = assignments_dir + "/worker_" + to_string(worker.id) + ".bin";
        if (!read_file(assignment_path, session.assignment.object)) {
            cerr << "Worker " << worker.id << ": could not read " << assignment_path << "\n";
            return false;
        }

        session.assignment.transfer_id = assignment_transfer_id(worker.id);
        session.assignment.receiver_id = worker.id;
        session.assignment.object_size = static_cast<uint32_t>(session.assignment.object.size());
        session.assignment.total_fragments =
            max<uint32_t>(1, static_cast<uint32_t>(
                                 (session.assignment.object.size() + MAX_PAYLOAD - 1) / MAX_PAYLOAD));

        session.result.transfer_id = gradient_transfer_id(worker.id);
        cout << "Worker " << worker.id << ": queued WORK_ASSIGNMENT ("
             << session.assignment.object.size() << " bytes)\n";
        sessions.push_back(session);
    }
    return true;
}

bool write_results(const string& results_dir, const vector<WorkerSession>& sessions) {
    for (const WorkerSession& session : sessions) {
        const string result_path = results_dir + "/worker_" + to_string(session.worker.id) + ".bin";
        if (!write_file(result_path, session.result.data)) {
            cerr << "Worker " << session.worker.id << ": could not write " << result_path << "\n";
            return false;
        }
        cout << "Worker " << session.worker.id << ": wrote " << session.result.data.size()
             << " bytes to " << result_path << "\n";
    }
    return true;
}

bool run_event_loop(int sock, vector<WorkerSession>& sessions) {
    while (!all_done(sessions)) {
        if (any_failed(sessions)) {
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
            if (!read_datagram(sock, datagram, from, corrupt)) {
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

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "usage: ./master <assignments_dir> <results_dir>\n";
        return 1;
    }

    const string assignments_dir = argv[1];
    const string results_dir = argv[2];

    struct stat info {};
    const bool results_dir_exists = stat(results_dir.c_str(), &info) == 0;
    if ((results_dir_exists && !S_ISDIR(info.st_mode)) ||
        (!results_dir_exists && mkdir(results_dir.c_str(), 0755) != 0)) {
        cerr << "could not create or access results directory: " << results_dir << "\n";
        return 1;
    }

    vector<WorkerSession> sessions;
    if (!load_sessions(assignments_dir, sessions)) {
        return 1;
    }

    const int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        cerr << "could not create UDP socket\n";
        return 1;
    }

    const bool ok = run_event_loop(sock, sessions) && write_results(results_dir, sessions);
    close(sock);
    return ok ? 0 : 1;
}

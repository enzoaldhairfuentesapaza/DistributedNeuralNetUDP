#include "protocol.hpp"

#include <sys/select.h>
#include <sys/socket.h>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <utility>

constexpr bool PACKET_LOSS = false;
constexpr float LOSS_RATE = 0.1;

constexpr bool PACKET_CORRUPTION = false;
constexpr float CORRUPTION_RATE = 0.1;

constexpr bool TIMEOUT_TEST = false;
constexpr float TIMEOUT_RATE = 0.1;


//constexpr bool PACKET_DELAY = false;
//constexpr int DELAY_TIME = 2000;


bool fail_happens(float rate) {
    static thread_local std::mt19937 rng(std::random_device{}());
    std::bernoulli_distribution distribution(rate);
    return distribution(rng);
}

const char* datagram_type_name(DatagramType type) {
    switch (type) {
        case DatagramType::Data:
            return "DATA";
        case DatagramType::Ack:
            return "ACK";
        case DatagramType::Start:
            return "START";
        case DatagramType::End:
            return "END";
    }
    return "UNKNOWN";
}

const char* object_type_name(ObjectType type) {
    switch (type) {
        case ObjectType::None:
            return "NONE";
        case ObjectType::WorkerPayload:
            return "WORKER_PAYLOAD";
        case ObjectType::MasterPayload:
            return "MASTER_PAYLOAD";
    }
    return "UNKNOWN";
}


bool send_datagram(int sock, const sockaddr_in& address, const Datagram& datagram) {
    if (datagram.payload.size() > MAX_PAYLOAD) {
        return false;
    }

    std::vector<uint8_t> bytes = serialize_datagram(datagram);

    if (PACKET_LOSS && fail_happens(LOSS_RATE)) {
        std::cout << "Simulating packet loss for datagram with seq=" << datagram.seq << std::endl;
        return true;
    }
    if(PACKET_CORRUPTION && fail_happens(CORRUPTION_RATE)) {
        std::cout << "Simulating packet corruption for datagram with seq=" << datagram.seq << std::endl;
        if (!bytes.empty()) {
            std::uniform_int_distribution<size_t> byte_distribution(0, bytes.size() - 1);
            static thread_local std::mt19937 rng(std::random_device{}());
            bytes[byte_distribution(rng)] ^= 0xFF; //corrupt random byte
        }
    }
    if (TIMEOUT_TEST && datagram.type == DatagramType::Ack && fail_happens(TIMEOUT_RATE)) {
        std::cout << "Simulating ACK drop ack=" << datagram.ack << std::endl;
        return true;
    }/*
    if (PACKET_DELAY && (rand() / static_cast<float>(RAND_MAX)) < 0.5) {
        std::cout << "Simulating packet delay for datagram with seq=" << datagram.seq << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(DELAY_TIME));
    }*/

    const ssize_t sent = sendto(sock, bytes.data(), bytes.size(), 0,
                                reinterpret_cast<const sockaddr*>(&address),
                                sizeof(address));
    if (sent != static_cast<ssize_t>(bytes.size())) {
        return false;
    }

    std::cout << "-----------------------\nSENT DATAGRAM \ntype=" << datagram_type_name(datagram.type)
        << " seq=" << datagram.seq
        << " ack=" << datagram.ack
        << " transfer_id=" << datagram.transfer_id
        << " sender_id=" << datagram.sender_id
        << " receiver_id=" << datagram.receiver_id
         << " object_type=" << object_type_name(datagram.object_type)
         << " object_size=" << datagram.object_size
         << " payload_size=" << datagram.payload.size()
         << " datagram_size=" << bytes.size()
         << " crc32=0x" << std::hex << std::setw(8) << std::setfill('0') << read4bytes(bytes.data(), 24) << std::dec << std::endl;
    std::cout << "-----------------------\n";

    return true;
}

bool same_address(const sockaddr_in& a, const sockaddr_in& b) {
    return a.sin_family == b.sin_family &&
           a.sin_port == b.sin_port &&
           a.sin_addr.s_addr == b.sin_addr.s_addr;
}

bool receive_datagram(int sock, Datagram& datagram, sockaddr_in& from, bool& corrupt) {
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
        std::cout << "-----------------\nRECEIVED CORRUPT datagram ! size=" << received << 
            "-------------------------"<< std::endl;
        return false;
    }

    std::cout << "-----------------------\nRECEIVED DATAGRAM \ntype=" << datagram_type_name(datagram.type)
        << " seq=" << datagram.seq
        << " ack=" << datagram.ack
        << " transfer_id=" << datagram.transfer_id
        << " sender_id=" << datagram.sender_id
        << " receiver_id=" << datagram.receiver_id
         << " object_type=" << object_type_name(datagram.object_type)
         << " object_size=" << datagram.object_size
         << " payload_size=" << datagram.payload.size()
         << " datagram_size=" << received
         << " crc32=0x" << std::hex << std::setw(8) << std::setfill('0') << read4bytes(buffer, 24) << std::dec << std::endl;
    std::cout << "-----------------------\n";
    return true;
}

bool wait_for_datagram(int sock, Datagram& datagram, sockaddr_in& from, bool& corrupt) {
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(sock, &read_set);

    timeval timeout {};
    timeout.tv_sec = TIMEOUT_MS / 1000;
    timeout.tv_usec = (TIMEOUT_MS % 1000) * 1000;

    const int ready = select(sock + 1, &read_set, nullptr, nullptr, &timeout);
    if (ready <= 0) {
        corrupt = false;
        return false;
    }

    return receive_datagram(sock, datagram, from, corrupt);
}


RdtSender::RdtSender(const sockaddr_in& address, std::vector<uint8_t> object, uint32_t transfer_id,
                uint16_t sender_id, uint16_t receiver_id, ObjectType object_type)
    : address_(address),
      object_(std::move(object)),
      transfer_id_(transfer_id),
      sender_id_(sender_id),
      receiver_id_(receiver_id),
      object_type_(object_type),
      total_fragments_(fragment_count(static_cast<uint32_t>(object_.size()))) {}

void RdtSender::fail(const std::string& reason) {
    phase_ = Phase::Failed;
    error_ = reason;
}

void RdtSender::send_control(int sock, DatagramType type) {
    Datagram datagram = make_datagram(type, transfer_id_,
                                      sender_id_, receiver_id_,
                                      object_type_, static_cast<uint32_t>(object_.size()));
    if (type == DatagramType::End) {
        datagram.seq = total_fragments_;
    }

    if (!send_datagram(sock, address_, datagram)) {
        fail(type == DatagramType::Start ? "failed to send START" : "failed to send END");
        return;
    }

    waiting_ = true;
    last_send_ = std::chrono::steady_clock::now();
}

void RdtSender::send_window(int sock) {
    while (next_ < total_fragments_ && next_ < base_ + WINDOW_SIZE) {
        Datagram datagram = make_data_datagram(object_, transfer_id_,
                                               sender_id_, receiver_id_,
                                               object_type_,  next_);
        if (!send_datagram(sock, address_, datagram)) {
            fail("failed to send DATA");
            return;
        }
        ++next_;
    }

    waiting_ = base_ < next_;
    last_send_ = std::chrono::steady_clock::now();
}

void RdtSender::pump(int sock) {
    if (done() || failed()) {
        return;
    }
    std::cout << "Pumping RdtSender (phase=" << (phase_ == Phase::Start ? "START" :
                                        phase_ == Phase::Data ? "DATA" :
                                        phase_ == Phase::End ? "END" : "UNKNOWN")
              << ", base=" << base_
              << ", next=" << next_
              << ", retries=" << retries_
              << ", waiting=" << std::boolalpha << waiting_ << std::noboolalpha
              << ")\n";
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - last_send_);
    if (waiting_ && elapsed.count() >= TIMEOUT_MS) {
        std::cout << "Timeout occurred, retransmitting... (retries=" << retries_ + 1 << ")\n";
        ++retries_;

        if (retries_ > MAX_RETRIES) {
            fail("transfer timed out");
            return;
        }
        if (phase_ == Phase::Data) {
            next_ = base_;
        }
        waiting_ = false;
    }

    if (waiting_) {
        return;
    }

    if (phase_ == Phase::Start) {
        send_control(sock, DatagramType::Start);
    } else if (phase_ == Phase::Data) {
        send_window(sock);
    } else if (phase_ == Phase::End) {
        send_control(sock, DatagramType::End);
    }
}

void RdtSender::handle_ack(const Datagram& datagram, const sockaddr_in& from) {
    if (!same_address(from, address_) ||
        datagram.type != DatagramType::Ack ||
        datagram.transfer_id != transfer_id_ ||
        datagram.sender_id != receiver_id_ ||
        datagram.receiver_id != sender_id_) {
        return;
    }

    if (phase_ == Phase::Start && datagram.ack == ACK_NONE) {
        phase_ = Phase::Data;
        waiting_ = false;
        retries_ = 0;
        return;
    }

    if (phase_ == Phase::Data) {
        if (datagram.ack == ACK_NONE) {
            base_ = 0;
            next_ = 0;
            waiting_ = false;
        } else if (datagram.ack >= base_ && datagram.ack < total_fragments_) {
            base_ = datagram.ack + 1;
            retries_ = 0;
            if (base_ == total_fragments_) {
                phase_ = Phase::End;
                waiting_ = false;
            } else if (base_ == next_) {
                waiting_ = false;
            } else {
                last_send_ = std::chrono::steady_clock::now();
            }
        }
        return;
    }

    if (phase_ == Phase::End && datagram.ack == total_fragments_) {
        phase_ = Phase::Done;
        waiting_ = false;
        retries_ = 0;
    }
}

bool RdtSender::done() const {
    return phase_ == Phase::Done;
}

bool RdtSender::failed() const {
    return phase_ == Phase::Failed;
}

const std::string& RdtSender::error() const {
    return error_;
}

RdtReceiver::RdtReceiver(uint32_t transfer_id,
                         uint16_t expected_sender,
                         uint16_t expected_receiver,
                         ObjectType object_type)
    : transfer_id_(transfer_id),
      expected_sender_(expected_sender),
      expected_receiver_(expected_receiver),
      object_type_(object_type) {}

uint32_t RdtReceiver::last_ack() const {
    return expected_seq_ == 0 ? ACK_NONE : expected_seq_ - 1;
}

void RdtReceiver::send_ack(int sock, uint32_t ack) const {
    send_datagram(sock,
                  peer_address_,
                  make_ack_datagram(transfer_id_, expected_receiver_, expected_sender_, ack));
}

void RdtReceiver::handle_corrupt(int sock, const sockaddr_in& from) {
    if (receiving_ && same_address(from, peer_address_)) {
        send_ack(sock, last_ack());
    }
}

void RdtReceiver::handle_transfer_datagram(int sock, const Datagram& datagram, const sockaddr_in& from) {
    if (!matches_transfer(datagram,
                          transfer_id_,
                          expected_sender_,
                          expected_receiver_,
                          object_type_) ||
        (receiving_ && !same_address(from, peer_address_))) {
        return;
    }

    if (done_) {
        if (datagram.type == DatagramType::End) {
            send_ack(sock, datagram.seq);
        }
        return;
    }

    if (datagram.type == DatagramType::Start) {
        if (receiving_) {
            send_ack(sock, last_ack());
            return;
        }

        receiving_ = true;
        peer_address_ = from;
        expected_seq_ = 0;
        object_size_ = datagram.object_size;
        total_fragments_ = fragment_count(object_size_);
        data_.clear();
        send_ack(sock, ACK_NONE);
        return;
    }

    if (!receiving_) {
        return;
    }

    if (datagram.type == DatagramType::Data) {
        if (datagram.seq == expected_seq_) {
            data_.insert(data_.end(), datagram.payload.begin(), datagram.payload.end());
            send_ack(sock, expected_seq_++);
        } else {
            send_ack(sock, last_ack());
        }
        return;
    }

    if (datagram.type == DatagramType::End) {
        if (expected_seq_ == total_fragments_ &&
            datagram.seq == total_fragments_ &&
            data_.size() == object_size_) {
            send_ack(sock, datagram.seq);
            done_ = true;
            wait_until_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(TIMEOUT_MS*3);
        } else {
            send_ack(sock, last_ack());
        }
    }
}

bool RdtReceiver::receiving() const {
    return receiving_;
}

bool RdtReceiver::done() const {
    return done_ && std::chrono::steady_clock::now() >= wait_until_;
}

std::vector<uint8_t> RdtReceiver::take_data() {
    return std::move(data_);
}

const sockaddr_in& RdtReceiver::peer_address() const {
    return peer_address_;
}

bool send_object(int sock, const sockaddr_in& address, const std::vector<uint8_t>& object,
                 uint32_t transfer_id, uint16_t sender_id, uint16_t receiver_id,
                 ObjectType object_type) {

    RdtSender sender(address, object, transfer_id, sender_id, receiver_id, object_type);
    while (!sender.done() && !sender.failed()) {
        sender.pump(sock);

        Datagram datagram;
        sockaddr_in from {};
        bool corrupt = false;
        if (wait_for_datagram(sock, datagram, from, corrupt)) {
            sender.handle_ack(datagram, from);
        }
    }
    return sender.done();
}

bool receive_object(int sock, uint32_t transfer_id, uint16_t expected_sender, uint16_t expected_receiver,
                    ObjectType object_type, std::vector<uint8_t>& object, sockaddr_in& peer_address) {
    
    RdtReceiver receiver(transfer_id, expected_sender, expected_receiver, object_type);
    while (!receiver.done()) {
        Datagram datagram;
        sockaddr_in from {};
        bool corrupt = false;
        if (!wait_for_datagram(sock, datagram, from, corrupt)) {
            if (corrupt) {
                receiver.handle_corrupt(sock, from);
            }
            continue;
        }
        receiver.handle_transfer_datagram(sock, datagram, from);
    }

    object = receiver.take_data();
    peer_address = receiver.peer_address();
    return true;
}

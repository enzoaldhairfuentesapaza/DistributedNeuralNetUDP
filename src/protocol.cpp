#include "protocol.hpp"
#include "debug.hpp"
#include "fault_injection.hpp"

#include <sys/select.h>
#include <sys/socket.h>

#include <algorithm>
#include <utility>

bool send_datagram(int sock, const sockaddr_in& address, const Datagram& datagram) {
    if (datagram.payload.size() > MAX_PAYLOAD) {
        return false;
    }

    // ----------------------------------------------------------------
    // SIMULACION DE FALLAS (solo aplica a datagramas DATA enviados por
    // un worker hacia el master, es decir, GRADIENT_RESULT). Si esta
    // desactivado (FAULT_INJECTION_ENABLED = false) o el datagrama no
    // corresponde a un worker objetivo, estas funciones no hacen nada.
    // ----------------------------------------------------------------
    const uint16_t sender_id = datagram.sender_id;

    if (sender_id >= FIRST_WORKER_ID && sender_id <= LAST_WORKER_ID) {
        // 1) Perdida de paquete: el datagrama se descarta, nunca se envia.
        if (fault_should_drop_packet(sender_id, datagram)) {
            return true;  // se reporta como "enviado" para no romper el
                          // flujo del protocolo; en realidad nunca sale.
        }

        // 2) Paquete tardio: se retiene antes de enviarse.
        if (fault_should_delay_packet(sender_id, datagram)) {
            fault_apply_delay();
        }

        // 3) Dato corrupto: se corrompe el payload DESPUES de serializar
        //    (es decir, despues de que el CRC32 ya fue calculado sobre los
        //    bytes correctos). Asi el CRC que viaja queda desincronizado
        //    del contenido real, y el receptor (que recalcula el CRC sobre
        //    lo que recibe) debe detectar la inconsistencia y rechazarlo.
        if (fault_should_corrupt_packet(sender_id, datagram)) {
            std::vector<uint8_t> corrupt_bytes = serialize_datagram(datagram);
            fault_corrupt_serialized_bytes(corrupt_bytes);

            const ssize_t corrupt_sent = sendto(sock, corrupt_bytes.data(), corrupt_bytes.size(), 0,
                                                reinterpret_cast<const sockaddr*>(&address),
                                                sizeof(address));
            const bool corrupt_ok = corrupt_sent == static_cast<ssize_t>(corrupt_bytes.size());
            if (corrupt_ok) {
                // Reconstruye un Datagram a partir de los bytes REALMENTE
                // enviados (ya corruptos) solo para fines de impresion, de
                // modo que el log muestre el payload alterado tal cual viajo
                // por la red (y no el contenido original/limpio).
                Datagram printed_view = datagram;
                if (corrupt_bytes.size() > HEADER_SIZE) {
                    printed_view.payload.assign(corrupt_bytes.begin() + HEADER_SIZE, corrupt_bytes.end());
                }
                printDatagram(printed_view, /*sent=*/true);
            }
            return corrupt_ok;
        }
    }

    const std::vector<uint8_t> bytes = serialize_datagram(datagram);
    const ssize_t sent = sendto(sock, bytes.data(), bytes.size(), 0,
                                reinterpret_cast<const sockaddr*>(&address),
                                sizeof(address));
    const bool ok = sent == static_cast<ssize_t>(bytes.size());

    if (ok) {
        printDatagram(datagram, /*sent=*/true);
    }

    return ok;
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
        return false;
    }

    printDatagram(datagram, /*sent=*/false);

    return true;
}

namespace {

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

bool wait_for_ack(int sock,
                  const sockaddr_in& address,
                  uint32_t transfer_id,
                  uint16_t expected_sender,
                  uint16_t expected_receiver,
                  uint32_t& ack_value) {
    Datagram datagram;
    sockaddr_in from {};
    bool corrupt = false;
    if (!wait_for_datagram(sock, datagram, from, corrupt) || !same_address(from, address)) {
        return false;
    }

    if (datagram.type != DatagramType::Ack ||
        datagram.transfer_id != transfer_id ||
        datagram.sender_id != expected_sender ||
        datagram.receiver_id != expected_receiver) {
        return false;
    }

    ack_value = datagram.ack;
    return true;
}

bool send_control_with_ack(int sock,
                           const sockaddr_in& address,
                           const Datagram& datagram,
                           uint32_t expected_ack) {
    for (int attempt = 0; attempt <= MAX_RETRIES; ++attempt) {
        if (!send_datagram(sock, address, datagram)) {
            return false;
        }

        uint32_t ack = ACK_NONE;
        if (wait_for_ack(sock,
                         address,
                         datagram.transfer_id,
                         datagram.receiver_id,
                         datagram.sender_id,
                         ack) &&
            ack == expected_ack) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool send_object(int sock,
                 const sockaddr_in& address,
                 const std::vector<uint8_t>& object,
                 uint32_t transfer_id,
                 uint16_t sender_id,
                 uint16_t receiver_id,
                 ObjectType object_type) {
    const uint32_t total_fragments =
        std::max<uint32_t>(1, static_cast<uint32_t>((object.size() + MAX_PAYLOAD - 1) / MAX_PAYLOAD));
    const uint32_t object_size = static_cast<uint32_t>(object.size());

    Datagram start = make_datagram(DatagramType::Start,
                                   transfer_id,
                                   sender_id,
                                   receiver_id,
                                   object_type,
                                   object_size,
                                   total_fragments);

    if (!send_control_with_ack(sock, address, start, ACK_NONE)) {
        return false;
    }

    uint32_t base = 0;
    uint32_t next = 0;
    int retries = 0;

    while (base < total_fragments) {
        while (next < total_fragments && next < base + WINDOW_SIZE) {
            Datagram data = make_data_datagram(object,
                                               transfer_id,
                                               sender_id,
                                               receiver_id,
                                               object_type,
                                               next,
                                               total_fragments);
            if (retries > 0) {
                // Esta no es la primera vez que se intenta enviar este
                // fragmento: es una retransmision tras timeout.
                fault_log_retransmit(sender_id, transfer_id, data.seq);
            }
            if (!send_datagram(sock, address, data)) {
                return false;
            }
            ++next;
        }
        const uint32_t last_sent_seq = next > 0 ? next - 1 : 0;  // ultimo fragmento mandado en este intento de ventana

        uint32_t ack = ACK_NONE;
        if (wait_for_ack(sock, address, transfer_id, receiver_id, sender_id, ack)) {
            if (ack == ACK_NONE) {
                base = 0;
                next = 0;
            } else if (ack >= base && ack < total_fragments) {
                fault_log_recovered(sender_id, transfer_id, ack, ack);
                base = ack + 1;
                retries = 0;
            }
            continue;
        }

        ++retries;
        fault_log_timeout_detected(sender_id, transfer_id, last_sent_seq, retries);
        if (retries > MAX_RETRIES) {
            return false;
        }
        next = base;
    }

    Datagram end = make_datagram(DatagramType::End,
                                 transfer_id,
                                 sender_id,
                                 receiver_id,
                                 object_type,
                                 object_size,
                                 total_fragments);
    end.seq = total_fragments;
    return send_control_with_ack(sock, address, end, total_fragments);
}

bool receive_object(int sock,
                    uint32_t transfer_id,
                    uint16_t expected_sender,
                    uint16_t expected_receiver,
                    ObjectType object_type,
                    std::vector<uint8_t>& object,
                    sockaddr_in& peer_address) {
    bool receiving = false;
    uint32_t expected_seq = 0;
    uint32_t total_fragments = 0;
    uint32_t object_size = 0;
    std::vector<uint8_t> received_data;

    while (true) {
        Datagram datagram;
        sockaddr_in from {};
        bool corrupt = false;
        if (!wait_for_datagram(sock, datagram, from, corrupt)) {
            if (corrupt && (!receiving || same_address(from, peer_address))) {
                const uint32_t last_ack = expected_seq == 0 ? ACK_NONE : expected_seq - 1;
                send_datagram(sock,
                              receiving ? peer_address : from,
                              make_ack_datagram(transfer_id,
                                                expected_receiver,
                                                expected_sender,
                                                last_ack));
            }
            continue;
        }

        if (!matches_transfer(datagram, transfer_id, expected_sender, expected_receiver, object_type)) {
            continue;
        }

        if (!receiving) {
            peer_address = from;
        } else if (!same_address(from, peer_address)) {
            continue;
        }

        if (datagram.type == DatagramType::Start) {
            receiving = true;
            peer_address = from;
            expected_seq = 0;
            total_fragments = datagram.total_fragments;
            object_size = datagram.object_size;
            received_data.clear();
            send_datagram(sock,
                          peer_address,
                          make_ack_datagram(datagram.transfer_id,
                                            datagram.receiver_id,
                                            datagram.sender_id,
                                            ACK_NONE));
            continue;
        }

        if (!receiving) {
            continue;
        }

        if (datagram.type == DatagramType::Data) {
            if (datagram.seq == expected_seq && datagram.fragment == expected_seq) {
                received_data.insert(received_data.end(), datagram.payload.begin(), datagram.payload.end());
                send_datagram(sock,
                              peer_address,
                              make_ack_datagram(datagram.transfer_id,
                                                datagram.receiver_id,
                                                datagram.sender_id,
                                                expected_seq));
                ++expected_seq;
            } else {
                const uint32_t last_ack = expected_seq == 0 ? ACK_NONE : expected_seq - 1;
                send_datagram(sock,
                              peer_address,
                              make_ack_datagram(datagram.transfer_id,
                                                datagram.receiver_id,
                                                datagram.sender_id,
                                                last_ack));
            }
            continue;
        }

        if (datagram.type == DatagramType::End) {
            if (expected_seq == total_fragments &&
                datagram.seq == total_fragments &&
                received_data.size() == object_size) {
                send_datagram(sock,
                              peer_address,
                              make_ack_datagram(datagram.transfer_id,
                                                datagram.receiver_id,
                                                datagram.sender_id,
                                                datagram.seq));
                object = std::move(received_data);
                return true;
            }

            const uint32_t last_ack = expected_seq == 0 ? ACK_NONE : expected_seq - 1;
            send_datagram(sock,
                          peer_address,
                          make_ack_datagram(datagram.transfer_id,
                                            datagram.receiver_id,
                                            datagram.sender_id,
                                            last_ack));
        }
    }
}

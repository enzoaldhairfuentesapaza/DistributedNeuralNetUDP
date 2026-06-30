#ifndef DNN_UDP_FAULT_INJECTION_HPP
#define DNN_UDP_FAULT_INJECTION_HPP

// =====================================================================
// MODULO DE SIMULACION DE FALLAS DE RED (solo para pruebas del worker)
// =====================================================================
//
// Este archivo es completamente independiente del protocolo real.
// Se activa/desactiva con un solo switch (FAULT_INJECTION_ENABLED),
// igual que DEBUG_DATAGRAMS en debug.hpp.
//
// Cada worker dispara UNA falla distinta, una sola vez, sobre uno de
// los ULTIMOS fragmentos DATA del envio de GRADIENT_RESULT (worker -> master):
//
//   Worker 1 -> PERDIDA DE PAQUETE   (el datagrama nunca se envia;
//                                     el master lo detecta por timeout
//                                     y el worker reenvia por reintento)
//   Worker 2 -> PAQUETE TARDIO       (se retiene el envio mas tiempo
//                                     que TIMEOUT_MS; el master ya habra
//                                     reintentado para cuando llega)
//   Worker 3 -> DATO CORRUPTO        (se corrompe el payload ANTES de
//                                     calcular el CRC32, asi el receptor
//                                     detecta el fallo de checksum)
//   Worker 4+ -> comportamiento normal (sin fallas)
//
// Para desactivar TODAS las simulaciones, basta con poner
// FAULT_INJECTION_ENABLED en false.
// =====================================================================

#include "datagram.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

// --------------------------------------------------------------------
// SWITCH GLOBAL: true = simulaciones activas | false = desactivadas
// --------------------------------------------------------------------
constexpr bool FAULT_INJECTION_ENABLED = true;

// --------------------------------------------------------------------
// SWITCH DE NARRACION: true = ademas del datagrama crudo (ENVIADO/
// RECIBIDO), se imprime una linea corta tipo "seq=5 ack=4 ..." cada vez
// que el protocolo hace algo relevante en respuesta a una falla forzada
// (detecta timeout, retransmite, etc). false = solo se ven las 3 lineas
// [SIMULACION] de aviso, sin narracion adicional.
// --------------------------------------------------------------------
constexpr bool FAULT_NARRATE_PROTOCOL = true;

// IDs de worker asignados a cada tipo de falla (editable)
constexpr uint16_t FAULT_WORKER_PACKET_LOSS = 1;
constexpr uint16_t FAULT_WORKER_LATE_PACKET = 2;
constexpr uint16_t FAULT_WORKER_CORRUPTION  = 3;

// Cuanto tiempo (ms) se retiene el paquete "tardio" antes de enviarlo.
// Debe ser mayor que TIMEOUT_MS (500 ms) para forzar al menos un timeout
// real en el master.
constexpr int FAULT_LATE_DELAY_MS = 900;

// Colores ANSI exclusivos para las simulaciones de fallas
// (distintos de los usados en debug.hpp para ENVIADO/RECIBIDO)
#define FAULT_RESET   "\033[0m"
#define FAULT_LOSS    "\033[38;5;208m"   // naranja -> perdida de paquete
#define FAULT_LATE    "\033[38;5;213m"   // violeta/rosa -> paquete tardio
#define FAULT_CORRUPT "\033[1;31m"       // rojo brillante -> dato corrupto

// --------------------------------------------------------------------
// Estado interno: cada falla se dispara UNA sola vez por ejecucion
// --------------------------------------------------------------------
struct FaultInjectionState {
    bool loss_triggered = false;
    bool late_triggered = false;
    bool corrupt_triggered = false;

    // Para narrar el "antes/despues" se guarda el momento en que se forzo
    // cada falla y el seq involucrado, asi luego se puede medir e indicar
    // cuanto tardo el protocolo en reaccionar (timeout, retransmision).
    std::chrono::steady_clock::time_point loss_started_at {};
    std::chrono::steady_clock::time_point late_started_at {};
    std::chrono::steady_clock::time_point corrupt_started_at {};
    uint32_t loss_seq = 0;
    uint32_t late_seq = 0;
    uint32_t corrupt_seq = 0;
    bool loss_recovered = false;
    bool late_recovered = false;
    bool corrupt_recovered = false;
};

inline FaultInjectionState& fault_state() {
    static FaultInjectionState state;
    return state;
}

// ---------------------------------------------------------------
// NARRACION: imprime una linea corta y legible (sin tener que
// descifrar el header de 36 digitos) cada vez que el protocolo
// reacciona a una falla forzada. Se activa/desactiva con
// FAULT_NARRATE_PROTOCOL, independiente de FAULT_INJECTION_ENABLED.
// ---------------------------------------------------------------
inline void fault_log(const char* color, const std::string& line) {
    if (!FAULT_NARRATE_PROTOCOL) return;
    std::cout << color << "    >> " << line << FAULT_RESET << std::endl;
}

// Linea de inicio de cada simulacion: deja explicito el seq/ack/transfer_id
// del datagrama afectado, ANTES de que el protocolo reaccione.
inline void fault_log_start(const char* color,
                            const char* fault_name,
                            uint16_t worker_id,
                            const Datagram& datagram) {
    if (!FAULT_NARRATE_PROTOCOL) return;
    std::cout
        << color
        << "    >> INICIO " << fault_name
        << " | worker_id=" << worker_id
        << " seq=" << datagram.seq
        << " fragment=" << datagram.fragment << "/" << datagram.total_fragments
        << " transfer_id=" << datagram.transfer_id
        << " ack_esperado=NINGUNO (recien se envia)"
        << FAULT_RESET
        << std::endl;
}

// Linea que marca cuanto tiempo paso desde que se forzo la falla hasta
// el evento de reaccion del protocolo (timeout, reenvio, recuperacion).
inline double fault_elapsed_ms(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

// Decide si el fragmento actual es "uno de los ultimos" del envio.
// Se usa el ultimo fragmento DATA antes del END (index == total_fragments - 1).
inline bool is_last_data_fragment(const Datagram& datagram) {
    return datagram.type == DatagramType::Data &&
           datagram.total_fragments > 0 &&
           datagram.fragment == datagram.total_fragments - 1;
}

// ---------------------------------------------------------------
// 1) PERDIDA DE PAQUETE
// ---------------------------------------------------------------
// Se llama ANTES de enviar. Si retorna true, el datagrama NO se envia
// (se simula que se perdio en la red). El protocolo, al no recibir ACK,
// debe reintentar por timeout -- comportamiento real de RDT sobre UDP.
inline bool fault_should_drop_packet(uint16_t worker_id, const Datagram& datagram) {
    if (!FAULT_INJECTION_ENABLED) return false;
    if (worker_id != FAULT_WORKER_PACKET_LOSS) return false;
    if (!is_last_data_fragment(datagram)) return false;

    FaultInjectionState& state = fault_state();
    if (state.loss_triggered) return false;

    state.loss_triggered = true;
    state.loss_started_at = std::chrono::steady_clock::now();
    state.loss_seq = datagram.seq;

    std::cout
        << FAULT_LOSS
        << "[SIMULACION] Worker " << worker_id
        << ": PERDIDA DE PAQUETE forzada -> fragmento "
        << datagram.fragment << "/" << datagram.total_fragments
        << " (transfer_id=" << datagram.transfer_id
        << ") sera descartado, no llegara al master."
        << FAULT_RESET
        << std::endl;

    fault_log_start(FAULT_LOSS, "PERDIDA DE PAQUETE", worker_id, datagram);
    fault_log(FAULT_LOSS,
             "el datagrama NO sale por el socket (descartado en software); "
             "el worker debe esperar TIMEOUT_MS=" + std::to_string(TIMEOUT_MS) +
             " ms sin recibir ack=" + std::to_string(datagram.seq) +
             " antes de reintentar.");

    return true;
}

// ---------------------------------------------------------------
// 2) PAQUETE TARDIO
// ---------------------------------------------------------------
// Se llama ANTES de enviar. Si retorna true, el llamador debe dormir
// FAULT_LATE_DELAY_MS antes de hacer el sendto() real, simulando una
// demora de red mayor al timeout del protocolo (TIMEOUT_MS).
inline bool fault_should_delay_packet(uint16_t worker_id, const Datagram& datagram) {
    if (!FAULT_INJECTION_ENABLED) return false;
    if (worker_id != FAULT_WORKER_LATE_PACKET) return false;
    if (!is_last_data_fragment(datagram)) return false;

    FaultInjectionState& state = fault_state();
    if (state.late_triggered) return false;

    state.late_triggered = true;
    state.late_started_at = std::chrono::steady_clock::now();
    state.late_seq = datagram.seq;

    std::cout
        << FAULT_LATE
        << "[SIMULACION] Worker " << worker_id
        << ": PAQUETE TARDIO forzado -> fragmento "
        << datagram.fragment << "/" << datagram.total_fragments
        << " (transfer_id=" << datagram.transfer_id
        << ") se retiene " << FAULT_LATE_DELAY_MS
        << " ms (> TIMEOUT_MS) antes de enviarse."
        << FAULT_RESET
        << std::endl;

    fault_log_start(FAULT_LATE, "PAQUETE TARDIO", worker_id, datagram);
    fault_log(FAULT_LATE,
             "seq=" + std::to_string(datagram.seq) +
             " se retiene " + std::to_string(FAULT_LATE_DELAY_MS) +
             " ms (TIMEOUT_MS=" + std::to_string(TIMEOUT_MS) +
             " ms), por lo que el receptor ya habra agotado su espera "
             "antes de que el dato realmente llegue.");

    return true;
}

inline void fault_apply_delay() {
    std::this_thread::sleep_for(std::chrono::milliseconds(FAULT_LATE_DELAY_MS));
}

// ---------------------------------------------------------------
// 3) DATO CORRUPTO
// ---------------------------------------------------------------
// Se llama ANTES de serializar/enviar, solo para decidir si corresponde
// y para imprimir el aviso. NO modifica el datagrama (eso se hace
// despues, sobre los bytes ya serializados, ver fault_corrupt_serialized_bytes).
inline bool fault_should_corrupt_packet(uint16_t worker_id, const Datagram& datagram) {
    if (!FAULT_INJECTION_ENABLED) return false;
    if (worker_id != FAULT_WORKER_CORRUPTION) return false;
    if (!is_last_data_fragment(datagram)) return false;
    if (datagram.payload.empty()) return false;

    FaultInjectionState& state = fault_state();
    if (state.corrupt_triggered) return false;

    state.corrupt_triggered = true;
    state.corrupt_started_at = std::chrono::steady_clock::now();
    state.corrupt_seq = datagram.seq;

    std::cout
        << FAULT_CORRUPT
        << "[SIMULACION] Worker " << worker_id
        << ": DATO CORRUPTO forzado -> fragmento "
        << datagram.fragment << "/" << datagram.total_fragments
        << " (transfer_id=" << datagram.transfer_id
        << ") se alterara un byte del payload YA SERIALIZADO"
        << " (despues de calcular el CRC32), para que el checksum"
        << " quede desincronizado del contenido real;"
        << " el receptor debe rechazarlo al recalcular el CRC."
        << FAULT_RESET
        << std::endl;

    fault_log_start(FAULT_CORRUPT, "DATO CORRUPTO", worker_id, datagram);
    fault_log(FAULT_CORRUPT,
             "seq=" + std::to_string(datagram.seq) +
             " se envia con CRC32 invalido respecto al payload real; "
             "el receptor debe descartarlo y responder con el ultimo "
             "ack confirmado, NO con ack=" + std::to_string(datagram.seq) + ".");

    return true;
}

// Corrompe un byte dentro de la zona de PAYLOAD del buffer ya
// serializado (es decir, despues de HEADER_SIZE), sin tocar los 4 bytes
// del campo CRC (offset 32..35). Como el CRC fue calculado ANTES de
// esta alteracion, queda desincronizado del contenido real: el receptor,
// al recalcular el CRC sobre los bytes que efectivamente recibe, debe
// obtener un valor distinto al que viaja en el datagrama y descartarlo.
inline void fault_corrupt_serialized_bytes(std::vector<uint8_t>& bytes) {
    if (bytes.size() <= HEADER_SIZE) return;  // no hay payload que corromper
    bytes[HEADER_SIZE] ^= 0xFF;
}

// ---------------------------------------------------------------
// NARRACION DE LA REACCION DEL PROTOCOLO
// ---------------------------------------------------------------
// Estas funciones se llaman desde protocol.cpp, en los puntos donde el
// protocolo YA esta reaccionando a la falla (recibe un ack que no avanza,
// agota el timeout, o retransmite). Solo imprimen si corresponde al
// worker/seq que efectivamente se vio afectado por una falla activa y
// aun no marcada como recuperada, para no ensuciar el log con lineas de
// narracion en transferencias que van normales.

// Se llama cuando, tras enviar el datagrama, NO llega el ack esperado
// dentro de TIMEOUT_MS (entra a la rama de timeout/reintento de
// wait_for_ack / send_control_with_ack en protocol.cpp).
inline void fault_log_timeout_detected(uint16_t worker_id,
                                       uint32_t transfer_id,
                                       uint32_t seq,
                                       int attempt) {
    if (!FAULT_NARRATE_PROTOCOL) return;
    FaultInjectionState& state = fault_state();

    const char* color = nullptr;
    const char* tag = nullptr;
    double elapsed = 0.0;

    if (worker_id == FAULT_WORKER_PACKET_LOSS && state.loss_triggered &&
        seq == state.loss_seq && !state.loss_recovered) {
        color = FAULT_LOSS; tag = "PERDIDA DE PAQUETE";
        elapsed = fault_elapsed_ms(state.loss_started_at);
    } else if (worker_id == FAULT_WORKER_LATE_PACKET && state.late_triggered &&
               seq == state.late_seq && !state.late_recovered) {
        color = FAULT_LATE; tag = "PAQUETE TARDIO";
        elapsed = fault_elapsed_ms(state.late_started_at);
    } else if (worker_id == FAULT_WORKER_CORRUPTION && state.corrupt_triggered &&
               seq == state.corrupt_seq && !state.corrupt_recovered) {
        color = FAULT_CORRUPT; tag = "DATO CORRUPTO";
        elapsed = fault_elapsed_ms(state.corrupt_started_at);
    } else {
        return;  // no relacionado a ninguna falla activa
    }

    std::cout
        << color
        << "    >> TIMEOUT detectado (" << tag << ") | worker_id=" << worker_id
        << " seq=" << seq
        << " transfer_id=" << transfer_id
        << " intento=" << attempt << "/" << (MAX_RETRIES + 1)
        << " tiempo_transcurrido=" << static_cast<int>(elapsed) << " ms"
        << " (TIMEOUT_MS=" << TIMEOUT_MS << " ms)"
        << " -> el protocolo va a reenviar."
        << FAULT_RESET
        << std::endl;
}

// Se llama justo antes de retransmitir un datagrama (mismo seq que ya se
// habia enviado antes y no fue confirmado).
inline void fault_log_retransmit(uint16_t worker_id,
                                 uint32_t transfer_id,
                                 uint32_t seq) {
    if (!FAULT_NARRATE_PROTOCOL) return;
    FaultInjectionState& state = fault_state();

    const char* color = nullptr;
    const char* tag = nullptr;

    if (worker_id == FAULT_WORKER_PACKET_LOSS && state.loss_triggered &&
        seq == state.loss_seq && !state.loss_recovered) {
        color = FAULT_LOSS; tag = "PERDIDA DE PAQUETE";
    } else if (worker_id == FAULT_WORKER_LATE_PACKET && state.late_triggered &&
               seq == state.late_seq && !state.late_recovered) {
        color = FAULT_LATE; tag = "PAQUETE TARDIO";
    } else if (worker_id == FAULT_WORKER_CORRUPTION && state.corrupt_triggered &&
               seq == state.corrupt_seq && !state.corrupt_recovered) {
        color = FAULT_CORRUPT; tag = "DATO CORRUPTO";
    } else {
        return;
    }

    std::cout
        << color
        << "    >> RETRANSMISION (" << tag << ") | worker_id=" << worker_id
        << " seq=" << seq
        << " transfer_id=" << transfer_id
        << " -> reenviando el mismo fragmento, esta vez sin la falla."
        << FAULT_RESET
        << std::endl;
}

// Se llama cuando el ack que confirma definitivamente ese seq llega
// (es decir, el protocolo se recupero de la falla forzada).
inline void fault_log_recovered(uint16_t worker_id,
                                uint32_t transfer_id,
                                uint32_t seq,
                                uint32_t ack_received) {
    if (!FAULT_NARRATE_PROTOCOL) return;
    FaultInjectionState& state = fault_state();

    const char* color = nullptr;
    const char* tag = nullptr;
    double elapsed = 0.0;
    bool* recovered_flag = nullptr;

    if (worker_id == FAULT_WORKER_PACKET_LOSS && state.loss_triggered &&
        seq == state.loss_seq && !state.loss_recovered) {
        color = FAULT_LOSS; tag = "PERDIDA DE PAQUETE";
        elapsed = fault_elapsed_ms(state.loss_started_at);
        recovered_flag = &state.loss_recovered;
    } else if (worker_id == FAULT_WORKER_LATE_PACKET && state.late_triggered &&
               seq == state.late_seq && !state.late_recovered) {
        color = FAULT_LATE; tag = "PAQUETE TARDIO";
        elapsed = fault_elapsed_ms(state.late_started_at);
        recovered_flag = &state.late_recovered;
    } else if (worker_id == FAULT_WORKER_CORRUPTION && state.corrupt_triggered &&
               seq == state.corrupt_seq && !state.corrupt_recovered) {
        color = FAULT_CORRUPT; tag = "DATO CORRUPTO";
        elapsed = fault_elapsed_ms(state.corrupt_started_at);
        recovered_flag = &state.corrupt_recovered;
    } else {
        return;
    }

    *recovered_flag = true;

    std::cout
        << color
        << "    >> RECUPERADO (" << tag << ") | worker_id=" << worker_id
        << " seq=" << seq
        << " ack_recibido=" << ack_received
        << " transfer_id=" << transfer_id
        << " tiempo_total_desde_la_falla=" << static_cast<int>(elapsed) << " ms"
        << " -> el protocolo confirmo este fragmento correctamente."
        << FAULT_RESET
        << std::endl;
}

#endif

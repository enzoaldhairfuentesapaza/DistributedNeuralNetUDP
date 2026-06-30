#ifndef DNN_UDP_FAULT_INJECTION_HPP
#define DNN_UDP_FAULT_INJECTION_HPP

// =====================================================================
// MODULO DE SIMULACION DE FALLAS DE RED (solo para pruebas del worker)
// =====================================================================
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
// Para desactivar todas las simulaciones de falla, cambiar
// FAULT_INJECTION_ENABLED en false.

#include "datagram.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>


constexpr bool FAULT_INJECTION_ENABLED = true;

constexpr uint16_t FAULT_WORKER_PACKET_LOSS = 1;
constexpr uint16_t FAULT_WORKER_LATE_PACKET = 2;
constexpr uint16_t FAULT_WORKER_CORRUPTION  = 3;

constexpr int FAULT_LATE_DELAY_MS = 900;

#define FAULT_RESET   "\033[0m"
#define FAULT_LOSS    "\033[38;5;208m"   // naranja -> perdida de paquete
#define FAULT_LATE    "\033[38;5;213m"   // violeta/rosa -> paquete tardio
#define FAULT_CORRUPT "\033[1;31m"       // rojo brillante -> dato corrupto


struct FaultInjectionState {
    bool loss_triggered = false;
    bool late_triggered = false;
    bool corrupt_triggered = false;
};

inline FaultInjectionState& fault_state() {
    static FaultInjectionState state;
    return state;
}


inline bool is_last_data_fragment(const Datagram& datagram) {
    return datagram.type == DatagramType::Data &&
           datagram.total_fragments > 0 &&
           datagram.fragment == datagram.total_fragments - 1;
}

// 1) PERDIDA DE PAQUETE
inline bool fault_should_drop_packet(uint16_t worker_id, const Datagram& datagram) {
    if (!FAULT_INJECTION_ENABLED) return false;
    if (worker_id != FAULT_WORKER_PACKET_LOSS) return false;
    if (!is_last_data_fragment(datagram)) return false;

    FaultInjectionState& state = fault_state();
    if (state.loss_triggered) return false;

    state.loss_triggered = true;
    std::cout
        << FAULT_LOSS
        << "[SIMULACION] Worker " << worker_id
        << ": PERDIDA DE PAQUETE forzada -> fragmento "
        << datagram.fragment << "/" << datagram.total_fragments
        << " (transfer_id=" << datagram.transfer_id
        << ") sera descartado, no llegara al master."
        << FAULT_RESET
        << std::endl;

    return true;
}

// 2) PAQUETE TARDIO
inline bool fault_should_delay_packet(uint16_t worker_id, const Datagram& datagram) {
    if (!FAULT_INJECTION_ENABLED) return false;
    if (worker_id != FAULT_WORKER_LATE_PACKET) return false;
    if (!is_last_data_fragment(datagram)) return false;

    FaultInjectionState& state = fault_state();
    if (state.late_triggered) return false;

    state.late_triggered = true;
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

    return true;
}

inline void fault_apply_delay() {
    std::this_thread::sleep_for(std::chrono::milliseconds(FAULT_LATE_DELAY_MS));
}

// 3) DATO CORRUPTO
inline bool fault_should_corrupt_packet(uint16_t worker_id, Datagram& datagram) {
    if (!FAULT_INJECTION_ENABLED) return false;
    if (worker_id != FAULT_WORKER_CORRUPTION) return false;
    if (!is_last_data_fragment(datagram)) return false;
    if (datagram.payload.empty()) return false;

    FaultInjectionState& state = fault_state();
    if (state.corrupt_triggered) return false;

    state.corrupt_triggered = true;

    // Corrompe el primer byte del payload
    datagram.payload[0] ^= 0xFF;

    std::cout
        << FAULT_CORRUPT
        << "[SIMULACION] Worker " << worker_id
        << ": DATO CORRUPTO forzado -> fragmento "
        << datagram.fragment << "/" << datagram.total_fragments
        << " (transfer_id=" << datagram.transfer_id
        << ") payload alterado antes de calcular el CRC32;"
        << " el receptor debe rechazarlo por checksum invalido."
        << FAULT_RESET
        << std::endl;

    return true;
}

#endif

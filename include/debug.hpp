#pragma once

#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <string>
#include "protocol.hpp"

// Muestra mensajes generales del Master
constexpr bool DEBUG_MASTER = true;

// Muestra mensajes generales de los Workers
constexpr bool DEBUG_WORKER = true;

// Imprime el contenido de los datagramas
constexpr bool DEBUG_DATAGRAMS = true;

// true  -> imprime TODOS los datagramas | false -> imprime solo el primero y el último de cada transferencia
constexpr bool DEBUG_ALL_DATAGRAMS = true;

// Ancho fijo (en caracteres) del datagrama impreso, igual que en server.cpp/client.cpp
constexpr size_t PRINTED_DATAGRAM_WIDTH = 500;

inline bool shouldPrintFragment(
    uint32_t fragment,
    uint32_t total)
{
    if (DEBUG_ALL_DATAGRAMS)
        return true;

    if (total <= 2)
        return true;

    return fragment == 0 ||
           fragment == total - 1;
}

#define RESET   "\033[0m"

#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"
#define WHITE   "\033[37m"

#define BOLD    "\033[1m"



inline std::string byteToString(uint8_t b)
{
    std::ostringstream oss;

    oss
        << std::setw(3)
        << std::setfill('0')
        << (int)b;

    return oss.str();
}


inline std::string fixedNumber(uint32_t value, int digits)
{
    std::ostringstream oss;
    oss << std::setw(digits)
        << std::setfill('0')
        << value;
    return oss.str();
}

inline void printWorkerHeader(int id)
{
    if (!DEBUG_WORKER) return;

    std::cout
        << MAGENTA
        << "\nWORKER "
        << id
        << RESET
        << '\n';
}

inline void printMasterHeader()
{
    if (!DEBUG_MASTER) return;

    std::cout
        << YELLOW
        << "\nMASTER"
        << RESET
        << '\n';
}

// Encabezado "visual" del datagrama (36 caracteres), con todos los campos
// del protocolo concatenados sin separadores, en ancho fijo.
inline std::string visualHeader(const Datagram& d)
{
    std::string s;

    s += fixedNumber((uint32_t)d.type, 2);
    s += fixedNumber(d.seq, 4);
    s += fixedNumber(d.ack, 4);
    s += fixedNumber(d.transfer_id, 4);
    s += fixedNumber(d.sender_id, 2);
    s += fixedNumber(d.receiver_id, 2);
    s += fixedNumber((uint32_t)d.object_type, 2);
    s += fixedNumber(d.object_size, 4);
    s += fixedNumber(d.fragment, 4);
    s += fixedNumber(d.total_fragments, 4);
    s += fixedNumber((uint32_t)d.payload.size(), 4);

    return s;
}

// true  -> el relleno se imprime completo, '#' repetido hasta llenar
//          PRINTED_DATAGRAM_WIDTH (comportamiento original)
// false -> el relleno se compacta a "#...#" (solo unos pocos '#', "...",
//          y otro '#'), util cuando el datagrama es mucho mas chico que
//          el ancho fijo y no quieres ver una fila larga de '#'.
constexpr bool VERBOSE_PADDING = false;

// Construye la representación de exactamente PRINTED_DATAGRAM_WIDTH (500)
// caracteres: header + payload (imprimible, '.' para no imprimibles) +
// relleno con '#' hasta completar el ancho fijo cuando el datagrama
// (o su último fragmento) no llena el espacio disponible.
inline std::string buildPrintableDatagram(const Datagram& datagram)
{
    std::string packet = visualHeader(datagram);

    for (uint8_t b : datagram.payload)
    {
        if (b >= 32 && b <= 126)
            packet.push_back((char)b);
        else
            packet.push_back('.');
    }

    if (packet.size() < PRINTED_DATAGRAM_WIDTH)
    {
        const size_t padding_needed = PRINTED_DATAGRAM_WIDTH - packet.size();

        if (VERBOSE_PADDING || padding_needed <= 5)
        {
            // Relleno completo (o ya es tan corto que no vale la pena
            // compactarlo): comportamiento original.
            packet.append(padding_needed, '#');
        }
        else
        {
            // Relleno compacto: "#...#" en vez de '#' repetido.
            packet += "#...#";
        }
    }
    else if (packet.size() > PRINTED_DATAGRAM_WIDTH)
    {
        packet.resize(PRINTED_DATAGRAM_WIDTH);
    }

    return packet;
}

// Imprime un datagrama en formato:
// ENVIADO>>>...500 caracteres...<<<
// RECIBIDO>>>...500 caracteres...<<<
//
// sent = true  -> se está enviando (ENVIADO)
// sent = false -> se está recibiendo (RECIBIDO)
//
// Respeta DEBUG_DATAGRAMS y DEBUG_ALL_DATAGRAMS / shouldPrintFragment:
// si DEBUG_ALL_DATAGRAMS es false, solo se imprime el primer y el último
// fragmento DATA de cada transferencia. Los datagramas de control
// (START/END/ACK) siempre se imprimen, ya que marcan el inicio/fin de la
// transferencia y no son fragmentos DATA.
inline void printDatagram(
    const Datagram& datagram,
    bool sent)
{
    if (!DEBUG_DATAGRAMS)
        return;

    bool shouldPrint = true;
    if (datagram.type == DatagramType::Data)
    {
        shouldPrint = shouldPrintFragment(datagram.fragment, datagram.total_fragments);
    }

    if (!shouldPrint)
        return;

    const std::string label = sent ? "ENVIADO" : "RECIBIDO";
    const std::string color = sent ? GREEN : BLUE;

    const std::string packet = buildPrintableDatagram(datagram);

    std::cout
        << color
        << label
        << ">>>"
        << RESET
        << packet
        << color
        << "<<<"
        << RESET
        << "\n";
}

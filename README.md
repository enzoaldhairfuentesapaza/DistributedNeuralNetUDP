# Reliable UDP Master-Worker Transport

This project implements reliable object transfer over UDP between one master and
ten workers.

The transport treats every object as opaque bytes. It only distinguishes the
direction of the payload:

- `WORKER_PAYLOAD`: sent by the master to one worker.
- `MASTER_PAYLOAD`: sent by one worker back to the master.

## Architecture

```text
MasterTransport
    |
    +-- thread + UDP socket --> worker 1
    +-- thread + UDP socket --> worker 2
    |
    +-- thread + UDP socket --> worker 10
```

Each master thread owns one socket and communicates with exactly one worker.
Sessions do not share socket state or payload buffers.

Workers use IDs `1..10` and listen on ports `9001..9010`. The master uses ID
`0`.

## Reliable Transfer

Each object is divided into datagrams with:

```text
Maximum datagram size: 512 bytes
Header size:            36 bytes
Maximum payload:       476 bytes
```

The protocol uses:

- `START`, `DATA`, `ACK`, and `END` datagrams.
- Go-Back-N with a window of `8`.
- Cumulative ACKs.
- CRC32 validation.
- A fixed `500 ms` retransmission timeout.
- Up to `5` retransmissions.

The master waits up to `60 seconds` after completing a `WORKER_PAYLOAD`
transfer for the worker to start sending its `MASTER_PAYLOAD`.

## Public API

```cpp
API_RDT_UDP::MasterTransport master;

std::map<uint16_t, API_RDT_UDP::Bytes> worker_payloads;
std::map<uint16_t, API_RDT_UDP::Bytes> master_payloads =
    master.exchange(worker_payloads);
```

```cpp
API_RDT_UDP::WorkerTransport worker(worker_id);

API_RDT_UDP::Bytes worker_payload = worker.receive_worker_payload();
worker.send_master_payload(master_payload);
```

`MasterTransport::exchange()` requires payloads for exactly ten workers. It
waits for every session to finish and reports all session errors together.

## Files

- `datagram.hpp`: datagram format, serialization, parsing, and CRC32.
- `protocol.hpp` / `protocol.cpp`: generic reliable object transfer.
- `API_RDT_UDP.hpp` / `API_RDT_UDP.cpp`: master and worker API.
- `master.cpp` / `worker.cpp`: command-line test programs.
- `ARQUITECTURA_RDT_UDP.md`: implementation details.

## Build

```bash
make clean
make
```

The build creates:

- `libAPI_RDT_UDP.a`
- `master`
- `worker`

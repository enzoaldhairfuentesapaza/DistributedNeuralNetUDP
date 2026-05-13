# DistributedNeuralNetUDP

Distributed Neural Network (DNN) project developed using Python and C++ with a custom UDP-based Reliable Data Transfer (RDT) protocol.

---

# Overview

This project implements a distributed neural network system where neural processing is executed in parallel across multiple distributed nodes using UDP communication.

The neural network logic is implemented in Python, while the distributed communication layer and networking protocol are implemented in C++.

The system uses a custom RDT protocol over UDP including:

- ACK packets
- Sequence numbers
- Timeout handling
- Corrupted datagram detection using hashes
- Lost datagram recovery
- Retransmission system

---

# Main Idea

The project distributes matrix computations of a neural network across multiple slave nodes.

Each slave node processes part of the computation and sends results back to the main server.

The main server:
1. Collects partial matrices
2. Computes the average of resulting weights
3. Updates the global weights
4. Synchronizes updated weights with Python neural modules

---

# System Architecture

```text
                +----------------------------------+
                |          MAIN SERVER             |
                |        C++ UDP Coordinator       |
                +----------------+-----------------+
                                 |
        -------------------------------------------------------
        |                        |                           |
+---------------+      +---------------+       +---------------+
| Slave Node #1 |      | Slave Node #2 |       | Slave Node #3 |
| C++ UDP RDT   |      | C++ UDP RDT   |       | C++ UDP RDT   |
+-------+-------+      +-------+-------+       +-------+-------+
        |                      |                       |
        ------------------------------------------------
                                 |
                         Python Neural Network
```

---

# Technologies

## Languages
- Python
- C++

## Networking
- UDP sockets
- Custom RDT protocol

## AI Concepts
- Distributed Neural Networks
- Parallel matrix processing
- Weight synchronization
- Gradient averaging

---

# Features

## Neural Network
- 3-layer neural network
- Minimum 200 neurons per layer
- Matrix-based computation
- Forward propagation
- Weight updating

## Distributed Protocol
- UDP communication
- Reliable Data Transfer (RDT)
- Fixed timeout strategy
- Packet retransmission
- Sequence numbering
- ACK responses
- Datagram integrity verification using hashes

## Error Handling
- Corrupted datagram detection
- Lost packet recovery
- Timeout-based retransmission

---

# Custom UDP Protocol

Each datagram includes:

```text
| SEQ# | TYPE | HASH | DATA |
```

## Packet Types
- DATA
- ACK
- RETRANSMIT
- UPDATE

## Reliability Mechanisms
- Timeout detection
- Packet retransmission
- Integrity validation
- Sequence synchronization

---

# Project Structure

```text
DNN-UDP-Distributed/
│
├── docs/
│   ├── protocol_documentation/
│   ├── technical_report/
│   ├── architecture/
│   └── research_timeout/
│
├── demo/
│   ├── screenshots/
│   ├── videos/
│   └── execution_examples/
│
├── cpp/
│   ├── main_server/
│   ├── slave_server/
│   ├── protocol/
│   └── udp_rdt/
│
├── python/
│   ├── neural_network/
│   ├── matrix_processing/
│   └── training/
│
└── README.md
```

---

# Timeout Strategy

The timeout value is fixed and selected after network behavior research and testing.

The timeout mechanism is responsible for:
- Detecting lost packets
- Triggering retransmissions
- Maintaining synchronization

---

# Documentation Included

## Protocol Documentation
Complete explanation of:
- Datagram format
- ACK system
- Sequence control
- Error handling
- Timeout mechanism

## Technical Report
Small academic report including:
- Objectives
- Architecture
- Methodology
- Results
- Conclusions

## Demo
Includes:
- Execution examples
- Screenshots
- Demonstration videos

---

# Objectives

- Implement a Distributed Neural Network (DNN)
- Develop a custom UDP-based RDT protocol
- Apply distributed processing concepts
- Synchronize neural network weights across nodes
- Integrate Python AI modules with C++ networking systems

---

# Team Members

- Enzo Aldhair Fuentes Apaza

---

# Future Improvements

- Dynamic timeout calculation
- GPU acceleration
- Adaptive load balancing
- Multi-machine deployment
- Real-time monitoring dashboard
- Distributed training optimization

---

# Academic Purpose

This project was developed for educational purposes in:
- Computer Networks
- Distributed Systems
- Artificial Intelligence
- Parallel Computing

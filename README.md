# DNN-UDP-Distributed

Distributed Neural Network (DNN) project developed using Python and C++ with a custom UDP-based Reliable Data Transfer (RDT) protocol.

---
# Team Members

- Enzo Aldhair Fuentes Apaza
- Iván Matthías Sardón Medina

---

# Overview

This project implements a distributed neural network system where the neural network is executed in Python while the distributed communication layer is implemented in C++.

The Main Server acts as an intermediary between the Python Neural Network and multiple Slave Servers. The neural network never communicates directly with the slave nodes.

The Main Server:
- Receives matrices and weights from the Python Neural Network
- Distributes workloads across slave nodes
- Collects processed matrices
- Computes the average of returned weights
- Updates the global weights
- Sends updated data back to the Python Neural Network

Communication between the Main Server and Slave Servers is performed using a custom UDP-based RDT protocol.

---

# System Architecture

```text
                  +--------------------------------+
                  |     Python Neural Network      |
                  |      Training / Inference      |
                  +---------------+----------------+
                                  |
                                  |
                         Local Interface/API
                                  |
                                  v
                +----------------------------------+
                |          MAIN SERVER             |
                |     C++ UDP Coordinator          |
                +----------------+-----------------+
                                 |
        -------------------------------------------------------
        |                        |                           |
+---------------+      +---------------+       +---------------+
| Slave Node #1 |      | Slave Node #2 |       | Slave Node #3 |
| C++ UDP RDT   |      | C++ UDP RDT   |       | C++ UDP RDT   |
+---------------+      +---------------+       +---------------+

```

---

# Main Idea

The neural network logic is implemented entirely in Python.

The distributed system is implemented in C++.

The Main Server receives matrix operations from the neural network and distributes the computational workload among multiple slave nodes using UDP communication.

Each slave node processes part of the matrix computation and sends results back to the Main Server.

The Main Server:
1. Receives partial results
2. Merges matrices
3. Computes average weights
4. Updates global parameters
5. Sends updated weights back to Python

---

# Technologies

## Languages
- Python
- C++

## Networking
- UDP sockets
- Custom Reliable Data Transfer (RDT)

## AI Concepts
- Distributed Neural Networks
- Parallel matrix computation
- Weight synchronization
- Distributed processing

---

# Features

## Neural Network
- 3-layer neural network
- Minimum 200 neurons per layer
- Matrix-based operations
- Forward propagation
- Weight updating

## Distributed Communication
- Main Server distributes computations
- Slave nodes process assigned workloads
- Parallel matrix processing
- Result synchronization

## UDP RDT Protocol
- ACK packets
- Sequence numbers
- Fixed timeout
- Packet retransmission
- Corrupted datagram detection using hashes
- Lost datagram recovery

---

# Custom UDP Protocol

Each datagram contains:

```text
| SEQ# | TYPE | HASH | DATA |
```

## Packet Types
- DATA
- ACK
- UPDATE
- RETRANSMIT

## Reliability Features
- Timeout detection
- Retransmission mechanism
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
│   └── timeout_research/
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
│   ├── matrix_operations/
│   └── training/
│
└── README.md
```

---

# Timeout Strategy

The timeout value is fixed and selected based on network testing and research.

The timeout mechanism is responsible for:
- Detecting lost packets
- Triggering retransmissions
- Maintaining synchronization

---

# Documentation Included

## Protocol Documentation
Complete explanation of:
- Datagram structure
- ACK system
- Sequence numbering
- Timeout handling
- Error detection
- Packet retransmission

## Technical Report
Small academic report including:
- Objectives
- Methodology
- System architecture
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
- Apply distributed systems concepts
- Parallelize matrix computations
- Synchronize neural network weights
- Integrate Python AI modules with C++ networking systems

---

# Future Improvements

- Dynamic timeout calculation
- GPU acceleration
- Adaptive load balancing
- Real-time monitoring
- Distributed training optimization

---

# Academic Purpose

This project was developed for educational purposes in:
- Computer Networks
- Distributed Systems
- Artificial Intelligence
- Parallel Computing

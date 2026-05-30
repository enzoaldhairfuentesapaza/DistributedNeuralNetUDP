# DNN-UDP-Distributed

Distributed Neural Network (DNN) project developed using Python and C++ with a custom UDP-based Reliable Data Transfer (RDT) protocol.

---
# Team Members

- Enzo Aldhair Fuentes Apaza
- Iván Matthías Sardón Medina
- Dayanna Milagros Vizcarra Vargas
- Brandon Glem Diaz Rivera

---

# Overview

This project implements a distributed neural network system where the neural network logic runs in Python and the distributed communication layer is implemented in C++.

The Main Server acts as the coordinator between the Python Neural Network and 10 Worker nodes. The Python training code does not communicate directly with the workers; it calls the C++ communication library through a local Python/C++ interface.

The Main Server:
- Selects one global training batch
- Executes the forward pass in the master process
- Builds one serialized `WORK_ASSIGNMENT` object for each worker
- Sends each object through the UDP RDT layer
- Receives one serialized `GRADIENT_RESULT` object from each worker
- Averages the gradients returned by the workers and the master partition
- Updates the global neural network weights in Python

Communication between the Main Server and Worker nodes is performed using a custom UDP-based RDT protocol based on pure Go-Back-N with cumulative ACKs.

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
                | Python + C++ UDP Coordinator     |
                +----------------+-----------------+
                                 |
        -------------------------------------------------------
        |                        |                           |
+---------------+      +---------------+       +---------------+
| Worker Node #1|      | Worker Node #2|       | Worker Node #10|
| C++ UDP RDT   |      | C++ UDP RDT   |       | C++ UDP RDT   |
+---------------+      +---------------+       +---------------+

```

---

# Main Idea

The neural network logic is implemented in Python.

The distributed communication system is implemented in C++ and exposed to Python using `pybind11`.

Training uses one global batch. The master divides that batch into 11 partitions:

- Partition 0 stays in the master
- Partitions 1 to 10 are assigned to the 10 workers

The forward pass is performed only in the master. After the forward pass, the master sends each worker a serialized `WORK_ASSIGNMENT` object containing everything needed to perform local backpropagation correctly, including its batch partition, weights, activations, preactivations, labels, loss derivatives, and metadata.

Each worker:
1. Receives and reconstructs its `WORK_ASSIGNMENT`
2. Deserializes the object
3. Runs local backpropagation without running its own forward pass
4. Builds a `GRADIENT_RESULT` object
5. Sends the object back to the master

The Main Server averages the gradients from the 10 workers and its own local partition, then Python updates the global weights.

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
- Distributed backpropagation
- Gradient averaging
- Weight updating
- Distributed processing

---

# Features

## Neural Network
- 4 hidden layers
- 200 neurons per hidden layer
- One global training batch
- Forward pass only in the master
- Distributed backpropagation across 1 master and 10 workers
- Gradient averaging before global weight update

## Distributed Communication
- Main Server distributes serialized `WORK_ASSIGNMENT` objects
- Worker nodes reconstruct assignments and compute local gradients
- Worker nodes return serialized `GRADIENT_RESULT` objects
- RDT transports opaque serialized objects, not individual tensors
- Tensor semantics are handled by the application layer after deserialization

## UDP RDT Protocol
- Pure Go-Back-N over UDP
- Cumulative ACK datagrams
- Sequence numbers
- Fixed initial timeout
- Datagram retransmission
- CRC32 corruption detection over header and valid payload
- Lost datagram recovery through timeout and retransmission
- `ACK_NONE = 0xFFFFFFFF` for the initial state before any valid DATA datagram is received

---

# Custom UDP Protocol

The RDT layer transports serialized application objects as opaque byte streams. It does not interpret tensors, weights, activations, labels, or gradients.

Each UDP datagram has a maximum size of 512 bytes:

```text
Header:  36 bytes
Payload: up to 476 bytes
```

## Datagram Types
- START
- DATA
- ACK
- END

## Application Object Types
- `WORK_ASSIGNMENT`: sent by the master to one worker
- `GRADIENT_RESULT`: sent by one worker to the master
- `CONTROL`: reserved for future application-level control messages

## Datagram Header Fields
- Datagram type
- Sequence number
- ACK number
- Transfer ID
- Sender ID
- Receiver ID
- Object type
- Total serialized object size
- Fragment number
- Total fragments
- Valid payload size
- CRC32

## Reliability Features
- Go-Back-N sliding window
- Cumulative ACKs
- Timeout detection
- Retransmission mechanism
- CRC32 integrity validation
- Sequence synchronization
- Duplicate and out-of-order datagram detection
- No NACK datagrams

---

# Project Structure

```text
DNN-UDP-Distributed/
│
├── ManualProtocolo.tex
├── Manual del Protocolo.pdf
├── protocol.hpp
├── basicClasificacion.py
├── Dataset of Diabetes.csv
└── README.md
```

The implementation is expected to keep the neural network/training logic in Python and the UDP/RDT communication primitives in C++.

---

# Timeout Strategy

The timeout value is fixed and selected based on network testing and research.

The timeout mechanism is responsible for:
- Detecting lost datagrams
- Triggering retransmissions
- Maintaining synchronization

---

# Documentation Included

## Protocol Documentation
Complete explanation of:
- Datagram structure
- ACK system
- Sequence numbering
- Go-Back-N window behavior
- Timeout handling
- Error detection
- Datagram retransmission
- Object serialization model
- `WORK_ASSIGNMENT` and `GRADIENT_RESULT` application objects

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
- Distribute backpropagation over multiple workers
- Average gradients and update global neural network weights
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

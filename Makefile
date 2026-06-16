CXX ?= g++

CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

AR ?= ar

PYTHON_INCLUDES := $(shell python -m pybind11 --includes)
PYTHON_SUFFIX := $(shell python3-config --extension-suffix)

.PHONY: all clean

all: libAPI_RDT_UDP.a master worker dnn_udp$(PYTHON_SUFFIX)

# -----------------------------
# LIBRERIA RDT
# -----------------------------

API_RDT_UDP.o: API_RDT_UDP.cpp API_RDT_UDP.hpp protocol.hpp datagram.hpp
	$(CXX) $(CXXFLAGS) -c API_RDT_UDP.cpp -o API_RDT_UDP.o

protocol.o: protocol.cpp protocol.hpp datagram.hpp
	$(CXX) $(CXXFLAGS) -c protocol.cpp -o protocol.o

libAPI_RDT_UDP.a: API_RDT_UDP.o protocol.o
	$(AR) rcs libAPI_RDT_UDP.a API_RDT_UDP.o protocol.o

# -----------------------------
# MASTER
# -----------------------------

master: master.cpp libAPI_RDT_UDP.a
	$(CXX) $(CXXFLAGS) master.cpp libAPI_RDT_UDP.a -o master

# -----------------------------
# WORKER
# -----------------------------

worker: worker.cpp libAPI_RDT_UDP.a
	$(CXX) $(CXXFLAGS) worker.cpp libAPI_RDT_UDP.a -o worker

# -----------------------------
# PYBIND11
# -----------------------------

dnn_udp$(PYTHON_SUFFIX): \
	dnn_udp_bindings.cpp \
	API_RDT_UDP.cpp \
	protocol.cpp \
	API_RDT_UDP.hpp \
	protocol.hpp \
	datagram.hpp

	$(CXX) \
		-O3 \
		-Wall \
		-shared \
		-std=c++17 \
		-fPIC \
		$(PYTHON_INCLUDES) \
		dnn_udp_bindings.cpp \
		API_RDT_UDP.cpp \
		protocol.cpp \
		-o dnn_udp$(PYTHON_SUFFIX)

# -----------------------------
# CLEAN
# -----------------------------

clean:
	rm -f \
		*.o \
		*.a \
		master \
		worker \
		*.so
CXX ?= clang++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
AR ?= ar

.PHONY: all clean

all: libAPI_RDT_UDP.a master worker

API_RDT_UDP.o: API_RDT_UDP.cpp API_RDT_UDP.hpp protocol.hpp datagram.hpp
	$(CXX) $(CXXFLAGS) -c -o API_RDT_UDP.o API_RDT_UDP.cpp

protocol.o: protocol.cpp protocol.hpp datagram.hpp
	$(CXX) $(CXXFLAGS) -c -o protocol.o protocol.cpp

libAPI_RDT_UDP.a: API_RDT_UDP.o protocol.o
	$(AR) rcs libAPI_RDT_UDP.a API_RDT_UDP.o protocol.o

master: master.cpp API_RDT_UDP.hpp libAPI_RDT_UDP.a
	$(CXX) $(CXXFLAGS) -o master master.cpp libAPI_RDT_UDP.a

worker: worker.cpp API_RDT_UDP.hpp protocol.hpp datagram.hpp libAPI_RDT_UDP.a
	$(CXX) $(CXXFLAGS) -o worker worker.cpp libAPI_RDT_UDP.a

clean:
	rm -f master worker API_RDT_UDP.o protocol.o libAPI_RDT_UDP.a

CXX ?= clang++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
AR ?= ar

PYTHON := $(shell if [ -x .venv/bin/python3 ]; then .venv/bin/python3; else python3; fi)
PYBIND_INCLUDES := $(shell $(PYTHON) -m pybind11 --includes 2>/dev/null)
PYBIND_LIBS := $(shell $(PYTHON) -c 'import sysconfig; print(sysconfig.get_config_var("LDFLAGS") or "")') -Wl,-rpath,/usr/lib64

.PHONY: all clean python_module

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

python_module: api_rdt_udp.so

api_rdt_udp.so: pybind_api_rdt_udp.cpp API_RDT_UDP.cpp protocol.cpp API_RDT_UDP.hpp protocol.hpp datagram.hpp
	$(CXX) $(CXXFLAGS) -fPIC $(PYBIND_INCLUDES) -shared -o api_rdt_udp.so pybind_api_rdt_udp.cpp API_RDT_UDP.cpp protocol.cpp $(PYBIND_LIBS)

clean:
	rm -f master worker api_rdt_udp.so API_RDT_UDP.o protocol.o libAPI_RDT_UDP.a

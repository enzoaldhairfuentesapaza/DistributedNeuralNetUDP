CXX ?= clang++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

.PHONY: all clean

all: server worker

server: server.cpp protocol.hpp
	$(CXX) $(CXXFLAGS) -o server server.cpp

worker: worker.cpp protocol.hpp
	$(CXX) $(CXXFLAGS) -o worker worker.cpp

clean:
	rm -f server worker

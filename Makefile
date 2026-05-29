CXX ?= clang++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

.PHONY: all clean

all: master worker

master: master.cpp protocol.hpp
	$(CXX) $(CXXFLAGS) -o master master.cpp

worker: worker.cpp protocol.hpp
	$(CXX) $(CXXFLAGS) -o worker worker.cpp

clean:
	rm -f master worker

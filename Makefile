# Distributed Neural Network UDP - Makefile

# ============================================================

# Compiler configuration

CXX ?= g++
AR  ?= ar

CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -Iinclude

PYTHON_INCLUDES := $(shell python3 -m pybind11 --includes)
PYTHON_SUFFIX   := $(shell python3-config --extension-suffix)

# Directories

SRC_DIR      := src
INC_DIR      := include
APP_DIR      := apps
BUILD_DIR    := build
LIB_DIR      := lib
BIN_DIR      := bin
BINDINGS_DIR := bindings

# Library

LIB_NAME := $(LIB_DIR)/libAPI_RDT_UDP.a

LIB_SRC := $(wildcard $(SRC_DIR)/*.cpp)
LIB_OBJ := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(LIB_SRC))

# Executables

MASTER_SRC := $(APP_DIR)/master.cpp
WORKER_SRC := $(APP_DIR)/worker.cpp

MASTER_BIN := $(BIN_DIR)/master
WORKER_BIN := $(BIN_DIR)/worker

# Python binding

PYTHON_MODULE := $(BINDINGS_DIR)/dnn_udp$(PYTHON_SUFFIX)

# Default target

.PHONY: all clean help library bindings executables

all: $(LIB_NAME) $(MASTER_BIN) $(WORKER_BIN)

# Create directories if necessary

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(LIB_DIR):
	mkdir -p $(LIB_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Compile library objects
HEADERS := $(wildcard $(INC_DIR)/*.hpp)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp $(HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Static library

$(LIB_NAME): $(LIB_OBJ) | $(LIB_DIR)
	$(AR) rcs $@ $^

library: $(LIB_NAME)

# Master

$(MASTER_BIN): $(MASTER_SRC) $(LIB_NAME) $(HEADERS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $< $(LIB_NAME) -o $@

# Worker

$(WORKER_BIN): $(WORKER_SRC) $(LIB_NAME) $(HEADERS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $< $(LIB_NAME) -o $@

executables: $(MASTER_BIN) $(WORKER_BIN)

# Python bindings (pybind11)

$(PYTHON_MODULE): \
	$(BINDINGS_DIR)/dnn_udp_bindings.cpp \
	$(LIB_SRC) \
	$(INC_DIR)/API_RDT_UDP.hpp \
	$(INC_DIR)/protocol.hpp \
	$(INC_DIR)/datagram.hpp

	$(CXX) \
		-O3 \
		-Wall \
		-shared \
		-std=c++17 \
		-fPIC \
		-I$(INC_DIR) \
		$(PYTHON_INCLUDES) \
		$(BINDINGS_DIR)/dnn_udp_bindings.cpp \
		$(SRC_DIR)/API_RDT_UDP.cpp \
		$(SRC_DIR)/protocol.cpp \
		-o $(PYTHON_MODULE)

bindings: $(PYTHON_MODULE)

# Help

help:
	@echo ""
	@echo "Distributed Neural Network UDP"
	@echo ""
	@echo "Available targets:"
	@echo "  make              Build everything"
	@echo "  make library      Build static library"
	@echo "  make executables  Build master and worker"
	@echo "  make bindings     Build Python module"
	@echo "  make clean        Remove generated files"
	@echo ""

# Clean

clean:
	rm -f $(BUILD_DIR)/*.o
	rm -f $(LIB_DIR)/*.a
	rm -f $(BIN_DIR)/master
	rm -f $(BIN_DIR)/worker
	rm -f $(BINDINGS_DIR)/*.so
	rm -f $(BINDINGS_DIR)/*.pyd
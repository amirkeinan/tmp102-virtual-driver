# TMP102 Virtual Driver Makefile
# Pure C99, zero-dependency build system

CC ?= gcc
CFLAGS ?= -Wall -Wextra -Werror -std=c99
INCLUDES ?= -Iinclude

SRC_DIR ?= src
INC_DIR ?= include
TEST_DIR ?= tests
BUILD_DIR ?= build

# Source and object files
DRIVER_SRCS = $(wildcard $(SRC_DIR)/*.c)
DRIVER_OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(DRIVER_SRCS))

TEST_SRCS = $(wildcard $(TEST_DIR)/*.c)
TEST_OBJS = $(patsubst $(TEST_DIR)/%.c, $(BUILD_DIR)/%.o, $(TEST_SRCS))
TEST_BIN = $(BUILD_DIR)/test_runner

.PHONY: all test clean

# Default target: compile driver and emulator object files
all: $(DRIVER_OBJS)

# Rule for compiling source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Rule for compiling test files
$(BUILD_DIR)/%.o: $(TEST_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Target to build and execute test suite
test: $(TEST_BIN)
	./$(TEST_BIN)

# Link test executable
$(TEST_BIN): $(DRIVER_OBJS) $(TEST_OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@

# Ensure build directory exists
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Remove build artifacts
clean:
	rm -rf $(BUILD_DIR)

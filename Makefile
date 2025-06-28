CC = gcc
CXX = g++
CFLAGS = -Wall -Wextra -std=c11 -O2 -Iinclude
CXXFLAGS = -Wall -Wextra -std=c++17 -O2 -Iinclude
LDFLAGS = -lpthread -lrt

SRC_DIR = src
BUILD_DIR = build
TEST_DIR = tests
INCLUDE_DIR = include

CORE_OBJS = \
	$(BUILD_DIR)/emitter.o \
	$(BUILD_DIR)/receiver.o \
	$(BUILD_DIR)/dispatcher.o

ADAPTER_OBJS = \
	$(BUILD_DIR)/stack_trace.o

LIB_CORE = $(BUILD_DIR)/libtracering.a
LIB_ADAPTERS = $(BUILD_DIR)/libtracering-adapter.a

TESTS = \
	$(BUILD_DIR)/emit_test \
	$(BUILD_DIR)/receive_test \
	$(BUILD_DIR)/stack_trace_test \
	$(BUILD_DIR)/stack_trace_gui \
	$(BUILD_DIR)/stack_trace_window_gui

.PHONY: all clean core adapter tests

# Default: build everything
all: core adapter tests

# Only build the core library
core: $(LIB_CORE)
# Only build the adapter library
adapter: $(LIB_ADAPTERS)
# Only build test executables
tests: $(TESTS)

# Core library
$(LIB_CORE): $(CORE_OBJS)
	ar rcs $@ $^

# Adapter library
$(LIB_ADAPTERS): $(ADAPTER_OBJS)
	ar rcs $@ $^

# Pattern rule for .o files (C)
$(BUILD_DIR)/%.o: $(SRC_DIR)/*/%.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Test executables
$(BUILD_DIR)/emit_test: $(TEST_DIR)/emit_test.c $(LIB_CORE)
	$(CC) $(CFLAGS) $< -o $@ -L$(BUILD_DIR) -ltracering $(LDFLAGS)

$(BUILD_DIR)/receive_test: $(TEST_DIR)/receive_test.c $(LIB_CORE)
	$(CC) $(CFLAGS) $< -o $@ -L$(BUILD_DIR) -ltracering $(LDFLAGS)

$(BUILD_DIR)/stack_trace_test: $(TEST_DIR)/stack_trace_test.c $(LIB_CORE) $(LIB_ADAPTERS)
	$(CC) $(CFLAGS) $< -o $@ -L$(BUILD_DIR) -ltracering -ltracering-adapter $(LDFLAGS)

$(BUILD_DIR)/stack_trace_gui: $(TEST_DIR)/stack_trace_gui.cpp $(LIB_CORE) $(LIB_ADAPTERS)
	$(CXX) $(CXXFLAGS) $< -o $@ -L$(BUILD_DIR) -ltracering -ltracering-adapter -lncurses $(LDFLAGS)

$(BUILD_DIR)/stack_trace_window_gui: $(TEST_DIR)/stack_trace_window_gui.cpp $(LIB_CORE) $(LIB_ADAPTERS)
	$(CXX) $(CXXFLAGS) $< -o $@ -L$(BUILD_DIR) -ltracering -ltracering-adapter -lSDL2 -lSDL2_ttf $(LDFLAGS)

# Run test message
test: $(TESTS)
	@echo "Run a receiver test in one terminal: ./build/receive_test ./build/stack_trace_test ./build/stack_trace_gui or ./build/stack_trace_window_gui"
	@echo "Then run one (or more) emit tests in another terminal: ./build/emit_test"

clean:
	rm -rf $(BUILD_DIR)

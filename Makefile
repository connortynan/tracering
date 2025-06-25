CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -Iinclude
LDFLAGS = -lpthread -lrt

SRC_DIR = src
BUILD_DIR = build
TEST_DIR = tests
INCLUDE_DIR = include

LIB_OBJS = $(BUILD_DIR)/tracering_emitter.o $(BUILD_DIR)/tracering_receiver.o
LIB_NAME = $(BUILD_DIR)/libtracer.a

TESTS = $(BUILD_DIR)/emit_test $(BUILD_DIR)/receive_test

.PHONY: all clean test

all: $(LIB_NAME) $(TESTS)

# Build static library
$(LIB_NAME): $(LIB_OBJS)
	ar rcs $@ $^

# Compile object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/*/%.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Test executables
$(BUILD_DIR)/emit_test: $(TEST_DIR)/emit_test.c $(LIB_NAME)
	$(CC) $(CFLAGS) $< -o $@ -L$(BUILD_DIR) -ltracer $(LDFLAGS)

$(BUILD_DIR)/receive_test: $(TEST_DIR)/receive_test.c $(LIB_NAME)
	$(CC) $(CFLAGS) $< -o $@ -L$(BUILD_DIR) -ltracer $(LDFLAGS)

test: all
	@echo "Run ./build/receive_test in one terminal, then ./build/emit_test in another."

clean:
	rm -rf $(BUILD_DIR)

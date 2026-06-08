CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -O2
LDFLAGS = -lSDL2

SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj

# Find all C files in the src directory
SRCS = $(wildcard $(SRC_DIR)/*.c)
# Translate them to corresponding .o files in the obj directory
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
# Find all headers to use as basic dependencies
DEPS = $(wildcard $(INC_DIR)/*.h)

TARGET = daxgb_emulator

# Default target
all: $(TARGET)

# Link the final executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Compile individual object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(DEPS)
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -I$(INC_DIR) -c -o $@ $<

# Clean up build artifacts
clean:
	rm -rf $(OBJ_DIR) $(TARGET)

.PHONY: all clean
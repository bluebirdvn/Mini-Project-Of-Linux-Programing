CC = gcc
CFLAGS = -Wall -Werror -O2 -Wextra -I./inc
LDFLAGS = -pthread

SRCS = main.c notify_sig.c 
OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.c=.o))
DEPS = $(addprefix $(BUILD_DIR)/, $(SRCS:.c=.d))

TARGET = signal-aware-multithreading-daemon
BUILD_DIR = ./build
RUN_DIR = ./run

INC_DIR = ./inc

VPATH = inc

.PHONY: all clean run debug

all: $(BUILD_DIR) $(RUN_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(RUN_DIR):
	mkdir -p $(RUN_DIR)

$(BUILD_DIR)/%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(TARGET): $(OBJS)
	@echo "Linking $@..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

-include $(DEPS)

run: $(TARGET)
	@echo "Running..."
	./$(TARGET)

clean:
	@echo "Cleaning build folder..."
	rm -rf $(BUILD_DIR)
	rm -f $(TARGET)
	@echo "Clean complete"

debug: CFLAGS += -g -DDEBUG
debug: clean all
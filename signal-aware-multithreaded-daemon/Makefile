CC = gcc
CFLAGS = -Wall -Werror -O2 -Wextra
LDFLAGS = -pthread

SRCS = main.c
OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)

TARGET = signal-aware-multithreading-daemon
BUILD_DIR = ./build
RUN_DIR = ./run
HEADERS =

.PHONY: all clean run debug

all: $(BUILD_DIR) $(RUN_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(RUN_DIR):
	mkdir -p $(RUN_DIR)

$(BUILD_DIR)/%.o: %.c $(HEADERS)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(TARGET): $(addprefix $(BUILD_DIR)/, $(OBJS))
	@echo "Linking $@..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

-include $(addprefix $(BUILD_DIR)/, $(DEPS))

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
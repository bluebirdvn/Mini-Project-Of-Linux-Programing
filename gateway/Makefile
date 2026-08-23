CC = gcc

CFLAGS = -Wall -Wextra -O2 -Iinclude -fsanitize=address -fno-omit-frame-pointer 
LDFLAGS= -pthread -fsanitize=address

BIN_DIR = bin
OBJ_DIR = obj

TARGET = $(BIN_DIR)/gateway

SRCS = main.c $(wildcard src/*.c )
OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(SRCS))
DEPS = $(OBJS:.o=.d)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	mkdir -p $(dir $@)
	$(CC) $^ -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(DEPS)

clean:
	rm -rf $(BIN_DIR) $(OBJ_DIR)
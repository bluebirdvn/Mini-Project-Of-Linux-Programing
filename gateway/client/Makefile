CC = gcc

CFLAGS = -Wall -Wextra -g -I../include

LDFLAGS = -lpthread
TLS_LDFLAGS = -lssl -lcrypto

OBJ_DIR = objs

COMMON_SRCS = ../src/crc.c \
              ../src/protocol_message.c \
              ../src/common.c \
              ../src/logger.c \
              ../src/cJSON.c

COMMON_OBJS = $(COMMON_SRCS:../src/%.c=$(OBJ_DIR)/%.o)

CLIENT_OBJS = $(OBJ_DIR)/client.o
TLS_OBJS    = $(OBJ_DIR)/client_tls.o

TARGETS = client client_tls

all: $(TARGETS)

client: $(CLIENT_OBJS) $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

client_tls: $(TLS_OBJS) $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(TLS_LDFLAGS)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/client.o: client.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/client_tls.o: client_tls.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: ../src/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(TARGETS)

.PHONY: all clean
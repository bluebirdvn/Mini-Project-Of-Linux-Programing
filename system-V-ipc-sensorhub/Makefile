GCC = gcc
CFLAGS = -Wall -Werror -Wextra -O2 -g -I./common
LDFLAGS = -lpthread


BIN_DIR := bin
OBJ_DIR := obj

COMMON_SRC := common/common.c
COMMON_OBJ := $(OBJ_DIR)/common.o

APPS := hub_controller sensor_monitor sensor_writer

hub_controller_SRC   := hub/hub_controller.c
sensor_monitor_SRC   := sensor/sensor_monitor.c
sensor_writer_SRC    := sensor/sensor_writer.c

MAIN_SRC := main.c
MAIN_BIN := $(BIN_DIR)/sensorhub


.PHONY: all
all: $(BIN_DIR) $(OBJ_DIR) $(MAIN_BIN) $(addprefix $(BIN_DIR)/, $(APPS))

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(COMMON_OBJ): $(COMMON_SRC) common/common.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(MAIN_BIN): $(MAIN_SRC) $(COMMON_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/hub_controller: $(hub_controller_SRC) $(COMMON_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/sensor_monitor: $(sensor_monitor_SRC) $(COMMON_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/sensor_writer: $(sensor_writer_SRC) $(COMMON_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

.PHONY: run
run: all
	./$(MAIN_BIN)

.PHONY: clean
clean:
	rm -rf $(BIN_DIR) $(OBJ_DIR)




CC = gcc
CFLAGS = -Wall -Werror -Wextra -O2 -g -I./common -D_GNU_SOURCE
LDFLAGS = -lpthread -lrt

BIN_DIR := bin
OBJ_DIR := obj

COMMON_SRC := common/common.c
COMMON_OBJ := $(OBJ_DIR)/common.o

APPS := controller sensor monitor

controller_SRC := controller/controller.c
sensor_SRC     := sensor/sensor.c
monitor_SRC    := monitor/monitor.c

.PHONY: all
all: $(BIN_DIR) $(OBJ_DIR) $(COMMON_OBJ) $(addprefix $(BIN_DIR)/, $(APPS))

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(COMMON_OBJ): $(COMMON_SRC) common/common.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

define BUILD_APP
$(BIN_DIR)/$(1): $$($(1)_SRC) $(COMMON_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) $$^ -o $$@ $(LDFLAGS)
endef

$(foreach app,$(APPS),$(eval $(call BUILD_APP,$(app))))

.PHONY: clean
clean:
	rm -rf $(BIN_DIR) $(OBJ_DIR)

.PHONY: run-controller
run-controller: all
	./$(BIN_DIR)/controller

.PHONY: run-sensor
run-sensor: all
	./$(BIN_DIR)/sensor

.PHONY: run-monitor
run-monitor: all
	./$(BIN_DIR)/monitor

.PHONY: run
run: run-monitor   

CXX      := g++
CXXSTD   := -std=c++17
WARN     := -Wall -Wextra
DEBUG    := -g
THREAD   := -pthread

INCLUDES := -Icommon -Igatewayd -Igatewaycli

CXXFLAGS := $(CXXSTD) $(WARN) $(DEBUG) $(THREAD) $(INCLUDES)
LDFLAGS  := -pthread

BIN_DIR := bin
OBJ_DIR := obj
LOG_DIR := log

SERVER_BIN := $(BIN_DIR)/server
CLIENT_BIN := $(BIN_DIR)/client

SERVER_OBJ := $(OBJ_DIR)/server.o
CLIENT_OBJ := $(OBJ_DIR)/client.o
LOGGER_OBJ := $(OBJ_DIR)/logger.o

COMMON_HDRS := common/common.h common/logger.h
SERVER_HDRS := gatewayd/server.h $(COMMON_HDRS)
CLIENT_HDRS := gatewaycli/client.h gatewayd/server.h $(COMMON_HDRS)

.PHONY: all clean dirs run-server run-client rebuild

all: dirs $(SERVER_BIN) $(CLIENT_BIN)
	@echo ">> Built: $(SERVER_BIN), $(CLIENT_BIN)"

server: dirs $(SERVER_BIN)

client: dirs $(CLIENT_BIN)

dirs:
	@mkdir -p $(BIN_DIR) $(OBJ_DIR) $(LOG_DIR)

$(SERVER_BIN): $(SERVER_OBJ) $(LOGGER_OBJ) | dirs
	$(CXX) $(LDFLAGS) -o $@ $^

$(CLIENT_BIN): $(CLIENT_OBJ) $(LOGGER_OBJ) | dirs
	$(CXX) $(LDFLAGS) -o $@ $^

$(OBJ_DIR)/server.o: gatewayd/server.c $(SERVER_HDRS) | dirs
	$(CXX) $(CXXFLAGS) -x c++ -c $< -o $@

$(OBJ_DIR)/client.o: gatewaycli/client.c $(CLIENT_HDRS) | dirs
	$(CXX) $(CXXFLAGS) -x c++ -c $< -o $@

$(OBJ_DIR)/logger.o: common/logger.c common/logger.h | dirs
	$(CXX) $(CXXFLAGS) -x c++ -c $< -o $@

run-server: $(SERVER_BIN)
	./$(SERVER_BIN)

run-client: $(CLIENT_BIN)
	./$(CLIENT_BIN)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
	rm -f $(LOG_DIR)/*.log

rebuild: clean all
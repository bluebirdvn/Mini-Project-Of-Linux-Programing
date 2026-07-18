CC = gcc
CFLAGS = -Wall -O2 -Werror -Wextra

TARGET = safe-config-tool
SRCS = main.c
OBJS = $(SRCS:.c=.o)

TEST_APP = inotify-test
SRCS_TEST = notify.c
OBJS_TEST = $(SRCS_TEST:.c=.o)

.PHONY: all clean run

all: $(TARGET) $(TEST_APP)

%.o: %.c
	@echo "Compiling $<"
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	@echo "Linking $^"
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_APP): $(OBJS_TEST)
	@echo "Linking $^"
	$(CC) $(CFLAGS) -o $@ $^

run: $(TARGET)
	./$(TARGET)

clean:
	@echo "Cleaning up..."
	rm -f $(TARGET) $(OBJS) $(TEST_APP) $(OBJS_TEST)
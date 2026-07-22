CC = gcc
CFLAGS = -Wall -O2 -Werror -Wextra

LDFLAGS = -pthread


TARGET = worker
SRCS = worker.c
OBJS = $(SRCS:.c=.o)


.PHONY: all clean run

all: $(TARGET) 

%.o: %.c
	@echo "Compiling $<"
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	@echo "Linking $^"
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)


run: $(TARGET)
	./$(TARGET)

clean:
	@echo "Cleaning up..."
	rm -f $(TARGET) $(OBJS) 

CC = gcc
CFLAGS = -Wall -Wextra -pthread -std=c99
TARGET = nimd
SRC = nimd.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET) *.o

.PHONY: all clean

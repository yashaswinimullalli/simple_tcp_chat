CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2
LDFLAGS = -lpthread

TARGETS = server_select server_thread server_fork client

all: $(TARGETS)

server_select: server_select.c
	$(CC) $(CFLAGS) server_select.c -o server_select

server_thread: server_thread.c
	$(CC) $(CFLAGS) server_thread.c -o server_thread $(LDFLAGS)

server_fork: server_fork.c
	$(CC) $(CFLAGS) server_fork.c -o server_fork

client: client.c
	$(CC) $(CFLAGS) client.c -o client $(LDFLAGS)

clean:
	rm -f $(TARGETS) *.o *.exe server

.PHONY: all clean

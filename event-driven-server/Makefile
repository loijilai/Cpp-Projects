CC = gcc
CFLAGS = -Wall -g

all: read_server write_server

read_server: main.c server.c business_logic.c
	$(CC) $(CFLAGS) -D READ_SERVER -o read_server main.c server.c business_logic.c

write_server: main.c server.c business_logic.c
	$(CC) $(CFLAGS) -D WRITE_SERVER -o write_server main.c server.c business_logic.c

clean:
	rm read_server write_server
all: client

client: client.c
	gcc -g  -O3   $< -o $@ `pkg-config libwebsockets --libs --cflags` -lpthread -g Queue.c
	
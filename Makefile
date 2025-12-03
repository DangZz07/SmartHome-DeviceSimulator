all: server client

server:
	gcc -o server Server/server.c -pthread

client:
	gcc -o client Client/client.c -pthread

clean:
	rm -f server client

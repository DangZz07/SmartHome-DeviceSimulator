all: server client

server:
	gcc -o server Server/server.c cJSON.c -I. -pthread 

client:
	gcc -o client Client/client.c cJSON.c -I. -pthread 

clean:
	rm -f server client

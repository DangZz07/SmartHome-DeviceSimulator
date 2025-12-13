all: server1 client1

server1:
	gcc -o server1 Server/server1.c cJSON.c -I. -pthread 

client1:
	gcc -o client1 Client/client1.c cJSON.c -I. -pthread 

clean:
	rm -f server1 client1

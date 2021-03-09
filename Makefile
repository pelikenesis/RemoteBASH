# RemoteBASH
# Makefile
server: server.c tpool.c
	gcc -std=gnu99 -Wall -o server server.c tpool.c -pthread
server-debug: server.c tpool.c
	gcc -std=gnu99 -Wall -DDEBUG -o server-debug server.c tpool.c -pthread
client: client.c
	gcc -std=gnu99 -Wall -o client client.c

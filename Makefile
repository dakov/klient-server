CC=g++
PRE=-std=c++98 -pedantic
SERVER=server
CLIENT=client

all: server client

server: server.cpp
	$(CC) $(PRE) $(SERVER).cpp -o $(SERVER)

srun: $(SERVER)
	./$(SERVER) -p 10000 -d 20

client: client.cpp
	$(CC) $(PRE) $(CLIENT).cpp -o $(CLIENT)

pack:
	tar -cvzf xkovar66.tar.gz Makefile client.cpp server.cpp

send: pack
	scp client.cpp server.cpp xkovar66@eva.fit.vutbr.cz:~/workspace/ipk/ipk2/


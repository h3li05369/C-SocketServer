# Creates binary for server.c
 all:server.c 
	gcc -g -Wall -o server server.c

 clean: 
	rm server

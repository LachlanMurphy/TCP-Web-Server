# Make file to compile web server code
# Lachlan Murphy 2025

CC = gcc

CFLAGS = -g -Wall -pthread -lpthread

default: all

all: server

server: web_server.c
	$(CC) $(CFLAGS) -o web_server web_server.c array.c

# clean:
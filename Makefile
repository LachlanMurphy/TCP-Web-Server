# Make file to compile web server code
# Lachlan Murphy 2025

CC = gcc

CLIENT_CFLAGS = -g -Wall
SERVER_CFLAGS = -g -Wall

default: all

all: server

server: web_server.c
	$(CC) $(SERVER_CFLAGS) -o web_server web_server.c

# clean:
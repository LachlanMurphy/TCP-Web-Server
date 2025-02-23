# Make file to compile web server code
# Lachlan Murphy 2025

CC = gcc

CFLAGS = -g -Wall -pthread -lpthread

default: all

all: server

server: server.c
	$(CC) $(CFLAGS) -o server server.c array.c
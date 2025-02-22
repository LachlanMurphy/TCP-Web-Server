/*
 * Basic TCP Web Server
 * Lachlan Murphy
 * 21 February 2025
 */


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <sys/time.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include "array.h"

#define BUFFERSIZE 1024
#define ROOT_DIR "./www"

char* res = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 12\r\n\r\n";

// sigint handler
void sigint_handler(int sig);

// argument struct for socket_handler function
typedef struct {
	int* serverfd;
	int* clientfd;
    struct sockaddr_in* serveraddr;
    socklen_t* addrlen;
	array* arr;
} socket_arg_t;

// multi-thread function to handle new socket connections
void* socket_handler(void* arg);

/*
 * error - wrapper for perror
 */
void error(char *msg) {
	perror(msg);
	exit(1);
}

array socks;

int main(int argc, char** argv) {
    int sockfd, new_socket;
    int portno;
    int optval;
    struct sockaddr_in serveraddr;
    socklen_t addrlen = sizeof(serveraddr);
    // char buf[BUFFERSIZE];


    /* 
	* check command line arguments
	*/
	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}

	// set up signal handling
	signal(SIGINT, sigint_handler);

	// initialize shared array
	array_init(&socks);

	portno = atoi(argv[1]);

    /* 
	* socket: create the parent socket 
	*/
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) error("ERROR opening socket");

    optval = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval , sizeof(int));

    /*
	* build the server's Internet address
	*/
	bzero((char *) &serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons((unsigned short)portno);

    /* 
	* bind: associate the parent socket with a port 
	*/
	if (bind(sockfd, (struct sockaddr *) &serveraddr, addrlen) < 0) 
        error("ERROR on binding");
    

    // main loop, listen for sockets and deal with them
    while (1) {
        // wait for new connection to be prompted
        if (listen(sockfd, 5) < 0) error("ERROR on listen");
        
        // create new fd for that socket
        if ((new_socket = accept(sockfd, (struct sockaddr *) &serveraddr, &addrlen)) < 0) error("ERROR accepting new socket");

        // create new pthread to handle socket request
		// this pointer will eventually be removed from scopre without freeing the memmory
		// the memmory will be freed when the socket is successfully completed
		pthread_t* thread_id = malloc(sizeof(pthread_t));
		
		// init arguments for new thread
		socket_arg_t* socket_arg = malloc(sizeof(socket_arg_t)); // will also be freed later
		socket_arg->addrlen = &addrlen;
		socket_arg->clientfd = &new_socket;
		socket_arg->serveraddr = &serveraddr;
		socket_arg->serverfd = &sockfd;
		socket_arg->arr = &socks;

		// create thread
		pthread_create(thread_id, NULL, socket_handler, socket_arg);

		// add new thread to array
		array_put(&socks, thread_id);

		// detatch thread so resources are unallocated independent of parent thread
		pthread_detach(*thread_id);
		thread_id = NULL;
    }
}

void sigint_handler(int sig) {
	print_array(&socks);
	array_free(&socks);
	printf("Server closed on SIGINT\n");
	exit(0);
}

void* socket_handler(void* arg) {
	socket_arg_t* args = (socket_arg_t *) arg;
	char buf[BUFFERSIZE];

	// read in message
	read(*args->clientfd, buf, BUFFERSIZE);
	printf("%s\n", buf);
	buf[strcspn(buf, "\n")] = '\0';

	// parse message
	char req[3][BUFFERSIZE]; // req[0]=method ; req[1]=URI ; req[2]=version
	char* tmp = strtok(buf, " ");
	memcpy(req[0], tmp, BUFFERSIZE);
	for (int i = 1; i < 3; i++) {
		tmp = strtok(NULL, " ");
		memcpy(req[i], tmp, BUFFERSIZE);
	}

	char file_name[BUFFERSIZE];
	strncpy(file_name, ROOT_DIR, strlen(ROOT_DIR));
	strcat(file_name, req[1]);
	printf("%s\n", file_name);

	// attempt to access file requested
	if (!access(req[1], F_OK)) {
		// file exists, send it
		// send file
		FILE* file = fopen(file_name, "r");
		int n = 0;
		send(*args->clientfd, res, strlen(res), 0);
		while (1) {
			n = fread(buf, 1, BUFFERSIZE, file);
			if (n <= 0) break;
			send(*args->clientfd, buf, n, 0);
		}
		fclose(file);
	} else {
		error("ERROR opening file");
	}

	close(*args->clientfd);

	// free up the memory usage now
	pthread_t* to_free = NULL;
	array_get(args->arr, &to_free);

	free(to_free);
	free(args);
	return NULL;
}
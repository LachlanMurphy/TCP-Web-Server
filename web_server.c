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
#include <string.h>
#include "array.h"

#define BUFFERSIZE 1024
#define ROOT_DIR "./www"
#define INDEX "index.html"
// HTTP version, status, content type, content length
#define HEADER "%s %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n\r\n"

#define FILE_SUF_LEN 8

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

// matches a file suffix with a file type
int find_file_type(char* file_name);

/*
 * error - wrapper for perror
 */
void error(char *msg) {
	perror(msg);
	exit(1);
}

// global values
array socks; // semaphores used, thread safe
const char* file_types[FILE_SUF_LEN] = {
	"text/html",
	"text/plain",
	"image/png",
	"image/gif",
	"image/jpg",
	"image/x-icon",
	"text/css",
	"application/javascript"
}; // read only, thread safe

// indices align with file_types
const char* file_suff[FILE_SUF_LEN] = {
	".html",
	".txt",
	".png",
	".gif",
	".jpg",
	".ico",
	".css",
	".js"
}; // read only, thread safe

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
	char req[3][BUFFERSIZE / 2]; // req[0]=method ; req[1]=URI ; req[2]=version
	char* tmp;
	int parse_err = 0;
	for (int i = 0; i < 3; i++) {
		if (i == 0) tmp = strtok(buf, " ");
		else tmp = strtok(NULL, " ");

		if (!tmp) {
			parse_err = 1;
			break;
		}
		memcpy(req[i], tmp, BUFFERSIZE);
	}

	// version may have a cairraige return on the end, replace with str terminator
	req[2][strcspn(req[2], "\r\n")] = '\0';

	if (parse_err) {
		// 400 Bad request
		sprintf(buf, HEADER, "HTTP/1.1", "400 Bad Request", "text/plain", 0UL);
		send(*args->clientfd, buf, strlen(buf), 0);
	} else if (strncmp(req[0], "GET", strlen("GET"))) { // Check method, only get is allowed
		// 405 Method Not Allowed
		sprintf(buf, HEADER, req[2], "405 Method Not Allowed", "text/plain", 0UL);
		send(*args->clientfd, buf, strlen(buf), 0);
	} else if (strncmp("HTTP/1.0", req[2], strlen(req[2]) - 1) &&
			   strncmp("HTTP/1.1", req[2], strlen(req[2]) - 1)) { // check HTTP version
		// 505 HTTP Version Not Supported
		sprintf(buf, HEADER, req[2], "505 HTTP Version Not Supported", "text/plain", 0UL);
		send(*args->clientfd, buf, strlen(buf), 0);
	} else {
		char file_name[BUFFERSIZE];
		strncpy(file_name, ROOT_DIR, strlen(ROOT_DIR)+1);
		strcat(file_name, req[1]);

		// if we are requesting a directory, replace ending with index.html
		if (file_name[strlen(file_name)-1] == '/') {
			// char indx_suffix
			strcat(file_name, (char *) &INDEX);
		}

		// attempt to access file requested
		if (!access(file_name, F_OK) && !access(file_name, R_OK)) {
			// file exists, send it
			// send file
			FILE* file = fopen(file_name, "r");
			int n = 0;

			// get file size
			fseek(file, 0, SEEK_END);
			unsigned long file_size = ftell(file);
			fseek(file, 0, SEEK_SET);

			sprintf(buf, HEADER, req[2], "200 OK", file_types[find_file_type(file_name)], file_size);
			send(*args->clientfd, buf, strlen(buf), 0);
			while (1) {
				n = fread(buf, 1, BUFFERSIZE, file);
				if (n <= 0) break;
				send(*args->clientfd, buf, n, 0);
			}
			fclose(file);
		} else {
			// check whether it was a permission err or file didnt exist
			if (access(file_name, F_OK)) {
				// 404 file not found
				sprintf(buf, HEADER, req[2], "404 Not Found", "text/plain", 0UL);
				send(*args->clientfd, buf, strlen(buf), 0);
			} else if (access(file_name, R_OK)) {
				// 403 forbidden
				sprintf(buf, HEADER, req[2], "403 Forbidden", "text/plain", 0UL);
				send(*args->clientfd, buf, strlen(buf), 0);
			}
		}

	}

	close(*args->clientfd);

	// free up the memory usage now
	pthread_t* to_free = NULL;
	array_get(args->arr, &to_free);

	free(to_free);
	free(args);
	return NULL;
}

int find_file_type(char* file_name) {
	// get suffic of file name
	char* suf = strrchr(file_name, '.');
	if (!suf || suf == file_name) return 1; // by default do plain text

	// match with suffixes from file_suf array
	for (int i = 0; i < FILE_SUF_LEN; i++) {
		if (strncmp(suf, file_suff[i], strlen(suf)) == 0) return i;
	}

	return 1; // by default just do plain text
}
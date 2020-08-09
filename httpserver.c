#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <stdio.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <fcntl.h>
#include <unistd.h> // write
#include <string.h> // memset
#include <stdlib.h> // atoi
#include <errno.h>
#include <ctype.h>
#include <stdbool.h>
#include <pthread.h>
#include <math.h>

pthread_t threadpool[4];
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex2 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex3 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex4 = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condition_var = PTHREAD_COND_INITIALIZER;


//Create the queue


struct node {
	struct node* next;
	int *client_sockd;
};
typedef struct node node_t;

typedef struct args {
	int reqs;
	int errors;
	uint64_t offset;
	bool logBool;
	char* logName;
} inputs;

//for enqueue and dequeue, I learned by referencing this code
//https://youtube/P6Z5K8zmEmc
node_t* head = NULL;
node_t* tail = NULL;

void enqueue(int *client_sockd) {
	//do we need to free?
	node_t* newNode = malloc(sizeof(node_t));
	newNode->client_sockd = client_sockd;
	newNode->next = NULL;
	if (tail == NULL) {
		head = newNode;
	}
	else {
		tail->next = newNode;
	}
	tail = newNode;
}

//returns pointer to client socket, if it exists
int* dequeue() {
	if (head == NULL) {
		printf("%s\n", "Queue is empty, nothing to dequeue");
		return NULL;
	}
	else {
		int* result = head -> client_sockd;
		node_t* temp = head;
		head = head->next;
		if (head == NULL) {
			tail = NULL;
		}
		free(temp);
		return result;
	}
}

//function to check that the file of correct syntax
bool fileCheck(char* fileName, uint8_t *copy_buff, int index) {
	bool invalid = false;
	if (strlen(fileName) > 27) {
		printf("%s\n", "file length is too long");
		invalid = true;
	}

	for (int i = 0; i < strlen(fileName); i++) {
		//check for alphanumeric chars , - and _ in the file name. If not, return 400
		if ( (isalnum(copy_buff[index]) == 0) && (copy_buff[index] != '-') && (copy_buff[index] != '_') ) {
			printf("%s\n", "Invalid string");
			invalid = true;
		}
		index++;
	}
	return invalid;
}

//logging for GET and PUT requests
void gnarlog(inputs *globals, char* fileName, int size, int* logFile, char* request) {
	int log = *logFile;
	uint8_t logWrite[400];
	//create the first line of the log function which specifies the request, fileName and content length of the file
	sprintf((char*) logWrite, "%s /%s length %d\n", request, fileName, size);
	//set current to be the value of the current global offset. Lock this operation as this is considered a critical region which must not be accessed by other threads.
	pthread_mutex_lock(&mutex1);
	uint64_t current = globals->offset;

	//gnarly formula to calcuate the global offset
	uint64_t numLines = (uint64_t)(floor(size / 20));
	uint64_t totalBytes = numLines * 69;

	uint64_t extraLine = (size % 20);
	if (extraLine > 0) {
		extraLine *= 3;
		extraLine += 9;
	}
	globals->offset += (strlen((char*)logWrite) + totalBytes + extraLine + 9);
	pthread_mutex_unlock(&mutex1);

	pwrite(log, logWrite, strlen((char*)logWrite), current);
	current += strlen((char*)logWrite);

	int fd = open(fileName, O_RDONLY);
	ssize_t len;
	uint8_t content[20];
	uint8_t toWrite[20];
	uint8_t temp[20];
	ssize_t count = 0;

	//read from the file descritpor until nothing else is read and convert each byte to hex. Write each byte.
	sprintf((char*) temp, "%08ld ", count);
	pwrite(log, temp, strlen((char*) temp), current);
	current += strlen((char*) temp);
	//read 20 chars at a time since content is a buffer of size 20
	while ((len = read(fd, content, sizeof(content))) > 0) {

		for (size_t idx = 0; idx < len / sizeof(char); ++idx) {
			///writing the last byte, don't add a space
			if (idx == 19 || count == (size - 1)) {
				snprintf((char*) toWrite, 3, "%02x", content[idx]);
				pwrite(log, toWrite, strlen((char*) toWrite), current);
			}
			else {
				snprintf((char*) toWrite, 4, "%02x ", content[idx]);
				//printf("%s ", (char*)toWrite);
				pwrite(log, toWrite, strlen((char*) toWrite), current);
			}
			current += strlen((char*) toWrite);
			count += 1;
		}
		//we have not written everything
		if (count != size) {
			sprintf((char*) temp, "\n%08ld ", count);
			pwrite(log, temp, strlen((char*) temp), current);
			current += strlen((char*) temp);
		}
		//we did not write a full line, this is the last line
		else {
			sprintf((char*) temp, "\n========\n");
			pwrite(log, temp, strlen((char*) temp), current);
			current += strlen((char*) temp);
		}
	}
}

//log the error requests
void errorLog(char* request, char* name, char* version, inputs *globals, int* logFile, int errorNum) {
	int log = *logFile;
	printf("%s %s %s\n", request, name, version);
	char logWrite[300];
	sprintf(logWrite, "FAIL: %s %s %s --- response %d\n========\n", request, name, version, errorNum);

	pthread_mutex_lock(&mutex1);
	int current = globals->offset;
	globals->offset += strlen(logWrite);
	pthread_mutex_unlock(&mutex1);

	pwrite(log, &logWrite, strlen(logWrite), current);
	pthread_mutex_lock(&mutex3);
	globals->errors += 1;
	pthread_mutex_unlock(&mutex3);
}

//handle the parsing of the HTTP requests (GET, HEAD, PUT, ETC)
void req_parse(int *pclient, inputs *globals) {
	//allocate buffer +1 to add null terminating char
	uint8_t buff[512 + 1];

	//read from socket and put information in buff
	ssize_t bytes = recv(*pclient, buff, 512, 0);
	buff[bytes] = 0; // null terminate
	uint8_t copy_buff[512 + 1];
	memcpy(copy_buff, buff, sizeof(buff));
	copy_buff[bytes] = 0;

	//split the string at this delimeter
	//const char s[4] = "\r\n";
	char request[10];
	char name[300];
	char version[10];
	sscanf((char*)buff, "%s %s %s", request, name, version);

	int log = open(globals->logName, O_CREAT | O_WRONLY, 0644);

	char get[4] = "GET ";
	char head[5] = "HEAD ";
	char put[4] = "PUT ";
	char* charBuff = (char*)(buff);
	int getReq = strncmp(charBuff, get, 4);
	int headReq = strncmp(charBuff, head, 5);
	int putReq = strncmp(charBuff, put, 4);

	//printf("%s\n",buff );
	//code to parse get request
	if (getReq == 0) {
		printf("%s\n", "get request");

		char* space = strstr((char*)(&copy_buff[5]), " ");
		space[0] = '\0';
		char* fileName;

		//the name of the file provided in the request (the endpoint)
		fileName  = (char*)(&copy_buff[5]);

		//implementing health check and ensuring log file exists
		if (strcmp(fileName, "healthcheck") == 0 && globals->logBool == true) {
			printf("%s\n", "dealing with healthcheck in GET");
			char content[100];
			pthread_mutex_lock(&mutex4);
			globals->reqs -= 1;
			sprintf(content, "%d\n%d", globals->errors, globals->reqs);
			char nums[100];
			sprintf(nums, "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\n\r\n%d\n%d", strlen(content), globals->errors, globals->reqs);
			globals->reqs += 1;
			write(*pclient, nums, strlen(nums));
			close(*pclient);
			pthread_mutex_unlock(&mutex4);

			if (globals->logBool == true) {
				char logged[100];
				sprintf(logged, "GET /healthcheck length %ld\n", strlen(content));
				pthread_mutex_lock(&mutex4);
				uint64_t current = globals->offset;
				globals->offset += strlen(logged) + 9 + 8 + 10;
				pthread_mutex_unlock(&mutex4);

				pwrite(log, logged, strlen(logged), current);
				current += strlen(logged);

				uint8_t toWrite[20];
				uint8_t temp[20];
				ssize_t count = 0;

				sprintf((char*) temp, "%08ld ", count);
				pwrite(log, temp, strlen((char*) temp), current);
				current += strlen((char*) temp);
				//convert to each byte to hex and pwrite to the log
				for (size_t idx = 0; idx < strlen(content); ++idx) {
					///writing the last byte, don't add a space
					if (count == (strlen(content) - 1)) {
						snprintf((char*) toWrite, 3, "%02x", content[idx]);
						pwrite(log, toWrite, strlen((char*) toWrite), current);
					}
					else {
						snprintf((char*) toWrite, 4, "%02x ", content[idx]);
						//printf("%s ", (char*)toWrite);
						pwrite(log, toWrite, strlen((char*) toWrite), current);
					}
					current += strlen((char*) toWrite);
					count += 1;
				}
				sprintf((char*) temp, "\n========\n");
				pwrite(log, temp, strlen((char*) temp), current);
				current += strlen((char*) temp);
			}
			return;
		}

		int fd = open(fileName, O_RDONLY, 0666);
		//This code checks that the file name is of the correct format
		//int index = 5;
		bool invalid = fileCheck(fileName, copy_buff, 5);

		if (invalid == true || strcmp(version, "HTTP/1.1") != 0) {
			write(*pclient, "HTTP/1.1 400 BAD REQUEST\r\nContent-Length: 0\r\n\r\n", strlen("HTTP/1.1 400 NOT FOUND\r\nContent-Length: 0\r\n\r\n"));
			//logging the error
			if (globals->logBool == true) {
				int errornum = 400;
				errorLog(request, name, version, globals, &log, errornum);

			}
		}


		//check to see if file was accessed correctly
		if (fd == -1 && invalid == false) {
			fprintf(stderr, "%s\n", strerror(errno));
			//permission denied, return 403
			if (strcmp(strerror(errno), "Permission denied") == 0) {
				write(*pclient, "HTTP/1.1 403 FORBIDDEN\r\nContent-Length: 0\r\n\r\n", strlen("HTTP/1.1 403 FORBIDDEN\r\nContent-Length: 0\r\n\r\n"));

				//logging the error
				if (globals->logBool == true) {
					int errornum = 403;
					errorLog(request, name, version, globals, &log, errornum);
				}
			}

			//file DNE, return 404
			else if (strcmp(strerror(errno), "No such file or directory") == 0) {
				write(*pclient, "HTTP/1.1 404 NOT FOUND\r\nContent-Length: 0\r\n\r\n", strlen("HTTP/1.1 404 NOT FOUND\r\nContent-Length: 0\r\n\r\n"));
				if (globals->logBool == true) {
					int errornum = 404;
					errorLog(request, name, version, globals, &log, errornum);
				}
			}
		}

		//valid get
		else if (invalid == false && fd != -1) {
			struct stat buf;
			stat(fileName, &buf);
			int size = buf.st_size;

			char* cl = malloc(100);
			//convert int to string
			sprintf(cl, "%d", size);
			strcat(cl, "\r\n\r\n");

			write(*pclient, "HTTP/1.1 200 OK\r\nContent-Length: ", strlen("HTTP/1.1 200 OK\r\nContent-Length: "));
			write(*pclient, cl, strlen(cl));
			free(cl);

			uint8_t message[4096];
			ssize_t len;
			while ((len = read(fd, message, sizeof(message))) > 0) {
				write(*pclient, message, len);
			}
			close(fd);

			if (globals->logBool == true) {
				//log the valid get request
				gnarlog(globals, fileName, size, &log, request);
			}
		}
	}
	//code to parse head request
	else if (headReq == 0) {
		printf("%s\n", "head request");
		char* space = strstr((char*)(&copy_buff[5]), " ");
		space[0] = '\0';


		//code to parse content length
		char* fileName  = (char*)(&copy_buff[6]);
		int fd = open(fileName, O_RDONLY);
		//int log = open("log", O_CREAT | O_WRONLY, 0644);
		//This code checks that the file name is of the correct format
		bool invalid = fileCheck(fileName, copy_buff, 6);

		if (invalid == true || strcmp(version, "HTTP/1.1") != 0) {
			write(*pclient, "HTTP/1.1 400 BAD REQUEST\r\nContent-Length: 0\r\n\r\n", strlen("HTTP/1.1 400 BAD REQUEST\r\nContent-Length: 0\r\n\r\n"));
			//this might be wrong, doesn't write the wrong http version
			if (globals->logBool == true) {
				int errornum = 400;
				errorLog(request, name, version, globals, &log, errornum);
			}
		}
		//check to see if file was accessed correctly
		if (fd == -1 && invalid == false) {
			fprintf(stderr, "%s\n", strerror(errno));
			//permission denied, return 403
			if (strcmp(strerror(errno), "Permission denied") == 0 || strcmp(fileName, "healthcheck") == 0) {
				write(*pclient, "HTTP/1.1 403 FORBIDDEN\r\nContent-Length: 0\r\n\r\n", strlen("HTTP/1.1 403 FORBIDDEN\r\nContent-Length: 0\r\n\r\n"));
				if (globals->logBool == true) {
					int errornum = 403;
					errorLog(request, name, version, globals, &log, errornum);
				}

			}
			//file DNE, return 404
			else if (strcmp(strerror(errno), "No such file or directory") == 0) {
				write(*pclient, "HTTP/1.1 404 NOT FOUND\r\nContent-Length: 0\r\n\r\n", strlen("HTTP/1.1 404 NOT FOUND\r\nContent-Length: 0\r\n\r\n"));
				if (globals->logBool == true) {
					int errornum = 404;
					errorLog(request, name, version, globals, &log, errornum);
				}
			}
		}

		//file was accessed correctly, proceed with correct response
		else {
			if (invalid == false) {
				struct stat buf;
				stat(fileName, &buf);
				int size = buf.st_size;
				char* cl = malloc(100);
				//convert int to string
				sprintf(cl, "%d", size);
				strcat(cl, "\r\n\r\n");

				write(*pclient, "HTTP/1.1 200 OK\r\nContent-Length: ", strlen("HTTP/1.1 200 OK\r\nContent-Length: "));
				write(*pclient, cl, strlen(cl));
				free(cl);

				//writing to logFile
				if (globals->logBool == true) {
					char logWrite[400];
					sprintf(logWrite, "HEAD /%s length %d\n========\n", fileName, size);
					pthread_mutex_lock(&mutex1);
					int current = globals->offset;
					globals->offset += strlen(logWrite);
					pthread_mutex_unlock(&mutex1);
					pwrite(log, &logWrite, strlen(logWrite), current);

				}
			}
		}
	}

	//code to parse put request
	else if (putReq == 0) {
		printf("%s\n", "put request");

		char* space = strstr((char*)(&copy_buff[4]), " ");
		space[0] = '\0';
		//printf("%s\n", space);
		char* token;
		char *saveptr;
		const char s[4] = "\r\n";
		token = strtok_r(space + 1, s, &saveptr);
		char* cl;
		char* fileName  = (char*)(&copy_buff[5]);

		//checking if healthcheck was sent as PUT
		if (strcmp(fileName, "healthcheck") == 0) {
			printf("%s\n", "healthcheck in PUT");
			write(*pclient, "HTTP/1.1 403 FORBIDDEN\r\nContent-Length: 0\r\n\r\n", strlen("HTTP/1.1 403 FORBIDDEN\r\nContent-Length: 0\r\n\r\n"));
			if (globals->logBool == true) {
				int errornum = 403;
				errorLog(request, name, version, globals, &log, errornum);
			}
			return;
		}

		//find the length of the content header
		while (token != NULL) {
			//if the token contains content-length, make the pointer cl point to the number specifed by content length
			if (strstr(token, "Content-Length:") != NULL) {
				cl = token + 16;
			}
			token = strtok_r(NULL, s, &saveptr);
		}

		//convert the char* to an integer
		int contentLength;

		//convert int content length to a string cl
		sscanf(cl, "%d", &contentLength);

		//check to see fileName is of correct syntax
		bool invalid = fileCheck(fileName, copy_buff, 5);

		if (invalid == true || strcmp(version, "HTTP/1.1") != 0) {
			write(*pclient, "HTTP/1.1 400 BAD REQUEST\r\nContent-Length: 0\r\n\r\n", strlen("HTTP/1.1 400 NOT FOUND\r\nContent-Length: 0\r\n\r\n"));
			if (globals->logBool == true) {
				int errornum = 400;
				errorLog(request, name, version, globals, &log, errornum);
			}
		}

		//create the file or open with write only if it is created
		int fd = open(fileName, O_CREAT | O_WRONLY | O_TRUNC, 0644);
		//size_t bias = strlen(cl);

		//check to see if file was accessed correctly
		if (fd == -1 && invalid == false) {
			fprintf(stderr, "%s\n", strerror(errno));
			if (strcmp(strerror(errno), "Permission denied") == 0) {
				write(*pclient, "HTTP/1.1 403 FORBIDDEN\r\nContent-Length: 0\r\n\r\n", strlen("HTTP/1.1 403 FORBIDDEN\r\nContent-Length: 0\r\n\r\n"));

				if (globals->logBool == true) {
					int errornum = 403;
					errorLog(request, name, version, globals, &log, errornum);
				}
			}
		}

		else {
			if (invalid == false) {
				ssize_t count = 0;
				ssize_t total = 0;

				while (total != (ssize_t)(contentLength)) {
					uint8_t messageBuff[4096];
					count = read(*pclient, messageBuff, sizeof(messageBuff));
					write(fd, messageBuff, count);
					//printf("%ld\n", count);
					total = total + count;
					//printf("%ld\n", total);
				}
				//write the response
				write(*pclient, "HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n", sizeof("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n"));
				close(fd);
				//logging is enabled
				//to pass, struct globals, fileName, contentlengthsize,
				if (globals->logBool == true) {
					gnarlog(globals, fileName, total, &log, request);
				}
			}
		}
	}
	//request is not a GET,HEAD,or PUT. Treat as an invalid request.
	else {
		printf("%s\n", "wrong request");
		write(*pclient, "HTTP/1.1 400 BAD REQUEST\r\nContent-Length: 0\r\n\r\n", strlen("HTTP/1.1 400 NOT FOUND\r\nContent-Length: 0\r\n\r\n"));
		//printf("%s %s %s\n", request, name, version );
		if (globals->logBool == true) {
			int errornum = 400;
			errorLog(request, name, version, globals, &log, errornum);
		}
	}
	close(log);
	close(*pclient);
}

//when signal is received, dequeue a request and assign to worker thread. This iwll be handed off to req_parse.
void* resolve(void *args) {
	//infinite loop so we reuse our threads
	while (1) {
		inputs *globals = (inputs*)args;
		int *pclient;
		pthread_mutex_lock(&mutex);

		//making sure threads will only wait if they can't get work from the queue
		if ((pclient = dequeue()) == NULL) {
			//thread waits until it is signaled
			pthread_cond_wait(&condition_var, &mutex);
			pclient = dequeue();

		}
		pthread_mutex_unlock(&mutex);
		//there is a node on the queue. We have work to do
		if (pclient != NULL) {
			req_parse(pclient, globals);
		}
	}
}



#define BUFFER_SIZE 512

int main(int argc, char** argv) {
	//default number of threads
	int numThreads = 4;

	//create new struct of type inputs called args
	inputs args;
	args.reqs = 0;
	args.errors = 0;
	args.offset = 0;
	args.logBool = false;
	args.logName = "";

	int port = 8080;
	if (argc < 2) {
		printf("%s\n", "Not enough args specified");
		abort();
	}

	int opt;
	while (1) {
		opt = getopt(argc, argv, "N:l:");
		if (opt == -1) {
			break;
		}
		switch (opt) {
		case 'N':
			numThreads = atoi(optarg);
			break;
		case 'l':
			args.logBool = true;
			args.logName = optarg;
			break;
		case '?':
			printf("%s\n", "invalid input");
		}
	}

	//option that was provided was caught as one of the cases, must be the port
	if (optind != argc) {
		port = atoi(argv[optind]);
		if ((port) <= 0) {
			fprintf(stderr, "%s\n", "Port number must be greater than 0");
			abort();
		}
	}
	
	printf("Number of threads: %d\nPort: %d\nlogging enabled: %d\nlogfile: %s\n", numThreads, port, args.logBool, args.logName);


	int logFile;
	if (args.logBool == true) {
		//if the log file exists, clear it
		if ( open(args.logName, O_WRONLY) != -1) {
			int file = open(args.logName, O_TRUNC);
			close(file);
		}
		//regardless, creating the log file to pass the tests :/
		logFile = open(args.logName, O_CREAT | O_WRONLY, 0644);
		close(logFile);

	}

	//create the threads
	for (int i = 0; i < numThreads; i++) {
		pthread_create(&threadpool[i], NULL, resolve, &args);
	}

	/*
	    Create sockaddr_in with server information
	*/


	//char* port = argv[1];
	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons((port));
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	socklen_t addrlen = sizeof(server_addr);

	/*
	    Create server socket
	*/
	int server_sockd = socket(AF_INET, SOCK_STREAM, 0);

	// Need to check if server_sockd < 0, meaning an error
	if (server_sockd < 0) {
		perror("socket");
	}

	/*
	    Configure server socket
	*/
	int enable = 1;

	/*
	    This allows you to avoid: 'Bind: Address Already in Use' error
	*/
	int ret = setsockopt(server_sockd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

	/*
	    Bind server address to socket that is open
	*/
	ret = bind(server_sockd, (struct sockaddr *) &server_addr, addrlen);
	//printf("%d\n", ret);
	/*
	    Listen for incoming connections
	*/
	ret = listen(server_sockd, 5); // 5 should be enough, if not use SOMAXCONN

	if (ret < 0) {
		return 1;
	}

	/*
	    Connecting with a client
	*/
	struct sockaddr client_addr;

	socklen_t client_addrlen = sizeof(client_addr);

	//infinite loop to accept all requests sent to server
	//referenced this tutorial online: https://youtube/P6Z5K8zmEmc
	while (1) {
		printf("[+] server is waiting...\n");
		//increment total reqs
		int client_sockd = accept(server_sockd, &client_addr, &client_addrlen);

		//dispatch thread
		int *pclient = malloc(sizeof(int));
		*pclient = client_sockd;

		//lock the enqueue operation until it is finished
		pthread_mutex_lock(&mutex2);

		enqueue(pclient);
		//increment number of requests receieved
		args.reqs += 1;
		//signal thread (in dequeue) to stop waiting
		pthread_cond_signal(&condition_var);
		pthread_mutex_unlock(&mutex2);
		//printf("%d %s\n", client_sockd, strerror(errno));
	}
}

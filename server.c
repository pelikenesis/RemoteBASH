// RemoteBASH
// Server

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 199309L
#define _XOPEN_SOURCE 600
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <netinet/in.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include "tpool.h"

// Define preprocessor constants for the I/O buffer, port, and shared secret
#define PORT 4070
#define SECRET "<rembash>\n"
#define BUFF_SIZE 4096
#define MAX_EVENTS 1
#define MAX_NUM_CLIENTS 1000

// Function prototypes
void set_up_socket(int *server_sockfd);
void *event_loop();
void process_task(int task);
void handle_client(int connect_fd);
void relay_data(int source, int target);
int check_secret(int connect_fd);
int set_up_pty(int *master_fd, char **slave_fd);
void pty_exec_bash(char *slave_name);
void print_id_info(char *message);

//Globals for epoll FD and array of socket/pty-master FD pairs
int epfd;
int fds[MAX_NUM_CLIENTS*2+5];
int fdstate[MAX_NUM_CLIENTS*2+5];


int main()
{
	#ifdef DEBUG
	print_id_info("Server starting: \n");
	#endif

	// Rembash and socket FD variables
	const char * const rembash = "<rembash>\n";
	int server_sockfd, client_sockfd;
	pthread_t tid;

	// Call function to set up server socket
	set_up_socket(&server_sockfd);

	// Create epoll unit
	if ((epfd = epoll_create1(EPOLL_CLOEXEC)) == -1) {
		perror("Server: Error creating epoll unit");
		exit(EXIT_FAILURE); }

	// Create thread for event_loop
	if (pthread_create(&tid, NULL, event_loop, NULL) == -1) {
		perror("Server: Error creating event_loop thread\n");
		exit(EXIT_FAILURE); }
	
	// Set SIGCHLD signal to be ignored so don't have to wait for child process
	signal(SIGCHLD, SIG_IGN);
	
	// Initialize thread pool
	if (tpool_init(process_task) != 1) {
		perror("Server: Error initializing thread pool");
		exit(EXIT_FAILURE); }

	// Accept and client handling loop
	while (1) {
		
		#ifdef DEBUG
		printf("before accept\n");
		#endif
		
		// Accept connections from clients and handle them
		if ((client_sockfd = accept4(server_sockfd, NULL, NULL, SOCK_CLOEXEC|SOCK_NONBLOCK)) != -1) {
			#ifdef DEBUG
			printf("after accept\n");
			#endif
			
			// Check if space for client
			if (client_sockfd >= 2 * MAX_NUM_CLIENTS + 5) {
				close(client_sockfd);
				break; }
			
			// Add client FD to state array; 0 means before secret
			fdstate[client_sockfd] = 0;
			
			// Add client FD to epoll interest list
			struct epoll_event event;
			event.events = EPOLLIN|EPOLLET;
			event.data.fd = client_sockfd;
			if (epoll_ctl(epfd, EPOLL_CTL_ADD, client_sockfd, &event) == -1) {
				perror("Server: Error adding client_sockfd to epoll interest list");
				pthread_exit(NULL); }
			
			// Write initial rembash message to client
			if (write(client_sockfd, rembash, strlen(rembash)) == -1) {
				perror("Server: Error writing rembash to socket");
				close(client_sockfd);
				continue; } }
	}

	// Program should not get here, so exit with failure if it does
	exit(EXIT_FAILURE);
}

// Function to create server socket, bind to port, and start listening for connections
void set_up_socket(int *server_sockfd)
{
	// Struct for server
	struct sockaddr_in server_address;

	// Create socket for server
	if ((*server_sockfd = socket(AF_INET, SOCK_STREAM|SOCK_CLOEXEC, 0)) == -1) {
		perror("Server: socket call failed");
		exit(EXIT_FAILURE); }

	// Set socket to reuse ports immediately
	int i = 1;
	setsockopt(*server_sockfd, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i));

	// Set up server struct for TCP, PORT, and any IP Address
	memset(&server_address, 0, sizeof(server_address));
	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(PORT);
	server_address.sin_addr.s_addr = htonl(INADDR_ANY);

	// Bind socket to PORT using server struct
	if (bind(*server_sockfd, (struct sockaddr *)&server_address, sizeof(server_address)) == -1) {
		perror("Server: bind call failed");
		exit(EXIT_FAILURE); }

	// Set up listening socket, ignore child exit status, and loop for connections
	if (listen(*server_sockfd, 10) == -1) {
		perror("Server: listen call failed");
		exit(EXIT_FAILURE); }

	return;
}

void *event_loop()
{
	#ifdef DEBUG
	print_id_info("New thread for event_loop: \n");
	#endif

	// Variables for epoll loop
	int ready_fds;
	struct epoll_event current_event;
	struct epoll_event events[MAX_EVENTS];
	
	#ifdef DEBUG
	printf("before epoll_wait\n");
	#endif

	// Start epoll_wait loop
	while ((ready_fds = epoll_wait(epfd, events, MAX_EVENTS, -1)) > 0) {
		#ifdef DEBUG
		printf("in epoll_wait\n");
		#endif
		
		// Loop through ready FDs and relay data
		for (int i=0; i < ready_fds; i++) {
			// Get current event from returned epoll events struct
			current_event = events[i];
			
			// If error or no data to read after epoll_wait, then close FDs
			if (current_event.events & (EPOLLHUP|EPOLLERR|EPOLLRDHUP)) {
				#ifdef DEBUG
				printf("\nClient closed using \"exit\"\n");
				printf("Closing FDs %d and %d...\n\n", fds[current_event.data.fd], current_event.data.fd);
				#endif

				// Close current FDs to avoid leaks
				close(fds[current_event.data.fd]);
				close(current_event.data.fd); }

			// Check if data available for read
			else if (current_event.events & EPOLLIN) {
				#ifdef DEBUG
				printf("Adding FD %d to task queue\n", current_event.data.fd);
				#endif
				// Add client to task queue
				if (tpool_add_task(current_event.data.fd) != 1) {
					perror("Server: Failed to add client to task queue");
					close(fds[current_event.data.fd]);
					close(current_event.data.fd); } }
		}
	}

	// Thread should not get here, so exit with failure if it does
	perror("epoll_wait failed");
	exit(EXIT_FAILURE);
}

void process_task(int task)
{
	#ifdef DEBUG
	printf("Processing task %d:\n", task);
	#endif
	if (fdstate[task] == 0) {
		handle_client(task);
		fdstate[task] = 1; }
	else {
		relay_data(task, fds[task]); }
}

void handle_client(int connect_fd)
{
	#ifdef DEBUG
	printf("Handling new client (FD %d): \n", connect_fd);
	#endif

	const char * const ok = "<ok>\n";
	const char * const err = "<error>\n";
	char *slave_name;
	int master_fd;
	char input[513];
	ssize_t nread;
	
	#ifdef DEBUG
	printf("Reading secret from new client (FD %d)\n", connect_fd);
	#endif

	// Get secret from client
	if ((nread = read(connect_fd, input, 512)) == -1 && nread != EAGAIN) {
		perror("Server: Error reading SECRET from socket");
		write(connect_fd, err, strlen(err));
		close(connect_fd); }

	// Check that new message from client is valid secret
	input[nread] = '\0';
	if (strcmp(input, SECRET)) {
		fprintf(stderr, "Server: Invalid secret received: %s", input);
		close(connect_fd); }

	// Set up pty master/slave pair
	if (set_up_pty(&master_fd, &slave_name)) {
		close(connect_fd); }

	// Fork child process and exec bash
	// Fork subprocess
	switch (fork()) {
	case -1: // Fork failed
		perror("Server: fork call failed");
		close(connect_fd);

	case 0: // Child process
		#ifdef DEBUG
		print_id_info("Running bash in subprocess (pre setsid): \n");
		#endif
		
		close(connect_fd);
		close(master_fd);
		pty_exec_bash(slave_name);
		
		// Make sure child process exits
		exit(EXIT_FAILURE);
	}
	
	// Parent Process
	
	fdstate[connect_fd] = 1;
	fdstate[master_fd] = 1;
	
	// Store connect_fd and master_fd in FD array
	fds[connect_fd] = master_fd;
	fds[master_fd] = connect_fd;
	
	// Add master FD to the epoll interest list
	struct epoll_event event;
	event.events = EPOLLIN|EPOLLET;
	event.data.fd = master_fd;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, master_fd, &event) == -1) {
		perror("Server: Error adding master_fd to epoll interest list");
		close(connect_fd);
		close(master_fd); }
	
	// Write ok to client
	if (write(connect_fd, ok, strlen(ok)) == -1) {
		perror("Server: Error writing OK to socket");
		close(connect_fd);
		close(master_fd); }

	#ifdef DEBUG
	printf("Finished protocol exchange for new client (FD %d)\n\n", connect_fd);
	#endif

	// Handle_client finished successfully
	return;
}

void relay_data(int source, int target)
{
	#ifdef DEBUG
	printf("Relaying data: %d -> %d\n", source, target);
	#endif
	
	// Variables for I/O
	char buff[BUFF_SIZE];
	ssize_t nread, nwritten, total;
	
	// Relay data from current_event FD to its pair
	errno = 0;
	while ((nread = read(source, buff, BUFF_SIZE)) > 0) {
		total = 0;
		do {
		if ((nwritten = write(target,buff+total,nread-total)) == -1) break;
			total += nwritten;
		} while (total < nread); }

	// Check if error or EOF encountered and close FDs
	if (nread < 1 && errno != EAGAIN) {
		#ifdef DEBUG
		if (errno)
			perror("Server: Error reading from current_event FD:");
		else
			printf("\nClient closed using \"Ctrl + C\"\n");
		printf("Closing FDs %d and %d...\n\n", source, target);
		#endif

		// Close current FDs to avoid leaks
		close(source);
		close(target); }
		
		return;
}

// Function to set up pty and open master and slave FDs
int set_up_pty(int *master_fd, char **slave_name)
{
	// Variables for pty slave name
	char *temp_name;
	char *sname;

	// Open pty master and get its file descriptor
	if ((*master_fd = posix_openpt(O_RDWR|O_CLOEXEC|O_NONBLOCK)) == -1) {
		perror("Server: failed to open pty master");
		return 1; }

	// Set up permissions for pty slave
	if ((grantpt(*master_fd) == -1)) {
		perror("Server: grantpt call failed");
		close(*master_fd);
		return 1; }

	// Unlock the pty slave so it can be opened
	if (unlockpt(*master_fd) == -1) {
		perror("Server: unlockpt call failed");
		close(*master_fd);
		return 1; }

	// Get the pty slave name
	if ((temp_name = ptsname(*master_fd)) == NULL) {
		if (errno) {
			perror("Server: ptsname call failed"); }
		else {
			fprintf(stderr, "Server: ptsname call returned NULL\n"); }
		close(*master_fd);
		return 1; }

	if ((sname = malloc(strlen(temp_name)+1)) == NULL) {
		perror("Server: Failed to allocate memory for slave_name");
		return 1; }
	strcpy(sname, temp_name);
	*slave_name = sname;

	return 0;
}

// Function to fork subprocess, create new session, redirect std fds to slave fd, and exec bash
void pty_exec_bash(char *slave_name)
{
	// Set child's sid to be child's pid
	if (setsid() == -1) {
		perror("Server: setsid call failed");
		return; }
	
	// Open the pty slave and get its file descriptor
	int slave_fd;
	if ((slave_fd = open(slave_name, O_RDWR)) == -1) {
		perror("Server: failed to open pty slave");
		return; }

	#ifdef DEBUG
	print_id_info("Starting bash in subprocess (post setsid): \n");
	#endif

	// Redirect stdin, stdout, and stderr fds to socket
	if (dup2(slave_fd, STDIN_FILENO) == -1) {
		perror("Server: dup2 call for STDIN_FILENO failed");
		return; }
	if (dup2(slave_fd, STDOUT_FILENO) == -1) {
		perror("Server: dup2 call for STDOUT_FILENO failed");
		return; }
	if (dup2(slave_fd, STDERR_FILENO) == -1) {
		perror("Server: dup2 call for STDERR_FILENO failed");
		return; }

	// Exec bash, using redirected fds for I/O
	execlp("bash", "bash", NULL);

	// In case exec call fails, make sure to exit child process
	exit(EXIT_FAILURE);

	return;
}


// Function to print process/thread information
void print_id_info(char *message)
{
	printf("%sTID=%ld, PID=%ld, PGID=%ld, SID=%ld\n\n", message, (long)syscall(SYS_gettid), (long)getpid(),(long)getpgrp(),(long)getsid(0));
}


// EOF

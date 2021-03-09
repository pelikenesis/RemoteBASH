// RemoteBASH
// Client

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <netinet/in.h>
#include <signal.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <termios.h>

// Define preprocessor constants for the command buffer, port, and shared secret
#define BUFF_SIZE 4096
#define PORT 4070
#define SECRET "<rembash>\n"

// Function prototypes
void set_up_socket(int *sockfd, const char * const server_ip);
void proto_exchange(int sockfd);
void set_term_attr();
int set_sigchld_handler(struct sigaction *act);
void fork_IO_loops(int sockfd);
int reset_sigchld_handler(struct sigaction *act);
void restore_term_attr();
void sigchld_handler(int signal);

// Global struct for saved terminal attributes
struct termios saved_attr;


int main(int argc, char **argv)
{
	// Check for proper number of command line arguments
	if (argc != 2) {
		fprintf(stderr, "Usage: client SERVER_IP_ADDRESS\n");
		exit(EXIT_FAILURE); }

	// Variables for socket connection
	const char * const server_ip = argv[1];
	int sockfd;

	// Set up client socket and connect to server
	set_up_socket(&sockfd, server_ip);

	// Handle protocol exchange with server
	proto_exchange(sockfd);

	// Set noncanonical mode and disable echoing
	set_term_attr();

	// Fork subprocess and create loops for I/O with server
	fork_IO_loops(sockfd);

	// Reset original terminal attributes
	restore_term_attr();

	// Loops exited, I/O finished, child terminated, now exit program with success
	printf("\n");
	exit(EXIT_SUCCESS);
}


// Function to create socket and connect to server
void set_up_socket(int *sockfd, const char * const server_ip)
{
	// Struct for server
	struct sockaddr_in address;

	// Create a socket for the client
	if ((*sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("Client: failed to create socket");
		exit(EXIT_FAILURE); }

	// Set up socket struct
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(PORT);
	inet_aton(server_ip, &address.sin_addr);

	// Connect client socket to server socket
	if (connect(*sockfd, (struct sockaddr *)&address, sizeof(address)) == -1) {
		perror("Client: failed to connect socket to server");
		exit(EXIT_FAILURE); }

	return;
}

// Function for rembash protocol exchange with server
void proto_exchange(int sockfd)
{
	// Variables for protocol exchange
	const char * const rembash = "<rembash>\n";
	const char * const ok = "<ok>\n";
	char input[513];
	ssize_t nread;

	// Get initial message from server
	if ((nread = read(sockfd, input, 512)) < 1) {
		if (errno) {
			perror("Client: Error reading protocol ID from server"); }
		else {
			fprintf(stderr,"Client: server connection closed unexpectedly\n"); }
		exit(EXIT_FAILURE); }

	// Check that first message from server is "<rembash>\n"
	input[nread] = '\0';
	if (strcmp(input, rembash)) {
		fprintf(stderr, "Client: invalid protocol ID from server: %s\n", input);
		exit(EXIT_FAILURE); }

	// Write shared secret to server
	if (write(sockfd, SECRET, strlen(SECRET)) == -1) {
		perror("Client: Error writing shared secret to socket");
		exit(EXIT_FAILURE); }

	// Get last protocol message from server
	if ((nread = read(sockfd, input, 512)) < 1) {
		if (errno) {
			perror("Client: Error reading shared secret acknowledgment from server"); }
		else {
			fprintf(stderr, "Client: server connection closed unexpectedly\n"); }
		exit(EXIT_FAILURE); }

	// Check that last protocol message is "<ok>\n
	input[nread] = '\0';
	if (strcmp(input, ok)) {
		fprintf(stderr, "Client: invalid shared secret acknowledgment from server\n");
		exit(EXIT_FAILURE); }

	return;
}

// Function to fork subprocess and create I/O loops in parent and child
// Parent reads from fd and writes to stdout
// Child reads from stdin and writes to fd
void fork_IO_loops(int sockfd)
{
	// Variables for subprocess ID and I/O loops
	char buff[BUFF_SIZE];
	pid_t pid;
	ssize_t nread, nwritten, total;
	struct sigaction act;

	// Set sigchld_handler to be used for SIGCHLD signal
	if (set_sigchld_handler(&act)) {
		perror("Client: Error registering handler for SIGCHLD");
		exit(EXIT_FAILURE); }

	// Fork subprocess and store child's PID in pid
	switch (pid = fork()) {
	case -1: // Fork failed
		perror("Client: fork call failed");
		exit(EXIT_FAILURE);

	case 0: // Child process
		// Loop, reading from stdin and writing to socket
		nwritten = 0;
		while (nwritten != -1 && (nread = read(STDIN_FILENO, buff, BUFF_SIZE)) > 0) {
			total = 0;
			do {
				if ((nwritten = write(sockfd, buff+total, nread-total)) == -1)
					break;
				total += nwritten;
			} while (total < nread);
		}

		// Check if error occurred or EOF encountered
		if (errno) {
			perror("Client: Error reading commands from stdin and/or writing to socket"); }
		else {
			fprintf(stderr,"Client: socket connection closed prematurely"); }

		// Parent should have terminated child; if not, something went wrong
		exit(EXIT_FAILURE);
	}

	// Parent process
	// Loop, reading from socket and writing to stdout
	nwritten = 0;
	while (nwritten != -1 && (nread = read(sockfd, buff, BUFF_SIZE)) > 0) {
		total = 0;
		do {
			if ((nwritten = write(STDOUT_FILENO, buff+total, nread-total)) == -1) {
				break; }
			total += nwritten;
		} while (total < nread);
	}

	// Check if error occurred in I/O loop
	if (errno) {
		perror("Client: Error reading from socket and/or writing to stdout"); }

	// I/O finished; call function to reset SIGCHLD signal to be ignored
	if (reset_sigchld_handler(&act)) {
		perror("Client: Error setting SIGCHLD to be ignored"); }

	// Make sure child has been terminated
	kill(pid, SIGTERM);

	return;
}

// Function to set terminal attributes
// First saves current terminal attributes
// Then sets noncanonical mode and disables echoing
void set_term_attr()
{
	struct termios new_attr;

	// Check that STDIN_FILENO is associated with a terminal
	if (!isatty(STDIN_FILENO)) {
		fprintf(stderr, "Client: FD %d is not a terminal\n", STDIN_FILENO);
		exit(EXIT_FAILURE); }

	// Saves original terminal attributes in saved_attr struct
	if (tcgetattr(STDIN_FILENO, &saved_attr) == -1) {
		perror("Client: Error getting terminal attributes");
		exit(EXIT_FAILURE); }

	// Initialize new_attr with saved_attr values and set proper flags
	new_attr = saved_attr;
	new_attr.c_lflag &= ~(ICANON|ECHO);
	new_attr.c_cc[VMIN] = 1;
	new_attr.c_cc[VTIME] = 0;

	// Set the new attributes using new_attr after any pending write is finished
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_attr) == -1) {
		perror("Client: Error setting terminal attributes");
		exit(EXIT_FAILURE); }

	return;
}

// Function to immediately reset original terminal attributes
void restore_term_attr()
{
	if (tcsetattr(STDIN_FILENO, TCSANOW, &saved_attr) == -1) {
		perror("Client: Error restoring terminal attributes");
		exit(EXIT_FAILURE); }

	return;
}

// Function to set up SIGCHLD handler
// Returns 0 on success or -1 on failure
int set_sigchld_handler(struct sigaction *act)
{
	act->sa_handler = sigchld_handler;
	act->sa_flags = 0;
	sigemptyset(&act->sa_mask);
	if (sigaction(SIGCHLD, act, NULL) == -1) {
		return -1; }

	return 0;
}

// Function to reset SIGCHLD signal to be ignored
// Returns 0 on success or -1 on failure
int reset_sigchld_handler(struct sigaction *act)
{
	act->sa_handler = SIG_IGN;
	if (sigaction(SIGCHLD, act, NULL) == -1) {
		return -1; }

	return 0;
}

// Handler to deal with SIGCHLD signal
// Collects child process and exits with failure status
void sigchld_handler(int signal)
{
	wait(NULL);
	restore_term_attr();
	exit(EXIT_FAILURE);
}


// EOF

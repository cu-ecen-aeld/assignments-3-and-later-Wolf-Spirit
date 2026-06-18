#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>
#include <arpa/inet.h>

#define PORT "9000"
#define BACKLOG 10
#define OUT_FILE "/var/tmp/aesdsocketdata"
#define BUFF_SIZE 512

int server_fd = -1;
struct addrinfo *server_info;

void signal_handler(int sig) {

	if (sig == SIGINT || sig == SIGTERM) {
		syslog(LOG_DEBUG, "Caught signal, exiting");

		if (!access(OUT_FILE, F_OK)) {
			if (unlink(OUT_FILE) == -1)
				perror("Couldn't delete the file");
		}

		if (server_info != NULL)
			freeaddrinfo(server_info);

		if (server_fd != -1) {
			syslog(LOG_DEBUG, "Closing socket");
			close(server_fd);
		}

		syslog(LOG_DEBUG, "Closing syslog");
		closelog();	
		
		exit(EXIT_SUCCESS);
	}
}

void client_handler(int client_fd) {
	char recv_buff[BUFF_SIZE];

	FILE *file = fopen(OUT_FILE, "a+");
	if (file == NULL) {
		perror("Couldn't create or open file");
		close(client_fd);
		return;
	}

	int data_to_recv;
	while ((data_to_recv = recv(client_fd, recv_buff, BUFF_SIZE - 1, 0)) > 0) {
		if (fwrite(recv_buff, sizeof(char), data_to_recv, file) == 0) {
			perror("Couldn't write to file");
			fclose(file);
			return;
		}
		fflush(file);

		// Check if newline exists in currently received chunk
		char *newline = memchr(recv_buff, '\n', data_to_recv);
		if (newline != NULL) {
			fseek(file, 0, SEEK_SET);
			char send_buff[BUFF_SIZE];
			int data_to_send;
			while ((data_to_send = fread(send_buff, 1, BUFF_SIZE - 1, file)) > 0) {
				if (send(client_fd, send_buff, data_to_send, 0) < 0) {
					perror("Couldn't send data to client");
					fclose(file);
					return;
				}
			}
			fseek(file, 0, SEEK_END);
		}
	}

	if (data_to_recv < 0)
		perror("Error while receiveing data from client");

	fclose(file);
	close(client_fd);
}

int create_daemon(void) {
	pid_t pid = fork();

	if (pid < 0)
		exit(EXIT_FAILURE);
	else if (pid > 0)
		exit(EXIT_SUCCESS);

	if (setsid() < 0)
		exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
	if (argc > 1 && strcmp(argv[1], "-d") == 0)
		create_daemon();

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = &signal_handler;
	sa.sa_flags = 0;

	if (sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGTERM, &sa, NULL) == -1) {
		perror("Error while setting signal handlers");
		exit(EXIT_FAILURE);
	}

	openlog("aesdsocket", LOG_PID, LOG_USER);

	struct addrinfo hints;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	int status = getaddrinfo(NULL, PORT, &hints, &server_fd);
	if (status != 0) {
		perror("getaddrinfo error");
		exit(EXIT_FAILURE);
	}

	server_fd = socket(server_info->ai_family, server_info->ai_socktype, server_info->ai_protocol);
	if (server_fd == -1) {
		perror("Error while creating socket");\
		freeaddrinfo(server_info);
		exit(EXIT_FAILURE);
	}

	// Allow socket address reuse immediately after shutdown
	int reuse = 1;
	setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

	if (bind(server_fd, server_info->ai_addr, server_info->ai_addrlen) < 0) {
		perror("Binding failed");
		freeaddrinfo(server_info);
		exit(EXIT_FAILURE);
	}

	freeaddrinfo(server_info); // Save to free after binding

	if (listen(server_fd, BACKLOG) < 0) {
		perror("Listening to port failed");
		exit(EXIT_FAILURE);
	}

	while(1) {
		struct sockaddr_in client_addr; // Fixed struct type
		socklen_t addr_len = sizeof(client_addr);

		int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
		if (client_fd == -1) {
			perror("ERROR: Accept failed");
			continue;
		}

		char ip_str[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));

		syslog(LOG_DEBUG, "Accepted connection from %s", ip_str);

		client_handler(client_fd);

		syslog(LOG_DEBUG, "Closed connection from %s", ip_str);
	}

	return 0;
}

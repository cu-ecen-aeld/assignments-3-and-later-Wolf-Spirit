#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
// #include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
// #include <netinet/in.h>
#include <netdb.h>
// #include <arpa/inet.h>
// #include <sys/wait.h>
#include <signal.h>
#include <syslog.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/sendfile.h>

#define PORT "9000"
#define BACKLOG 10
#define OUT_FILE "/var/tmp/aesdsocketdata" 
#define BUFF_SIZE 512

int server_fd = -1;
int client_fd = -1;
int ret;

void handler(int sig) {
	// Close any open connections
	close(client_fd);
	close(server_fd);

	// Delete data file
	ret = remove(OUT_FILE);
	if (ret == -1) {
		perror("remove");
		exit(EXIT_FAILURE);
	}
	syslog(LOG_DEBUG, "Caught signal, exiting");

	exit(EXIT_SUCCESS);
}

int set_handler(void){
	struct sigaction act;
	act.sa_handler = handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	ret = sigaction(SIGINT, &act, NULL);
	if (ret == -1) {
		perror("Error");
		return -1;
	}
	ret = sigaction(SIGTERM, &act, NULL);
	if (ret == -1) {
		perror("Error");
		return -1;
	}

	return 0;
}

int create_daemon(void) {
	pid_t pid = fork(); // fork
	if (pid == -1) {
		perror("fork");
		return -1;
	}
	if (pid > 0)
		exit(EXIT_SUCCESS); // exit parent

	pid_t sid = setsid(); // create new session
	if (sid == -1) {
		perror("setsid");
		return -1;
	}

	pid = fork(); // fork again
	if (pid == -1) {
		perror("fork");
		return -1;
	}
	if (pid > 0)
		exit(EXIT_SUCCESS); // exit parent

	ret = chdir("/");
	if (ret == -1) {
		perror("chdir");
		return -1;
	}

	umask(0); // reset file permissons

	// Redirect std fd's
	int fd = open("/dev/null", O_RDWR);
	if (fd == -1) {
		perror("open /dev/null");
		return -1;
	}
	int new_fd = dup2(fd, 0);
	if (new_fd == -1) {
		perror("dup2");
		return -1;
	}

	new_fd = dup2(fd, 1);
	if (new_fd == -1) {
		perror("dup2");
		return -1;
	}

	new_fd = dup2(fd, 2);
	if (new_fd == -1) {
		perror("dup2");
		return -1;
	}

	if (fd > 2)
		close(fd);
	
	return 0;
}

int bind_socket(char* port_str) {
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	struct addrinfo *res;
	ret = getaddrinfo(NULL, port_str, &hints, &res);
	if (ret != 0) {
		printf("getaddrinfo: %s\n", gai_strerror(ret));
		return -1;
	}

	ret = bind(server_fd, res->ai_addr, res->ai_addrlen);
	if (ret == -1) {
		perror("bind");
		return -1;
	}
	freeaddrinfo(res);

	return 0;
}

int resize_buf(char** buf, size_t* buf_len, size_t add_len) {
	size_t new_len = *buf_len + add_len;
	char* tmp = realloc(*buf, new_len);
	if (tmp == NULL) {
		printf("realloc failed\n");
		return -1;
	}

	*buf = tmp;
	*buf_len = new_len;

	return 0;
}

int main(int argc, char *argv[]) {
	// Set signal handler for SIGINT and SIRTERM
	ret = set_handler();
	if (ret == -1)
		return ret;

	// Create socket
	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		perror("socket");
		return -1;
	}

	// Bind socket to port
	ret = bind_socket(PORT);
	if (ret == -1)
		return -1;

	// If '-d' option passed, run as daemon
	int opt = getopt(argc, argv, "d");
	if (opt == 'd') {
		ret = create_daemon();
		if (ret == -1)
			return -1;
	}

	// Set socket to listen for connection
	int backlog = 1;
	ret = listen(server_fd, backlog);
	if (ret == -1) {
		perror("listen");
		return -1;
	}

	// Open or create data file
	int datafd = open(OUT_FILE, O_RDWR | O_APPEND | O_CREAT);
	if (datafd == -1) {
		perror("open");
		return -1;
	}

	size_t packet_buf_len = 500;
	char* packet_buf = (char*)malloc(packet_buf_len);
	size_t packet_len = 0;

	struct sockaddr_in accepted_sockaddr;
	socklen_t addrlen = sizeof(accepted_sockaddr);
	ssize_t nread;
	const size_t BUF_SIZE = 500;
	char buf[BUF_SIZE];
	char* newline_ptr;
	ptrdiff_t newline_pos;
	size_t str_len;
	ssize_t nwritten;
	ssize_t nsent;
	struct stat statbuf;
	off_t offset = 0;
	
	// Accept conncetion until SIGINT or SIGTERM
	while(1) {
		// Peek from socket until closed or no more data
		nread = recv(client_fd, buf, BUF_SIZE, MSG_PEEK);
		
		if (nread == -1) {
			perror("recv");
			return -1;
		}
		else if (nread == 0) // If socket closed or no more data, break
			break;

		// Find newline character
		newline_ptr = memchr(buf, '\n', nread);
		newline_pos = newline_ptr == NULL ? nread - 1 : newline_ptr - buf;
		str_len = newline_pos + 1;

		// Increase packet buffer size if exceeded
		if ((packet_len + str_len) > packet_buf_len) {
			ret = resize_buf(&packet_buf, &packet_buf_len, str_len);
			if (ret == -1)
				return -1;
		}

		// Append str_len from stream to packet buffer
		nread = recv(client_fd, packet_buf + packet_len, str_len, 0);
		if (nread == -1) {
			perror("recv");
			return -1;
		}
		packet_len += str_len;

		// If packet not complete, loop back to receive again
		if (newline_ptr == NULL)
			continue;

		// Packet complete: write to data file
		nwritten = write(datafd, packet_buf, packet_len);
		if (nwritten == -1) {
			perror("write");
			return -1;
		}

		// Return contents of data file to clien
		ret = fstat(datafd, &statbuf);
		if (ret == -1) {
			perror("stat");
			return -1;
		}
		offset = 0;
		nsent = sendfile(client_fd, datafd, &offset, statbuf.st_size);
		if (nsent == -1) {
			perror("sendfile");
			return -1;
		}

		// Reset packet_len
		packet_len = 0;

	}

	// Close connection
	ret = close(client_fd);
	if (ret == -1) {
		perror("close");
		return -1;
	}
	syslog(LOG_DEBUG, "Closed connection from %u", accepted_sockaddr.sin_addr.s_addr);

	return 0;
}

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <syslog.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#define FILE_PERMISSIONS 0777

int main(int argc, char *argv[])
{
	openlog("writer", 0, LOG_USER);
	syslog(LOG_INFO, "Writing %s to %s", argv[2], argv[1]);

	if (argc < 3) {
		syslog(LOG_ERR, "Error! I missed something!");
		return 1;
	}

	int fd;
	fd = open(argv[1], O_WRONLY | O_APPEND | O_CREAT, FILE_PERMISSIONS);

	if (fd == -1) {
		syslog(LOG_ERR, "Error! Something wrong with the file!");
		return 1;
	}

	ssize_t nr;
	nr = write(fd, argv[2], strlen(argv[2]));

	if (nr == -1) {
		syslog(LOG_ERR, "Error! Something went wrong while writing to the file!");
		return 1;
	}

	return 0;
}

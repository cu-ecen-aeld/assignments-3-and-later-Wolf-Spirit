CC := $(CROSS_COMPILE)gcc

all: aesdsocket

default: all

aesdsocket: aesdsocket.c
	$(CC) aesdsocket.c -o aesdsocket

clean:
	rm -f *.o aesdsocket

# NAME: Joe Qian
# EMAIL: qzy@g.ucla.edu
# ID: 404816794

CC=gcc
CFLAGS=-std=c11 -Wall -Wextra -O2 -g
LDFLAGS=-lpthread

all: lab2_add

lab2_add: lab2_add.c
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

.PHONY: clean dist all
clean:
	-rm -rf *.o *.s lab2_add *.dSYM *.tar.gz

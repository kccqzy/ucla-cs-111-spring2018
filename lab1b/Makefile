# NAME: Joe Qian
# EMAIL: qzy@g.ucla.edu
# ID: 404816794

#CC=gcc
#CFLAGS=-std=c99 -Wall -Wextra -O2

CC=/usr/local/opt/llvm/bin/clang
CFLAGS=-Wall -Wextra  -O0 -g -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -g

all: lab1b-client lab1b-server

lab1b-common.o: lab1b-common.c lab1b-common.h
	$(CC) $(CFLAGS) $< -c -o $@

lab1b-client.o: lab1b-client.c lab1b-common.h
	$(CC) $(CFLAGS) $< -c -o $@

lab1b-server.o: lab1b-server.c lab1b-common.h
	$(CC) $(CFLAGS) $< -c -o $@

lab1b-client: lab1b-client.o lab1b-common.o
	$(CC) $(CFLAGS) $^ -o $@

lab1b-server: lab1b-server.o lab1b-common.o
	$(CC) $(CFLAGS) $^ -o $@

.PHONY: clean dist all
clean:
	-rm -rf *.o *.s lab1b-client lab1b-server *.dSYM lab1b-404816794.tar.gz

dist: lab1a-404816794.tar.gz

lab1b-404816794.tar.gz: Makefile README lab1b-client.c lab1b-server.c lab1b-common.h
	tar czf $@ $^

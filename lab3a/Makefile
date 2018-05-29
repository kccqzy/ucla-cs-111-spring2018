# NAME: Joe Qian
# EMAIL: qzy@g.ucla.edu
# ID: 404816794

CC=gcc
CFLAGS=-std=c99 -Wall -Wextra -O2
LDFLAGS=

all: lab3a

lab3a: lab3a.c ext2_fs.h
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

.PHONY: clean dist all
clean:
	-rm -rf *.o *.s lab3a *.dSYM lab3a-404816794.tar.gz

dist: lab3a-404816794.tar.gz

lab3a-404816794.tar.gz: Makefile README ext2_fs.h lab3a.c
	tar czf $@ $^

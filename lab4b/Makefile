# NAME: Joe Qian
# EMAIL: qzy@g.ucla.edu
# ID: 404816794

CC=gcc
CFLAGS=-std=c99 -Wall -Wextra -O2
LDFLAGS=-lm -lmraa

all: lab4b

lab4b: lab4b.c button.h temp.h
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

.PHONY: clean dist all

clean:
	-rm -rf *.o *.s lab4b lab4b-404816794.tar.gz

dist: lab4b-404816794.tar.gz

lab4b-404816794.tar.gz: Makefile README button.h temp.h lab4b.c
	tar czf $@ $^

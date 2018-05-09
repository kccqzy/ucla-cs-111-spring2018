# NAME: Joe Qian
# EMAIL: qzy@g.ucla.edu
# ID: 404816794

CC=gcc
CFLAGS=-std=c11 -Wall -Wextra -O2 -g
LDFLAGS=-lpthread

# Higher level targets
build: lab2_add lab2_list

tests: lab2_add.csv lab2_list.csv

clean:
	-rm -rf *.o *.s lab2_add lab2_list *.csv *.dSYM *.tar.gz *.png

dist: lab2a-404816794.tar.gz

.PHONY: clean dist build tests graphs

# Details

lab2_add: lab2_add.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

lab2_list: lab2_list.o SortedList.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

lab2_add.o: lab2_add.c
	$(CC) $(CFLAGS) $< -c -o $@

lab2_list.o: lab2_list.c SortedList.h
	$(CC) $(CFLAGS) $< -c -o $@

SortedList.o: SortedList.c SortedList.h
	$(CC) $(CFLAGS) $< -c -o $@

lab2_add.csv: lab2_add
	./lab2_add_test.sh

lab2_list.csv: lab2_list
	./lab2_list_test.sh

lab2a-404816794.tar.gz: lab2_add.c SortedList.h SortedList.c lab2_list.c Makefile lab2_add.csv lab2_list.csv lab2_add-1.png lab2_add-2.png lab2_add-3.png lab2_add-4.png lab2_add-5.png
	tar cjvf $@ $^

lab2_add-%.png: lab2_add.gp lab2_add.csv
	./lab2_add.gp

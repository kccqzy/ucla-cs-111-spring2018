# NAME: Joe Qian
# EMAIL: qzy@g.ucla.edu
# ID: 404816794

CC=gcc
CFLAGS=-std=c11 -Wall -Wextra -O2
LDFLAGS=-lpthread

# Higher level targets
build: lab2_add lab2_list

tests: lab2_add.csv lab2_list.csv

clean:
	-rm -rf *.o *.s lab2_add lab2_list *.dSYM *.tar.gz
# XXX clean should not delete graphs and data

clean-full:
	-rm -rf *.o *.s lab2_add lab2_list *.csv *.dSYM *.tar.gz *.png

dist: lab2a-404816794.tar.gz

graphs: lab2_add-1.png lab2_add-2.png lab2_add-3.png lab2_add-4.png lab2_add-5.png lab2_list-1.png lab2_list-2.png lab2_list-3.png lab2_list-4.png

.PHONY: clean clean-full real-dist dist build tests graphs

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

lab2_add-%.png: lab2_add.gp lab2_add.csv
	./lab2_add.gp

lab2_list-%.png: lab2_list.gp lab2_list.csv
	./lab2_list.gp

lab2a-404816794.tar.gz: lab2_add.c SortedList.h SortedList.c lab2_list.c Makefile lab2_add.csv lab2_list.csv lab2_add-1.png lab2_add-2.png lab2_add-3.png lab2_add-4.png lab2_add-5.png lab2_list-1.png lab2_list-2.png lab2_list-3.png lab2_list-4.png README lab2_add_test.sh lab2_list_test.sh lab2_add.gp lab2_list.gp
	tar czf $@ $^

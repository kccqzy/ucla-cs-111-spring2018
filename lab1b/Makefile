# NAME: Joe Qian
# EMAIL: qzy@g.ucla.edu
# ID: 404816794

all: lab1b-client lab1b-server

lab1b-common.o: lab1b-common.c lab1b-common.h
	gcc -std=c99 -Wall -Wextra -O2 $< -c -o $@

lab1b-client.o: lab1b-client.c lab1b-common.h
	gcc -std=c99 -Wall -Wextra -O2 $< -c -o $@

lab1b-server.o: lab1b-server.c lab1b-common.h
	gcc -std=c99 -Wall -Wextra -O2 $< -c -o $@

lab1b-client: lab1b-client.o lab1b-common.o
	gcc -std=c99 -Wall -Wextra -O2 $^ -o $@

lab1b-server: lab1b-server.o lab1b-common.o
	gcc -std=c99 -Wall -Wextra -O2 $^ -o $@

.PHONY: clean dist all
clean:
	-rm -rf *.o *.s lab1b-client lab1b-server *.dSYM lab1b-404816794.tar.gz

dist: lab1a-404816794.tar.gz

lab1b-404816794.tar.gz: Makefile README lab1b-client.c lab1b-server.c lab1b-common.h
	tar czf $@ $^

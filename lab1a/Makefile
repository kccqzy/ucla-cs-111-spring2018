# NAME: Joe Qian
# EMAIL: qzy@g.ucla.edu
# ID: 404816794

lab1a: lab1a.c
	gcc -g -std=c99 -Wall -Wextra -O2 $^ -o $@

.PHONY: clean dist check
clean:
	-rm -rf *.o *.s lab1a *.dSYM lab1a-404816794.tar.gz

dist: lab1a-404816794.tar.gz

lab1a-404816794.tar.gz: README lab1a.c Makefile
	tar czf $@ $^

check:
	true

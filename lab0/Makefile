lab0: lab0.c
	gcc -g -std=c99 -Wall -Wextra -O2 $^ -o $@

.PHONY: clean dist
clean:
	-rm -rf *.o *.s lab0 *.dSYM

dist: lab0.tar.gz

lab0.tar.gz: README lab0.c
	tar czf $@ $^

lab0: lab0.c
	gcc -g -std=c99 -Wall -Wextra -O2 $^ -o $@

.PHONY: clean dist check
clean:
	-rm -rf *.o *.s lab0 *.dSYM lab0-404816794.tar.gz

dist: lab0-404816794.tar.gz

lab0-404816794.tar.gz: README lab0.c smoke_test.sh Makefile backtrace.png breakpoint.png
	tar czf $@ $^

check: lab0 smoke_test.sh
	./smoke_test.sh

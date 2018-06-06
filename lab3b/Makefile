# NAME: Joe Qian
# EMAIL: qzy@g.ucla.edu
# ID: 404816794

all: lab3b

lab3b: lab3b.py
	cp lab3b.py lab3b

.PHONY: clean dist all
clean:
	-rm -f lab3b lab3b-404816794.tar.gz

dist: lab3b-404816794.tar.gz

lab3b-404816794.tar.gz: Makefile README lab3b.py
	tar czf $@ $^

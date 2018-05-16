# NAME: Joe Qian
# EMAIL: qzy@g.ucla.edu
# ID: 404816794

CC=gcc
CFLAGS=-std=c11 -Wall -Wextra -O2 -g
LDFLAGS=-lpthread -Wl,--no-as-needed,-lprofiler,--as-needed

# Higher level targets
build: lab2_list

tests: lab2b_1.csv lab2b_3.csv lab2b_4.csv lab2b_5.csv

graphs: lab2b_1.png lab2b_2.png lab2b_3.png lab2b_4.png lab2b_5.png

clean:
	-rm -rf *.o *.s lab2_list *.dSYM *.tar.gz

clean-full:
	-rm -rf *.o *.s lab2_list *.csv *.dSYM *.tar.gz *.png

profile: profile.out

.PHONY: clean clean-full real-dist dist build tests graphs

# Details

lab2_list: lab2_list.o SortedList.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

lab2_list.o: lab2_list.c SortedList.h
	$(CC) $(CFLAGS) $< -c -o $@

SortedList.o: SortedList.c SortedList.h
	$(CC) $(CFLAGS) $< -c -o $@

lab2b_1.csv: lab2_list lab2b_1.sh
	./lab2b_1.sh

lab2b_3.csv: lab2_list lab2b_3.sh
	./lab2b_3.sh

lab2b_4.csv: lab2_list lab2b_4.sh
	./lab2b_4.sh

lab2b_5.csv: lab2_list lab2b_5.sh
	./lab2b_5.sh

lab2b_1.png: lab2b_1.csv lab2b_1.gp
	./lab2b_1.gp

lab2b_2.png: lab2b_1.csv lab2b_2.gp
	./lab2b_2.gp

lab2b_3.png: lab2b_3.csv lab2b_3.gp
	./lab2b_3.gp

lab2b_4.png: lab2b_4.csv lab2b_4.gp
	./lab2b_4.gp

lab2b_5.png: lab2b_5.csv lab2b_5.gp
	./lab2b_5.gp

profile.out: lab2_list
	env CPUPROFILE=$@ ./lab2_list --iterations 1000 --threads 12 --sync s

lab2b-404816794.tar.gz: SortedList.h SortedList.c lab2_list.c Makefile lab2b_1.sh lab2b_1.gp profile.out lab2b_1.png lab2b_2.png lab2b_3.png lab2b_4.png lab2b_5.png
	tar czf $@ $^

# Local Variables:
# tab-width: 8
# End:

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static const char *progname = NULL;
static struct termios original_termios;

static void
restore_term(void) {
  if (tcsetattr(0, TCSANOW, &original_termios) == -1) {
    _exit(1); /* directly exit */
  }
}

static void
setup_term(void) {
  struct termios t;
  if (tcgetattr(0, &t) == -1) {
    fprintf(stderr,
            "%s: cannot get terminal attributes for standard input: %s\n",
            progname, strerror(errno));
    exit(1);
  }
  original_termios = t;

  /* Make the changes as in the spec */
  t.c_iflag = ISTRIP;
  t.c_oflag = 0;
  t.c_lflag = 0;

  if (tcsetattr(0, TCSANOW, &t) == -1) {
    fprintf(stderr,
            "%s: cannot set terminal attributes for standard input: %s\n",
            progname, strerror(errno));
    exit(1);
  }
  atexit(restore_term);
}

int
main(int argc, char *argv[]) {
  progname = argv[0];
  (void) argc;
  setup_term();
  return 0;
}

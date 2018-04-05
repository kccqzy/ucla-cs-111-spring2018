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

static ssize_t
noeintr_write(int fd, const uint8_t *buf, size_t n) {
  size_t bytes_left = n;
  while (bytes_left > 0) {
    ssize_t written = write(fd, buf, bytes_left);
    if (written == -1) {
      if (errno == EINTR) {
        continue;
      } else {
        return -1;
      }
    }
    buf += (size_t) written;
    bytes_left -= (size_t) written;
  }
  return n;
}

static ssize_t
noeintr_read(int fd, uint8_t *buf, size_t n) {
  while (1) {
    ssize_t bytes_read = read(fd, buf, n);
    if (bytes_read == -1 && errno == EINTR) { continue; }
    return bytes_read;
  }
}


static void
do_echo(void) {
  while (1) {
    uint8_t buf[1];
    ssize_t bytes_read = noeintr_read(0, buf, 1);
    if (bytes_read == 0) {
      return; /* EOF somehow */
    } else if (bytes_read == -1) {
      fprintf(stderr, "%s: cannot read from standard input: %s\n", progname,
              strerror(errno));
      exit(1);
    }
    switch (*buf) {
    case '\r': /* FALLTHROUGH */
    case '\n': noeintr_write(1, (const uint8_t *) "\r\n", 2); break;
    case 4: /* ^D */ return;
    default: noeintr_write(1, buf, 1); break;
    }
  }
}

int
main(int argc, char *argv[]) {
  progname = argv[0];
  (void) argc;
  setup_term();
  do_echo();
  return 0;
}

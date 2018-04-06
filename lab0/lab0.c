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
#include <unistd.h>

/* Options */
static struct option prog_options[] = {
  {.name = "input", .has_arg = required_argument, .val = 'i'},
  {.name = "output", .has_arg = required_argument, .val = 'o'},
  {.name = "segfault", .has_arg = no_argument, .val = 's'},
  {.name = "catch", .has_arg = no_argument, .val = 'c'},
  {}};

static const char *progname = NULL;

/* Parsed options */
static char const *input = NULL;
static char const *output = NULL;
static bool segfault = false;
static bool catch = false;

static void
parse_args(int argc, char *argv[]) {
  while (1) {
    switch (getopt_long(argc, argv, "", prog_options, NULL)) {
    case 'i': input = optarg; break;
    case 'o': output = optarg; break;
    case 's': segfault = true; break;
    case 'c': catch = true; break;
    case -1:
      /* Determine whether there are no-option parameters left */
      if (optind == argc) {
        return;
      } else {
        /* FALLTHROUGH */
      }
    default:
      fprintf(
        stderr,
        "usage: %s [--input=INPUT] [--output=OUTPUT] [--segfault] [--catch]\n",
        argv[0]);
      _exit(1);
    }
  }
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

static int
noeintr_dup2(int fd, int fd2) {
  while (1) {
    int r = dup2(fd, fd2);
    if (r == -1 && errno == EINTR) { continue; }
    return r;
  }
}

static void
do_copy(void) {
  uint8_t buf[65536];
  while (1) {
    ssize_t bytes_read = noeintr_read(0, buf, sizeof buf);
    if (bytes_read == 0) {
      return;
    } else if (bytes_read == -1) {
      fprintf(stderr, "%s: could not read from standard input (%s): %s\n",
              progname, input ? input : "original standard input",
              strerror(errno));
      _exit(5);
    }
    ssize_t bytes_written = noeintr_write(1, buf, bytes_read);
    if (bytes_written == -1) {
      fprintf(stderr, "%s: could not write to standard output (%s): %s\n",
              progname, output ? output : "original standard output",
              strerror(errno));
      _exit(5);
    }
  }
}

static void
reopen(void) {
  if (input) {
    int r = open(input, O_RDONLY);
    if (r == -1) {
      fprintf(stderr,
              "%s: could not open file %s as specified by '--input': %s\n",
              progname, input, strerror(errno));
      _exit(2);
    }
    noeintr_dup2(r, 0);
  }

  if (output) {
    int r = open(output, O_WRONLY | O_CREAT, 0777);
    if (r == -1) {
      fprintf(stderr,
              "%s: could not open file %s as specified by '--output': %s\n",
              progname, output, strerror(errno));
      _exit(3);
    }
    noeintr_dup2(r, 1);
  }
}

static void
cause_segfault(void) {
  volatile char *p = (volatile char *) 8;
  /* avoid dereferencing NULL: undefined behavior */
  *p = 'A';
}

static void
handler(int sig) {
  /* The handler should only call async-signal-safe functions */
  noeintr_write(2, (const uint8_t *) progname, strlen(progname));
  char *msg = ": caught signal sig";
  noeintr_write(2, (const uint8_t *) msg, strlen(msg));
  noeintr_write(2, (const uint8_t *) sys_signame[sig],
                strlen(sys_signame[sig]));
  noeintr_write(2, (const uint8_t *) "\n", 1);
  _exit(4);
}

static void
register_handler(void) {
  signal(SIGSEGV, handler);
}

int
main(int argc, char *argv[]) {
  progname = argv[0];
  parse_args(argc, argv);
  reopen();
  if (catch) register_handler();
  if (segfault) cause_segfault();
  do_copy();
  return 0;
}

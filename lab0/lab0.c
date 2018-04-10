#define _GNU_SOURCE

/* NAME: Joe Qian */
/* EMAIL: qzy@g.ucla.edu */
/* ID: 404816794 */

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

#define DIE_IF_MINUS_ONE(rv, code, reason, ...)                                \
  do {                                                                         \
    if (rv == -1) {                                                            \
      fprintf(stderr, "%s: " reason ": %s\n", progname, ##__VA_ARGS__,         \
              strerror(errno));                                                \
      exit(code);                                                              \
    }                                                                          \
  } while (0)

/* Options */
static struct option prog_options[] = {
  {.name = "input", .has_arg = required_argument, .val = 'i'},
  {.name = "output", .has_arg = required_argument, .val = 'o'},
  {.name = "segfault", .has_arg = no_argument, .val = 's'},
  {.name = "catch", .has_arg = no_argument, .val = 'c'},
  {}};

static const char *progname = NULL;

/* Parsed options */
static char const *opt_input = NULL;
static char const *opt_output = NULL;
static bool opt_segfault = false;
static bool opt_catch = false;

static void
parse_args(int argc, char *argv[]) {
  while (1) {
    switch (getopt_long(argc, argv, "", prog_options, NULL)) {
    case 'i': opt_input = optarg; break;
    case 'o': opt_output = optarg; break;
    case 's': opt_segfault = true; break;
    case 'c': opt_catch = true; break;
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
      exit(1);
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
    DIE_IF_MINUS_ONE(bytes_read, 5, "could not read from standard input (%s)",
                     opt_input ? opt_input : "original standard input");
    if (bytes_read == 0) { return; }
    ssize_t bytes_written = noeintr_write(1, buf, bytes_read);
    DIE_IF_MINUS_ONE(bytes_written, 5,
                     "could not write to standard output (%s)",
                     opt_output ? opt_output : "original standard output");
  }
}

static void
reopen(void) {
  if (opt_input) {
    int r = open(opt_input, O_RDONLY);
    DIE_IF_MINUS_ONE(r, 2, "could not open '%s' as specified by '--input'",
                     opt_input);
    int rr = noeintr_dup2(r, 0);
    DIE_IF_MINUS_ONE(rr, 5,
                     "could not duplicate file descriptor as standard input");
  }

  if (opt_output) {
    int r = open(opt_output, O_WRONLY | O_TRUNC | O_CREAT, 0777);
    DIE_IF_MINUS_ONE(r, 3, "could not open '%s' as specified by '--output'",
                     opt_output);
    int rr = noeintr_dup2(r, 1);
    DIE_IF_MINUS_ONE(rr, 5,
                     "could not duplicate file descriptor as standard output");
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
  const char *msg = ": caught signal: ";
  noeintr_write(2, (const uint8_t *) msg, strlen(msg));
  noeintr_write(2, (const uint8_t *) sys_siglist[sig],
                strlen(sys_siglist[sig]));
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
  if (opt_catch) { register_handler(); }
  if (opt_segfault) { cause_segfault(); }
  do_copy();
  return 0;
}

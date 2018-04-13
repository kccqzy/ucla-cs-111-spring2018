#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

static const char *progname = NULL;
static struct termios original_termios;
static volatile bool has_received_sigpipe = false;

#define DIE(reason, ...)                                                       \
  do {                                                                         \
    fprintf(stderr, "%s: " reason ": %s\r\n", progname, ##__VA_ARGS__,         \
            strerror(errno));                                                  \
    exit(1);                                                                   \
  } while (0)

#define DIE_IF_MINUS_ONE(rv, reason, ...)                                      \
  do {                                                                         \
    if (rv == -1) { DIE(reason, ##__VA_ARGS__); }                              \
  } while (0)

static void
restore_term(void) {
  assert(isatty(0));
  if (tcsetattr(0, TCSANOW, &original_termios) == -1) {
    /* Really?? */
    _exit(1); /* directly exit */
  }
}

static void
setup_term(void) {
  struct termios t;
  int get_rv = tcgetattr(0, &t);
  DIE_IF_MINUS_ONE(get_rv, "cannot get terminal attributes for standard input");
  original_termios = t;

  /* Make the changes as in the spec */
  t.c_iflag = ISTRIP;
  t.c_oflag = 0;
  t.c_lflag = 0;

  int set_rv = tcsetattr(0, TCSANOW, &t);
  DIE_IF_MINUS_ONE(set_rv, "cannot set terminal attributes for standard input");
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

/* Get a single character from standard input. Echo the character back to the
   standard output with special handling for CR, LF, and ^D. Returns EOF or the
   character read. */
static int
get_one_char_echo(void) {
  uint8_t buf[1];
  ssize_t bytes_read = noeintr_read(0, buf, 1);
  DIE_IF_MINUS_ONE(bytes_read, "cannot read from standard input");
  if (bytes_read == 0) { return -1; /* EOF somehow */ }

  ssize_t written = 0;
  switch (*buf) {
  case '\r': /* FALLTHROUGH */
  case '\n': written = noeintr_write(1, (const uint8_t *) "\r\n", 2); break;
  case 4: /* ^D */ return -1;
  default: written = noeintr_write(1, buf, 1); break;
  }
  DIE_IF_MINUS_ONE(written, "cannot write to standard output");
  return *buf;
}

static void
do_echo(void) {
  while (1) {
    if (get_one_char_echo() == -1) { break; }
  }
}

static size_t
translate_buffer(const uint8_t *inbuf, size_t in_size, uint8_t *outbuf) {
  size_t j = 0;
  for (size_t i = 0; i < in_size; ++i) {
    if (inbuf[i] == '\n') {
      outbuf[j++] = '\r';
      outbuf[j++] = '\n';
    } else {
      outbuf[j++] = inbuf[i];
    }
  }
  return j;
}

static void
close_or_die(int fd) {
  while (close(fd)) {
    if (errno == EINTR) { continue; }
    DIE("could not close");
  }
}

static void
do_shell_interact(pid_t p, int infd, int outfd) {
  struct pollfd fds[] = {{.fd = 0, .events = POLLIN},
                         {.fd = outfd, .events = POLLIN}};

  bool expecting_shell_output = true;
  bool expecting_keyboard_input = true;
  while (1) {
    int n_ready = poll(fds, 2, -1);
    DIE_IF_MINUS_ONE(n_ready, "could not poll");

    /* Does the shell have any output for us? */
    if (expecting_shell_output && fds[1].revents & POLLIN) {
      uint8_t buf[65536];
      ssize_t bytes_read = noeintr_read(outfd, buf, sizeof buf);
      DIE_IF_MINUS_ONE(bytes_read, "could not read from pipe");
      if (bytes_read == 0) {
        expecting_shell_output = false;
        close_or_die(outfd);
        fds[1].fd = -1; /* Ignore further events */
      } else {
        uint8_t outbuf[65536 * 2];
        size_t bytes_to_write = translate_buffer(buf, bytes_read, outbuf);
        ssize_t bytes_written = noeintr_write(1, outbuf, bytes_to_write);
        DIE_IF_MINUS_ONE(bytes_written,
                         "could not write shell output to standard output");
        continue;
        /* The shell may have had more output beyond those we've consumed, so
           try reading again. */
      }
    }

    /* Has the shell exited? */
    if (expecting_shell_output &&
        (fds[1].revents & POLLHUP || fds[1].revents & POLLERR)) {
      expecting_shell_output = false;
      close_or_die(outfd);
      fds[1].fd = -1; /* Ignore further events */
    }

    /* Has the user typed anything here? */
    if (expecting_keyboard_input && fds[0].revents & POLLIN) {
      int ch = get_one_char_echo();
      if (ch == -1) {
        expecting_keyboard_input = false;
        close_or_die(infd);
      } else if (ch == 3) {
        int r = kill(p, SIGTERM);
        DIE_IF_MINUS_ONE(r, "could not send signal to shell");
      } else {
        uint8_t c = ch == '\r' ? '\n' : ch;
        ssize_t wr = noeintr_write(infd, &c, 1);
        DIE_IF_MINUS_ONE(wr, "could not send character to shell");
      }
    }

    /* Has the user closed it? */
    if (expecting_keyboard_input &&
        (fds[0].revents & POLLHUP || fds[0].revents & POLLERR)) {
      expecting_keyboard_input = false;
      close_or_die(infd);
    }

    /* Have we received SIGPIPE? */
    if (expecting_shell_output && has_received_sigpipe) {
      expecting_shell_output = false;
      close_or_die(outfd);
      fds[1].fd = -1; /* Ignore further events */
    }

    /* Return if we do not expect any more shell output. */
    if (!expecting_shell_output) { break; }
  }

  /* Now perform orderly shutdown. */
  int stat;
  waitpid(p, &stat, 0);
  fprintf(stderr, "SHELL EXIT SIGNAL=%d STATUS=%d\r\n", stat & 0x7f,
          (stat & 0xff00) >> 8);
}

static void
handler(int sig) {
  if (sig == SIGSEGV) { has_received_sigpipe = true; }
}

static void
pipe_or_die(int fd[2]) {
  int r = pipe(fd);
  DIE_IF_MINUS_ONE(r, "could not create pipe for communication with shell");
}

static void
dup2_or_die(int fd, int fd2) {
  while (1) {
    int r = dup2(fd, fd2);
    if (r == -1) {
      if (errno == EINTR) { continue; }
      DIE_IF_MINUS_ONE(r, "could not duplicate file descriptor");
    }
    return;
  }
}

int
main(int argc, char *argv[]) {
  progname = argv[0];
  bool do_shell = false;
  if (argc == 2 && strcmp(argv[1], "--shell") == 0) {
    do_shell = true;
  } else if (argc > 1) {
    fprintf(stderr,
            "%s: unrecognized command line arguments\n"
            "usage: %s [--shell]\n",
            progname, progname);
    exit(1);
  }

  setup_term();

  if (do_shell) {
    /* Install SIGPIPE handler */
    signal(SIGPIPE, handler);
    /* Make pipes */
    int infd[2], outfd[2];
    pipe_or_die(infd);
    pipe_or_die(outfd);
    /* In the child, we only need the read-end of the infd and the write-end of
       the outfd. So close the write-end of the infd and the read-end of outfd.
     */
    pid_t p = fork();
    DIE_IF_MINUS_ONE(p, "could not fork");
    if (p == 0) {
      char bash[] = "/bin/bash";
      char *const args[] = {bash, NULL};
      dup2_or_die(infd[0], 0);
      dup2_or_die(outfd[1], 1);
      close_or_die(infd[1]);
      close_or_die(outfd[0]);
      execvp(bash, args);
      fprintf(stderr, "%s: could not execute: %s\r\n", progname,
              strerror(errno));
      _exit(1); /* Inside the child; skip atexit(3) functions. */
    }

    /* Close the opposite ends of pipes in the parent. */
    close_or_die(infd[0]);
    close_or_die(outfd[1]);
    do_shell_interact(p, infd[1], outfd[0]);
  } else {
    do_echo();
  }

  return 0;
}

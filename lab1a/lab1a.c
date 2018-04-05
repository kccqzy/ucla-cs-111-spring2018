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

static bool do_shell = false;

static volatile bool has_received_sigpipe = false;

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

/* Get a single character from standard input. Echo the character back to the
   standard output with special handling for CR, LF, and ^D. Returns EOF or the
   character read. */
static int
get_one_char_echo(void) {
  uint8_t buf[1];
  ssize_t bytes_read = noeintr_read(0, buf, 1);
  if (bytes_read == 0) {
    return -1; /* EOF somehow */
  } else if (bytes_read == -1) {
    fprintf(stderr, "%s: cannot read from standard input: %s\n", progname,
            strerror(errno));
    exit(1);
  }
  switch (*buf) {
  case '\r': /* FALLTHROUGH */
  case '\n': noeintr_write(1, (const uint8_t *) "\r\n", 2); break;
  case 4: /* ^D */ return -1;
  default: noeintr_write(1, buf, 1); break;
  }
  return *buf;
}

static void
do_echo(void) {
  while (1) {
    if (get_one_char_echo() == -1) break;
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
do_shell_interact(pid_t p, int infd, int outfd) {
  FILE *logger = fopen("/tmp/lab1a.log", "w");
  setlinebuf(logger);

  struct pollfd fds[] = {{.fd = 0, .events = POLLIN},
                         {.fd = outfd, .events = POLLIN}};

  bool expecting_shell_output = true;
  bool expecting_keyboard_input = true;
  while (1) {
    fprintf(logger, "entering main event loop\n");
    int n_ready = poll(fds, 2, -1);
    fprintf(logger,
            "woke up from poll(2):\n"
            "    stdin revents = %d\n"
            "    shell revents = %d\n",
            fds[0].revents, fds[1].revents);
    if (n_ready == -1) {
      fprintf(stderr, "%s: could not poll: %s\n", progname, strerror(errno));
      exit(1);
    }

    /* Does the shell have any output for us? */
    if (expecting_shell_output && (fds[1].revents & POLLIN)) {
      fprintf(logger, "shell fd POLLIN\n");
      uint8_t buf[65536];
      ssize_t bytes_read = noeintr_read(outfd, buf, sizeof buf);
      /* NOTE that we assume we cannot have a pipe capacity greater than 65536
         bytes. This may not be the case in future versions of Linux. */
      if (bytes_read == -1) {
        fprintf(stderr, "%s: could not read from pipe: %s\n", progname,
                strerror(errno));
        exit(1);
      } else if (bytes_read == 0) {
        expecting_shell_output = false;
        close(outfd);
      } else {
        fprintf(logger, "shell fd read %zu\n", (size_t) bytes_read);
        uint8_t outbuf[65536 * 2];
        size_t bytes_to_write = translate_buffer(buf, bytes_read, outbuf);
        noeintr_write(1, outbuf, bytes_to_write);
      }
    }

    /* Has the shell exited? */
    if (expecting_shell_output &&
        ((fds[1].revents & POLLHUP) || (fds[1].revents & POLLERR))) {
      fprintf(logger, "shell fd POLLHUP or POLLERR\n");
      expecting_shell_output = false;
      close(outfd);
    }

    /* Has the user typed anything here? */
    if (expecting_keyboard_input && (fds[0].revents & POLLIN)) {
      fprintf(logger, "stdin fd POLLIN\n");
      int ch = get_one_char_echo();
      if (ch == -1) {
        expecting_keyboard_input = false;
        close(infd);
      } else {
        fprintf(logger, "stdin fd read char 0x%02x\n", ch);
        uint8_t c = ch == '\r' ? '\n' : ch;
        noeintr_write(infd, &c, 1);
      }
    }

    /* Has the user closed it? */
    if (expecting_keyboard_input &&
        ((fds[0].revents & POLLHUP) || (fds[0].revents & POLLERR))) {
      fprintf(logger, "stdin fd POLLHUP or POLLERR\n");
      expecting_keyboard_input = false;
      close(infd);
    }

    /* Have we received SIGPIPE? */
    if (expecting_shell_output && has_received_sigpipe) {
      fprintf(logger, "received SIGPIPE\n");
      expecting_shell_output = false;
      close(outfd);
    }

    /* Return if we do not expect any more input from either. */
    if (!expecting_keyboard_input && !expecting_shell_output) { break; }
  }

  fprintf(logger, "preparing to shut down\n");

  /* Now perform orderly shutdown. */
  int stat;
  waitpid(p, &stat, 0);
  fprintf(stderr, "SHELL EXIT SIGNAL=%d STATUS=%d\r\n", stat & 0x7f,
          (stat & 0xff00) >> 8);

  fclose(logger);
}

static void
handler(int sig) {
  if (sig == SIGSEGV) has_received_sigpipe = true;
}

int
main(int argc, char *argv[]) {
  progname = argv[0];
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
    /* TODO check error */
    pipe(infd);
    pipe(outfd);
    /* In the child, we only need the read-end of the infd and the write-end of
       the outfd. So close the write-end of the infd and the read-end of outfd.
     */
    fcntl(infd[1], F_SETFD, FD_CLOEXEC);
    fcntl(outfd[0], F_SETFD, FD_CLOEXEC);

    pid_t p = fork();
    if (p == 0) {
      char *const args[] = {"/bin/bash", NULL};
      dup2(infd[0], 0);
      dup2(outfd[1], 1);
      execvp("/bin/bash", args);
      fprintf(stderr, "%s: could not execute: %s\n", progname, strerror(errno));
      _exit(1);
    }

    /* Close the opposite ends of pipes in the parent. */
    close(infd[0]);
    close(outfd[1]);
    do_shell_interact(p, infd[1], outfd[0]);
  } else {
    do_echo();
  }

  return 0;
}

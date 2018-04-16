#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

/* Options */
static const struct option prog_options[] = {
  {.name = "port", .has_arg = required_argument, .val = 'p'},
  {.name = "host", .has_arg = required_argument, .val = 'h'},
  {.name = "compress", .has_arg = no_argument, .val = 'c'},
  {.name = "log", .has_arg = required_argument, .val = 'l'},
  {}};

static const char* opt_port = NULL;
static const char* opt_host = "127.0.0.1";
static bool opt_compress = false;
static FILE* opt_log = NULL;

static const char* progname = NULL;
static struct termios original_termios;

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
  if (!isatty(0) || !isatty(1)) {
    fprintf(stderr, "%s: stdin and stdout must be connected to a terminal\n",
            progname);
    exit(1);
  }

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
noeintr_write(int fd, const uint8_t* buf, size_t n) {
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
noeintr_read(int fd, uint8_t* buf, size_t n) {
  while (1) {
    ssize_t bytes_read = read(fd, buf, n);
    if (bytes_read == -1 && errno == EINTR) { continue; }
    return bytes_read;
  }
}

static size_t
translate_buffer(const uint8_t* inbuf, size_t in_size, uint8_t* outbuf) {
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
parse_args(int argc, char* argv[]) {
  while (1) {
    switch (getopt_long(argc, argv, "", prog_options, NULL)) {
    case 'p': opt_port = optarg; break;
    case 'h': opt_host = optarg; break;
    case 'c': opt_compress = true; break;
    case 'l':
      opt_log = fopen(optarg, "w"); /* Considering opening the file here */
      if (!opt_log) {
        fprintf(stderr, "%s: could not open log file '%s' for writing: %s\n",
                argv[0], optarg, strerror(errno));
        exit(1);
      }
      break;
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
        "usage: %s --port=PORT [--host=HOST] [--compress] [--log=LOGFILE]\n",
        argv[0]);
      exit(1);
    }
  }
}

static int
try_connect(void) {
  /* Resolve addresses */
  struct addrinfo* result;
  int r = getaddrinfo(opt_host, opt_port,
                      &(struct addrinfo){.ai_family = AF_UNSPEC,
                                         .ai_socktype = SOCK_STREAM,
                                         .ai_flags = AI_NUMERICSERV},
                      &result);
  if (r) {
    fprintf(stderr, "%s: could not resolve %s port %s: %s\n", progname,
            opt_host, opt_port, gai_strerror(r));
    exit(1);
  }


  int cfd = 0;
  struct addrinfo* p = result;
  for (; p; p = p->ai_next) {
    cfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (cfd == -1) { continue; } /* Try next */
    if (connect(cfd, p->ai_addr, p->ai_addrlen) != -1) {
      break;
    } else {
      int e = errno;
      if (p->ai_family == AF_INET6) {
        char addr[INET6_ADDRSTRLEN];
        struct sockaddr_in6* saddr = (struct sockaddr_in6*) p->ai_addr;
        inet_ntop(AF_INET6, &saddr->sin6_addr, addr, INET6_ADDRSTRLEN);
        fprintf(stderr, "%s: could not connect to host %s [%s] port %s: %s\n",
                progname, opt_host, addr, opt_port, strerror(e));
      } else if (p->ai_family == AF_INET) {
        char addr[INET_ADDRSTRLEN];
        struct sockaddr_in* saddr = (struct sockaddr_in*) p->ai_addr;
        inet_ntop(AF_INET, &saddr->sin_addr, addr, INET_ADDRSTRLEN);
        fprintf(stderr, "%s: could not connect to host %s [%s] port %s: %s\n",
                progname, opt_host, addr, opt_port, strerror(e));

      } else {
        fprintf(stderr, "%s: could not connect to host %s port %s: %s\n",
                progname, opt_host, opt_port, strerror(errno));
      }
    }
    close(cfd);
  }
  if (!p) { exit(1); }
  freeaddrinfo(result);
  assert(cfd);
  return cfd;
}

/* Now we need to do a poll; either read data from the server, or from the
   keyboard */
static void
interact_with_server(int socket_fd) {
  struct pollfd fds[] = {{.fd = 0, .events = POLLIN},
                         {.fd = socket_fd, .events = POLLIN}};

  while (1) {
    int n_ready = poll(fds, 2, -1);
    DIE_IF_MINUS_ONE(n_ready, "could not poll");

    /* Does the shell have any output for us? */
    if (fds[1].revents & POLLIN) {
      uint8_t buf[65536];
      ssize_t bytes_read = noeintr_read(socket_fd, buf, sizeof buf);
      DIE_IF_MINUS_ONE(bytes_read, "could not read from socket");
      if (bytes_read == 0) {
        break;
      } else {
        uint8_t outbuf[65536 * 2];
        size_t bytes_to_write = translate_buffer(buf, bytes_read, outbuf);
        ssize_t bytes_written = noeintr_write(1, outbuf, bytes_to_write);
        DIE_IF_MINUS_ONE(bytes_written,
                         "could not write server output to standard output");
        continue;
        /* The shell may have had more output beyond those we've consumed, so
           try reading again. */
      }
    }

    /* We need not separately handle shutdown behavior, because when this
       occurs, the revents field would contain POLLIN|POLLOUT|POLLRDHUP, and the
       read(2) returns zero. */

    /* Has the user typed anything here? */
    if (fds[0].revents & POLLIN) {
      uint8_t buf[1];
      ssize_t bytes_read = noeintr_read(0, buf, 1);
      DIE_IF_MINUS_ONE(bytes_read, "cannot read from keyboard");
      if (bytes_read == 0) { return; /* EOF somehow */ }

      ssize_t written = 0;
      switch (*buf) {
      case '\r': /* FALLTHROUGH */
      case '\n': written = noeintr_write(1, (const uint8_t*) "\r\n", 2); break;
      default: written = noeintr_write(1, buf, 1); break;
      }
      DIE_IF_MINUS_ONE(written, "cannot write to screen");

      written = write(socket_fd, buf, 1);
      if (written == -1 && errno == EPIPE) { return; }
      DIE_IF_MINUS_ONE(written, "cannot write to socket");
    }

    if (fds[0].revents & POLLHUP) { break; }
  }
  int r = close(socket_fd);
  DIE_IF_MINUS_ONE(r, "cannot close socket");
}

int
main(int argc, char* argv[]) {
  progname = argv[0];

  parse_args(argc, argv);
  if (!opt_port) {
    fprintf(stderr, "%s: required argument '--port' not provided\n", argv[0]);
    exit(1);
  }
  signal(SIGPIPE, SIG_IGN); /* Prefer handling EPIPE */
  int socket_fd = try_connect();

  setup_term();
  interact_with_server(socket_fd);
}

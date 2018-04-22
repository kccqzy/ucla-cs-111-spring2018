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
#include "lab1b-common.h"

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

enum LineEndingTranslation {
  IDENTITY,
  CR_TO_LF,
  CR_TO_CRLF,
  LF_TO_CRLF,

};

static size_t
translate_buffer(const uint8_t* inbuf, size_t in_size, uint8_t* outbuf,
                 enum LineEndingTranslation trans) {
  size_t j = 0;
  switch (trans) {
  case IDENTITY: memcpy(outbuf, inbuf, in_size); return in_size;
  case CR_TO_LF:
    for (size_t i = 0; i < in_size; ++i) {
      if (inbuf[i] == 0x0d) {
        outbuf[j++] = 0x0a;
      } else {
        outbuf[j++] = inbuf[i];
      }
    }
    return j;
  case CR_TO_CRLF:
    for (size_t i = 0; i < in_size; ++i) {
      if (inbuf[i] == 0x0d) {
        outbuf[j++] = 0x0d;
        outbuf[j++] = 0x0a;
      } else {
        outbuf[j++] = inbuf[i];
      }
    }
    return j;
  case LF_TO_CRLF:
    for (size_t i = 0; i < in_size; ++i) {
      if (inbuf[i] == 0x0a) {
        outbuf[j++] = 0x0d;
        outbuf[j++] = 0x0a;
      } else {
        outbuf[j++] = inbuf[i];
      }
    }
    return j;
  }
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

static int
try_listen_and_accept(void) {
  assert(false && "unimplemented");
}

static void
server_event_loop(int socket_fd) {
  assert(false && "unimplemented");
}

static void
client_event_loop(int socket_fd) {
  assert(false && "unimplemented");
}

int
client_main(int argc, char* argv[]) {
  progname = argv[0];

  parse_args(argc, argv);
  if (!opt_port) {
    fprintf(stderr, "%s: required argument '--port' not provided\n", argv[0]);
    exit(1);
  }
  signal(SIGPIPE, SIG_IGN); /* Prefer handling EPIPE */
  int socket_fd = try_connect();
  setup_term();
  client_event_loop(socket_fd);
  return 0;
}

int
server_main(int argc, char*argv[]) {
  progname = argv[0];

  parse_args(argc, argv);
  if (!opt_port) {
    fprintf(stderr, "%s: required argument '--port' not provided\n", argv[0]);
    exit(1);
  }
  signal(SIGPIPE, SIG_IGN); /* Prefer handling EPIPE */
  int socket_fd = try_listen_and_accept();
  server_event_loop(socket_fd);
  return 0;
}

#define _GNU_SOURCE
#define ZLIB_CONST
#include "lab1b-common.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
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
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <zlib.h>

/*************************************************************************
 * Global variables and constants
 *************************************************************************/

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


/*************************************************************************
 * Ways to die
 *************************************************************************/

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


/*************************************************************************
 * Terminal processing
 *************************************************************************/

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

/*************************************************************************
 * Vector class
 *************************************************************************/
struct Vector {
  uint8_t* buf;
  size_t len, cap;
};

static inline struct Vector
vector_new(void) {
  struct Vector r = {.buf = NULL, .len = 0, .cap = 0};
  return r;
}

static inline void
vector_delete(struct Vector* this) {
  free(this->buf);
  *this = vector_new();
}

static inline int
vector_get_next(struct Vector const* this) {
  assert(this->len <= this->cap);
  if (!this->len) {
    return -1;
  } else {
    return *this->buf;
  }
}

static inline void
vector_consume(struct Vector* this, size_t size) {
  assert(this->len <= this->cap);
  assert(size <= this->len);
  if (size == this->len) {
    this->len = 0;
  } else {
    memmove(this->buf, this->buf + size, this->len - size);
    this->len -= size;
  }
}

static inline bool
vector_has_content(struct Vector const* this) {
  return this->len;
}

static inline void
vector_reserve(struct Vector* this, size_t target_size) {
  if (this->cap < target_size) {
    /* For efficiency reasons, if the requested target size is smaller than
       2*cap, we make it 2*cap. */
    if (target_size < this->cap * 2) { target_size = this->cap * 2; }
    /* Round up to the next multiple of 4096; add 4096 if already a multiple */
    size_t new_cap = ((target_size >> 12) + 1) << 12;
    this->buf = realloc(this->buf, new_cap);
    if (!this->buf) { abort(); }
    this->cap = new_cap;
  }
}

static inline void
vector_push_into(struct Vector* this, uint8_t const* buf, size_t size) {
  if (!size) { return; }
  size_t target_size = this->len + size;
  vector_reserve(this, target_size);
  memcpy(this->buf + this->len, buf, size);
  this->len = target_size;
}

/*************************************************************************
 * BufferManager class
 *************************************************************************/
enum Compression { DO_NOTHING, DO_COMPRESS, DO_DECOMPRESS };

struct BufferManager {
  struct Vector v;
  enum Compression comp;
  z_stream z;
};

static inline struct BufferManager
bm_new(enum Compression comp) {
  struct BufferManager bm = {.v = vector_new(), .comp = comp};
  if (comp == DO_COMPRESS) {
    int r = deflateInit(&bm.z, Z_DEFAULT_COMPRESSION);
    assert(r == Z_OK);
  } else if (comp == DO_DECOMPRESS) {
    int r = inflateInit(&bm.z);
    assert(r == Z_OK);
  }
  return bm;
}

static inline void
bm_delete(struct BufferManager* this) {
  if (this->comp == DO_COMPRESS) {
    deflateEnd(&this->z);
  } else if (this->comp == DO_DECOMPRESS) {
    inflateEnd(&this->z);
  }
  vector_delete(&this->v);
  this->z = (z_stream){};
}

static inline int
bm_get_next(struct BufferManager const* this) {
  assert(this->comp != DO_COMPRESS);
  return vector_get_next(&this->v);
}

static inline void
bm_consume(struct BufferManager* this, size_t size) {
  vector_consume(&this->v, size);
}

static inline bool
bm_has_content(struct BufferManager const* this) {
  return vector_has_content(&this->v);
}

static inline void
bm_push_into(struct BufferManager* this, uint8_t const* buf, size_t size, bool no_comp) {
  if (!size) {return;}
  enum {chunk_size = 4096};
  if (this->comp == DO_NOTHING || no_comp) {
    vector_push_into(&this->v, buf, size);
  } else if (this->comp == DO_COMPRESS) {
    this->z.next_in = buf;
    this->z.avail_in = size;
    uint8_t current_chunk[chunk_size];
    do {
      this->z.next_out = current_chunk;
      this->z.avail_out = chunk_size;
      int r = deflate(&this->z, Z_SYNC_FLUSH);
      assert(r == Z_OK);
      size_t have = chunk_size - this->z.avail_out;
      vector_push_into(&this->v, current_chunk, have);
    } while (this->z.avail_out == 0);
    assert(this->z.avail_in == 0);
  } else if (this->comp == DO_DECOMPRESS) {
    this->z.next_in = buf;
    this->z.avail_in = size;
    uint8_t current_chunk[chunk_size];
    do {
      this->z.next_out = current_chunk;
      this->z.avail_out = chunk_size;
      int r = inflate(&this->z, Z_SYNC_FLUSH);
      assert(r == Z_OK);
      size_t have = chunk_size - this->z.avail_out;
      vector_push_into(&this->v, current_chunk, have);
    } while (this->z.avail_out == 0);
    assert(this->z.avail_in == 0);
  }
}

/*************************************************************************
 * Line translation
 *************************************************************************/

enum LineEndingTranslation {
  IDENTITY,
  CR_TO_LF,
  CR_TO_CRLF,
  LF_TO_CRLF,

};

static void
translate_vector(struct Vector* this, enum LineEndingTranslation trans) {
  switch (trans) {
  case IDENTITY: return;
  case CR_TO_LF:
    for (size_t i = 0; i < this->len; ++i) {
      if (this->buf[i] == 0x0d) { this->buf[i] = 0x0a; }
    }
    return;
  case CR_TO_CRLF: {
    uint8_t tmp[this->len * 2]; /* VLA */
    size_t j = 0;
    for (size_t i = 0; i < this->len; ++i) {
      if (this->buf[i] == 0x0d) {
        tmp[j++] = 0x0d;
        tmp[j++] = 0x0a;
      } else {
        tmp[j++] = this->buf[i];
      }
    }
    vector_reserve(this, j);
    memcpy(this->buf, tmp, j);
    this->len = j;
    return;
  }
  case LF_TO_CRLF: {
    uint8_t tmp[this->len * 2]; /* VLA */
    size_t j = 0;
    for (size_t i = 0; i < this->len; ++i) {
      if (this->buf[i] == 0x0a) {
        tmp[j++] = 0x0d;
        tmp[j++] = 0x0a;
      } else {
        tmp[j++] = this->buf[i];
      }
    }
    vector_reserve(this, j);
    memcpy(this->buf, tmp, j);
    this->len = j;
    return;
  }
  }
}


/*************************************************************************
 * Wrapped read/write
 *************************************************************************/

static inline void
log_data(uint8_t const* buf, size_t size, char const* prefix) {
  if (opt_log) {
    fprintf(opt_log, "%s %zu bytes: ", prefix, size);
    fwrite(buf, 1, size, opt_log);
    fputc('\n', opt_log);
  }
}

static inline struct Vector
read_alot(int from, bool* more, bool do_log) {
  struct Vector buf = vector_new();
  while (1) {
    uint8_t b[65536];
    ssize_t r = read(from, b, sizeof b);
    if (r == 0) {
      *more = false;
      return buf;
    } else if (r > 0) {
      vector_push_into(&buf, b, r);
      if (do_log) { log_data(b, r, "RECEIVED"); }
      continue;
    } else {
      if (errno == EINTR) {
        continue;
      } else if (errno == EAGAIN) {
        *more = true;
        return buf;
      } else {
        DIE("could not read");
      }
    }
  }
}

static bool
do_read(int from, struct BufferManager* to, enum LineEndingTranslation trans,
        bool do_log) {
  bool more;
  struct Vector buf = read_alot(from, &more, do_log);
  translate_vector(&buf, trans);
  bm_push_into(to, buf.buf, buf.len, false);
  vector_delete(&buf);
  return more;
}

static bool
do_write(struct BufferManager* from, int to, bool do_log) {
  while (1) {
    if (!bm_has_content(from)) { return true; }
    ssize_t w = write(to, from->v.buf, from->v.len);
    if (w > -1) {
      if (do_log) { log_data(from->v.buf, w, "SENT"); }
      bm_consume(from, w);
      continue;
    } else {
      if (errno == EAGAIN) {
        return true;
      } else if (errno == EPIPE) {
        return false;
      }
      DIE("could not write");
    }
  }
}

/*************************************************************************
 * Utility functions
 *************************************************************************/

static inline bool
has_input(struct pollfd const* pfd) {
  return pfd->revents & POLLIN;
}

static inline bool
has_hup(struct pollfd const* pfd) {
  return pfd->revents & POLLHUP;
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
      setlinebuf(opt_log);
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
  struct addrinfo* result;
  int r =
    getaddrinfo(opt_host, opt_port,
                &(struct addrinfo){.ai_socktype = SOCK_STREAM,
                                   .ai_family = AF_UNSPEC,
                                   .ai_flags = AI_PASSIVE | AI_NUMERICSERV},
                &result);
  if (r) {
    fprintf(stderr, "%s: could not resolve %s port %s: %s\n", progname,
            opt_host, opt_port, gai_strerror(r));
    exit(1);
  }

  int lfd = 0;
  struct addrinfo* p = result;
  for (; p; p = p->ai_next) {
    lfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (lfd == -1) { continue; } /* Try next */
    int ssr = setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
    DIE_IF_MINUS_ONE(ssr, "could not set SO_REUSEADDR");
    if (bind(lfd, p->ai_addr, p->ai_addrlen) != -1) { break; }
    close(lfd);
  }
  if (!p) { exit(1); }
  int listenr = listen(lfd, 1);
  DIE_IF_MINUS_ONE(listenr, "could not listen");
  freeaddrinfo(result);
  assert(lfd);

  /* We are now successfully listening. We accept the first client and then stop
     listening. */
  while (1) {
    int cfd = accept(lfd, NULL, NULL);
    if (cfd == -1) continue;
    close(lfd);
    /* Because we only service a single client, close the listening socket. */
    return cfd;
  }
}

static void
start_child(int* child_stdin_fd, int* child_stdout_fd, pid_t* child_pid,
            int socket_fd) {
  int in[2], out[2];
  int inr = pipe(in);
  int outr = pipe(out);
  DIE_IF_MINUS_ONE(inr, "could not create pipe for stdin");
  DIE_IF_MINUS_ONE(outr, "could not create pipe for stdout");
  pid_t pid = fork();
  DIE_IF_MINUS_ONE(pid, "could not fork");
  if (pid == 0) {
    char bash[] = "/bin/bash";
    char* const args[] = {bash, NULL};
    dup2(in[0], 0);
    dup2(out[1], 1);
    dup2(out[1], 2);
    close(in[1]);
    close(out[0]);
    close(socket_fd);
    execvp(bash, args);
    fprintf(stderr, "%s: could not execute bash: %s\n", progname,
            strerror(errno));
    _exit(1);
  }
  close(in[0]);
  close(out[1]);
  *child_stdin_fd = in[1];
  *child_stdout_fd = out[0];
  *child_pid = pid;
}

static void
make_non_blocking(int fd) {
  int flags = fcntl(fd, F_GETFL);
  DIE_IF_MINUS_ONE(flags, "could not get fd FL");
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void
server_event_loop(int socket_fd) {
  int child_stdin_fd, child_stdout_fd;
  pid_t child_pid;
  start_child(&child_stdin_fd, &child_stdout_fd, &child_pid, socket_fd);

  make_non_blocking(child_stdin_fd);
  make_non_blocking(child_stdout_fd);
  make_non_blocking(socket_fd);

  struct BufferManager child_stdin_buf = bm_new(opt_compress ? DO_DECOMPRESS : DO_NOTHING);
  struct BufferManager socket_buf = bm_new(opt_compress ? DO_COMPRESS : DO_NOTHING);

  while (1) {
    struct pollfd poll_fds[] = {
      /* CSE please! */
      {.fd = child_stdout_fd, .events = POLLIN},
      {.fd = socket_fd,
       .events = bm_has_content(&socket_buf) ? POLLIN | POLLOUT : POLLIN},
      {.fd = child_stdin_fd,
       .events = bm_has_content(&child_stdin_buf) ? POLLOUT : 0}};

    int poll_rv = poll(poll_fds, sizeof poll_fds / sizeof(struct pollfd), -1);
    DIE_IF_MINUS_ONE(poll_rv, "could not poll");

    /* Detect pipe closed */
    if (child_stdin_fd > -1) {
      struct pollfd p = {.fd = child_stdin_fd, .events = POLLOUT};
      int poll_rv = poll(&p, 1, 0);
      DIE_IF_MINUS_ONE(poll_rv, "could not poll");
      if (p.revents & (POLLERR | POLLNVAL)) {
        /* POLLNVAL is a special workaround for OSX. */
        close(child_stdin_fd);
        child_stdin_fd = -1;
      }
    }

    /* Handle child stdin */
    if (child_stdin_fd > -1 && bm_has_content(&child_stdin_buf)) {
      if (bm_get_next(&child_stdin_buf) == 3) {
        DIE_IF_MINUS_ONE(kill(child_pid, SIGINT),
                         "could not send signal to child");
        bm_consume(&child_stdin_buf, 1);
        continue;
      } else if (bm_get_next(&child_stdin_buf) == 4) {
        close(child_stdin_fd);
        child_stdin_fd = -1;
        continue;
      }
      if (do_write(&child_stdin_buf, child_stdin_fd, false) == false) {
        close(child_stdin_fd);
        child_stdin_fd = -1;
      }
    }

    /* Handle client */
    if (socket_fd > -1 && bm_has_content(&socket_buf)) {
      if (do_write(&socket_buf, socket_fd, true) == false) {
        if (child_stdin_fd > -1) {
          close(child_stdin_fd);
          child_stdin_fd = -1;
        }
        close(socket_fd);
        socket_fd = -1;
      }
    }

    /* Done with writers, now readers */
    if (child_stdout_fd > -1) {
      if ((has_input(&poll_fds[0]) &&
           do_read(child_stdout_fd, &socket_buf, LF_TO_CRLF, false) == false) ||
          has_hup(&poll_fds[0])) {
        close(child_stdout_fd);
        child_stdout_fd = -1;
      }
    }

    if (socket_fd > -1) {
      if ((has_input(&poll_fds[1]) &&
           do_read(socket_fd, &child_stdin_buf, IDENTITY, true) == false) ||
          has_hup(&poll_fds[1])) {
        /* No more read; so the client will not send any more data to us */
        if (child_stdin_fd > -1) {
          close(child_stdin_fd);
          child_stdin_fd = -1;
        }
        close(socket_fd);
        socket_fd = -1;
      }
    }

    // Shutdown handling: quit if the shell has died
    if (child_stdout_fd == -1 && child_stdin_fd == -1) { break; }

    // (non-) Shutdown handling: do not just quit merely because the client
    // has died because we still need to wait for the shell to die.
  }

  int stat;
  waitpid(child_pid, &stat, 0);
  fprintf(stderr, "SHELL EXIT SIGNAL=%d STATUS=%d\r\n", stat & 0x7f,
          (stat & 0xff00) >> 8);

  bm_delete(&child_stdin_buf);
  bm_delete(&socket_buf);
}

static void
client_event_loop(int socket_fd) {
  make_non_blocking(0);
  make_non_blocking(socket_fd);

  struct BufferManager socket_buf = bm_new(opt_compress ? DO_COMPRESS : DO_NOTHING);
  struct BufferManager stdout_buf = bm_new(opt_compress ? DO_DECOMPRESS : DO_NOTHING);

  while (1) {
    struct pollfd poll_fds[] = {
      {.fd = 0, .events = POLLIN},
      {.fd = socket_fd,
       .events = bm_has_content(&socket_buf) ? POLLIN | POLLOUT : POLLIN},
      {.fd = 1, .events = bm_has_content(&stdout_buf) ? POLLOUT : 0}};

    int poll_rv = poll(poll_fds, sizeof poll_fds / sizeof(struct pollfd), -1);
    DIE_IF_MINUS_ONE(poll_rv, "could not poll");

    if (bm_has_content(&socket_buf)) {
      if (do_write(&socket_buf, socket_fd, true) == false) {
        // No more write to the socket, but the server couldn't have
        // just shutdown the read half, so the server must be dead.
        break;
      }
    }

    if (bm_has_content(&stdout_buf)) {
      if (do_write(&stdout_buf, 1, false) == false) {
        // No more write to stdout
        break;
      }
    }

    if (has_input(&poll_fds[0])) {
      bool more;
      struct Vector buf_ori = read_alot(0, &more, false);
      struct Vector buf_ori_2 = vector_new();
      vector_push_into(&buf_ori_2, buf_ori.buf, buf_ori.len);
      translate_vector(&buf_ori, CR_TO_CRLF);
      translate_vector(&buf_ori_2, CR_TO_LF);
      bm_push_into(&stdout_buf, buf_ori.buf, buf_ori.len, true);
      bm_push_into(&socket_buf, buf_ori_2.buf, buf_ori_2.len, false);
      vector_delete(&buf_ori); /* TODO optimize copying */
      vector_delete(&buf_ori_2);
      if (more == false) {
        fprintf(stderr, "unexpected inability to read from keyboard; TODO\n");
        exit(1);
      }
    } else if (has_hup(&poll_fds[0])) {
      fprintf(stderr, "unexpected inability to read from keyboard; TODO\n");
      exit(1);
    }

    if (has_input(&poll_fds[1])) {
      if (do_read(socket_fd, &stdout_buf, IDENTITY, true) == false) { break; }
    } else if (has_hup(&poll_fds[1])) {
      break;
    }
  }

  bm_delete(&socket_buf);
  bm_delete(&stdout_buf);
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
  if (opt_log) { fclose(opt_log); }
  return 0;
}

int
server_main(int argc, char* argv[]) {
  progname = argv[0];

  parse_args(argc, argv);
  if (!opt_port) {
    fprintf(stderr, "%s: required argument '--port' not provided\n", argv[0]);
    exit(1);
  }
  signal(SIGPIPE, SIG_IGN); /* Prefer handling EPIPE */
  int socket_fd = try_listen_and_accept();
  server_event_loop(socket_fd);
  if (opt_log) { fclose(opt_log); }
  return 0;
}

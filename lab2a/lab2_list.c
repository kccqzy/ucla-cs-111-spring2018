#define _GNU_SOURCE

/* NAME: Joe Qian */
/* EMAIL: qzy@g.ucla.edu */
/* ID: 404816794 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "SortedList.h"

/*************************************************************************
 * Option parsing
 *************************************************************************/

static struct option prog_options[] = {
  {.name = "threads", .has_arg = required_argument, .val = 't'},
  {.name = "iterations", .has_arg = required_argument, .val = 'i'},
  {.name = "yield", .has_arg = required_argument, .val = 'y'},
  {.name = "sync", .has_arg = required_argument, .val = 's'},
  {}};

static const char *progname = NULL;

static int opt_threads = 1;
static int opt_iterations = 1;
int opt_yield = 0;
static char *opt_sync = "none";

static void
parse_args(int argc, char *argv[]) {
  while (1) {
    switch (getopt_long(argc, argv, "", prog_options, NULL)) {
    case 't':
      opt_threads = atoi(optarg);
      if (opt_threads <= 0) {
        fprintf(stderr, "%s: number of threads must be positive\n", argv[0]);
        exit(1);
      }
      break;
    case 'i':
      opt_iterations = atoi(optarg);
      if (opt_iterations <= 0) {
        fprintf(stderr, "%s: number of iterations must be positive\n", argv[0]);
        exit(1);
      }
      break;
    case 'y':
      for (size_t i = 0, ie = strlen(optarg); i < ie; ++i) {
        switch (optarg[i]) {
        case 'i': opt_yield |= INSERT_YIELD; break;
        case 'd': opt_yield |= DELETE_YIELD; break;
        case 'l': opt_yield |= LOOKUP_YIELD; break;
        default:
          fprintf(stderr, "%s: yield must be a set of {idl}\n", argv[0]);
          exit(1);
        }
      }
      break;
    case 's':
      opt_sync = optarg;
      if (strlen(opt_sync) != 1 || (*opt_sync != 'm' && *opt_sync != 's')) {
        fprintf(stderr, "%s: sync mode must be 'm' or 's'\n", argv[0]);
        exit(1);
      }
      break;
    case -1:
      /* Determine whether there are no-option parameters left */
      if (optind == argc) { return; }
      /* FALLTHROUGH */

    default: fprintf(stderr, "%s: invalid arguments\n", argv[0]); exit(1);
    }
  }
}

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

#define DIE_IF_ZERO(rv, reason, ...)                                           \
  do {                                                                         \
    if (rv == 0) { DIE(reason, ##__VA_ARGS__); }                               \
  } while (0)


/*************************************************************************
 * Utilities
 *************************************************************************/

void *
xmalloc(size_t s) {
  void *p = malloc(s);
  if (!p) {
    fprintf(stderr, "%s: could not allocate memory", progname);
    exit(1);
  }
  return p;
}

static char *all_keys = NULL;
static SortedListElement_t *all_elements = NULL;

static ssize_t
noeintr_full_read(int fd, uint8_t *buf, size_t n) {
  size_t bytes_left = n;
  while (bytes_left > 0) {
    ssize_t bytes_read = read(fd, buf, bytes_left);
    if (bytes_read == -1) {
      if (errno == EINTR) {
        continue;
      } else {
        return -1;
      }
    } else if (bytes_read == 0) {
      fprintf(stderr, "%s: unexpected EOF when doing a full read\n", progname);
      exit(1);
    }
    buf += (size_t) bytes_read;
    bytes_left -= (size_t) bytes_read;
  }
  return n;
}

static void
make_elements(void) {
  size_t items = opt_threads * opt_iterations;

  /* First get lots of random bytes. */
  uint8_t *random_bytes = xmalloc(8 * items);
  int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
  DIE_IF_MINUS_ONE(fd, "could not open /dev/urandom");
  ssize_t r = noeintr_full_read(fd, random_bytes, 8 * items);
  DIE_IF_MINUS_ONE(r, "could not read from /dev/urandom");
  close(fd);

  /* Then hex-encode them. */
  size_t all_keys_size = 17 * items;
  all_keys = xmalloc(all_keys_size);
  for (size_t i = 0; i < items; ++i) {
    snprintf(all_keys + i * 17, 17, "%02x%02x%02x%02x%02x%02x%02x%02x",
             random_bytes[i * 8 + 0], random_bytes[i * 8 + 1],
             random_bytes[i * 8 + 2], random_bytes[i * 8 + 3],
             random_bytes[i * 8 + 4], random_bytes[i * 8 + 5],
             random_bytes[i * 8 + 6], random_bytes[i * 8 + 7]);
  }

  /* Next create all the list elements. */
  all_elements = calloc(items, sizeof(SortedListElement_t));
  if (!all_elements) {
    fprintf(stderr, "%s: could not allocate memory for all elements\n",
            progname);
    exit(1);
  }
  for (size_t i = 0; i < items; ++i) {
    all_elements[i].key = all_keys + i * 17;
  }
}

static inline uint64_t
get_nano(void) {
  struct timespec t;
  int r = clock_gettime(CLOCK_MONOTONIC, &t);
  DIE_IF_MINUS_ONE(r, "could not get current time");
  return (uint64_t) t.tv_sec * 1000000000ull + (uint64_t) t.tv_nsec;
}

/*************************************************************************
 * Worker
 *************************************************************************/

SortedList_t *list;

#define CONSISTENCY_CHECK(condition, msg)                                      \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "Corrupted list detected, aborting: " msg "\n");         \
      exit(2);                                                                 \
    }                                                                          \
  } while (0)


pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
volatile int af = 0;

#define LOCK_m()                                                               \
  do { pthread_mutex_lock(&mutex); } while (0)
#define UNLOCK_m()                                                             \
  do { pthread_mutex_unlock(&mutex); } while (0)
#define LOCK_s()                                                               \
  do {                                                                         \
  } while (__sync_lock_test_and_set(&af, 1))
#define UNLOCK_s()                                                             \
  do { __sync_lock_release(&af); } while (0)
#define LOCK_none()
#define UNLOCK_none()


#define MAKE_WORKER(how)                                                       \
  static void *worker_##how(void *arg) {                                       \
    SortedListElement_t *insert_begin = (SortedListElement_t *) arg;           \
    int const it = opt_iterations;                                             \
    for (int i = 0; i < it; ++i) {                                             \
      LOCK_##how();                                                            \
      SortedList_insert(list, insert_begin + i);                               \
      UNLOCK_##how();                                                          \
    }                                                                          \
    LOCK_##how();                                                              \
    (void) SortedList_length(list);                                            \
    UNLOCK_##how();                                                            \
    for (int i = 0; i < it; ++i) {                                             \
      LOCK_##how();                                                            \
      SortedListElement_t *el = SortedList_lookup(list, insert_begin[i].key);  \
      UNLOCK_##how();                                                          \
      CONSISTENCY_CHECK(el == insert_begin + i,                                \
                        "Looking up inserted element got unexpected element"); \
      LOCK_##how();                                                            \
      int dr = SortedList_delete(el);                                          \
      UNLOCK_##how();                                                          \
      CONSISTENCY_CHECK(dr == 0,                                               \
                        "Deleting the inserted element reports corruption");   \
    }                                                                          \
    return NULL;                                                               \
  }

MAKE_WORKER(m)
MAKE_WORKER(s)
MAKE_WORKER(none)

#undef MAKE_WORKER
#undef LOCK_none
#undef LOCK_m
#undef LOCK_s
#undef UNLOCK_none
#undef UNLOCK_m
#undef UNLOCK_s

/*************************************************************************
 * Segfaults
 *************************************************************************/
static void
segfault_handler(int sig) {
  (void) sig;
  const char msg[] = "Corrupted list: segmentation fault\n";
  ssize_t r = write(2, msg, sizeof msg - 1);
  (void) r; // stupid warning about ignoring result of write(2)
  _exit(2);
}


int
main(int argc, char *argv[]) {
  progname = argv[0];
  parse_args(argc, argv);
  assert(opt_threads > 0);
  assert(opt_iterations > 0);

  /* Initialize an empty list. */
  list = xmalloc(sizeof(SortedList_t));
  list->key = NULL;
  list->next = list;
  list->prev = list;

  /* Make elements. */
  make_elements();

  /* Register segfault handler. */
  signal(SIGSEGV, segfault_handler);

  /* Dispatch to the right worker */
  void *(*worker)(void *);
  switch (*opt_sync) {
  case 'n': worker = worker_none; break;
  case 'm': worker = worker_m; break;
  case 's': worker = worker_s; break;
  default: assert(false && "unreachable"); exit(10);
  }

  pthread_t th[opt_threads]; // VLA

  uint64_t time_begin = get_nano();
  for (int i = 0; i < opt_threads; ++i) {
    if (0 != pthread_create(th + i, NULL, (void *(*) (void *) ) worker,
                            all_elements + opt_iterations * i)) {
      fprintf(stderr, "%s: could not create worker thread %d.\n", argv[0], i);
      return 1;
    }
  }
  for (int i = 0; i < opt_threads; ++i) {
    if (0 != pthread_join(th[i], NULL)) {
      fprintf(stderr, "%s: could not join worker thread %d.\n", argv[0], i);
      return 1;
    }
  }
  uint64_t time_end = get_nano();
  CONSISTENCY_CHECK(SortedList_length(list) == 0,
                    "Final list length is nonzero");

  uint64_t operations = opt_threads * opt_iterations * 3;
  uint64_t duration = time_end - time_begin;
  uint64_t average_duration = duration / operations;
  printf(
    "list-%s%s%s%s-%s,%d,%d,1,%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
    opt_yield & INSERT_YIELD ? "i" : "", /* Likely inefficient but don't care */
    opt_yield & DELETE_YIELD ? "d" : "", /* Likely inefficient but don't care */
    opt_yield & LOOKUP_YIELD ? "l" : "", /* Likely inefficient but don't care */
    opt_yield == 0 ? "none" : "",        /* Likely inefficient but don't care */
    opt_sync, opt_threads, opt_iterations, operations, duration,
    average_duration);

  return 0;
}

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
  {.name = "lists", .has_arg = required_argument, .val = 'l'},
  {}};

static const char *progname = NULL;

static int opt_threads = 1;
static int opt_iterations = 1;
int opt_yield = 0;
static char *opt_sync = "none";
static int opt_lists = 1;

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
    case 'l':
      opt_lists = atoi(optarg);
      if (opt_lists <= 0) {
        fprintf(stderr, "%s: number of lists must be positive\n", argv[0]);
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

static void *
xmalloc(size_t s) {
  void *p = malloc(s);
  if (!p) {
    fprintf(stderr, "%s: could not allocate memory", progname);
    exit(1);
  }
  return p;
}

static void *
xcalloc(size_t count, size_t size) {
  void *p = calloc(count, size);
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
  all_elements = xcalloc(items, sizeof(SortedListElement_t));
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

struct WorkerArgs {
  SortedListElement_t *insert_begin;
  uint64_t lock_acquire_time;
};

SortedList_t *lists;

#define CONSISTENCY_CHECK(condition, msg, ...)                                 \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "Corrupted list detected, aborting: " msg "\n",          \
              ##__VA_ARGS__);                                                  \
      exit(2);                                                                 \
    }                                                                          \
  } while (0)


static pthread_mutex_t *mutexes;
static volatile int *spinlocks;

#define LOCK_m(n)                                                              \
  do { pthread_mutex_lock(mutexes + (n)); } while (0)
#define UNLOCK_m(n)                                                            \
  do { pthread_mutex_unlock(mutexes + (n)); } while (0)
#define LOCK_s(n)                                                              \
  do {                                                                         \
  } while (__sync_lock_test_and_set(spinlocks + (n), 1))
#define UNLOCK_s(n)                                                            \
  do { __sync_lock_release(spinlocks + (n)); } while (0)
#define LOCK_none(n)
#define UNLOCK_none(n)


#define MAKE_WORKER(how)                                                       \
  static void *worker_##how(void *arg) {                                       \
    struct WorkerArgs *args = (struct WorkerArgs *) arg;                       \
    SortedListElement_t *insert_begin = args->insert_begin;                    \
    int const it = opt_iterations;                                             \
    int const li = opt_lists;                                                  \
    uint64_t lock_acquire_time = 0;                                            \
                                                                               \
    for (int i = 0; i < it; ++i) {                                             \
      int n = *insert_begin[i].key % opt_lists;                                \
      lock_acquire_time -= get_nano();                                         \
      LOCK_##how(n);                                                           \
      lock_acquire_time += get_nano();                                         \
      SortedList_t *list = lists + n;                                          \
      SortedList_insert(list, insert_begin + i);                               \
      UNLOCK_##how(n);                                                         \
    }                                                                          \
                                                                               \
    lock_acquire_time -= get_nano();                                           \
    for (int i = 0; i < li; ++i) {                                             \
      LOCK_##how(i);                                                           \
      (void) SortedList_length(lists + i);                                     \
      UNLOCK_##how(i);                                                         \
    }                                                                          \
    lock_acquire_time += get_nano();                                           \
                                                                               \
    for (int i = 0; i < it; ++i) {                                             \
      int n = *insert_begin[i].key % opt_lists;                                \
      lock_acquire_time -= get_nano();                                         \
      LOCK_##how(n);                                                           \
      lock_acquire_time += get_nano();                                         \
      SortedList_t *list = lists + n;                                          \
      SortedListElement_t *el = SortedList_lookup(list, insert_begin[i].key);  \
      UNLOCK_##how(n);                                                         \
      CONSISTENCY_CHECK(el == insert_begin + i,                                \
                        "Looking up inserted element got unexpected element; " \
                        "expecting %p found %p",                               \
                        insert_begin + i, el);                                 \
                                                                               \
      lock_acquire_time -= get_nano();                                         \
      LOCK_##how(n);                                                           \
      lock_acquire_time += get_nano();                                         \
      int dr = SortedList_delete(el);                                          \
      UNLOCK_##how(n);                                                         \
      CONSISTENCY_CHECK(dr == 0,                                               \
                        "Deleting the inserted element reports corruption");   \
    }                                                                          \
    args->lock_acquire_time = lock_acquire_time;                               \
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

  /* Initialize empty lists and locks. */
  lists = xcalloc(opt_lists, sizeof(SortedList_t));
  mutexes = xcalloc(opt_lists, sizeof(pthread_mutex_t));
  spinlocks = xcalloc(opt_lists, sizeof(int));
  for (int i = 0; i < opt_lists; ++i) {
    lists[i].next = &lists[i];
    lists[i].prev = &lists[i];
    pthread_mutex_init(mutexes + i, NULL);
    spinlocks[i] = 0;
  }

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

  /* Initialize per-thread objects */
  pthread_t th[opt_threads];
  struct WorkerArgs wa[opt_threads];
  for (int i = 0; i < opt_threads; ++i) {
    wa[i].lock_acquire_time = 0;
    wa[i].insert_begin = all_elements + opt_iterations * i;
  }

  uint64_t time_begin = get_nano();
  for (int i = 0; i < opt_threads; ++i) {
    if (0 != pthread_create(th + i, NULL, worker, wa + i)) {
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

  int final_list_length = 0;
  for (int i = 0; i < opt_lists; ++i) {
    final_list_length += SortedList_length(lists + i);
  }
  CONSISTENCY_CHECK(final_list_length == 0, "Final list length is nonzero");

  uint64_t operations = opt_threads * opt_iterations * 3;
  uint64_t duration = time_end - time_begin;
  uint64_t average_duration = duration / operations;
  uint64_t average_wait_for_lock = 0;
  for (int i = 0; i < opt_threads; ++i) {
    average_wait_for_lock += wa[i].lock_acquire_time;
  }
  average_wait_for_lock /= operations;
  printf(
    "list-%s%s%s%s-%s,%d,%d,%d,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
    "\n",
    opt_yield & INSERT_YIELD ? "i" : "", /* Likely inefficient but don't care */
    opt_yield & DELETE_YIELD ? "d" : "", /* Likely inefficient but don't care */
    opt_yield & LOOKUP_YIELD ? "l" : "", /* Likely inefficient but don't care */
    opt_yield == 0 ? "none" : "",        /* Likely inefficient but don't care */
    opt_sync, opt_threads, opt_iterations, opt_lists, operations, duration,
    average_duration, average_wait_for_lock);

  return 0;
}

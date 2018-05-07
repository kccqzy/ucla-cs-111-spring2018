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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/*************************************************************************
 * Option parsing
 *************************************************************************/

static struct option prog_options[] = {
  {.name = "threads", .has_arg = required_argument, .val = 't'},
  {.name = "iterations", .has_arg = required_argument, .val = 'i'},
  {.name = "yield", .has_arg = no_argument, .val = 'y'},
  {}};

static const char *progname = NULL;

static int opt_threads = 1;
static int opt_iterations = 1;
static bool opt_yield = false;

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
    case 'y': opt_yield = true; break;
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
 * Adding
 *************************************************************************/

long long counter = 0;

static void
add(long long *pointer, long long value) {
  long long sum = *pointer + value;
  if (opt_yield) { sched_yield(); }
  *pointer = sum;
}

/*************************************************************************
 * Worker
 *************************************************************************/
static int
worker(void) {
  for (int i = 0; i < opt_iterations; ++i) { add(&counter, 1); }
  for (int i = 0; i < opt_iterations; ++i) { add(&counter, -1); }
  return 0;
}

static inline uint64_t
get_nano(void) {
  struct timespec t;
  int r = clock_gettime(CLOCK_MONOTONIC, &t);
  DIE_IF_MINUS_ONE(r, "could not get current time");
  return (uint64_t) t.tv_sec * 1000000000ull + (uint64_t) t.tv_nsec;
}

/*************************************************************************
 * Main
 *************************************************************************/

int
main(int argc, char *argv[]) {
  progname = argv[0];
  parse_args(argc, argv);
  assert(opt_threads > 0);

  uint64_t time_begin = get_nano();

  pthread_t th[opt_threads]; // VLA
  for (int i = 0; i < opt_threads; ++i) {
    if (0 != pthread_create(th + i, NULL, (void *(*) (void *) ) worker, NULL)) {
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

  uint64_t operations = opt_threads * opt_iterations * 2;
  uint64_t duration = time_end - time_begin;
  uint64_t average_duration = duration / operations;
  printf("%s,%d,%d,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIi64 "\n",
         opt_yield ? "add-yield-none" : "add-none", opt_threads, opt_iterations,
         operations, duration, average_duration, counter);
}

#define _GNU_SOURCE
#include "button.h"
#include "temp.h"
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*************************************************************************
 * Global variables and constants
 *************************************************************************/
static const char *progname = NULL;

/* Options */
static struct option prog_options[] = {
    {.name = "period", .has_arg = required_argument, .val = 'p'},
    {.name = "scale", .has_arg = required_argument, .val = 's'},
    {.name = "log", .has_arg = required_argument, .val = 'l'},
    {0}};

static int opt_period = 1;
static char opt_scale = 'F';
static FILE *opt_log = NULL;

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
    if (rv == -1) {                                                            \
      DIE(reason, ##__VA_ARGS__);                                              \
    }                                                                          \
  } while (0)

/*************************************************************************
 * Utilities
 *************************************************************************/
static void parse_args(int argc, char *argv[]) {
  while (1) {
    switch (getopt_long(argc, argv, "", prog_options, NULL)) {
    case 'p':
      opt_period = atoi(optarg);
      if (opt_period <= 0) {
        fprintf(stderr, "%s: period must be a positive integer\n", argv[0]);
        exit(1);
      }
      break;
    case 's':
      if (strlen(optarg) != 1 || (*optarg != 'C' && *optarg != 'F')) {
        fprintf(stderr, "%s: scale must be either 'C' or 'F'\n", argv[0]);
        exit(1);
      }
      break;
    case 'l': {
      int logfd = open(
          optarg, O_CREAT | O_TRUNC | O_WRONLY | O_APPEND | O_CLOEXEC, 0666);
      DIE_IF_MINUS_ONE(logfd, "could not open log file '%s' for writing",
                       optarg);
      opt_log = fdopen(logfd, "w");
      if (!opt_log) {
        fprintf(stderr, "%s: could not create C stream for log file\n",
                argv[0]);
        exit(1);
      }
      setvbuf(opt_log, NULL, _IONBF, 0);
    } break;
    case -1:
      /* Determine whether there are no-option parameters left */
      if (optind == argc) {
        return;
      }
      /* FALLTHROUGH */
    default:
      fprintf(stderr, "usage: %s [arguments]\n", argv[0]);
      exit(1);
    }
  }
}

/*************************************************************************
 * Main program
 *************************************************************************/

static void sample(void) {
  struct timespec t;
  int r = clock_gettime(CLOCK_REALTIME, &t);
  DIE_IF_MINUS_ONE(r, "could not get current time");
  struct tm *tm = localtime(&t.tv_sec);
  char buf[40];
  strftime(buf, sizeof buf, "%T", tm);
  float temperature = opt_scale == 'F' ? get_temperature_fahrenheit()
                                       : get_temperature_celsius();
  char buf2[10];
  snprintf(buf2, sizeof buf2, " %.1f\n", temperature);

  fputs(buf, stdout);
  fputs(buf2, stdout);

  if (opt_log) {
    fputs(buf, opt_log);
    fputs(buf2, opt_log);
  }
}

static void shutdown() {
  struct timespec t;
  int r = clock_gettime(CLOCK_REALTIME, &t);
  DIE_IF_MINUS_ONE(r, "could not get current time");
  struct tm *tm = localtime(&t.tv_sec);
  char buf[60];
  strftime(buf, sizeof buf, "%T SHUTDOWN\n", tm);
  fputs(buf, stdout);
  if (opt_log) {
    fputs(buf, opt_log);
  }
}

int main(int argc, char *argv[]) {
  progname = argv[0];
  parse_args(argc, argv);

  setvbuf(stdin, NULL, _IONBF, 0); // Sigh

  init_button();
  init_temperature_sensor();

  struct pollfd poll_fds[] = {{.fd = 0, .events = POLLIN},
                              {.fd = button_fd[0], .events = POLLIN}};

  char *line = NULL;
  size_t linecap = 0;

  bool do_report = true;

  while (1) {
    int poll_rv = poll(poll_fds, sizeof poll_fds / sizeof(struct pollfd),
                       1000 * opt_period);
    DIE_IF_MINUS_ONE(poll_rv, "could not poll");

    // Is the button pressed?
    if (poll_fds[1].revents & POLLIN) {
      // Button is pressed.
      shutdown();
      break;
    }

    /* Did we receive any input? */
    if (poll_fds[0].revents & POLLIN) {
      ssize_t r = getline(&line, &linecap, stdin);
      DIE_IF_MINUS_ONE(r, "could not read from stdin");
      if (opt_log) {
        fputs(line, opt_log);
      }
      if (0 == strcmp(line, "SCALE=F\n")) {
        opt_scale = 'F';
      } else if (0 == strcmp(line, "SCALE=C\n")) {
        opt_scale = 'C';
      } else if (0 == strncmp(line, "PERIOD=", 7)) {
        opt_period = atoi(line + 7);
      } else if (0 == strcmp(line, "STOP\n")) {
        do_report = false;
      } else if (0 == strcmp(line, "START\n")) {
        do_report = true;
      } else if (0 == strcmp(line, "OFF\n")) {
        shutdown();
        break;
      }
    }

    if (do_report) {
      sample();
    }
  }
  free(line);
  return 0;
}

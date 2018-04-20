#include <assert.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
main(void) {
  int fds[2];
  int r = pipe(fds);
  if (r == -1) exit(1);

  // Close the read end
  r = close(fds[0]);
  if (r == -1) exit(1);

  // Poll the write end with zero timeout
  struct pollfd pfd = {.fd = fds[1], .events = POLLOUT};
  r = poll(&pfd, 1, 0);
  if (r == -1) exit(1);
  printf(
    "POLLIN = %d\n"
    "POLLOUT = %d\n"
    "POLLHUP = %d\n"
    "POLLERR = %d\n"
    "POLLNVAL = %d\n"
    "revents of the write-end of a pipe: %d\n",
    POLLIN, POLLOUT, POLLHUP, POLLERR, POLLNVAL, pfd.revents);
  assert(pfd.revents & POLLERR);

  return 0;
}

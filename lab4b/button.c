#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "button.h"

int main(void) {
  init_button();
  ssize_t r;
  char buf[1];
  while ((r = read(button_fd[0], buf, 1))) {
    if (r == -1) {
      fprintf(stderr, "Could not read from pipe");
    }
    printf("Button pressed.\n");
  }

  return 0;
}

#ifndef BUTTON_H
#define BUTTON_H

#include <mraa.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int button_fd[2];

static void write_button_fd(void *a) {
  (void)a;
  write(button_fd[1], &(uint8_t){1}, 1);
}

static void init_button(void) {
  mraa_gpio_context gpio_ctx = mraa_gpio_init(60);
  if (gpio_ctx == NULL) {
    fprintf(stderr, "Failed to initialize GPIO\n");
    exit(1);
  }

  {
    mraa_result_t r = mraa_gpio_dir(gpio_ctx, MRAA_GPIO_IN);
    if (r != MRAA_SUCCESS) {
      fprintf(stderr, "Failed to configure GPIO direction\n");
      exit(1);
    }
  }

  {
    int r = pipe(button_fd);
    if (r == -1) {
      fprintf(stderr, "Failed to set up pipe\n");
      exit(1);
    }
    mraa_gpio_isr(gpio_ctx, MRAA_GPIO_EDGE_RISING, write_button_fd, NULL);
  }
}

#endif

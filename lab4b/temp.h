#ifndef TEMP_H
#define TEMP_H

#include <math.h>
#include <stdio.h>
#include <mraa.h>
#include <stdlib.h>

static mraa_aio_context aio_ctx = NULL;

static void deinit_temperature_sensor(void) {
  mraa_aio_close(aio_ctx);
  aio_ctx = NULL;
}

static void init_temperature_sensor(void) {
  if (aio_ctx == NULL) {
    aio_ctx = mraa_aio_init(1);
    if (aio_ctx == NULL) {
      fprintf(stderr, "Failed to initialize AIO\n");
      exit(1);
    }
    atexit(deinit_temperature_sensor);
  }
}

static float get_temperature_celsius(void) {
  int a = mraa_aio_read(aio_ctx);
  if (a == -1) {
    fprintf(stderr, "Failed to read AIO\n");
    exit(1);
  }
  const int B = 4275;    // B value of the thermistor
  const int R0 = 100000; // R0 = 100k
  float R = 1023.0f / a - 1.0f;
  R *= R0;
  float temperature = 1.0f / (log(R / R0) / B + 1 / 298.15f) - 273.15f;

  return temperature;
}

static float get_temperature_fahrenheit(void) {
  return get_temperature_celsius() * 9.0f / 5.0f + 32.0f;
}

#endif

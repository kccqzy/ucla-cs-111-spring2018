#include <stdio.h>
#include "temp.h"

int main(void) {
  init_temperature_sensor();
  printf("Temperature %.1f\n", get_temperature_fahrenheit());
  return 0;
}

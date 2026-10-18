#include <cstdio>

#include <pico/stdlib.h>

#include <RP2040PIO_HalfDuplex.h>

RP2040PIO_HalfDuplex halfDuplex(27);

int main() {
  stdio_init_all();
  halfDuplex.begin(115200);

  while (true) {
    if (halfDuplex.available()) {
      printf("%X\n", halfDuplex.read());
    }

    halfDuplex.write(0xFF);

    sleep_ms(10);
  }
}

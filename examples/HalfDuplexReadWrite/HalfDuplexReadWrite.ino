#include <RP2040PIO_HalfDuplex.h>

RP2040PIO_HalfDuplex halfDuplex(27);

void setup() {
  Serial.begin(115200);
  halfDuplex.begin(115200);
}

void loop() {
  if (halfDuplex.available()) {
    Serial.println(halfDuplex.read(), HEX);
  }

  halfDuplex.write(0xFF);

  delay(10);
}

# RP2040PIO_HalfDuplex

Half-duplex UART library for RP2040/2350 PIO.

## Supported MCUs

- RP2040
- RP2350

## Getting Started

### Setup

```cpp
#include <RP2040PIO_HalfDuplex.h>

RP2040PIO_HalfDuplex halfDuplex(27);
// RP2040PIO_HalfDuplex halfDuplex(27, HalfDuplexTxMode::PUSH_PULL);

void setup() {
  Serial.begin(115200);
  halfDuplex.begin(115200);
}
```

### Receive

```cpp
if (halfDuplex.available()) {
  Serial.println(halfDuplex.read(), HEX);
}
```

### Transmit

```cpp
halfDuplex.write(0xFF);
halfDuplex.flush(); // wait until the last stop bit is sent
```

### DMA

```cpp
RP2040PIO_HalfDuplex halfDuplex(27, HalfDuplexTxMode::PUSH_PULL, HalfDuplexTransfer::DMA);
```

Receives and transmits by DMA (3 channels on RP2040, 2 on RP2350). `write(buffer, size)` sends the bytes without gaps, and `rx_queue_size` is rounded up to a power of two (up to 32768).

## License

GNU Lesser General Public License v2.1

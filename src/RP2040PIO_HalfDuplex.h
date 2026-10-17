#pragma once

#include <cstddef>
#include <cstdint>

#include <Arduino.h>
#include <hardware/pio.h>

#include "RingBuffer.h"

enum class HalfDuplexTxMode {
  OPEN_DRAIN,
  PUSH_PULL,
};

class RP2040PIO_HalfDuplex : public Stream {
public:
  RP2040PIO_HalfDuplex(pin_size_t pin, HalfDuplexTxMode tx_mode = HalfDuplexTxMode::OPEN_DRAIN);
  void begin(uint32_t baudrate = 115200, size_t rx_queue_size = 32);
  int available() override;
  int read() override;
  int peek() override;
  size_t write(uint8_t data) override;
  using Print::write;
  void flush() override;

private:
  pin_size_t pin_;
  HalfDuplexTxMode tx_mode_;
  PIO pio_;
  uint tx_sm_;
  uint rx_sm_;
  RingBuffer<uint8_t> rx_queue_;

  static void rxIrqHandler();
};
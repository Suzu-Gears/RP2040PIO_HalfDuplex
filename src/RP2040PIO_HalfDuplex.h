#pragma once

#include <cstddef>
#include <cstdint>

#include <Arduino.h>
#include <hardware/pio.h>

#include "Queue.h"

enum class HalfDuplexTxMode {
  OpenDrain,
  PushPull,
};

class RP2040PIO_HalfDuplex : public Stream {
public:
  RP2040PIO_HalfDuplex(pin_size_t pin, HalfDuplexTxMode tx_mode = HalfDuplexTxMode::OpenDrain);
  void begin(uint32_t baudrate = 115200, size_t rx_queue_size = 32);
  int available() override;
  int read() override;
  int peek() override;
  size_t write(uint8_t data) override;
  using Print::write;

private:
  pin_size_t pin_;
  HalfDuplexTxMode tx_mode_;
  PIO pio_;
  uint tx_sm_;
  uint rx_sm_;
  Queue<uint8_t> rx_queue_;

  static void rxIrqHandler();
};
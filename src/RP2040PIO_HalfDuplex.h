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

enum class HalfDuplexTransfer {
  INTERRUPT,
  DMA,
};

class RP2040PIO_HalfDuplex : public Stream {
public:
  RP2040PIO_HalfDuplex(pin_size_t pin, HalfDuplexTxMode tx_mode = HalfDuplexTxMode::OPEN_DRAIN,
                       HalfDuplexTransfer transfer = HalfDuplexTransfer::INTERRUPT);
  void begin(uint32_t baudrate = 115200, size_t rx_queue_size = 32);
  void end();
  int available() override;
  int read() override;
  int peek() override;
  size_t write(uint8_t data) override;
  size_t write(const uint8_t *buffer, size_t size) override;
  using Print::write;
  void flush() override;

private:
  pin_size_t pin_;
  HalfDuplexTxMode tx_mode_;
  HalfDuplexTransfer transfer_;
  PIO pio_;
  uint tx_sm_;
  uint rx_sm_;
  uint tx_offset_;
  uint rx_offset_;
  RingBuffer<uint8_t> rx_queue_;
  uint8_t *rx_buf_;
  size_t rx_buf_mask_;
  size_t rx_read_idx_;
  uint rx_dma_chan_;
  uint rx_dma_ctrl_chan_;
  uint tx_dma_chan_;

  void beginInterrupt(size_t rx_queue_size);
  void beginDma(size_t rx_queue_size);
  size_t rxWriteIdx();
  static void rxIrqHandler();
};
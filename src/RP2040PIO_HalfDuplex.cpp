#include <algorithm>
#include <new>

#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/irq.h>

#include "RP2040PIO_HalfDuplex.h"
#include "rp2040pio_half_duplex.pio.h"

static RP2040PIO_HalfDuplex *rp2040pio_half_duplex_map[NUM_PIOS][NUM_PIO_STATE_MACHINES];

#if PICO_RP2040
static uint32_t rx_dma_trans_count = 0xFFFFFFFF;
#else
static uint32_t rx_dma_trans_count = dma_encode_endless_transfer_count();
#endif

static const pio_program_t *tx_program(HalfDuplexTxMode tx_mode) {
  return tx_mode == HalfDuplexTxMode::OPEN_DRAIN ? &rp2040pio_half_duplex_tx_open_drain_program : &rp2040pio_half_duplex_tx_push_pull_program;
}

static bool pio_can_add_two_programs(PIO pio, const pio_program_t *program0, const pio_program_t *program1) {
  if (!pio_can_add_program(pio, program0)) {
    return false;
  }
  uint offset0 = pio_add_program(pio, program0);
  bool success = pio_can_add_program(pio, program1);
  pio_remove_program(pio, program0, offset0);
  return success;
}

static bool pio_claim_two_free_sms_and_add_programs(const pio_program_t *program0, const pio_program_t *program1,
                                                    PIO *pio, uint *sm_base, uint *offset0, uint *offset1) {
#if defined(PICO_RP2350)
  static PIO pios[] = { pio0, pio1, pio2 };
#elif defined(PICO_RP2040)
  static PIO pios[] = { pio0, pio1 };
#endif
  for (uint i = 0; i < count_of(pios); ++i) {
    PIO p = pios[i];
    if (!pio_can_add_two_programs(p, program0, program1)) {
      continue;
    }
    for (uint sm = 0; sm + 1 < NUM_PIO_STATE_MACHINES; ++sm) {
      if (!pio_sm_is_claimed(p, sm) && !pio_sm_is_claimed(p, sm + 1)) {
        *pio = p;
        *sm_base = sm;
        *offset0 = pio_add_program(p, program0);
        *offset1 = pio_add_program(p, program1);
        pio_sm_claim(p, sm);
        pio_sm_claim(p, sm + 1);
        return true;
      }
    }
  }
  return false;
}

RP2040PIO_HalfDuplex::RP2040PIO_HalfDuplex(pin_size_t pin, HalfDuplexTxMode tx_mode, HalfDuplexTransfer transfer)
  : pin_{ pin }, tx_mode_{ tx_mode }, transfer_{ transfer }, pio_{ nullptr } {}

void RP2040PIO_HalfDuplex::begin(uint32_t baudrate, size_t rx_queue_size) {
  end();
  if (!pio_claim_two_free_sms_and_add_programs(tx_program(tx_mode_), &rp2040pio_half_duplex_rx_program, &pio_, &tx_sm_, &tx_offset_, &rx_offset_)) {
    panic("RP2040PIO_HalfDuplex: no free PIO state machines");
  }
  rx_sm_ = tx_sm_ + 1;

  float div = static_cast<float>(clock_get_hz(clk_sys)) / (8 * baudrate);

  pio_sm_set_pins_with_mask64(pio_, tx_sm_, 0ull << pin_, 1ull << pin_);
  pio_sm_set_pindirs_with_mask64(pio_, tx_sm_, 0ull << pin_, 1ull << pin_);
  pio_gpio_init(pio_, pin_);

  pio_sm_config tx_config = tx_mode_ == HalfDuplexTxMode::OPEN_DRAIN
                              ? rp2040pio_half_duplex_tx_open_drain_program_get_default_config(tx_offset_)
                              : rp2040pio_half_duplex_tx_push_pull_program_get_default_config(tx_offset_);
  sm_config_set_out_shift(&tx_config, true, false, 32);
  sm_config_set_out_pins(&tx_config, pin_, 1);
  sm_config_set_set_pins(&tx_config, pin_, 1);
  sm_config_set_sideset_pins(&tx_config, pin_);
  sm_config_set_clkdiv(&tx_config, div);
  sm_config_set_fifo_join(&tx_config, PIO_FIFO_JOIN_TX);
  pio_sm_init(pio_, tx_sm_, tx_offset_, &tx_config);

  pio_sm_config rx_config = rp2040pio_half_duplex_rx_program_get_default_config(rx_offset_);
  sm_config_set_in_shift(&rx_config, true, true, 8);
  sm_config_set_in_pins(&rx_config, pin_);
  sm_config_set_jmp_pin(&rx_config, pin_);
  sm_config_set_clkdiv(&rx_config, div);
  sm_config_set_fifo_join(&rx_config, PIO_FIFO_JOIN_RX);
  pio_sm_init(pio_, rx_sm_, rx_offset_, &rx_config);

  if (transfer_ == HalfDuplexTransfer::DMA) {
    beginDma(rx_queue_size);
  } else {
    beginInterrupt(rx_queue_size);
  }

  pio_sm_set_enabled(pio_, tx_sm_, true);
  pio_sm_set_enabled(pio_, rx_sm_, true);
}

void RP2040PIO_HalfDuplex::end() {
  if (!pio_) {
    return;
  }
  pio_sm_set_enabled(pio_, tx_sm_, false);
  pio_sm_set_enabled(pio_, rx_sm_, false);
  if (transfer_ == HalfDuplexTransfer::DMA) {
    uint32_t dma_mask = (1u << rx_dma_chan_) | (1u << tx_dma_chan_);
#if PICO_RP2040
    dma_mask |= 1u << rx_dma_ctrl_chan_;
#endif
    dma_hw->abort = dma_mask; // all at once, so that the control channel cannot restart the RX channel
    while (dma_hw->abort & dma_mask) {
    }
    dma_unclaim_mask(dma_mask);
    operator delete[](rx_buf_, std::align_val_t(rx_buf_mask_ + 1));
  } else {
    pio_set_irq0_source_enabled(pio_, pio_get_rx_fifo_not_empty_interrupt_source(rx_sm_), false);
    RP2040PIO_HalfDuplex **map = rp2040pio_half_duplex_map[pio_get_index(pio_)];
    map[rx_sm_] = nullptr;
    rx_queue_.clear();
    if (std::all_of(map, map + NUM_PIO_STATE_MACHINES, [](RP2040PIO_HalfDuplex *b) { return !b; })) { // the last one on this PIO
      uint pio_irq = pio_get_irq_num(pio_, 0);
      irq_set_enabled(pio_irq, false);
      irq_remove_handler(pio_irq, rxIrqHandler);
    }
  }
  pio_remove_program(pio_, tx_program(tx_mode_), tx_offset_);
  pio_remove_program(pio_, &rp2040pio_half_duplex_rx_program, rx_offset_);
  pio_sm_unclaim(pio_, tx_sm_);
  pio_sm_unclaim(pio_, rx_sm_);
  pio_ = nullptr;
}

int RP2040PIO_HalfDuplex::available() {
  if (transfer_ == HalfDuplexTransfer::DMA) {
    return (rxWriteIdx() - rx_read_idx_) & rx_buf_mask_;
  }
  return rx_queue_.size();
}

int RP2040PIO_HalfDuplex::read() {
  if (transfer_ == HalfDuplexTransfer::DMA) {
    int data = peek();
    if (data >= 0) {
      rx_read_idx_ = (rx_read_idx_ + 1) & rx_buf_mask_;
    }
    return data;
  }
  std::optional<uint8_t> data = rx_queue_.pop();
  return data ? *data : -1;
}

int RP2040PIO_HalfDuplex::peek() {
  if (transfer_ == HalfDuplexTransfer::DMA) {
    return rxWriteIdx() == rx_read_idx_ ? -1 : rx_buf_[rx_read_idx_];
  }
  std::optional<uint8_t> data = rx_queue_.peek();
  return data ? *data : -1;
}

size_t RP2040PIO_HalfDuplex::write(uint8_t data) {
  pio_sm_put_blocking(pio_, tx_sm_, data);
  return 1;
}

size_t RP2040PIO_HalfDuplex::write(const uint8_t *buffer, size_t size) {
  if (transfer_ == HalfDuplexTransfer::DMA) {
    dma_channel_transfer_from_buffer_now(tx_dma_chan_, buffer, size);
    dma_channel_wait_for_finish_blocking(tx_dma_chan_);
    return size;
  }
  return Print::write(buffer, size);
}

void RP2040PIO_HalfDuplex::flush() {
  // TXSTALL is set once the TX state machine waits for data after the last stop bit.
  uint32_t tx_stall = 1u << (PIO_FDEBUG_TXSTALL_LSB + tx_sm_);
  pio_->fdebug = tx_stall;
  while (!(pio_->fdebug & tx_stall)) {
  }
}

void RP2040PIO_HalfDuplex::beginInterrupt(size_t rx_queue_size) {
  rx_queue_.init(rx_queue_size);

  rp2040pio_half_duplex_map[pio_get_index(pio_)][rx_sm_] = this;

  uint pio_irq = pio_get_irq_num(pio_, 0);
  pio_set_irq0_source_enabled(pio_, pio_get_rx_fifo_not_empty_interrupt_source(rx_sm_), true);
  irq_set_exclusive_handler(pio_irq, rxIrqHandler);
  irq_set_enabled(pio_irq, true);
}

void RP2040PIO_HalfDuplex::beginDma(size_t rx_queue_size) {
  uint ring_size_bits = 0;
  while ((1u << ring_size_bits) < rx_queue_size && ring_size_bits < 15) {
    ++ring_size_bits;
  }
  rx_buf_ = new (std::align_val_t(1 << ring_size_bits)) uint8_t[1 << ring_size_bits];
  rx_buf_mask_ = (1 << ring_size_bits) - 1;
  rx_read_idx_ = 0;

  rx_dma_chan_ = dma_claim_unused_channel(true);
  dma_channel_config rx_dma_config = dma_channel_get_default_config(rx_dma_chan_);
  channel_config_set_transfer_data_size(&rx_dma_config, DMA_SIZE_8);
  channel_config_set_read_increment(&rx_dma_config, false);
  channel_config_set_write_increment(&rx_dma_config, true);
  channel_config_set_ring(&rx_dma_config, true, ring_size_bits);
  channel_config_set_dreq(&rx_dma_config, pio_get_dreq(pio_, rx_sm_, false));
#if PICO_RP2040
  // No endless mode on RP2040: a control channel restarts the RX channel whenever it finishes.
  rx_dma_ctrl_chan_ = dma_claim_unused_channel(true);
  dma_channel_config rx_dma_ctrl_config = dma_channel_get_default_config(rx_dma_ctrl_chan_);
  channel_config_set_transfer_data_size(&rx_dma_ctrl_config, DMA_SIZE_32);
  channel_config_set_read_increment(&rx_dma_ctrl_config, false);
  channel_config_set_write_increment(&rx_dma_ctrl_config, false);
  dma_channel_configure(rx_dma_ctrl_chan_, &rx_dma_ctrl_config, &dma_hw->ch[rx_dma_chan_].al1_transfer_count_trig, &rx_dma_trans_count, 1, false);
  channel_config_set_chain_to(&rx_dma_config, rx_dma_ctrl_chan_);
#endif
  dma_channel_configure(rx_dma_chan_, &rx_dma_config, rx_buf_, reinterpret_cast<io_ro_8 *>(&pio_->rxf[rx_sm_]) + 3, rx_dma_trans_count, true);

  tx_dma_chan_ = dma_claim_unused_channel(true);
  dma_channel_config tx_dma_config = dma_channel_get_default_config(tx_dma_chan_);
  channel_config_set_transfer_data_size(&tx_dma_config, DMA_SIZE_8);
  channel_config_set_read_increment(&tx_dma_config, true);
  channel_config_set_write_increment(&tx_dma_config, false);
  channel_config_set_dreq(&tx_dma_config, pio_get_dreq(pio_, tx_sm_, true));
  dma_channel_configure(tx_dma_chan_, &tx_dma_config, &pio_->txf[tx_sm_], nullptr, 0, false);
}

size_t RP2040PIO_HalfDuplex::rxWriteIdx() {
  size_t write_idx = (dma_hw->ch[rx_dma_chan_].write_addr - reinterpret_cast<uintptr_t>(rx_buf_)) & rx_buf_mask_;
  __compiler_memory_barrier();
  return write_idx;
}

void __not_in_flash_func(RP2040PIO_HalfDuplex::rxIrqHandler)() {
  for (uint p = 0; p < NUM_PIOS; ++p) {
    for (uint sm = 0; sm < NUM_PIO_STATE_MACHINES; ++sm) {
      RP2040PIO_HalfDuplex *rp2040pio_half_duplex = rp2040pio_half_duplex_map[p][sm];
      if (rp2040pio_half_duplex) {
        while (!pio_sm_is_rx_fifo_empty(rp2040pio_half_duplex->pio_, rp2040pio_half_duplex->rx_sm_)) {
          uint8_t data = pio_sm_get(rp2040pio_half_duplex->pio_, rp2040pio_half_duplex->rx_sm_) >> 24;
          rp2040pio_half_duplex->rx_queue_.push(data);
        }
      }
    }
  }
}
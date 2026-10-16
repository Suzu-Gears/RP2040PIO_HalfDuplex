#include <hardware/clocks.h>
#include <hardware/irq.h>

#include "RP2040PIO_HalfDuplex.h"
#include "rp2040pio_half_duplex.pio.h"

static RP2040PIO_HalfDuplex *rp2040pio_half_duplex_map[NUM_PIOS][NUM_PIO_STATE_MACHINES];

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

RP2040PIO_HalfDuplex::RP2040PIO_HalfDuplex(pin_size_t pin, HalfDuplexTxMode tx_mode)
  : pin_{ pin }, tx_mode_{ tx_mode } {}

void RP2040PIO_HalfDuplex::begin(uint32_t baudrate, size_t rx_queue_size) {
  rx_queue_.init(rx_queue_size);

  uint tx_offset;
  uint rx_offset;
  pio_claim_two_free_sms_and_add_programs(tx_mode_ == HalfDuplexTxMode::OPEN_DRAIN ? &rp2040pio_half_duplex_tx_open_drain_program : &rp2040pio_half_duplex_tx_push_pull_program, &rp2040pio_half_duplex_rx_program, &pio_, &tx_sm_, &tx_offset, &rx_offset);
  rx_sm_ = tx_sm_ + 1;

  float div = static_cast<float>(clock_get_hz(clk_sys)) / (8 * baudrate);

  pio_sm_set_pins_with_mask64(pio_, tx_sm_, 0ull << pin_, 1ull << pin_);
  pio_sm_set_pindirs_with_mask64(pio_, tx_sm_, 0ull << pin_, 1ull << pin_);
  pio_gpio_init(pio_, pin_);

  pio_sm_config tx_config = tx_mode_ == HalfDuplexTxMode::OPEN_DRAIN
                              ? rp2040pio_half_duplex_tx_open_drain_program_get_default_config(tx_offset)
                              : rp2040pio_half_duplex_tx_push_pull_program_get_default_config(tx_offset);
  sm_config_set_out_shift(&tx_config, true, false, 32);
  sm_config_set_out_pins(&tx_config, pin_, 1);
  sm_config_set_set_pins(&tx_config, pin_, 1);
  sm_config_set_sideset_pins(&tx_config, pin_);
  sm_config_set_clkdiv(&tx_config, div);
  pio_sm_init(pio_, tx_sm_, tx_offset, &tx_config);

  pio_sm_config rx_config = rp2040pio_half_duplex_rx_program_get_default_config(rx_offset);
  sm_config_set_in_shift(&rx_config, true, true, 8);
  sm_config_set_in_pins(&rx_config, pin_);
  sm_config_set_jmp_pin(&rx_config, pin_);
  sm_config_set_clkdiv(&rx_config, div);
  sm_config_set_fifo_join(&rx_config, PIO_FIFO_JOIN_RX);
  pio_sm_init(pio_, rx_sm_, rx_offset, &rx_config);

  rp2040pio_half_duplex_map[pio_get_index(pio_)][rx_sm_] = this;

  uint pio_irq = pio_get_irq_num(pio_, 0);
  pio_set_irq0_source_enabled(pio_, pio_get_rx_fifo_not_empty_interrupt_source(rx_sm_), true);
  irq_set_exclusive_handler(pio_irq, rxIrqHandler);
  irq_set_enabled(pio_irq, true);

  pio_sm_set_enabled(pio_, tx_sm_, true);
  pio_sm_set_enabled(pio_, rx_sm_, true);
}

int RP2040PIO_HalfDuplex::available() {
  return rx_queue_.size();
}

int RP2040PIO_HalfDuplex::read() {
  std::optional<uint8_t> data = rx_queue_.pop();
  return data ? *data : -1;
}

int RP2040PIO_HalfDuplex::peek() {
  std::optional<uint8_t> data = rx_queue_.peek();
  return data ? *data : -1;
}

size_t RP2040PIO_HalfDuplex::write(uint8_t data) {
  pio_sm_put_blocking(pio_, tx_sm_, data);
  return 1;
}

void __not_in_flash_func(RP2040PIO_HalfDuplex::rxIrqHandler)() {
  for (int p = 0; p < NUM_PIOS; ++p) {
    for (int sm = 0; sm < NUM_PIO_STATE_MACHINES; ++sm) {
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
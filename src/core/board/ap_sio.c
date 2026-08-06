#include "board/ap_sio.h"

bool ap_sio_reset(ap_sio_t *sio) {
  ap_mc68681_reset(&sio->port[0]);
  ap_mc68681_reset(&sio->port[1]);
  sio->clocked_to[0] = 0u;
  sio->clocked_to[1] = 0u;
  /* Refuses rather than rounds, which is `ap_clock_init`'s rule and the reason
   * `AP_TIME_BASE_HZ` was tripled to admit this rate at all: a refresh clock a
   * fraction out per pulse would drift a machine's dynamic memory against its
   * processor by an amount nobody could see. */
  return ap_clock_init(&sio->x1[0], AP_SIO_X1_HZ) &&
         ap_clock_init(&sio->x1[1], AP_SIO_X1_HZ);
}

bool ap_sio_decode(uint32_t address, unsigned *unit, unsigned *reg) {
  uint32_t base = address & ~(AP_SIO_RANGE - 1u);
  if (base != AP_SIO1_ADDR && base != AP_SIO2_ADDR) {
    return false;
  }
  *unit = base == AP_SIO2_ADDR ? 1u : 0u;
  /* Stride 2: both bytes of a word select the same register, which is why the
   * measured dump reads every value twice. */
  *reg = ((address - base) >> 1) & (AP_MC68681_REGISTERS - 1u);
  return true;
}

uint8_t ap_sio_read(ap_sio_t *sio, uint32_t address) {
  unsigned unit;
  unsigned reg;
  if (!ap_sio_decode(address, &unit, &reg)) {
    return 0u;
  }
  sio->register_reads[unit][reg]++;
  return ap_mc68681_read(&sio->port[unit], reg);
}

void ap_sio_write(ap_sio_t *sio, uint32_t address, uint8_t value) {
  unsigned unit;
  unsigned reg;
  if (!ap_sio_decode(address, &unit, &reg)) {
    return;
  }
  sio->register_writes[unit][reg]++;
  ap_mc68681_write(&sio->port[unit], reg, value);
}

bool ap_sio_irq(const ap_sio_t *sio) {
  return ap_mc68681_irq(&sio->port[0]) || ap_mc68681_irq(&sio->port[1]);
}

void ap_sio_receive(ap_sio_t *sio, unsigned unit, unsigned channel,
                    uint8_t byte) {
  if (unit >= 2u) {
    return;
  }
  ap_mc68681_receive(&sio->port[unit], channel, byte);
}

bool ap_sio_receiver_ready(ap_sio_t *sio, unsigned unit,
                           unsigned channel) {
  if (unit >= 2u) {
    return false;
  }
  /* Read the status register the same way the program does, rather than
   * reaching into the receive FIFO: the bit the firmware polls is the bit that
   * decides, and a helper that consulted different state could disagree with
   * the machine about whether a byte is waiting. */
  const unsigned status = (channel == 0u) ? AP_MC68681_SR_CSR_A
                                          : AP_MC68681_SR_CSR_B;
  return (ap_mc68681_read(&sio->port[unit], status) & AP_MC68681_SR_RXRDY) !=
         0u;
}

bool ap_sio_transmit(ap_sio_t *sio, unsigned unit, unsigned channel,
                     uint8_t *byte) {
  if (unit >= 2u) {
    return false;
  }
  return ap_mc68681_transmit(&sio->port[unit], channel, byte);
}

void ap_sio_receive_at(ap_sio_t *sio, unsigned unit, unsigned channel,
                       uint8_t byte, uint8_t sender_csr) {
  if (unit >= 2u) {
    return;
  }
  ap_mc68681_receive_at(&sio->port[unit], channel, byte, sender_csr);
}

void ap_sio_receive_framed(ap_sio_t *sio, unsigned unit, unsigned channel,
                           uint8_t byte, uint8_t sender_csr,
                           uint8_t sender_mr1) {
  if (unit >= 2u) {
    return;
  }
  ap_mc68681_receive_framed(&sio->port[unit], channel, byte, sender_csr,
                            sender_mr1);
}

uint8_t ap_sio_clock_select(ap_sio_t *sio, unsigned unit, unsigned channel) {
  if (unit >= 2u) {
    return 0u;
  }
  const unsigned reg =
      (channel == 0u) ? AP_MC68681_SR_CSR_A : AP_MC68681_SR_CSR_B;
  /* The register reads as *status*; the clock select is write-only, so the
   * channel's stored value is the only source. */
  (void)reg;
  return sio->port[unit].channel[channel].csr;
}

void ap_sio_advance(ap_sio_t *sio, ap_time_t now) {
  for (unsigned unit = 0; unit < 2u; unit++) {
    if (sio->x1[unit].period == 0u || now <= sio->clocked_to[unit]) {
      /* Monotonic: going backwards is ignored rather than wrapping, as every
       * other advance here is. */
      continue;
    }
    /* Whole pulses, with the remainder left behind in `clocked_to` -- so the
     * rate does not depend on how often this is called, which is the property
     * the whole tick loop rests on. */
    const uint64_t pulses =
        (now - sio->clocked_to[unit]) / sio->x1[unit].period;
    for (uint64_t i = 0; i < pulses; i++) {
      ap_mc68681_clock(&sio->port[unit]);
    }
    sio->clocked_to[unit] += pulses * sio->x1[unit].period;
  }

  /* **OP3 is wired back to IP0**, and that loopback is the whole reason the
   * refresh square wave is observable to a program.
   *
   * §3.9 puts the refresh on serial 1's OP3, and the board returns it to the
   * same part's IP0 -- the oracle's `sio_output` does exactly this, with the
   * comment that says why: "The counter/timer on the SIO chip is used for the
   * RAM refresh count ... to produce a square wave output on output OP3. The
   * period of the output is 15 microseconds."
   *
   * The boot PROM reads it: it programs the timer, routes it to OP3, starts the
   * counter and then polls `IPCR` for a **change on IP0**, counting five whole
   * cycles. Without the loopback that poll never ends, which is where every
   * normal-mode boot in this project stopped -- 9,982,874 reads of one
   * register. `FINDINGS.md` C116.
   *
   * Driven here rather than at the write to the output port, because the level
   * is the *timer's* and changes with time, not with anything a program does. */
  const uint8_t ip0 = ap_sio_refresh_output(sio) ? 0x01u : 0x00u;
  ap_mc68681_t *first = &sio->port[AP_SIO_RAM_CONFIG_UNIT];
  ap_mc68681_set_input(first, (uint8_t)((first->input & 0xFEu) | ip0));
}

bool ap_sio_diagnostic_interrupt(const ap_sio_t *sio) {
  const ap_mc68681_t *first = &sio->port[0];
  if ((first->opcr & AP_SIO_OPCR_OP7_IS_TXRDYB) != 0u) {
    /* `[MC68681]` §4.2.11.1: OP7 provides "either the complement of OPR[7] or
     * the channel B transmitter interrupt output, **which is the complement of
     * the channel B transmitter ready status bit**. When configured for the
     * channel B transmitter interrupt, OP7 acts as an open-collector output and
     * is **not masked by the contents of the interrupt mask register**."
     *
     * So the pin is still asserted low, as in the `OPR[7]` case below -- the
     * complement of a status bit rather than the complement of a register bit.
     * The line is up when channel B's transmitter is ready.
     *
     * This used to return false, on the grounds that nothing in any firmware
     * here selects the alternate source and a board asking for it should get no
     * interrupt "rather than a guess". But the datasheet says exactly what OP7
     * carries, so there was never a guess to avoid -- only an unread section.
     * The mask register is deliberately not consulted: the sentence above says
     * this source bypasses it. */
    return (first->channel[1].sr & AP_MC68681_SR_TXRDY) != 0u;
  }
  /* The pin is the *complement* of the bit, so the line is asserted when the
   * register bit is clear -- which is what makes the diagnostic's "reset output
   * port bits" command the one that raises the interrupt. */
  return (first->opr & AP_SIO_OPR_DIAGNOSTIC) == 0u;
}

bool ap_sio_refresh_output(const ap_sio_t *sio) {
  /* §3.9: serial 1's counter, "a square wave output on output OP3". */
  return sio->port[0].counter_output;
}

ap_time_t ap_sio_character_time(const ap_sio_t *sio, unsigned unit,
                                unsigned channel, unsigned baud) {
  if (unit >= 2u || channel >= 2u) {
    return 0u;
  }
  return ap_mc68681_character_time(sio->port[unit].channel[channel].mr[0],
                                   sio->port[unit].channel[channel].mr[1],
                                   baud);
}

unsigned ap_sio_character_bits(const ap_sio_t *sio, unsigned unit,
                               unsigned channel) {
  if (unit >= 2u || channel >= 2u) {
    return 0u;
  }
  return ap_mc68681_character_bits(sio->port[unit].channel[channel].mr[0]);
}

bool ap_sio_receiver_enabled(const ap_sio_t *sio, unsigned unit,
                             unsigned channel) {
  if (unit >= 2u || channel >= 2u) {
    return false;
  }
  return sio->port[unit].channel[channel].rx_enabled;
}

bool ap_sio_ram_config_byte(ap_model_id_t model, uint32_t ram_bytes,
                            uint8_t *out) {
  if (out == NULL) {
    return false;
  }
  /* A table, not an encoder. See the header: `20` is "8-8-8-8" on a DN3500 and
   * "2-2-2-2" on a DN3000, so the byte is not a per-bank size field and four
   * points do not determine a scheme. A pair not listed here is refused rather
   * than approximated -- a wrong byte is a machine that sizes memory it does
   * not have, and finds out by bus-erroring in the middle of its self-test. */
  static const struct {
    ap_model_id_t model;
    uint32_t megabytes;
    uint8_t byte;
  } table[] = {
      {AP_MODEL_DN3500, 8u, 0x64u},   /* 4-4-0-0 */
      {AP_MODEL_DN3500, 16u, 0x60u},  /* 4-4-4-4 */
      {AP_MODEL_DN3500, 32u, 0x20u},  /* 8-8-8-8 */
      {AP_MODEL_DN3000, 8u, 0x20u},   /* 2-2-2-2 */
      {AP_MODEL_DN5500, 16u, 0x14u},  /* 8-8-0-0 */
      {AP_MODEL_DN5500, 32u, 0x20u},  /* 8-8-8-8 */
  };
  const uint32_t megabytes = ram_bytes / (1024u * 1024u);
  for (unsigned i = 0; i < sizeof table / sizeof table[0]; i++) {
    if (table[i].model == model && table[i].megabytes == megabytes) {
      *out = table[i].byte;
      return true;
    }
  }
  return false;
}

void ap_sio_set_ram_config(ap_sio_t *sio, uint8_t config) {
  /* Seven pins, `IP0`-`IP6`; bit 7 is not an input the part has. */
  ap_mc68681_set_input(&sio->port[AP_SIO_RAM_CONFIG_UNIT],
                       (uint8_t)(config & 0x7Fu));
}

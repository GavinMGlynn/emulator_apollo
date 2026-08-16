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

ap_time_t ap_sio_interrupt_next_change(const ap_sio_t *sio) {
  ap_time_t next = AP_TIME_NEVER;
  for (unsigned unit = 0; unit < 2u; unit++) {
    if (sio->x1[unit].period == 0u) {
      continue; /* unclocked: only a write can move it */
    }
    /* **`ap_mc68681_clock`'s own guard, reused rather than restated.** A pulse
     * that arrives with the counter stopped and the timer mode off returns
     * immediately and changes nothing, so it cannot move `ISR` and must not
     * bound anything. Reusing the part's guard is what keeps the two from
     * drifting apart -- the same reason the disk's bound tests the phase
     * `ap_omti_advance` tests.
     *
     * This matters for more than tidiness. X1 is 3.6 MHz and the CPU 25, so a
     * pulse lands every ~6.9 CPU clocks: a bound of "the next pulse" is barely
     * one instruction ahead and would make the whole aggregate worthless. The
     * counter is idle for most of a boot, and then this part answers `never`. */
    const ap_mc68681_t *part = &sio->port[unit];
    if (!part->counter_running && !ap_mc68681_timer_mode(part)) {
      continue;
    }
    /* **Terminal count, not the next pulse.** A pulse only decrements; `ISR[3]`
     * moves when the countdown reaches zero, so at least `counter` pulses must
     * pass first -- and a counter already at zero fires on the very next one,
     * since `ap_mc68681_clock` tests after decrementing and a zero counter
     * skips the decrement.
     *
     * Conservative either way: the flag is set only on the *second* terminal
     * count, the first merely inverting the output, so this can be a whole
     * preload early. Early is a wasted sample; late would lose an interrupt.
     *
     * The distinction is the difference between the bound being useful and
     * being worthless on this machine. X1 is 3.6 MHz against a 25 MHz CPU, so
     * pulses land every ~6.9 CPU clocks -- less than an instruction -- and this
     * part's counter is never idle, because `§3.9`'s memory refresh runs off
     * it for the life of the machine. Bounding at the next pulse would have
     * pinned the whole aggregate one instruction ahead for ever. */
    const uint64_t pulses = part->counter > 0u ? part->counter : 1u;
    const ap_time_t at =
        sio->clocked_to[unit] + pulses * sio->x1[unit].period;
    if (at < next) {
      next = at;
    }
  }
  return next;
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

ap_time_t ap_sio_next_pulse(const ap_sio_t *sio) {
  ap_time_t next = AP_TIME_NEVER;
  for (unsigned unit = 0; unit < 2u; unit++) {
    if (sio->x1[unit].period == 0u) {
      continue;
    }
    const ap_time_t at = sio->clocked_to[unit] + sio->x1[unit].period;
    if (at < next) {
      next = at;
    }
  }
  return next;
}

void ap_sio_advance(ap_sio_t *sio, ap_time_t now) {
  /* Both parts carry the time, whether or not their counter/timer moved: the
   * output port's clock codes are free-running square waves and a caller may
   * read one at any instant. Set before the early-out below, which skips a part
   * whose counter has nothing to do. */
  for (unsigned unit = 0; unit < 2u; unit++) {
    if (now > sio->port[unit].now) {
      sio->port[unit].now = now;
    }
  }
  for (unsigned unit = 0; unit < 2u; unit++) {
    if (sio->x1[unit].period == 0u || now <= sio->clocked_to[unit]) {
      /* Monotonic: going backwards is ignored rather than wrapping, as every
       * other advance here is. */
      continue;
    }
    /* The divide is skipped when it can only yield zero -- under one period
     * elapsed means no pulses, an empty loop, and `clocked_to += 0`.
     *
     * A `continue` and **not** a return: the OP3-to-IP0 loopback below is this
     * function's other job and runs on every call regardless of whether the
     * counter ticked. The calendar's version of this guard was written as an
     * early return and silently disabled that part's periodic interrupt, which
     * two tests caught; the same shape here would have stopped the refresh
     * square wave the boot PROM polls. */
    const ap_time_t delta = now - sio->clocked_to[unit];
    if (delta < sio->x1[unit].period) {
      continue;
    }
    /* Whole pulses, with the remainder left behind in `clocked_to` -- so the
     * rate does not depend on how often this is called, which is the property
     * the whole tick loop rests on. */
    const uint64_t pulses = delta / sio->x1[unit].period;
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

unsigned ap_sio_transmit_baud(const ap_sio_t *sio, unsigned unit,
                              unsigned channel) {
  if (unit >= 2u || channel >= 2u) {
    return 0u;
  }
  /* §4.2.5.2: the transmitter's rate is the low nibble of the channel's
   * clock-select register; §4.2.5.1 and Table 4-5: `ACR[7]` chooses the set. */
  return ap_mc68681_baud(
      (uint8_t)(sio->port[unit].channel[channel].csr & 0x0Fu),
      (sio->port[unit].acr & 0x80u) != 0u);
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

unsigned ap_sio_receiver_flushed(const ap_sio_t *sio, unsigned unit,
                                 unsigned channel) {
  if (unit >= 2u || channel >= 2u) {
    return 0u;
  }
  return sio->port[unit].channel[channel].rx_flushed;
}

unsigned ap_sio_receiver_disabled_drops(const ap_sio_t *sio, unsigned unit,
                                        unsigned channel) {
  if (unit >= 2u || channel >= 2u) {
    return 0u;
  }
  return sio->port[unit].channel[channel].rx_disabled_drops;
}

unsigned ap_sio_receiver_reads(const ap_sio_t *sio, unsigned unit,
                               unsigned channel) {
  if (unit >= 2u || channel >= 2u) {
    return 0u;
  }
  return sio->register_reads[unit][channel == 0u ? AP_MC68681_RB_TB_A
                                                 : AP_MC68681_RB_TB_B];
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
  /* A table, not an encoder -- see the header. It is no longer four points from
   * the oracle: the Series 4000 firmware decodes this byte with an explicit
   * chain of `cmp.b #$xx,d0` against a per-value list of memory tops, and the
   * chain has been read out of both `3500_BOOT_12191_7` (`0077xx`-`0078xx`,
   * lists from `7972`) and `4500_BOOT_13167_02` (lists from `7992`). The two
   * are identical, fourteen values each, and the four the oracle knew fall out
   * of them unchanged -- including the bank layouts, which are the step sizes
   * of each list's progression. Still a table: fourteen points do not determine
   * a scheme either, and the *bytes* remain unexplained even though what each
   * one means is now measured.
   *
   * Where a size has two spellings the evener bank layout is taken, and `00` is
   * avoided for 20 MB because an unstrapped port reads zero -- a machine that
   * cannot say what it has must not be indistinguishable from one that says
   * twenty. A pair not listed is refused rather than approximated: a wrong byte
   * is a machine that sizes memory it does not have and finds out by
   * bus-erroring in the middle of its self-test. */
  static const struct {
    ap_model_id_t model;
    uint32_t megabytes;
    uint8_t byte;
  } table[] = {
      /* Series 4000 firmware, both revisions, and the DN3500 runs the same
       * chain. The unused spellings are `44` 8-0-0-0, `70` 8-4-0-0, `40`
       * 8-4-4-0, `00` 8-8-4-0 and `30` 8-8-8-0. */
      {AP_MODEL_DN3500, 4u, 0x54u},   /* 4-0-0-0 */
      {AP_MODEL_DN3500, 8u, 0x64u},   /* 4-4-0-0 */
      {AP_MODEL_DN3500, 12u, 0x50u},  /* 4-4-4-0 */
      {AP_MODEL_DN3500, 16u, 0x60u},  /* 4-4-4-4 */
      {AP_MODEL_DN3500, 20u, 0x10u},  /* 8-4-4-4 */
      {AP_MODEL_DN3500, 24u, 0x04u},  /* 8-8-4-4 */
      {AP_MODEL_DN3500, 28u, 0x24u},  /* 8-8-8-4 */
      {AP_MODEL_DN3500, 32u, 0x20u},  /* 8-8-8-8 */
      /* The DN3550 is a Series 3500 machine -- `[CFG]` p. D-77 opens "The
         Domain Series 3500 Model 3550" -- and runs the same fourteen-arm chain,
         so it strap for strap matches the rows above it. It needs its own rows
         because the lookup keys on the board, and a workstation is its own
         board. Without them it would go out unstrapped and the firmware would
         read the port's `00` as twenty megabytes, which is exactly how the
         DN4500 used to fail its memory self-test. */
      {AP_MODEL_DN3550, 4u, 0x54u},   /* 4-0-0-0 */
      {AP_MODEL_DN3550, 8u, 0x64u},   /* 4-4-0-0 */
      {AP_MODEL_DN3550, 12u, 0x50u},  /* 4-4-4-0 */
      {AP_MODEL_DN3550, 16u, 0x60u},  /* 4-4-4-4 */
      {AP_MODEL_DN3550, 20u, 0x10u},  /* 8-4-4-4 */
      {AP_MODEL_DN3550, 24u, 0x04u},  /* 8-8-4-4 */
      {AP_MODEL_DN3550, 28u, 0x24u},  /* 8-8-8-4 */
      {AP_MODEL_DN3550, 32u, 0x20u},  /* 8-8-8-8 */
      {AP_MODEL_DN4500, 4u, 0x54u},   /* 4-0-0-0 */
      {AP_MODEL_DN4500, 8u, 0x64u},   /* 4-4-0-0 */
      {AP_MODEL_DN4500, 12u, 0x50u},  /* 4-4-4-0 */
      {AP_MODEL_DN4500, 16u, 0x60u},  /* 4-4-4-4 */
      {AP_MODEL_DN4500, 20u, 0x10u},  /* 8-4-4-4 */
      {AP_MODEL_DN4500, 24u, 0x04u},  /* 8-8-4-4 */
      {AP_MODEL_DN4500, 28u, 0x24u},  /* 8-8-8-4 */
      {AP_MODEL_DN4500, 32u, 0x20u},  /* 8-8-8-8 */
      /* Series 3000: a different firmware whose chain has not been read, so
       * this stays the oracle's single point. `20` here is 2-2-2-2 and eight
       * megabytes, which is what makes the byte model-dependent. */
      {AP_MODEL_DN3000, 8u, 0x20u},   /* 2-2-2-2 */
      /* DN5500, confirmed against its own chain (lists from `8260`), which
       * carries the same fourteen and nineteen more above them. */
      {AP_MODEL_DN5500, 16u, 0x14u},  /* 8-8-0-0 */
      {AP_MODEL_DN5500, 32u, 0x20u},  /* 8-8-8-8 */
  };
  /* The strap is a property of the **board**, so a DSP variant is looked up as
   * the workstation it is built from. Keying this on the model left all four
   * unstrapped and failing their memory self-tests -- the same failure an
   * unlisted size gives, and for the same reason. The table says which board a
   * model is; this does not decide it here. */
  const ap_model_t *entry = ap_model_by_id(model);
  const ap_model_id_t board = entry != NULL ? entry->board_of : model;

  const uint32_t megabytes = ram_bytes / (1024u * 1024u);
  for (unsigned i = 0; i < sizeof table / sizeof table[0]; i++) {
    if (table[i].model == board && table[i].megabytes == megabytes) {
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

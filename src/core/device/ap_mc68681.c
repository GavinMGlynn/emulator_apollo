#include "device/ap_mc68681.h"

#include <string.h>

/* `[68681]` §4.2.7.2, "CHANNEL A MISCELLANEOUS COMMANDS - CRA[6:4]". */
#define CR_MISC 0x70u
#define CR_MISC_RESET_MR_POINTER 0x10u /* "0 0 1  Reset MR Pointer to MR1" */
#define CR_MISC_RESET_RECEIVER 0x20u   /* "0 1 0  Reset Receiver" */
#define CR_MISC_RESET_TRANSMITTER 0x30u
#define CR_MISC_RESET_ERROR 0x40u
/* §4.2.7.2's last three, which this file defined one of and acted on none.
 * Read from the datasheet after `[MC68681]` was fetched to
 * `docs/references/motorola/`; before that there was no manual on disk for the
 * part at all, and the four commands that *were* handled had been taken from
 * the same table by whoever wrote them. */
#define CR_MISC_RESET_BREAK 0x50u
#define CR_MISC_START_BREAK 0x60u
#define CR_MISC_STOP_BREAK 0x70u

/* CRA[3:0], the enable and disable commands. */
#define CR_ENABLE_RX 0x01u
#define CR_DISABLE_RX 0x02u
#define CR_ENABLE_TX 0x04u
#define CR_DISABLE_TX 0x08u

static void reset_channel(ap_mc68681_channel_t *ch) {
  memset(ch, 0, sizeof *ch);
  /* An idle transmitter is both ready and empty; a receiver with nothing in it
   * is neither ready nor full. Getting this wrong at reset makes a driver wait
   * for a transmitter that never announces itself. */
  ch->sr = AP_MC68681_SR_TXRDY | AP_MC68681_SR_TXEMT;
}

void ap_mc68681_reset(ap_mc68681_t *duart) {
  memset(duart, 0, sizeof *duart);
  for (unsigned i = 0; i < AP_MC68681_CHANNELS; i++) {
    reset_channel(&duart->channel[i]);
  }
}

bool ap_mc68681_timer_mode(const ap_mc68681_t *duart) {
  /* §4.2.13.2: ACR[6:4] values 0-3 are counter modes and 4-7 timer modes. */
  return (duart->acr & AP_MC68681_ACR_CT_MODE) >= 0x40u;
}

bool ap_mc68681_irq(const ap_mc68681_t *duart) {
  return (duart->isr & duart->imr) != 0u;
}

/* Keep the two per-channel interrupt bits in step with the channel's status.
 * They are not latched: the DUART derives them, so a driver that empties the
 * FIFO sees the bit go without writing anything. */
static void refresh_channel_interrupts(ap_mc68681_t *duart) {
  static const uint8_t txrdy[2] = {AP_MC68681_ISR_TXRDY_A,
                                   AP_MC68681_ISR_TXRDY_B};
  static const uint8_t rxrdy[2] = {AP_MC68681_ISR_RXRDY_A,
                                   AP_MC68681_ISR_RXRDY_B};
  for (unsigned i = 0; i < AP_MC68681_CHANNELS; i++) {
    const ap_mc68681_channel_t *ch = &duart->channel[i];
    if ((ch->sr & AP_MC68681_SR_TXRDY) != 0u) {
      duart->isr |= txrdy[i];
    } else {
      duart->isr = (uint8_t)(duart->isr & ~txrdy[i]);
    }
    if ((ch->sr & AP_MC68681_SR_RXRDY) != 0u) {
      duart->isr |= rxrdy[i];
    } else {
      duart->isr = (uint8_t)(duart->isr & ~rxrdy[i]);
    }
  }
}

/* The receiver's clock select is the upper nibble of CSR; the lower is the
 * transmitter's, and a sender's transmit rate is what this receiver must match.
 * Compared as the whole upper nibble rather than bit by bit: the codes are an
 * index into a baud-rate table, not a set of flags. */
static bool rate_matches(uint8_t receiver_csr, uint8_t sender_csr,
                         bool acr_set_two) {
  /* Our receive rate against the far end's **transmit** rate, which is what the
   * comment above has always said and what the code did not do: it compared
   * upper nibble against upper nibble, judging a sender by the rate it was
   * *listening* on. Invisible for every symmetric `CSR` -- `77`, `BB` -- which
   * is all this project had used.
   *
   * Compared as *rates* rather than as codes, because four of the sixteen codes
   * are not a fixed rate at all: `D` is the counter/timer and `E` and `F` the
   * external clock. This core does not know what those run at, so it cannot
   * claim a disagreement with them -- an unknown rate matches, which is a
   * refusal to invent an error rather than an assumption that the link is
   * good. */
  const unsigned receiving = ap_mc68681_baud((uint8_t)(receiver_csr >> 4),
                                             acr_set_two);
  const unsigned sending =
      ap_mc68681_baud((uint8_t)(sender_csr & 0x0Fu), acr_set_two);
  if (receiving == 0u || sending == 0u) {
    return true;
  }
  return receiving == sending;
}

void ap_mc68681_receive_framed(ap_mc68681_t *duart, unsigned channel,
                               uint8_t byte, uint8_t sender_csr,
                               uint8_t sender_mr1) {
  if (channel >= AP_MC68681_CHANNELS) {
    return;
  }
  ap_mc68681_channel_t *ch = &duart->channel[channel];
  const uint8_t own_mr1 = ch->mr[0];

  ap_mc68681_receive_at(duart, channel, byte, sender_csr);

  /* Enable *and* type together: two ports both using parity but disagreeing on
   * odd against even get a wrong bit on roughly half of all characters, which
   * is a link that works intermittently rather than one that never works. */
  const bool parity_agrees =
      ap_mc68681_parity_enabled(own_mr1) ==
          ap_mc68681_parity_enabled(sender_mr1) &&
      (own_mr1 & AP_MC68681_MR1_PARITY_TYPE_MASK) ==
          (sender_mr1 & AP_MC68681_MR1_PARITY_TYPE_MASK);

  /* Only when this channel uses parity at all: a receiver not looking for a
   * parity bit cannot find it wrong, however the sender was configured. */
  if (ch->rx_enabled && ap_mc68681_parity_enabled(own_mr1) && !parity_agrees) {
    ch->sr |= AP_MC68681_SR_PARITY;
  }
}

/* `[68681]`'s baud rate generator table. Zero for the codes that are not a
 * fixed rate: `D` is the counter/timer, `E` and `F` the external clock at
 * sixteen times and one times. The two ACR sets differ only at codes 0 and 3,
 * and this machine's firmware uses 4, 6, 7, 8, 9 and B, where they agree. */
unsigned ap_mc68681_baud(uint8_t csr_nibble, bool acr_set_two) {
  static const unsigned set_one[16] = {50,   110,  135,  200, 300, 600,
                                       1200, 1050, 2400, 4800, 7200, 9600,
                                       38400, 0,   0,    0};
  static const unsigned set_two[16] = {75,   110,  135,  150, 300, 600,
                                       1200, 1050, 2400, 4800, 7200, 9600,
                                       38400, 0,   0,    0};
  const unsigned code = csr_nibble & 0x0Fu;
  return acr_set_two ? set_two[code] : set_one[code];
}

uint8_t ap_mc68681_resample(uint8_t byte, unsigned bits, unsigned sender_baud,
                            unsigned receiver_baud) {
  if (bits == 0u || bits > 8u || sender_baud == 0u || receiver_baud == 0u ||
      sender_baud == receiver_baud) {
    /* Equal rates, or a rate that is not a rate -- an external clock or the
     * timer, where this model has nothing to say and must not invent a
     * corruption. The byte is what was sent. */
    return byte;
  }

  /* The sender's waveform, in units of its own bit time: index 0 is the start
   * bit, 1..bits the data least significant first, and everything at or past
   * `bits + 1` is stop or idle, which is high. */
  uint8_t received = 0u;
  for (unsigned i = 0; i < bits; i++) {
    /* The middle of where the *receiver* believes bit `i` sits, measured from
     * the start edge in receiver bit times, converted to sender bit times.
     * Scaled by two so the half is exact in integers. */
    const uint64_t position =
        ((uint64_t)(2u * i + 3u) * sender_baud) / (2u * receiver_baud);

    bool level;
    if (position == 0u) {
      level = false; /* still in the sender's start bit */
    } else if (position <= bits) {
      level = ((byte >> (unsigned)(position - 1u)) & 1u) != 0u;
    } else {
      level = true; /* stop bit, or the idle line beyond it */
    }
    if (level) {
      received |= (uint8_t)(1u << i);
    }
  }
  return received;
}

void ap_mc68681_receive_at(ap_mc68681_t *duart, unsigned channel, uint8_t byte,
                           uint8_t sender_csr) {
  if (channel >= AP_MC68681_CHANNELS) {
    return;
  }
  ap_mc68681_channel_t *ch = &duart->channel[channel];

  /* The character arrives with only as many bits as the link carries. A
   * receiver programmed for seven never sees an eighth: the bit is not
   * transmitted, so masking here is not truncation of a value but the absence
   * of a signal.
   *
   * This is why a seven-bit console shows `A` for both `41` and `C1`, and why a
   * driver that set `MR1` for seven and then sent eight-bit data gets a
   * silently altered stream rather than an error -- there is nothing for the
   * part to report, because nothing went wrong on the wire. */
  const unsigned bits = ap_mc68681_character_bits(ch->mr[0]);

  /* What the wrong rate actually does to the bits, before the width is applied.
   *
   * The receiver samples at the bit centres *its own* clock predicts, so at a
   * mismatched rate it reads the sender's waveform at the wrong instants and
   * gets a different byte. Modelling that as a flag beside an intact character
   * -- which this did -- is the difference between a UART and a note saying one
   * went wrong, and it is what left the boot PROM's autobaud unable to learn
   * anything: it compares the received byte against the shapes a carriage
   * return takes at five wrong rates, and an intact `0D` matches none of them.
   *
   * `ACR[7]` selects the baud set. The two published sets agree on every code
   * this machine's firmware uses. */
  const bool acr_set_two = (duart->acr & 0x80u) != 0u;
  const uint8_t resampled = ap_mc68681_resample(
      byte, bits,
      ap_mc68681_baud((uint8_t)(sender_csr & 0x0Fu), acr_set_two),
      ap_mc68681_baud((uint8_t)(ch->csr >> 4), acr_set_two));

  const uint8_t framed = (uint8_t)(resampled & ((1u << bits) - 1u));

  /* The two receive-side channel modes. Both retransmit what arrives; they
   * differ in whether the receiver also keeps it.
   *
   * Auto-echo passes the character on *and* delivers it -- a terminal sees its
   * own typing echoed by the part rather than by software. Remote loopback
   * retransmits and does **not** deliver: the channel is a mirror for someone
   * else's test, and a local program must not see traffic that was never
   * addressed to it. Delivering in both would make remote loopback
   * indistinguishable from auto-echo, which is the one thing separating them.
   *
   * Echoed through the transmit holding register rather than by calling the
   * write path, because the character is already framed and re-entering the
   * write path would frame it twice and consult the mode again. */
  const ap_mc68681_channel_mode_t mode = ap_mc68681_channel_mode(ch->mr[1]);
  if (mode == AP_MC68681_MODE_AUTO_ECHO ||
      mode == AP_MC68681_MODE_REMOTE_LOOPBACK) {
    ch->tx_holding = framed;
    ch->tx_holding_full = true;
    if (mode == AP_MC68681_MODE_REMOTE_LOOPBACK) {
      return;
    }
  }

  ap_mc68681_receive(duart, channel, framed);
  /* Set *after* delivery, and only if the byte was taken: a receiver that is
   * disabled or whose FIFO is full never sampled the character at all, so it
   * cannot have found its stop bit in the wrong place. */
  if (ch->rx_enabled && !rate_matches(ch->csr, sender_csr, acr_set_two)) {
    ch->sr |= AP_MC68681_SR_FRAMING;
  }
}

void ap_mc68681_receive(ap_mc68681_t *duart, unsigned channel, uint8_t byte) {
  if (channel >= AP_MC68681_CHANNELS) {
    return;
  }
  ap_mc68681_channel_t *ch = &duart->channel[channel];
  if (!ch->rx_enabled) {
    return;
  }
  if (ch->fifo_count >= AP_MC68681_RX_FIFO) {
    /* §4.2.9: the overrun bit, set when a character arrives with the FIFO
     * already full. The character is lost and the ones already held are not --
     * an overrun discards the newest, not the oldest, which is why a driver
     * that reads the FIFO after an overrun still gets valid earlier data. */
    ch->sr |= AP_MC68681_SR_OVERRUN;
    return;
  }
  ch->fifo[ch->fifo_count++] = byte;
  ch->sr |= AP_MC68681_SR_RXRDY;
  if (ch->fifo_count >= AP_MC68681_RX_FIFO) {
    ch->sr |= AP_MC68681_SR_FFULL;
  }
  refresh_channel_interrupts(duart);
}

bool ap_mc68681_transmit(ap_mc68681_t *duart, unsigned channel,
                         uint8_t *byte) {
  if (channel >= AP_MC68681_CHANNELS) {
    return false;
  }
  ap_mc68681_channel_t *ch = &duart->channel[channel];
  if (!ch->tx_holding_full) {
    return false;
  }
  *byte = ch->tx_holding;
  ch->tx_holding_full = false;
  ch->sr |= (uint8_t)(AP_MC68681_SR_TXRDY | AP_MC68681_SR_TXEMT);
  refresh_channel_interrupts(duart);
  return true;
}

void ap_mc68681_set_input(ap_mc68681_t *duart, uint8_t value) {
  uint8_t changed = (uint8_t)(duart->input ^ value);
  duart->input = value;
  if (changed != 0u) {
    /* §4.2.14: the input port change register records *which* pins changed in
     * its high nibble and their current state in the low one. */
    duart->ipcr |= (uint8_t)((changed & 0x0Fu) << 4);
    duart->isr |= AP_MC68681_ISR_INPUT;
  }
  duart->ipcr = (uint8_t)((duart->ipcr & 0xF0u) | (value & 0x0Fu));
}

void ap_mc68681_clock(ap_mc68681_t *duart) {
  if (!duart->counter_running && !ap_mc68681_timer_mode(duart)) {
    return;
  }
  /* §3: "Upon reaching $0000 (terminal count), the timer inverts its output,
   * reinitializes itself with the preload value, and repeats the countdown
   * sequence. After reaching terminal count this time, the timer sets the
   * counter/timer-ready bit in the interrupt status register (ISR[3])."
   *
   * So the square wave is half the interrupt rate: two terminal counts to a
   * period, which is what makes a square wave out of a countdown at all. The
   * output inverts every time, and the flag is set on the second.
   *
   * **"Upon reaching" is the clock that produces zero, not the one after it.**
   * This tested the counter *before* decrementing, which spent `preload + 1`
   * clocks on each half period instead of `preload` -- the well-known 68681
   * timer relation is output frequency = X1 / (2 x preload), and an extra clock
   * per half is 3.7% at the boot PROM's preload of 27 and 100% at a preload of
   * 1. Nothing noticed until `board/ap_sio.h` derived X1 from §3.9's stated
   * 15 microsecond period and the firmware's own preload: the two facts only
   * agree at `preload` clocks a half, and the model disagreed with both. */
  if (duart->counter > 0u) {
    duart->counter--;
  }
  if (duart->counter != 0u) {
    return;
  }
  duart->counter_output = !duart->counter_output;
  if (duart->counter_second_half) {
    duart->isr |= AP_MC68681_ISR_COUNTER;
  }
  duart->counter_second_half = !duart->counter_second_half;
  duart->counter = duart->preload;
}

uint8_t ap_mc68681_read(ap_mc68681_t *duart, unsigned reg) {
  reg &= (AP_MC68681_REGISTERS - 1u);
  unsigned index = reg >= 8u ? 1u : 0u;
  ap_mc68681_channel_t *ch = &duart->channel[index];

  switch ((ap_mc68681_reg_t)reg) {
  case AP_MC68681_MR_A:
  case AP_MC68681_MR_B: {
    uint8_t value = ch->mr[ch->mr_pointer ? 1u : 0u];
    /* The pointer advances to MR2 and stays there until a command resets it,
     * so a driver that reads MR twice gets two different registers. */
    ch->mr_pointer = true;
    return value;
  }
  case AP_MC68681_SR_CSR_A:
  case AP_MC68681_SR_CSR_B:
    return ch->sr;
  case AP_MC68681_CR_A:
  case AP_MC68681_CR_B:
    /* Table 4-1 marks this "Do Not Access": "Reading this location will result
     * in undesired effects and possible incorrect transmission or reception of
     * characters. Register contents may also be changed." The hardware's answer
     * is undefined, so this core returns zero and changes nothing -- the only
     * behaviour that cannot be wrong in a way that matters. */
    return 0u;
  case AP_MC68681_RB_TB_A:
  case AP_MC68681_RB_TB_B: {
    if (ch->fifo_count == 0u) {
      return 0u;
    }
    uint8_t value = ch->fifo[0];
    for (unsigned i = 1; i < ch->fifo_count; i++) {
      ch->fifo[i - 1u] = ch->fifo[i];
    }
    ch->fifo_count--;
    ch->sr = (uint8_t)(ch->sr & ~AP_MC68681_SR_FFULL);
    if (ch->fifo_count == 0u) {
      ch->sr = (uint8_t)(ch->sr & ~AP_MC68681_SR_RXRDY);
    }
    refresh_channel_interrupts(duart);
    return value;
  }
  case AP_MC68681_IPCR_ACR: {
    /* Cleared by being read -- measured in the oracle before it was read in the
     * manual: a dump of the real machine read `10` and then `00` from the two
     * bytes of the same register. `FINDINGS.md` C14. */
    uint8_t value = duart->ipcr;
    duart->ipcr = (uint8_t)(duart->ipcr & 0x0Fu);
    duart->isr = (uint8_t)(duart->isr & ~AP_MC68681_ISR_INPUT);
    return value;
  }
  case AP_MC68681_ISR_IMR:
    return duart->isr;
  case AP_MC68681_CUR_CTUR:
    return (uint8_t)(duart->counter >> 8);
  case AP_MC68681_CLR_CTLR:
    return (uint8_t)(duart->counter & 0xFFu);
  case AP_MC68681_IVR:
    return duart->ivr;
  case AP_MC68681_IP_OPCR:
    return duart->input;
  case AP_MC68681_START_OPR_SET:
    /* An address-triggered command, taken on a *read*. §3: "When a read at the
     * start counter command address is performed, the timer terminates the
     * current countdown sequence, inverts its output, reinitializes itself with
     * the preload value, and begins a new countdown sequence." */
    duart->counter_running = true;
    duart->counter_output = !duart->counter_output;
    /* The start command begins a fresh period, so the next terminal count is
     * the first half of it and does not raise the flag. */
    duart->counter_second_half = false;
    duart->counter = duart->preload;
    return 0u;
  case AP_MC68681_STOP_OPR_CLEAR:
    /* §3: in timer mode "the timer clears ISR[3] but does not stop", and in
     * counter mode "the counter stops the countdown sequence and clears
     * ISR[3]". One address, two behaviours, chosen by ACR. */
    duart->isr = (uint8_t)(duart->isr & ~AP_MC68681_ISR_COUNTER);
    if (!ap_mc68681_timer_mode(duart)) {
      duart->counter_running = false;
    }
    return 0u;
  }
  return 0u;
}

void ap_mc68681_write(ap_mc68681_t *duart, unsigned reg, uint8_t value) {
  reg &= (AP_MC68681_REGISTERS - 1u);
  unsigned index = reg >= 8u ? 1u : 0u;
  ap_mc68681_channel_t *ch = &duart->channel[index];

  switch ((ap_mc68681_reg_t)reg) {
  case AP_MC68681_MR_A:
  case AP_MC68681_MR_B:
    ch->mr[ch->mr_pointer ? 1u : 0u] = value;
    ch->mr_pointer = true;
    return;
  case AP_MC68681_SR_CSR_A:
  case AP_MC68681_SR_CSR_B:
    ch->csr = value;
    return;
  case AP_MC68681_CR_A:
  case AP_MC68681_CR_B:
    switch (value & CR_MISC) {
    case CR_MISC_RESET_MR_POINTER:
      ch->mr_pointer = false;
      break;
    case CR_MISC_RESET_RECEIVER:
      /* §4.2.7.2: "This command resets the channel A receiver. The receiver is
       * immediately disabled" -- and the FIFO goes with it. */
      ch->fifo_count = 0u;
      ch->rx_enabled = false;
      ch->sr = (uint8_t)(ch->sr &
                         ~(AP_MC68681_SR_RXRDY | AP_MC68681_SR_FFULL));
      break;
    case CR_MISC_RESET_TRANSMITTER:
      /* §4.2.7.2: "the transmitter is immediately disabled and the TxRDY and
       * TxEMT bits in the SRA are **cleared**."
       *
       * This file *set* them, which is the opposite, and it is the kind of
       * error that hides: a driver that resets the transmitter and then polls
       * TxRDY sees a transmitter ready to take a character from a transmitter
       * that has just been disabled. The three statements in this paragraph --
       * reset clears, enable asserts, disable resets -- are consistent only
       * when all three are implemented, and none of them was. */
      ch->tx_enabled = false;
      ch->tx_holding_full = false;
      ch->sr = (uint8_t)(ch->sr &
                         ~(AP_MC68681_SR_TXRDY | AP_MC68681_SR_TXEMT));
      break;
    case CR_MISC_RESET_ERROR:
      ch->sr = (uint8_t)(ch->sr &
                         ~(AP_MC68681_SR_OVERRUN | AP_MC68681_SR_PARITY |
                           AP_MC68681_SR_FRAMING | AP_MC68681_SR_BREAK));
      break;
    case CR_MISC_RESET_BREAK:
      /* §4.2.7.2: "This command causes the channel A break detect change bit
       * in the interrupt status register (ISR[2]) to be cleared to zero."
       * Channel B's is `ISR[6]`, per §4.2.8 -- "identical ... except that all
       * control actions apply to the channel B receiver and transmitter".
       *
       * A driver that enables the break-change interrupt and cannot clear it
       * is a driver that never leaves its interrupt handler, which is why a
       * silently ignored command here is worse than a refused one. */
      duart->isr = (uint8_t)(duart->isr &
                             ~(reg == AP_MC68681_CR_A ? AP_MC68681_ISR_BREAK_A
                                                      : AP_MC68681_ISR_BREAK_B));
      break;
    case CR_MISC_START_BREAK:
      /* §4.2.7.2: "forces the channel A transmitter serial-data output (TxDA)
       * low (spacing) ... **The transmitter must be enabled for this command to
       * be accepted.**" That condition is the observable part and is honoured.
       *
       * The break itself is state with no consumer: nothing in this machine
       * watches TxD at bit level, and the delays the datasheet gives -- up to
       * two bit times, or until the character in the holding register has gone
       * -- describe a serialiser this core does not have. Recorded rather than
       * dropped, so a driver's start/stop pair is answered by a controller that
       * knows it is in a break rather than by one that ignored both. */
      if (ch->tx_enabled) {
        ch->tx_break = true;
      }
      break;
    case CR_MISC_STOP_BREAK:
      /* §4.2.7.2: TxD "will go high (marking) within two bit times". No
       * enable condition is stated for this one, and none is imposed. */
      ch->tx_break = false;
      break;
    default:
      /* `000`, "No command." The only value left, and §4.2.7.2 gives it a
       * meaning rather than leaving it undefined -- so this arm is the command
       * being obeyed, not a command being dropped. */
      break;
    }
    /* The enable and disable bits are acted on after the miscellaneous command,
     * so a single write can reset the receiver and enable it again. Disable
     * wins over enable, which is what stops a driver writing `03` from getting
     * a receiver it did not ask for. */
    if ((value & CR_ENABLE_RX) != 0u) {
      ch->rx_enabled = true;
    }
    if ((value & CR_DISABLE_RX) != 0u) {
      ch->rx_enabled = false;
    }
    if ((value & CR_ENABLE_TX) != 0u) {
      /* §4.2.7.3: "Enable Transmitter ... The transmitter-ready status bit will
       * be asserted." */
      ch->tx_enabled = true;
      ch->sr |= AP_MC68681_SR_TXRDY;
    }
    if ((value & CR_DISABLE_TX) != 0u) {
      /* §4.2.7.3: "Disable Transmitter. This command terminates transmitter
       * operation and resets the transmitter-ready and transmitter-empty status
       * bits." */
      ch->tx_enabled = false;
      ch->sr = (uint8_t)(ch->sr &
                         ~(AP_MC68681_SR_TXRDY | AP_MC68681_SR_TXEMT));
    }
    refresh_channel_interrupts(duart);
    return;
  case AP_MC68681_RB_TB_A:
  case AP_MC68681_RB_TB_B:
    if (!ch->tx_enabled) {
      return;
    }
    if (ap_mc68681_channel_mode(ch->mr[1]) ==
        AP_MC68681_MODE_LOCAL_LOOPBACK) {
      /* "the transmitter output is internally connected to the receiver
       * input": the character never reaches the pin, so it must *not* also be
       * left in the transmit holding register for a caller to collect. A model
       * that did both would let a self-test pass while the outside world saw
       * traffic it should not.
       *
       * It arrives framed by this channel's own settings, which is the point of
       * the mode -- a self-test that bypassed framing would check the FIFO and
       * not the link. */
      const unsigned bits = ap_mc68681_character_bits(ch->mr[0]);
      ap_mc68681_receive(duart, index, (uint8_t)(value & ((1u << bits) - 1u)));
      return;
    }
    ch->tx_holding = value;
    ch->tx_holding_full = true;
    ch->sr = (uint8_t)(ch->sr &
                       ~(AP_MC68681_SR_TXRDY | AP_MC68681_SR_TXEMT));
    refresh_channel_interrupts(duart);
    return;
  case AP_MC68681_IPCR_ACR:
    duart->acr = value;
    return;
  case AP_MC68681_ISR_IMR:
    duart->imr = value;
    return;
  case AP_MC68681_CUR_CTUR:
    duart->preload = (uint16_t)((duart->preload & 0x00FFu) |
                                ((unsigned)value << 8));
    return;
  case AP_MC68681_CLR_CTLR:
    duart->preload = (uint16_t)((duart->preload & 0xFF00u) | value);
    return;
  case AP_MC68681_IVR:
    duart->ivr = value;
    return;
  case AP_MC68681_IP_OPCR:
    duart->opcr = value;
    return;
  case AP_MC68681_START_OPR_SET:
    /* Table 4-1: the write side of these two addresses is the output port's
     * bit-set and bit-reset commands -- so one address is a counter command
     * when read and an output port command when written. */
    duart->opr |= value;
    return;
  case AP_MC68681_STOP_OPR_CLEAR:
    duart->opr = (uint8_t)(duart->opr & ~value);
    return;
  }
}

unsigned ap_mc68681_character_bits(uint8_t mr1) {
  /* A count, not a table index: 00 is five bits and 11 is eight. */
  return 5u + (unsigned)(mr1 & AP_MC68681_MR1_BITS_MASK);
}

bool ap_mc68681_parity_enabled(uint8_t mr1) {
  /* Bit 2 clear means *with* parity. The inversion is the part worth a named
   * function rather than an inline test at each call site. */
  return (mr1 & AP_MC68681_MR1_PARITY_ENABLE) == 0u;
}

unsigned ap_mc68681_stop_code(uint8_t mr2) {
  return (unsigned)(mr2 & AP_MC68681_MR2_STOP_MASK);
}

ap_mc68681_channel_mode_t ap_mc68681_channel_mode(uint8_t mr2) {
  return (ap_mc68681_channel_mode_t)((mr2 >> 6) & 0x3u);
}

unsigned ap_mc68681_stop_sixteenths(uint8_t mr1, uint8_t mr2) {
  const unsigned code = ap_mc68681_stop_code(mr2);
  /* `[68681]` Table 4-5. Codes 8-15 run 1.563 to 2.000 in both columns, so
   * only the low half differs: 0.563-1.000 for a 6-8 bit character, and
   * 1.063-1.500 -- exactly half a bit more -- for a 5-bit one. */
  if (code >= 8u) {
    return 17u + code;
  }
  return (ap_mc68681_character_bits(mr1) == 5u ? 17u : 9u) + code;
}

ap_time_t ap_mc68681_character_time(uint8_t mr1, uint8_t mr2, unsigned baud) {
  if (baud == 0u) {
    /* The four clock-select codes that name no fixed rate. A character time
     * cannot be computed from them, and inventing one would pace a link at a
     * speed nothing chose. */
    return 0u;
  }
  /* In sixteenths of a bit: one start bit, the data bits, parity if enabled,
   * and the stop length. Everything is counted in sixteenths to the end so the
   * only division is the last one. */
  unsigned sixteenths = 16u;
  sixteenths += 16u * ap_mc68681_character_bits(mr1);
  if (ap_mc68681_parity_enabled(mr1)) {
    sixteenths += 16u;
  }
  sixteenths += ap_mc68681_stop_sixteenths(mr1, mr2);

  return ((ap_time_t)AP_TIME_BASE_HZ * sixteenths) / (16u * (ap_time_t)baud);
}

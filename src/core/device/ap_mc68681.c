#include "device/ap_mc68681.h"

#include <string.h>

/* `[68681]` §4.2.7.2, "CHANNEL A MISCELLANEOUS COMMANDS - CRA[6:4]". */
#define CR_MISC 0x70u
#define CR_MISC_RESET_MR_POINTER 0x10u /* "0 0 1  Reset MR Pointer to MR1" */
#define CR_MISC_RESET_RECEIVER 0x20u   /* "0 1 0  Reset Receiver" */
#define CR_MISC_RESET_TRANSMITTER 0x30u
#define CR_MISC_RESET_ERROR 0x40u
#define CR_MISC_RESET_BREAK 0x50u

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
static bool rate_matches(uint8_t receiver_csr, uint8_t sender_csr) {
  return (receiver_csr >> 4) == (sender_csr >> 4);
}

void ap_mc68681_receive_at(ap_mc68681_t *duart, unsigned channel, uint8_t byte,
                           uint8_t sender_csr) {
  if (channel >= AP_MC68681_CHANNELS) {
    return;
  }
  ap_mc68681_channel_t *ch = &duart->channel[channel];
  ap_mc68681_receive(duart, channel, byte);
  /* Set *after* delivery, and only if the byte was taken: a receiver that is
   * disabled or whose FIFO is full never sampled the character at all, so it
   * cannot have found its stop bit in the wrong place. */
  if (ch->rx_enabled && !rate_matches(ch->csr, sender_csr)) {
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
  if (duart->counter == 0u) {
    /* §3: "Upon reaching $0000 (terminal count), the timer inverts its output,
     * reinitializes itself with the preload value, and repeats the countdown
     * sequence. After reaching terminal count this time, the timer sets the
     * counter/timer-ready bit in the interrupt status register (ISR[3])."
     *
     * So the square wave is half the interrupt rate: two terminal counts to a
     * period, which is what makes a square wave out of a countdown at all. The
     * output inverts every time, and the flag is set on the second. */
    duart->counter_output = !duart->counter_output;
    if (duart->counter_second_half) {
      duart->isr |= AP_MC68681_ISR_COUNTER;
    }
    duart->counter_second_half = !duart->counter_second_half;
    duart->counter = duart->preload;
    return;
  }
  duart->counter--;
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
      ch->tx_enabled = false;
      ch->tx_holding_full = false;
      ch->sr |= (uint8_t)(AP_MC68681_SR_TXRDY | AP_MC68681_SR_TXEMT);
      break;
    case CR_MISC_RESET_ERROR:
      ch->sr = (uint8_t)(ch->sr &
                         ~(AP_MC68681_SR_OVERRUN | AP_MC68681_SR_PARITY |
                           AP_MC68681_SR_FRAMING | AP_MC68681_SR_BREAK));
      break;
    default:
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
      ch->tx_enabled = true;
    }
    if ((value & CR_DISABLE_TX) != 0u) {
      ch->tx_enabled = false;
    }
    refresh_channel_interrupts(duart);
    return;
  case AP_MC68681_RB_TB_A:
  case AP_MC68681_RB_TB_B:
    if (!ch->tx_enabled) {
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

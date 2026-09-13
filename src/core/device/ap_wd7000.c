#include "device/ap_wd7000.h"

#include <string.h>

/* The execution parameter block's defaults, Appendix A.8. Every one of the 26
 * bytes is printed there; the ones not named have a default of zero. */
static void ap_wd7000_default_parameters(ap_wd7000_t *asc) {
  memset(asc->parameters, 0, sizeof asc->parameters);
  for (unsigned i = 0; i < AP_WD7000_PARAM_SYNC_COUNT; ++i) {
    /* "(40 or C0)" for the even bytes and "(40)" for the odd: `40` is
     * asynchronous, and bit 7 is the flag the ASC sets once a rate has been
     * negotiated, so a reset block has it clear on both. */
    asc->parameters[AP_WD7000_PARAM_SYNC_FIRST + i] = AP_WD7000_SYNC_DEFAULT;
  }
  asc->parameters[AP_WD7000_PARAM_SBIC_CONTROL] = AP_WD7000_SBIC_CONTROL_DEFAULT;
  asc->parameters[AP_WD7000_PARAM_TIMEOUT] = AP_WD7000_TIMEOUT_DEFAULT;
  asc->parameters[AP_WD7000_PARAM_SOURCE_ID] = AP_WD7000_SOURCE_ID_DEFAULT;
  asc->parameters[AP_WD7000_PARAM_PARITY_RETRIES] =
      AP_WD7000_PARITY_RETRIES_DEFAULT;
}

/* Everything a reset clears, which is everything except the power-up key. */
static void ap_wd7000_clear(ap_wd7000_t *asc) {
  const bool powered_on = asc->powered_on;
  const ap_time_t now = asc->now;
  memset(asc, 0, sizeof *asc);
  asc->powered_on = powered_on;
  asc->now = now;
  ap_wd7000_default_parameters(asc);
  /* §5.2.1: the upper nibble clears, so READY is down and the port reads `0F`
   * until the diagnostics finish. §4.9.1: the LED is lit while they run and
   * turned off only if they pass. */
  asc->led = true;
  asc->diagnosing = true;
  asc->diagnose_at = asc->now + (powered_on ? AP_WD7000_T_SHORT_DIAGNOSTIC
                                            : AP_WD7000_T_LONG_DIAGNOSTIC);
  /* Table 5-3: `00` is the power-on condition, "no diagnostics executed", and
   * that is what the register holds until one has. */
  asc->int_status = AP_WD7000_DIAG_POWER_ON;
}

void ap_wd7000_power_on(ap_wd7000_t *asc) {
  memset(asc, 0, sizeof *asc);
  ap_wd7000_clear(asc);
}

void ap_wd7000_reset(ap_wd7000_t *asc) {
  ap_wd7000_clear(asc);
}

uint8_t ap_wd7000_status(const ap_wd7000_t *asc) {
  /* D3-D0 are undriven and read as ones whatever else is true; the upper
   * nibble is assembled from the four flags. */
  uint8_t status = AP_WD7000_ST_UNDRIVEN;
  if (asc->in_reset) {
    /* §5.2.1: "on reset the upper nibble clears", and §5.2.1.4 says D4 in
     * particular "is reset by host master reset (HMR or RESET DRV) or the ASC
     * reset port". Read as the *port* going quiet rather than as four flags
     * being cleared: the reset D-FF holds the LCPU, which then drives nothing,
     * and the flags behind it are what a release restores or discards. That
     * distinction is what lets a pulse narrower than §5.1.1's 25 us leave the
     * part as it was without leaving it half-cleared. */
    return status;
  }
  if (asc->interrupt) {
    status |= AP_WD7000_ST_INTERRUPT;
  }
  if (asc->ready) {
    status |= AP_WD7000_ST_READY;
  }
  if (asc->rejected) {
    status |= AP_WD7000_ST_REJECTED;
  }
  if (asc->initialized) {
    status |= AP_WD7000_ST_INITIALIZED;
  }
  return status;
}

bool ap_wd7000_irq(const ap_wd7000_t *asc) {
  /* §5.2.5.4: the line is tri-stated unless the enable bit is set, so a card
   * with interrupts disabled contributes nothing to a shared channel. The
   * status byte's image flag is *not* gated this way -- §5.2.1.1 says it is
   * there precisely so a host sharing an IRQ can poll it. */
  return asc->interrupt && (asc->control & AP_WD7000_CTL_IRQ_ENABLE) != 0u;
}

bool ap_wd7000_drq_driven(const ap_wd7000_t *asc) {
  return (asc->control & AP_WD7000_CTL_DMA_ENABLE) != 0u;
}

uint32_t ap_wd7000_ogmb_address(const ap_wd7000_t *asc, unsigned n) {
  if (!asc->initialized || n >= asc->ogmb_count) {
    return 0u;
  }
  return asc->mail_base + n * AP_WD7000_MAILBOX_BYTES;
}

uint32_t ap_wd7000_icmb_address(const ap_wd7000_t *asc, unsigned m) {
  if (!asc->initialized || m >= asc->icmb_count) {
    return 0u;
  }
  /* "ICMB Address = Starting address of mail block + mail box number m x 4 +
   * 4 (total number of OGMB's)" -- all the outgoing boxes first, then all the
   * incoming ones, and the counts are independent. */
  return asc->mail_base + m * AP_WD7000_MAILBOX_BYTES +
         AP_WD7000_MAILBOX_BYTES * asc->ogmb_count;
}

bool ap_wd7000_post_interrupt(ap_wd7000_t *asc, uint8_t status) {
  if (asc->queue_count >= AP_WD7000_IRQ_QUEUE) {
    return false;
  }
  const unsigned at = (asc->queue_head + asc->queue_count) % AP_WD7000_IRQ_QUEUE;
  asc->queue[at] = status;
  asc->queue_count += 1u;
  if (!asc->awaiting_ack) {
    /* §5.2.4: the next interrupt waits for the previous acknowledgement, so
     * only the head of the queue is ever visible. */
    asc->int_status = asc->queue[asc->queue_head];
    asc->interrupt = true;
    asc->awaiting_ack = true;
  }
  return true;
}

/* Retire the head of the interrupt queue and show the next, if any. */
static void ap_wd7000_acknowledge(ap_wd7000_t *asc) {
  if (!asc->awaiting_ack) {
    /* §5.2.1.1: the flag "stays asserted until an ASC reset or until the host
     * writes the port at address 1", so the strobe clears it even with nothing
     * queued. */
    asc->interrupt = false;
    return;
  }
  asc->queue_head = (asc->queue_head + 1u) % AP_WD7000_IRQ_QUEUE;
  asc->queue_count -= 1u;
  asc->awaiting_ack = false;
  asc->interrupt = false;
  if (asc->queue_count > 0u) {
    asc->int_status = asc->queue[asc->queue_head];
    asc->interrupt = true;
    asc->awaiting_ack = true;
  }
}

/* Accept or reject one byte of the command port, and start its 70 us. */
static void ap_wd7000_take(ap_wd7000_t *asc, bool accepted) {
  asc->rejected = !accepted;
  asc->ready = false;
  asc->busy = true;
  asc->ready_at = asc->now + AP_WD7000_T_COMMAND_PORT;
}

/* The ten bytes of Table 6-2, once the last has arrived. */
static void ap_wd7000_initialize(ap_wd7000_t *asc) {
  const uint8_t *p = asc->parameter;
  asc->scsi_id = p[1] & AP_WD7000_SCSI_ID_MASK;
  asc->bus_on = p[2];
  asc->bus_off = p[3];
  /* Bytes 05-07, MSB first. Table A-4 labels them LSB-to-MSB and Table 6-2,
   * §5.3's mailbox layout and Table A-8 all say MSB first; three against one,
   * and the mailbox pointers in the same structure are MSB-first, so a mail
   * block base written the other way round would be the only little-endian
   * field in the part. */
  asc->mail_base = ((uint32_t)p[5] << 16) | ((uint32_t)p[6] << 8) |
                   (uint32_t)p[7];
  /* "max. 64 (0,1 = 1)". */
  asc->ogmb_count = p[8] == 0u ? 1u : p[8];
  asc->icmb_count = p[9] == 0u ? 1u : p[9];
  asc->initialized = true;
}

/* Whether a command-port opcode is one the ASC accepts in its present state. */
static bool ap_wd7000_opcode_legal(const ap_wd7000_t *asc, uint8_t opcode) {
  if (opcode >= AP_WD7000_CMD_START_OGMB) {
    /* `80`-`FF`: start one mailbox or scan them all. Both need a configured
     * mail block, and §5.2.1.4 says that before initialization "any command
     * other than Initialization sets D5". */
    return asc->initialized;
  }
  switch (opcode) {
  case AP_WD7000_CMD_NOP:
    /* §6.1.1: it toggles the ready bit and nothing else, which is what makes
     * it a liveness check, so it is legal in every state. */
    return true;
  case AP_WD7000_CMD_INITIALIZE:
    /* §6.1.2: "The initialization sequence can only be initiated following an
     * ASC reset, after the READY status has been set by the ASC. At all other
     * times, the command will be rejected." */
    return !asc->initialized;
  case AP_WD7000_CMD_SCSI_SOFT_RESET:
  case AP_WD7000_CMD_SCSI_HARD_RESET_ACK:
    /* §6.1.7: in the pseudo idle loop the ASC accepts "only reset/abort
     * commands", and these are the two. */
    return asc->initialized;
  case AP_WD7000_CMD_DISABLE_UNSOLICITED:
  case AP_WD7000_CMD_ENABLE_UNSOLICITED:
  case AP_WD7000_CMD_INT_ON_FREE_OGMB:
    return asc->initialized && !asc->pseudo_idle;
  default:
    /* `07`-`7F` reserved. */
    return false;
  }
}

/* One byte written to the command port. */
static void ap_wd7000_command(ap_wd7000_t *asc, uint8_t value) {
  if (asc->sequence != AP_WD7000_SEQ_NONE) {
    /* A parameter byte of a sequence already under way. §6.1.2's own recovery
     * is that "a rejected byte may simply be resent", so a rejection does not
     * abandon the sequence. */
    asc->parameter[asc->taken] = value;
    asc->taken += 1u;
    const unsigned wanted = asc->sequence == AP_WD7000_SEQ_INITIALIZE
                                ? AP_WD7000_INIT_BYTES
                                : AP_WD7000_SOFT_RESET_BYTES;
    if (asc->taken >= wanted) {
      if (asc->sequence == AP_WD7000_SEQ_INITIALIZE) {
        ap_wd7000_initialize(asc);
      } else {
        /* §6.1.6: the abort or bus-device-reset message and its completion
         * belong to the bus, which this part does not have. **PROVISIONAL**:
         * the sequence is accepted and its parameter kept, and the ICMB the
         * real part would post is not, because there is no command on the bus
         * for it to be about. Named in `docs/PROJECT_STATUS.md`. */
        asc->scsi_reset = true;
      }
      asc->sequence = AP_WD7000_SEQ_NONE;
      asc->taken = 0u;
    }
    ap_wd7000_take(asc, true);
    return;
  }

  if (!ap_wd7000_opcode_legal(asc, value)) {
    ap_wd7000_take(asc, false);
    return;
  }

  switch (value) {
  case AP_WD7000_CMD_NOP:
    break;
  case AP_WD7000_CMD_INITIALIZE:
    asc->sequence = AP_WD7000_SEQ_INITIALIZE;
    asc->taken = 1u;
    asc->parameter[0] = value;
    break;
  case AP_WD7000_CMD_DISABLE_UNSOLICITED:
    /* §6.2.11.8: the same switch as parameter byte 22 bit 0. */
    asc->parameters[AP_WD7000_PARAM_USER_FLAGS] &=
        (uint8_t)~AP_WD7000_USER_FLAG_UNSOLICITED;
    break;
  case AP_WD7000_CMD_ENABLE_UNSOLICITED:
    asc->parameters[AP_WD7000_PARAM_USER_FLAGS] |=
        AP_WD7000_USER_FLAG_UNSOLICITED;
    break;
  case AP_WD7000_CMD_INT_ON_FREE_OGMB:
    /* §6.1.5 and parameter byte 22 bit 1 are the same function. */
    asc->interrupt_on_free_ogmb = true;
    asc->parameters[AP_WD7000_PARAM_USER_FLAGS] |= AP_WD7000_USER_FLAG_FREE_OGMB;
    break;
  case AP_WD7000_CMD_SCSI_SOFT_RESET:
    asc->sequence = AP_WD7000_SEQ_SOFT_RESET;
    asc->taken = 1u;
    asc->parameter[0] = value;
    break;
  case AP_WD7000_CMD_SCSI_HARD_RESET_ACK:
    /* §6.1.7: the first acknowledgement enters the pseudo idle loop and a
     * second releases it. */
    asc->pseudo_idle = !asc->pseudo_idle;
    break;
  default:
    /* `80`-`FF`. Starting a command block means reading it over the bus and
     * handing it to the SBIC. **PROVISIONAL**: accepted, because §5.2.1.3's
     * rejection is for an illegal opcode or a full queue and this is neither,
     * and then nothing happens -- there is no target. The mailbox the opcode
     * names is `value & AP_WD7000_INT_BOX_MASK` and the address is
     * `ap_wd7000_ogmb_address`. Named in `docs/PROJECT_STATUS.md`. */
    break;
  }
  ap_wd7000_take(asc, true);
}

/* A write to the host control register. */
static void ap_wd7000_control(ap_wd7000_t *asc, uint8_t value) {
  const uint8_t before = asc->control;
  asc->control = value;

  /* Bit 1, the SCSI port reset. §5.2.5.2: it resets the SBIC and, deliberately,
   * not the LCPU, so the host command queue survives. Recorded as a level. */
  asc->scsi_reset = (value & AP_WD7000_CTL_SCSI_RESET) != 0u;

  const bool held = (value & AP_WD7000_CTL_ASC_RESET) != 0u;
  const bool was_held = (before & AP_WD7000_CTL_ASC_RESET) != 0u;
  if (held && !was_held) {
    /* The rising edge. The instant is stamped at the next advance, because a
     * register write has no `now` of its own -- the same dating problem
     * `ap_sc499.c` solves the same way. */
    asc->in_reset = true;
    asc->hold_dating = true;
    asc->hold_dated = false;
    /* Held in reset the part drives nothing: §5.2.1 says the upper nibble
     * clears. */
    asc->ready = false;
    asc->rejected = false;
    asc->interrupt = false;
    asc->busy = false;
    return;
  }
  if (!held && was_held) {
    /* The falling edge. §5.1.1's table makes 25 us the minimum pulse width, so
     * a narrower one is not a reset and leaves the part as it was. */
    const bool long_enough =
        asc->hold_dated && asc->now - asc->held_since >= AP_WD7000_T_RESET_MIN;
    asc->in_reset = false;
    asc->hold_dating = false;
    asc->hold_dated = false;
    if (long_enough) {
      const uint8_t control = asc->control;
      ap_wd7000_reset(asc);
      /* "On power-up Reset or RESET DRV, all registers are cleared to all
       * zeros" is about the *host control* register on a hardware reset; a
       * reset the host drove through this register leaves what the host wrote,
       * which is the zero it just wrote to release the pulse. Kept explicitly
       * so the assignment is a decision rather than an accident of ordering. */
      asc->control = control & (uint8_t)~AP_WD7000_CTL_ASC_RESET;
    }
  }
}

uint8_t ap_wd7000_read(ap_wd7000_t *asc, unsigned reg) {
  switch (reg) {
  case AP_WD7000_STATUS_COMMAND:
    return ap_wd7000_status(asc);
  case AP_WD7000_INTSTAT_ACK:
    return asc->int_status;
  case AP_WD7000_CONTROL:
  case AP_WD7000_RESERVED:
  default:
    /* Table A-1 marks both reserved on read. Nothing drives them, which on
     * this bus is what every undriven address does. */
    return 0xFFu;
  }
}

void ap_wd7000_write(ap_wd7000_t *asc, unsigned reg, uint8_t value) {
  switch (reg) {
  case AP_WD7000_STATUS_COMMAND:
    /* §5.2.1.2: the write clears COMMAND PORT READY whatever else happens, and
     * a byte written while the port is busy is not a byte the ASC has seen. */
    if (asc->in_reset || asc->diagnosing || asc->busy) {
      return;
    }
    ap_wd7000_command(asc, value);
    return;
  case AP_WD7000_INTSTAT_ACK:
    /* A strobe: Table A-1 gives it no data bits. */
    ap_wd7000_acknowledge(asc);
    return;
  case AP_WD7000_CONTROL:
    ap_wd7000_control(asc, value);
    return;
  case AP_WD7000_RESERVED:
  default:
    return;
  }
}

void ap_wd7000_advance(ap_wd7000_t *asc, ap_time_t now) {
  if (now > asc->now) {
    asc->now = now;
  }

  if (asc->hold_dating) {
    asc->hold_dating = false;
    asc->hold_dated = true;
    asc->held_since = asc->now;
  }

  if (asc->in_reset) {
    return;
  }

  if (asc->diagnosing && asc->now >= asc->diagnose_at) {
    asc->diagnosing = false;
    /* §6.2.14.1: a pass turns the LED off and posts `01`; §5.1.1: no interrupt
     * is raised either way, so the host polls for READY and then reads this
     * register. This part has no failure to report, so the code is always the
     * pass -- a model of a working board, which is what every other device
     * here is. */
    asc->led = false;
    asc->int_status = AP_WD7000_DIAG_OK;
    asc->powered_on = true;
    asc->ready = true;
  }

  if (asc->busy && asc->now >= asc->ready_at) {
    asc->busy = false;
    asc->ready = true;
  }
}

/* ---- First-party DMA ------------------------------------------------------ */

void ap_wd7000_attach_memory(ap_wd7000_t *asc,
                             const ap_wd7000_memory_t *memory) {
  if (asc == nullptr) {
    return;
  }
  if (memory == nullptr || memory->read == nullptr ||
      memory->write == nullptr) {
    asc->memory_attached = false;
    return;
  }
  asc->memory = *memory;
  asc->memory_attached = true;
}

bool ap_wd7000_memory_attached(const ap_wd7000_t *asc) {
  return asc != nullptr && asc->memory_attached;
}

/* Whether a master cycle can start. `[WD7000]` §5.2.5.3: Host Control bit 2
 * gates DRQ, "so that several cards can share a channel", and a tri-stated DRQ
 * is a card that never asks for the bus. A part in reset cannot master either --
 * §5.2.5.1's falling edge is what starts the ASC, and before it there is no Z80
 * running to issue a cycle. */
static bool can_master(const ap_wd7000_t *asc) {
  return asc->memory_attached && !asc->in_reset &&
         (asc->control & AP_WD7000_CTL_DMA_ENABLE) != 0u;
}

uint8_t ap_wd7000_memory_read(ap_wd7000_t *asc, uint32_t address, bool *ok) {
  if (ok != nullptr) {
    *ok = false;
  }
  if (asc == nullptr || !can_master(asc)) {
    if (asc != nullptr) {
      asc->dma_refused++;
    }
    return 0u;
  }
  asc->dma_reads++;
  if (ok != nullptr) {
    *ok = true;
  }
  return asc->memory.read(asc->memory.context, address);
}

void ap_wd7000_memory_write(ap_wd7000_t *asc, uint32_t address, uint8_t value,
                            bool *ok) {
  if (ok != nullptr) {
    *ok = false;
  }
  if (asc == nullptr || !can_master(asc)) {
    if (asc != nullptr) {
      asc->dma_refused++;
    }
    return;
  }
  asc->dma_writes++;
  if (ok != nullptr) {
    *ok = true;
  }
  asc->memory.write(asc->memory.context, address, value);
}

uint32_t ap_wd7000_memory_read24(ap_wd7000_t *asc, uint32_t address,
                                 bool *ok) {
  uint32_t value = 0u;
  for (unsigned i = 0; i < 3u; i++) {
    bool one = false;
    const uint8_t byte = ap_wd7000_memory_read(asc, address + i, &one);
    if (!one) {
      if (ok != nullptr) {
        *ok = false;
      }
      return 0u;
    }
    /* MSB first: the byte at the lowest address is the most significant. */
    value = (value << 8) | byte;
  }
  if (ok != nullptr) {
    *ok = true;
  }
  return value;
}

void ap_wd7000_memory_write24(ap_wd7000_t *asc, uint32_t address,
                              uint32_t value, bool *ok) {
  for (unsigned i = 0; i < 3u; i++) {
    bool one = false;
    const uint8_t byte = (uint8_t)((value >> (8u * (2u - i))) & 0xFFu);
    ap_wd7000_memory_write(asc, address + i, byte, &one);
    if (!one) {
      if (ok != nullptr) {
        *ok = false;
      }
      return;
    }
  }
  if (ok != nullptr) {
    *ok = true;
  }
}

#include "device/ap_i8237.h"

#include <string.h>

void ap_i8237_reset(ap_i8237_t *dma) {
  memset(dma, 0, sizeof *dma);
  /* "The entire register is also set by a Reset. This disables all DMA requests
   * until a clear Mask register instruction allows them to occur." Four bits,
   * which is what the oracle's own controller was measured holding: register 15
   * reads `0F` out of reset. */
  dma->mask = 0x0Fu;
}

ap_i8237_mode_t ap_i8237_mode_of(const ap_i8237_t *dma, unsigned channel) {
  return (ap_i8237_mode_t)((dma->channel[channel].mode &
                            AP_I8237_MODE_SELECT) >> 6);
}

ap_i8237_transfer_t ap_i8237_transfer_of(const ap_i8237_t *dma,
                                         unsigned channel) {
  return (ap_i8237_transfer_t)((dma->channel[channel].mode &
                                AP_I8237_MODE_TRANSFER) >> 2);
}

int ap_i8237_service_pending(const ap_i8237_t *dma) {
  if ((dma->command & AP_I8237_CMD_CONTROLLER_DISABLE) != 0u) {
    return -1;
  }
  /* "Each channel has associated with it a mask bit which can be set to disable
   * the incoming DREQ." A software request goes through the same encoder --
   * "These are non-maskable and subject to prioritization by the Priority
   * Encoder network" -- so the mask applies to the pin and not to the request
   * register. */
  uint8_t asking = (uint8_t)((dma->dreq & (uint8_t)~dma->mask) | dma->request);
  asking &= 0x0Fu;
  if (asking == 0u) {
    return -1;
  }
  /* Fixed priority: "channel 0 has the highest priority". Rotating priority is
   * decoded but its rotation state is not kept, because nothing can rotate it
   * until a transfer completes -- and transfers await the arbiter. Reporting
   * fixed order under rotating priority would be a guess, so the bit is
   * honoured only in that it is stored and readable. */
  for (unsigned i = 0; i < AP_I8237_CHANNELS; i++) {
    if ((asking & (1u << i)) != 0u) {
      return (int)i;
    }
  }
  return -1;
}

void ap_i8237_set_request_pin(ap_i8237_t *dma, unsigned channel,
                              bool asserted) {
  if (channel >= AP_I8237_CHANNELS) {
    return;
  }
  uint8_t bit = (uint8_t)(1u << channel);
  if (asserted) {
    dma->dreq |= bit;
  } else {
    dma->dreq = (uint8_t)(dma->dreq & ~bit);
  }
}

/* ## Memory to memory: channel 0 reads, channel 1 writes, and only 1 counts
 *
 * `[8237]`, *Memory-to-Memory*: "Programming a bit in the Command register
 * selects **channels 0 and 1** to operate as memory-to-memory transfer
 * channels. The transfer is initiated by setting the **software DREQ for
 * channel 0** ... The channel 0 Current Address register is the source for the
 * address used and is decremented or incremented in the normal manner. The data
 * byte read from the memory is stored in the 8237A internal **Temporary
 * register**. Channel 1 then performs a four-state transfer of the data from
 * the Temporary register to memory using the address in its Current Address
 * register and incrementing or decrementing it in the normal manner. **The
 * channel 1 current Word Count is decremented.** When the word count of channel
 * 1 goes to FFFFH, a TC is generated causing an EOP output terminating the
 * service."
 *
 * So the count that ends the transfer is **channel 1's alone**, and channel 0's
 * is not touched. That reads oddly and the datasheet is explicit about it twice
 * -- the Autoinitialize paragraph says "In order to Autoinitialize both
 * channels in a memory-to-memory transfer, both word counts should be
 * programmed identically", which is advice that only makes sense if the
 * hardware does not decrement channel 0's for you.
 *
 * "Channel 0 may be programmed to retain the same address for all transfers.
 * This allows a single word to be written to a block of memory" -- the command
 * register's `CH0_ADDRESS_HOLD`, which is why the source advance is conditional
 * and the destination's is not.
 *
 * This module declined the whole feature until now, on the grounds that a
 * transfer needs a bus to arbitrate for. It does, and it has one: the board
 * runs `ap_i8237_transfer` from its bus tick. What it did not have was a
 * *reason* -- until the loaded diagnostic's `CPU (dma) Test #1` programmed a
 * block move from `1100000` to `1100800` and compared the halves. */
static ap_i8237_cycle_t memory_to_memory(ap_i8237_t *dma,
                                         const ap_i8237_bus_t *bus) {
  ap_i8237_cycle_t out = {0};

  /* Channel 0 asks, through the same encoder as any other request -- the
   * datasheet initiates this with "the software DREQ for channel 0", and a
   * masked pin request is still a request there. */
  if ((dma->command & AP_I8237_CMD_CONTROLLER_DISABLE) != 0u) {
    return out;
  }
  const uint8_t asking =
      (uint8_t)((dma->dreq & (uint8_t)~dma->mask) | dma->request);
  if ((asking & 0x01u) == 0u) {
    return out;
  }

  ap_i8237_channel_t *source = &dma->channel[0];
  ap_i8237_channel_t *destination = &dma->channel[1];
  const uint16_t from = source->current_address;
  const uint16_t to = destination->current_address;

  if (bus->memory_read != NULL) {
    /* Through the temporary register, which is where the datasheet puts it and
     * which software can read back at register 13. */
    dma->temporary = bus->memory_read(bus->context, from);
  }
  if (bus->memory_write != NULL) {
    bus->memory_write(bus->context, to, dma->temporary);
  }

  if ((dma->command & AP_I8237_CMD_CH0_ADDRESS_HOLD) == 0u) {
    source->current_address =
        (uint16_t)((source->mode & AP_I8237_MODE_DECREMENT) != 0u
                       ? from - 1u
                       : from + 1u);
  }
  destination->current_address =
      (uint16_t)((destination->mode & AP_I8237_MODE_DECREMENT) != 0u ? to - 1u
                                                                    : to + 1u);

  /* Channel 1's, and only channel 1's. */
  const bool expired = destination->current_count == 0u;
  destination->current_count = (uint16_t)(destination->current_count - 1u);

  out.ran = true;
  out.channel = 1u;
  out.address = to;
  out.wrote_memory = true;
  out.terminal_count = expired;
  if (expired) {
    ap_i8237_terminal_count(dma, 1u);
  }
  return out;
}

ap_i8237_cycle_t ap_i8237_transfer(ap_i8237_t *dma,
                                   const ap_i8237_bus_t *bus) {
  ap_i8237_cycle_t out = {0};

  if ((dma->command & AP_I8237_CMD_MEM_TO_MEM) != 0u) {
    return memory_to_memory(dma, bus);
  }

  const int pending = ap_i8237_service_pending(dma);
  if (pending < 0) {
    return out;
  }
  const unsigned channel = (unsigned)pending;

  /* A cascade channel transfers nothing: it exists to pass the request up, and
   * the bus it wins belongs to whatever asked through it. `board/ap_master.h`
   * is that route. */
  if (ap_i8237_mode_of(dma, channel) == AP_I8237_MODE_CASCADE) {
    return out;
  }

  const ap_i8237_transfer_t direction = ap_i8237_transfer_of(dma, channel);
  if (direction == AP_I8237_TRANSFER_ILLEGAL) {
    /* `[8237]` Figure 5 marks `11` "Illegal". Refused for the same reason the
     * all-mask register's read returns nothing invented: the datasheet defines
     * no behaviour, and this core does not supply one. */
    return out;
  }

  ap_i8237_channel_t *ch = &dma->channel[channel];
  const uint16_t address = ch->current_address;

  switch (direction) {
  case AP_I8237_TRANSFER_WRITE:
    /* "Write transfers move data from an I/O device to memory" -- named for
     * what the *memory* sees, which is the opposite of what the name suggests
     * to anyone reading it as the device's direction. */
    if (bus->device_read != NULL && bus->memory_write != NULL) {
      bus->memory_write(bus->context, address,
                        bus->device_read(bus->context, channel));
    }
    out.wrote_memory = true;
    break;
  case AP_I8237_TRANSFER_READ:
    if (bus->memory_read != NULL && bus->device_write != NULL) {
      bus->device_write(bus->context, channel,
                        bus->memory_read(bus->context, address));
    }
    break;
  case AP_I8237_TRANSFER_VERIFY:
    /* "Verify transfers are pseudo transfers. The 8237A operates as in Read or
     * Write transfers generating addresses, and responding to EOP, etc.
     * However, the memory and I/O control lines all remain inactive." So the
     * address and the count advance and no byte moves -- which is why this is a
     * case rather than an early return. */
    break;
  case AP_I8237_TRANSFER_ILLEGAL:
    break;
  }

  /* "The Current Address register ... is automatically incremented or
   * decremented after each transfer", the direction being the mode's bit 5. */
  if ((ch->mode & AP_I8237_MODE_DECREMENT) != 0u) {
    ch->current_address = (uint16_t)(ch->current_address - 1u);
  } else {
    ch->current_address = (uint16_t)(ch->current_address + 1u);
  }

  /* "The number of transfers is one more than the number programmed", and the
   * terminal count "occurs when the value in the register goes from zero to
   * FFFFH" -- so the decrement happens first and the borrow out of zero is the
   * end. A model that ended at zero would move one byte too few, every time. */
  const bool expired = ch->current_count == 0u;
  ch->current_count = (uint16_t)(ch->current_count - 1u);

  out.ran = true;
  out.channel = channel;
  out.address = address;
  out.terminal_count = expired;
  if (expired) {
    /* Autoinitialise reload, the status bit and the mask-on-TC rule are all
     * this one function's, already written and already tested. */
    ap_i8237_terminal_count(dma, channel);
  }
  return out;
}

void ap_i8237_terminal_count(ap_i8237_t *dma, unsigned channel) {
  if (channel >= AP_I8237_CHANNELS) {
    return;
  }
  uint8_t bit = (uint8_t)(1u << channel);

  /* "Bits 0-3 are set every time a TC is reached by that channel or an external
   * EOP is applied." */
  dma->status |= bit;

  ap_i8237_channel_t *ch = &dma->channel[channel];
  if ((ch->mode & AP_I8237_MODE_AUTOINIT) != 0u) {
    /* "It may also be reinitialized by an Autoinitialize back to its original
     * value. Autoinitialize takes place only after an EOP." */
    ch->current_address = ch->base_address;
    ch->current_count = ch->base_count;
  } else {
    /* "Each mask bit is set when its associated channel produces an EOP if the
     * channel is not programmed for Autoinitialize." So a one-shot transfer
     * disarms itself and an autoinitialising one does not -- the difference
     * between a channel that must be reprogrammed and one that free-runs. */
    dma->mask |= bit;
  }
  /* The software request is cleared by a terminal count as well: "Each register
   * bit is set or reset separately under software control or is cleared upon
   * generation of a TC or external EOP." */
  dma->request = (uint8_t)(dma->request & ~bit);
}

uint8_t ap_i8237_read(ap_i8237_t *dma, unsigned reg) {
  reg &= (AP_I8237_REGISTERS - 1u);

  if (reg < 8u) {
    unsigned channel = reg >> 1;
    uint16_t value = (reg & 1u) != 0u ? dma->channel[channel].current_count
                                      : dma->channel[channel].current_address;
    /* "subsequent accesses to register contents by the microprocessor will
     * address upper and lower bytes in the correct sequence" -- low first, and
     * the flip-flop toggles on every access. */
    uint8_t byte = dma->high_byte ? (uint8_t)(value >> 8)
                                  : (uint8_t)(value & 0xFFu);
    dma->high_byte = !dma->high_byte;
    return byte;
  }

  switch ((ap_i8237_reg_t)reg) {
  case AP_I8237_REG_STATUS_COMMAND: {
    /* "Bits 4-7 are set whenever their corresponding channel is requesting
     * service", so the request half is live rather than latched. */
    uint8_t asking = (uint8_t)((dma->dreq & (uint8_t)~dma->mask) |
                               dma->request);
    uint8_t value = (uint8_t)((dma->status & 0x0Fu) |
                              (uint8_t)((asking & 0x0Fu) << 4));
    /* "These bits are cleared upon Reset and on each Status Read." */
    dma->status = 0u;
    return value;
  }
  case AP_I8237_REG_TEMP_MASTERCLEAR:
    return dma->temporary;
  case AP_I8237_REG_REQUEST:
  case AP_I8237_REG_MASK_SINGLE:
  case AP_I8237_REG_MODE:
  case AP_I8237_REG_CLEAR_FLIPFLOP:
  case AP_I8237_REG_CLEAR_MASK:
  case AP_I8237_REG_MASK_ALL:
    /* `[8237]` Figure 6 marks every one of these "Illegal" to read. Answering
     * zero rather than inventing a value: the part drives nothing, and a caller
     * reading here has a bug this core should not paper over. */
    return 0u;
  }
  return 0u;
}

void ap_i8237_write(ap_i8237_t *dma, unsigned reg, uint8_t value) {
  reg &= (AP_I8237_REGISTERS - 1u);

  if (reg < 8u) {
    unsigned channel = reg >> 1;
    ap_i8237_channel_t *ch = &dma->channel[channel];
    uint16_t *base = (reg & 1u) != 0u ? &ch->base_count : &ch->base_address;
    uint16_t *current =
        (reg & 1u) != 0u ? &ch->current_count : &ch->current_address;

    if (dma->high_byte) {
      *base = (uint16_t)((*base & 0x00FFu) | ((unsigned)value << 8));
    } else {
      *base = (uint16_t)((*base & 0xFF00u) | value);
    }
    /* A write loads the base and the current register together; only an
     * autoinitialise reloads current from base afterwards. */
    *current = *base;
    dma->high_byte = !dma->high_byte;
    return;
  }

  switch ((ap_i8237_reg_t)reg) {
  case AP_I8237_REG_STATUS_COMMAND:
    dma->command = value;
    return;
  case AP_I8237_REG_REQUEST: {
    /* Figure 5: bits 1-0 select the channel, bit 2 sets or resets its bit. */
    uint8_t bit = (uint8_t)(1u << (value & 3u));
    if ((value & 0x04u) != 0u) {
      dma->request |= bit;
    } else {
      dma->request = (uint8_t)(dma->request & ~bit);
    }
    return;
  }
  case AP_I8237_REG_MASK_SINGLE: {
    uint8_t bit = (uint8_t)(1u << (value & 3u));
    if ((value & 0x04u) != 0u) {
      dma->mask |= bit;
    } else {
      dma->mask = (uint8_t)(dma->mask & ~bit);
    }
    return;
  }
  case AP_I8237_REG_MODE:
    /* The channel is named by bits 1-0 of the value written, not by the
     * address -- one address programs any of the four. */
    dma->channel[value & 3u].mode = value;
    return;
  case AP_I8237_REG_CLEAR_FLIPFLOP:
    /* "This initializes the flip-flop to a known state so that subsequent
     * accesses ... will address upper and lower bytes in the correct
     * sequence." */
    dma->high_byte = false;
    return;
  case AP_I8237_REG_TEMP_MASTERCLEAR:
    /* "This software instruction has the same effect as the hardware Reset." */
    ap_i8237_reset(dma);
    return;
  case AP_I8237_REG_CLEAR_MASK:
    /* "This command clears the mask bits of all four channels, enabling them to
     * accept DMA requests." */
    dma->mask = 0u;
    return;
  case AP_I8237_REG_MASK_ALL:
    /* Figure 5's second mask form: one bit per channel, in place. */
    dma->mask = (uint8_t)(value & 0x0Fu);
    return;
  }
}

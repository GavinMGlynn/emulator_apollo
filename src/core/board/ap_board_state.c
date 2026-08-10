/* Core board state hashing. See ap_board_state.h for what is covered, why the
 * diagnostic counters are not, and why main memory is hashed elsewhere. */

#include "board/ap_board_state.h"

/* Fed as one tagged byte rather than as whatever the compiler stores, so a
 * `bool` holding some value other than 0 or 1 cannot produce a hash the same
 * state would not. Same rule as the CPU's half. */
static void hash_bool(ap_hash_t *st, bool value) {
  ap_hash_u8(st, value ? 1u : 0u);
}

/* A clock is state, not configuration: the timer's three input rates and the
 * calendar's periodic rate are all programmable, and a machine whose timer is
 * running at a different rate is a different machine even before its counter
 * has moved. The period is derived from the rate but is fed as well, since a
 * clock that failed to initialise holds a zero period against a non-zero rate
 * and that difference is exactly what such a bug looks like. */
static void hash_clock(ap_hash_t *st, const ap_clock_t *clock) {
  ap_hash_u32(st, clock->hz);
  ap_hash_u64(st, clock->period);
}

void ap_board_hash_registers(ap_hash_t *st, const ap_boardreg_t *registers) {
  ap_hash_scope(st, "registers");
  ap_hash_u16(st, registers->cpu_status);
  ap_hash_u16(st, registers->cpu_control);
  ap_hash_u8(st, registers->cache_control);
  ap_hash_u16(st, registers->latch_page_on_parity);
}

void ap_board_hash_translation_map(ap_hash_t *st, const ap_atmap_t *map) {
  ap_hash_scope(st, "translation_map");
  /* All 128 entries, including the 64 an 8-bit transfer can never reach.
   * Software writes them and a later 16-bit transfer reads them, so an entry
   * out of an 8-bit transfer's range is still live state. */
  ap_hash_group_begin(st, "entries");
  for (unsigned i = 0; i < AP_ATMAP_ENTRIES; i++) {
    ap_hash_u16(st, map->entry[i]);
  }
  ap_hash_group_end(st);
}

static void hash_i8259(ap_hash_t *st, const ap_i8259_t *pic) {
  ap_hash_u8(st, pic->irr);
  ap_hash_u8(st, pic->isr);
  ap_hash_u8(st, pic->imr);

  /* The pins as driven, which in edge mode are not the same thing as the
   * request register: a level still high has no further edges to give, so two
   * controllers agreeing on IRR and differing on the pins behave differently on
   * the next transition. */
  ap_hash_u8(st, pic->pins);

  ap_hash_u8(st, pic->highest_priority);
  ap_hash_u8(st, pic->vector_base);
  ap_hash_u8(st, pic->cascade);

  hash_bool(st, pic->single);
  hash_bool(st, pic->level_triggered);
  hash_bool(st, pic->auto_eoi);
  hash_bool(st, pic->x86_mode);
  hash_bool(st, pic->buffered);
  hash_bool(st, pic->master);
  hash_bool(st, pic->special_fully_nested);
  hash_bool(st, pic->auto_rotate);
  hash_bool(st, pic->special_mask);
  hash_bool(st, pic->read_isr);
  hash_bool(st, pic->poll_pending);

  /* Where the initialization sequence has got to, and whether an ICW4 is still
   * expected. A part halfway through initialization is not the same part as one
   * that finished, and the difference is invisible in the registers above. */
  ap_hash_u8(st, (uint8_t)pic->init_state);
  hash_bool(st, pic->expect_icw4);

  hash_bool(st, pic->acknowledging);
  ap_hash_u8(st, pic->acknowledged_level);
}

void ap_board_hash_interrupts(ap_hash_t *st, const ap_intr_t *interrupts) {
  ap_hash_scope(st, "interrupts");
  /* Master then slave, in that order, so a machine with the two exchanged does
   * not hash the same as one without -- the same reason the CPU feeds its
   * instruction cache before its data cache. */
  hash_i8259(st, &interrupts->master);
  hash_i8259(st, &interrupts->slave);
}

static void hash_mc6840_timer(ap_hash_t *st, const ap_mc6840_timer_t *timer) {
  ap_hash_u16(st, timer->latch);
  ap_hash_u16(st, timer->counter);
  ap_hash_u8(st, timer->control);
  hash_bool(st, timer->interrupt_flag);
  hash_bool(st, timer->output);
  hash_bool(st, timer->gate);
  ap_hash_u8(st, timer->lsb_counter);
  hash_bool(st, timer->single_shot_fired);
  ap_hash_u8(st, timer->prescale_count);
}

void ap_board_hash_timer(ap_hash_t *st, const ap_timer_t *timer) {
  ap_hash_scope(st, "timer");
  for (unsigned i = 0; i < AP_MC6840_TIMERS; i++) {
    hash_mc6840_timer(st, &timer->ptm.timer[i]);
  }

  ap_hash_u8(st, timer->ptm.msb_buffer);
  ap_hash_u8(st, timer->ptm.lsb_buffer);
  ap_hash_u8(st, timer->ptm.control2);

  /* The status snapshot and whether it was taken. §3.11's clearing rule is
   * "read the status register, then read the timer causing the interrupt", so a
   * flag raised between the two survives -- and only this snapshot can tell the
   * two apart. A part that has had its status read is in a different state from
   * one that has not, with every register identical. */
  ap_hash_u8(st, timer->ptm.status_snapshot);
  hash_bool(st, timer->ptm.status_was_read);

  for (unsigned i = 0; i < AP_MC6840_TIMERS; i++) {
    hash_clock(st, &timer->clock[i]);
    /* How far each timer has been clocked. This is timing state in the strict
     * sense -- two machines whose counters agree but which have consumed
     * different amounts of time will diverge on the next advance. */
    ap_hash_time(st, timer->clocked_to[i]);
  }
}

void ap_board_hash_calendar(ap_hash_t *st, const ap_calendar_t *calendar) {
  ap_hash_scope(st, "calendar");
  const ap_mc146818_t *rtc = &calendar->rtc;

  ap_hash_bytes(st, rtc->ram, AP_MC146818_BYTES);

  /* The instant as plain numbers, which is how the part keeps it here: the
   * register format depends on DM and 24/12 and software may change either at
   * any moment, so the truth is stored apart from its presentation. */
  ap_hash_u32(st, (uint32_t)rtc->now.year);
  ap_hash_u32(st, (uint32_t)rtc->now.month);
  ap_hash_u32(st, (uint32_t)rtc->now.day);
  ap_hash_u32(st, (uint32_t)rtc->now.day_of_week);
  ap_hash_u32(st, (uint32_t)rtc->now.hour);
  ap_hash_u32(st, (uint32_t)rtc->now.minute);
  ap_hash_u32(st, (uint32_t)rtc->now.second);

  ap_hash_time(st, rtc->updated_to);
  hash_clock(st, &rtc->second_clock);

  /* The periodic interrupt runs at its own rate and must not be quantised to
   * the one-second update, so its cursor and clock are separate state. */
  ap_hash_time(st, rtc->periodic_to);
  hash_clock(st, &rtc->periodic_clock);
}

static void hash_i8237(ap_hash_t *st, const ap_i8237_t *dma) {
  for (unsigned i = 0; i < AP_I8237_CHANNELS; i++) {
    const ap_i8237_channel_t *channel = &dma->channel[i];
    /* Base and current both: autoinitialise reloads one from the other, so a
     * channel that has counted down is not the same as one that has not even
     * when the next transfer would restore it. */
    ap_hash_u16(st, channel->base_address);
    ap_hash_u16(st, channel->base_count);
    ap_hash_u16(st, channel->current_address);
    ap_hash_u16(st, channel->current_count);
    ap_hash_u8(st, channel->mode);
  }

  ap_hash_u8(st, dma->command);
  ap_hash_u8(st, dma->status);
  ap_hash_u8(st, dma->request);
  ap_hash_u8(st, dma->mask);
  ap_hash_u8(st, dma->temporary);

  /* One flip-flop for the whole part, and it is why an interrupted two-byte
   * sequence corrupts the *next* channel programmed. A machine halfway through
   * writing an address register is a real state and hashes differently. */
  hash_bool(st, dma->high_byte);

  ap_hash_u8(st, dma->dreq);
}

void ap_board_hash_dma(ap_hash_t *st, const ap_dma_t *dma) {
  ap_hash_scope(st, "dma");
  for (unsigned i = 0; i < 2u; i++) {
    hash_i8237(st, &dma->controller[i]);
  }
}

static void hash_mc68681_channel(ap_hash_t *st,
                                 const ap_mc68681_channel_t *channel) {
  ap_hash_u8(st, channel->mr[0]);
  ap_hash_u8(st, channel->mr[1]);
  /* Which mode register the next access reaches. Two ports with identical mode
   * registers and different pointers take the next write to different places. */
  hash_bool(st, channel->mr_pointer);

  ap_hash_u8(st, channel->csr);
  ap_hash_u8(st, channel->sr);

  /* The FIFO's occupancy and only the bytes in it. The part's FIFO holds three
   * characters and the entries past the count are whatever the last occupant
   * left, which no read can reach -- hashing them would make two ports that
   * behave identically disagree, the same false positive the CPU's caches had
   * to be fixed for. */
  ap_hash_u32(st, (uint32_t)channel->fifo_count);
  for (unsigned i = 0; i < channel->fifo_count && i < AP_MC68681_RX_FIFO; i++) {
    ap_hash_u8(st, channel->fifo[i]);
  }

  hash_bool(st, channel->rx_enabled);
  hash_bool(st, channel->tx_enabled);
  ap_hash_u8(st, channel->tx_holding);
  hash_bool(st, channel->tx_holding_full);
}

static void hash_mc68681(ap_hash_t *st, const ap_mc68681_t *duart) {
  for (unsigned i = 0; i < AP_MC68681_CHANNELS; i++) {
    hash_mc68681_channel(st, &duart->channel[i]);
  }

  ap_hash_u8(st, duart->acr);
  ap_hash_u8(st, duart->imr);
  ap_hash_u8(st, duart->isr);
  ap_hash_u8(st, duart->ivr);
  ap_hash_u8(st, duart->ipcr);
  ap_hash_u8(st, duart->opcr);
  ap_hash_u8(st, duart->opr);
  ap_hash_u8(st, duart->input);

  ap_hash_u16(st, duart->preload);
  ap_hash_u16(st, duart->counter);
  hash_bool(st, duart->counter_running);
  hash_bool(st, duart->counter_output);
  /* Which half of the square wave the next terminal count ends. Tracked
   * explicitly rather than inferred from the output, because the start command
   * inverts the output too -- so the phase is not recoverable from the pin. */
  hash_bool(st, duart->counter_second_half);
}

void ap_board_hash_sio(ap_hash_t *st, const ap_sio_t *sio) {
  ap_hash_scope(st, "sio");
  /* Both ports, in order. The per-register read and write tallies beside them
   * are instrumentation and are excluded -- see the header. */
  for (unsigned i = 0; i < 2u; i++) {
    hash_mc68681(st, &sio->port[i]);
  }
}

void ap_board_hash_node_id(ap_hash_t *st, const ap_nodeid_t *node_id) {
  ap_hash_scope(st, "node_id");
  /* A device whose whole purpose is to be unique per machine. Two nodes on one
   * ring that hashed alike would be the one thing this must never say. */
  ap_hash_u32(st, node_id->id);
}

void ap_board_hash_disk(ap_hash_t *st, const ap_disk_t *disk) {
  ap_hash_scope(st, "disk");
  const ap_omti_t *omti = &disk->controller;

  ap_hash_u16(st, omti->data);
  ap_hash_u8(st, omti->status);
  ap_hash_u8(st, omti->configuration);
  ap_hash_u8(st, omti->mask);

  /* The floppy half is entirely separate on this part, and is fed after the
   * fixed disk rather than interleaved with it. */
  ap_hash_u8(st, omti->dor);
  ap_hash_u8(st, omti->fdc_status);
  ap_hash_u8(st, omti->fdc_data);
  ap_hash_u8(st, omti->fdc_control);
  hash_bool(st, omti->disk_change);
}

void ap_board_hash_tape(ap_hash_t *st, const ap_tape_t *tape) {
  ap_hash_scope(st, "tape");
  const ap_sc499_t *controller = &tape->controller;
  ap_hash_u8(st, controller->control);
  ap_hash_u8(st, controller->data);
  hash_bool(st, controller->ready);
  hash_bool(st, controller->exception);
  hash_bool(st, controller->done);
  hash_bool(st, controller->direction);
  hash_bool(st, controller->dma_active);

  const ap_qic_t *drive = &tape->drive;
  /* The cartridge by its extent **and its digest**, which closes the
   * approximation this used to carry: two cartridges of exactly equal size
   * hashed alike until one of them was read. The digest is taken once when the
   * image is opened and kept current by a write, so a hundred megabytes of
   * read-only media costs one pass at load and nothing per hash.
   *
   * Whether an image is present at all is fed separately, since a drive with no
   * cartridge is not a drive with an empty one. */
  hash_bool(st, drive->image.data != NULL);
  ap_hash_u64(st, (uint64_t)drive->image.size);
  ap_hash_u64(st, drive->image.blocks);
  ap_hash_u64(st, drive->image.digest);

  hash_bool(st, drive->loaded);
  ap_hash_u8(st, (uint8_t)drive->cartridge);
  hash_bool(st, drive->selected);
  hash_bool(st, drive->soft_lock);
  hash_bool(st, drive->q24_format);
  ap_hash_u64(st, drive->position);
  hash_bool(st, drive->reading);

  /* The buffered block, which is the part of the tape a run *has* changed its
   * relationship with. Only when it holds something: an invalid buffer keeps
   * whatever the last block left, and no read can reach it. */
  hash_bool(st, tape->block_valid);
  ap_hash_u32(st, (uint32_t)tape->offset);
  if (tape->block_valid) {
    ap_hash_bytes(st, tape->block, AP_CT_BLOCK_SIZE);
  }
}

void ap_board_hash_graphics(ap_hash_t *st, const ap_graphics_t *graphics) {
  ap_hash_scope(st, "graphics");
  ap_hash_u8(st, (uint8_t)graphics->screen);

  /* The frame buffers in full. Nothing else covers them -- they hang off the
   * display controller rather than off the machine -- and they are the
   * machine's output, so a run that drew a different picture is a different
   * run. The extents are fed before the bytes, so a card with a larger memory
   * whose extra bytes are all zero does not hash as one with a smaller. */
  ap_hash_u32(st, graphics->colour_bytes);
  if (graphics->colour_memory != NULL) {
    ap_hash_bytes(st, graphics->colour_memory, graphics->colour_bytes);
  }
  ap_hash_u32(st, graphics->mono_bytes);
  if (graphics->mono_memory != NULL) {
    ap_hash_bytes(st, graphics->mono_memory, graphics->mono_bytes);
  }
}

/* One window of the ring controller. Both are hashed: `RING.md` finding 38
 * makes a unit the *pair*, and two machines that differ only in what the `a1`
 * window holds are two different machines. */
static void hash_ring_window(ap_hash_t *st, const ap_ring_ctl_window_t *w) {
  ap_hash_u8(st, w->id);
  ap_hash_u16(st, w->status);
  ap_hash_u16(st, w->slot_002);
  ap_hash_u16(st, w->slot_004);
  ap_hash_u16(st, w->pointer);
  ap_hash_u16(st, w->command_402);
  ap_hash_u16(st, w->command_404);
  ap_hash_u16(st, w->slot_406);
  /* The read-ahead latch decides what the *next* access to the data port
   * returns, so two cards holding the same buffer mid-read are in different
   * states -- the same reason the 8254's LSB/MSB cursors are here. */
  ap_hash_u16(st, w->read_ahead);
  for (unsigned t = 0; t < 2u; t++) {
    const ap_i8254_t *pit = t == 0u ? &w->timer_a : &w->timer_b;
    for (unsigned i = 0; i < AP_I8254_COUNTERS; i++) {
      const ap_i8254_counter_t *c = &pit->counter[i];
      /* Every field, not the visible ones: the LSB/MSB cursors and the latch
       * valid flags decide what the *next* access returns, so two parts holding
       * the same count mid-sequence are in different states. */
      ap_hash_u8(st, c->control);
      ap_hash_u16(st, c->counter);
      ap_hash_u16(st, c->latch);
      hash_bool(st, c->gate);
      hash_bool(st, c->out);
      hash_bool(st, c->null_count);
      hash_bool(st, c->counting);
      ap_hash_u16(st, c->count_latch);
      hash_bool(st, c->count_latched);
      ap_hash_u8(st, c->status_latch);
      hash_bool(st, c->status_latched);
      hash_bool(st, c->write_msb_next);
      hash_bool(st, c->read_msb_next);
    }
  }
}

void ap_board_hash_ring(ap_hash_t *st, const ap_ring_ctl_t *ring) {
  ap_hash_scope(st, "ring");
  /* Fitted-ness first, and hashed even though every field below it is zero
   * when the slot is empty: a machine with a card whose registers happen to
   * read zero is not a machine with no card, because the two answer the AT
   * window differently. */
  hash_bool(st, ring->present);
  hash_ring_window(st, &ring->a1);
  hash_ring_window(st, &ring->a2);
  /* The dual-ported RAM in full. It is the card's memory and the frames pass
   * through it, so a run that moved different bytes is a different run -- the
   * same argument the frame buffers get. */
  ap_hash_group_begin(st, "buffer");
  for (unsigned i = 0; i < AP_RING_CTL_BUFFER_WORDS; i++) {
    ap_hash_u16(st, ring->buffer[i]);
  }
  ap_hash_group_end(st);
}

void ap_board_hash_keyboard(ap_hash_t *st, const ap_kbd_t *keyboard) {
  ap_hash_scope(st, "keyboard");
  /* Which keys are down. A model that let a repeated press through would
   * desynchronise the firmware's own shift state, and this is the state that
   * stops it -- so two keyboards differing only in what is held are different
   * machines. */
  for (unsigned i = 0; i < AP_KBD_KEYS; i++) {
    hash_bool(st, keyboard->down[i]);
  }
}

void ap_board_hash(ap_hash_t *st, const ap_board_t *board) {
  ap_board_hash_registers(st, &board->registers);
  ap_board_hash_translation_map(st, &board->translation_map);
  ap_board_hash_interrupts(st, &board->interrupts);
  ap_board_hash_timer(st, &board->timer);
  ap_board_hash_calendar(st, &board->calendar);
  ap_board_hash_dma(st, &board->dma);
  ap_board_hash_sio(st, &board->sio);
  ap_board_hash_node_id(st, &board->node_id);
  ap_board_hash_disk(st, &board->disk);
  ap_board_hash_tape(st, &board->tape);
  ap_board_hash_graphics(st, &board->graphics);
  ap_board_hash_ring(st, &board->ring);
  ap_board_hash_keyboard(st, &board->keyboard);
  /* The board's own fields, after the devices it owns. */
  ap_hash_scope(st, "board");
  ap_hash_u32(st, board->quirks.bits);

  /* Which appendix's AT bus cycle times this board keeps. Configuration rather
   * than something a program can change, and in the hash for the same reason
   * the CPU's clock rate is in the machine's: two boards that answer the same
   * bytes at different speeds are not the same board, and a fast mode that
   * reached the same state over a differently-timed bus is exactly what this
   * must catch. */
  ap_hash_u8(st, (uint8_t)board->at_bus_series);

  /* The boot PROM in full: which firmware is running is the largest single fact
   * about a boot, and the region is 64 Kbyte at most. Its absence is fed as a
   * marker rather than as nothing, because a machine with no PROM is a real
   * configuration and must not hash as one with a blank PROM -- the same rule
   * the CPU applies to an absent access context. */
  if (board->prom == NULL) {
    ap_hash_u8(st, 0xFFu);
  } else {
    ap_hash_u8(st, 0x01u);
    ap_hash_u32(st, board->prom_bytes);
    ap_hash_bytes(st, board->prom, board->prom_bytes);
  }

  /* Main memory's extent and not its contents: the board and the machine share
   * one buffer and `ap_machine_hash` has hashed it since before there was a
   * board. The extent still counts -- 8 Mbyte fitted is a different machine
   * from 16, however well the bytes in common agree. */
  ap_hash_u32(st, board->ram_bytes);
}

uint64_t ap_board_state_hash(const ap_board_t *board) {
  ap_hash_t st = ap_hash_begin();
  ap_board_hash(&st, board);
  return ap_hash_end(&st);
}

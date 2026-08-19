/* Core board state hashing. See ap_board_state.h for what is covered, why the
 * diagnostic counters are not, and why main memory is hashed elsewhere. */

#include "board/ap_board_state.h"

#include <stdio.h>

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
  /* **Both of Table 2-8's remaining registers, which were writable state
   * outside the hash.** They were added to the model as byte-wide storage and
   * never added here, so a divergence in either was invisible to the one
   * instrument this project checks every optimisation with -- and the firmware
   * does write one: the reference boot's report reads `master request 0/1`.
   * A hash that omits storage software can change is weaker than the guarantee
   * built on it.
   *
   * What stays out, and why it is not the same question: `memory_present` is
   * how much memory is *fitted*, `ds5500_cache_status` and
   * `active_low_parity_lanes` are model facts, and `interrupt_pending` is the
   * master controller's line refreshed from elsewhere each sample. None of
   * those is state a run can change, and hashing configuration would make two
   * identical runs of different machines differ for a reason the hash already
   * has better ways to say. */
  ap_hash_u8(st, registers->master_request);
  ap_hash_u8(st, registers->task_alias);
}

void ap_board_hash_translation_map(ap_hash_t *st, const ap_atmap_t *map) {
  ap_hash_scope(st, "translation_map");
  /* Every entry in the region, not the 128 a 16-bit transfer can reach: the
   * map is 1024 entries of storage and only some of them are ever indexed by a
   * transfer. Software writes them all -- the loaded diagnostic walks the whole
   * region -- so an entry out of any transfer's range is still live state.
   *
   * This said "all 128 entries" while looping over `AP_ATMAP_ENTRIES`, from
   * before storage and reach were separated in `ap_atmap.h`. The loop was
   * right; the sentence was describing a map that no longer existed. */
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

/* `name` identifies this channel in the dump. As with the CPU's caches, the
 * receive FIFO below is hashed **only up to its occupancy**, so the number of
 * lines it emits varies with what the port happens to hold and every field
 * after it in the scope would be renumbered by an ordinary character arriving.
 * Four channels share the `sio` scope, so the group names carry the port and
 * channel or the dump would have four lines with one key. */
static void hash_mc68681_channel(ap_hash_t *st,
                                 const ap_mc68681_channel_t *channel,
                                 const char *name) {
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
  ap_hash_group_begin(st, name);
  for (unsigned i = 0; i < channel->fifo_count && i < AP_MC68681_RX_FIFO; i++) {
    ap_hash_u8(st, channel->fifo[i]);
  }
  ap_hash_group_end(st);

  hash_bool(st, channel->rx_enabled);
  hash_bool(st, channel->tx_enabled);
  ap_hash_u8(st, channel->tx_holding);
  hash_bool(st, channel->tx_holding_full);
}

static void hash_mc68681(ap_hash_t *st, const ap_mc68681_t *duart,
                         unsigned port) {
  for (unsigned i = 0; i < AP_MC68681_CHANNELS; i++) {
    char name[24];
    snprintf(name, sizeof name, "p%u_ch%u_rxfifo", port, i);
    hash_mc68681_channel(st, &duart->channel[i], name);
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
    hash_mc68681(st, &sio->port[i], i);
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
  /* AT `3F7` written, which shared `3F6`'s byte until Table 4-3 was walked
   * against this model. Two registers, two fields, two hashed values. */
  ap_hash_u8(st, omti->fdc_rate);
  hash_bool(st, omti->disk_change);
  /* The command in flight, and when it lands. A controller waiting out a seek
   * is not a controller that has finished one, and two machines whose drives
   * complete at different instants raise `IRQ14` at different instants -- which
   * is exactly the difference that decides whether Domain/OS boots. Hashing the
   * phase alone would not separate them: the deadline is the state. */
  ap_hash_u8(st, (uint8_t)omti->phase);
  ap_hash_u64(st, omti->completion_at);

  /* ## The floppy half's own progress, which nothing here used to cover
   *
   * Everything above this line was the fixed disk's, plus the four floppy
   * *registers*. The floppy's command phase, its head positions and its
   * outstanding seeks were absent, so two machines that differed only in where
   * a floppy head stood -- or in whether a seek had landed -- hashed alike.
   * That was invisible while every floppy command completed in the instant it
   * was issued and no head position could differ; giving the drive its access
   * time from `008778-03` chapter 7 is what makes the difference observable,
   * and an identity harness that cannot see it is not an identity harness.
   *
   * The two 1024-byte sector buffers are deliberately still out, as the fixed
   * disk's is: their *indices* are here, so a half-drained transfer is
   * distinguished from a full one, and the contents come from an image both
   * machines share. */
  ap_hash_u8(st, (uint8_t)omti->fdc_phase);
  ap_hash_u64(st, omti->fdc_completion_at);
  ap_hash_u64(st, omti->fdc_seek_at[0]);
  ap_hash_u64(st, omti->fdc_seek_at[1]);
  ap_hash_u8(st, omti->fdc_cylinder[0]);
  ap_hash_u8(st, omti->fdc_cylinder[1]);
  hash_bool(st, omti->fdc_seek_done);
  ap_hash_u8(st, omti->fdc_seek_st0);
  for (unsigned i = 0; i < AP_OMTI_FDC_COMMAND_MAX; i++) {
    ap_hash_u8(st, omti->fdc_command[i]);
  }
  ap_hash_u32(st, (uint32_t)omti->fdc_command_length);
  ap_hash_u32(st, (uint32_t)omti->fdc_command_index);
  for (unsigned i = 0; i < AP_OMTI_FDC_RESULT_MAX; i++) {
    ap_hash_u8(st, omti->fdc_result[i]);
  }
  ap_hash_u32(st, (uint32_t)omti->fdc_result_length);
  ap_hash_u32(st, (uint32_t)omti->fdc_result_index);
  ap_hash_u32(st, (uint32_t)omti->fdc_buffer_index);
  ap_hash_u32(st, (uint32_t)omti->fdc_buffer_length);
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
  /* ## The drive's own latches and counters, which were not in this
   *
   * `reading` was hashed and `writing` was not, and neither were the two
   * conditions a driver reads out of the status block: `power_on`, which
   * survives until a status read reports it, and `illegal_command`, which does
   * the same. Two drives differing only in whether they have already
   * acknowledged their power-on reset are two different machines -- one will
   * send an initialisation sequence and the other will not.
   *
   * `status_pending` is the READ STATUS data phase, which is phase state like
   * `reading`. The two counters are the drive's own, reported in bytes 2-5 of
   * the status block, and so are machine state rather than this core watching
   * the machine -- the distinction the diagnostic counters elsewhere fall on
   * the other side of.
   *
   * Found the same way the display controller's hole was, one commit earlier:
   * by adding a latch and asking what would notice it. */
  hash_bool(st, drive->writing);
  hash_bool(st, drive->status_pending);
  hash_bool(st, drive->power_on);
  hash_bool(st, drive->illegal_command);
  ap_hash_u16(st, drive->data_errors);
  ap_hash_u16(st, drive->underruns);

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

  /* ## What the controller was programmed to, which the frame buffer does not
   * imply
   *
   * The buffers above were the whole of this, and they are only half the
   * picture -- literally: a frame buffer holds palette *indices*, so two runs
   * that drew identical bits through different palettes are different pictures
   * and used to hash alike. So did two controllers with the same memory and
   * different raster operations, plane selects or video-enable states.
   *
   * Found while adding the 4-plane board's lookup table from `002398-04`
   * p. 12-19: new state that nothing hashed would have been invisible, and
   * checking why led to the registers being invisible too.
   *
   * The programmed registers, then both palettes -- the 8-plane board's Bt458
   * and the 4-plane board's three-register table. `lut_control` and `lut_data`
   * are the ports' own latched state and are hashed with them; the FIFO is
   * hashed by its contents *and* its extent, so a committed palette and a
   * pending one are not confusable.
   *
   * Deliberately **not** hashed, for the reason the disk controller's counters
   * are not: `lut_fifo_overruns`, `lut_ad_accesses` and `diag_refresh_requests`
   * are this core watching the machine rather than state the machine has. */
  ap_hash_u8(st, graphics->reg.cr0);
  ap_hash_u8(st, graphics->reg.cr1);
  ap_hash_u8(st, graphics->reg.cr2);
  ap_hash_u8(st, graphics->reg.cr2b);
  ap_hash_u8(st, graphics->reg.cr3a);
  ap_hash_u8(st, graphics->reg.cr3b);
  ap_hash_u16(st, graphics->reg.write_enable);
  ap_hash_u32(st, graphics->reg.rop);

  ap_hash_u8(st, graphics->lut_control);
  ap_hash_u8(st, graphics->lut_data);
  ap_hash_u32(st, (uint32_t)graphics->lut_fifo_count);
  for (unsigned i = 0; i < graphics->lut_fifo_count; i++) {
    ap_hash_u8(st,
               graphics->lut_fifo[(graphics->lut_fifo_head + i) %
                                  AP_GRAPHICS_LUT_FIFO_BYTES]);
  }
  for (unsigned index = 0; index < 256u; index++) {
    uint8_t rgb[3] = {0u, 0u, 0u};
    (void)ap_bt458_palette(&graphics->lut, index, rgb);
    ap_hash_bytes(st, rgb, sizeof rgb);
  }
  for (unsigned gun = 0; gun < AP_GRAPHICS_LUT4_GUNS; gun++) {
    ap_hash_bytes(st, graphics->lut4[gun], AP_GRAPHICS_LUT4_ENTRIES);
  }
  ap_hash_u8(st, graphics->diag_channel);
  ap_hash_u8(st, graphics->diag_refresh_request);

  /* The blitter's carried state and the raster the diagnostic wound by hand.
   * Two controllers mid-blit are not the same machine, and the stepped
   * counters are where the boot PROM's display test left the beam. */
  for (unsigned i = 0; i < AP_GRAPHICS_MAX_PLANES; i++) {
    ap_hash_u32(st, graphics->guard_latch[i]);
  }
  ap_hash_u32(st, (uint32_t)graphics->blt_cycle);
  ap_hash_u32(st, (uint32_t)graphics->h_clock);
  ap_hash_u32(st, (uint32_t)graphics->v_clock);
  ap_hash_u32(st, (uint32_t)graphics->p_clock);
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

  /* ## And the rest of it, which nothing here used to cover
   *
   * The loop above was the whole hasher, so two keyboards differing in which
   * *code set* they were in, whether they were still in loopback, how far a
   * command had accumulated, or whether the beeper was sounding all hashed
   * alike. Every one of those is state a run changes, and three of them a host
   * command changes directly.
   *
   * `loopback` is the sharpest of them: a keyboard that has been taken out of
   * loopback answers commands where one still in it echoes them, which is a
   * different machine by any reading. It is also the field a `memset` would get
   * wrong, since the part powers up with it *set* -- see `ap_kbd_reset`.
   *
   * The repeat cursor is here for the reason the timer's and the calendar's
   * are: two keyboards holding the same key but due to repeat at different
   * instants diverge on the next advance.
   *
   * `caps_lock_led` joins them as of the `008778-03` chapter 12 walk, which is
   * what put a lamp in the model at all -- chapter 12's opening sentence has
   * this part "controls and reports the status of the CAPS LOCK LED", and a
   * reported status that the hash cannot see is a claim the identity harness
   * cannot check. */
  hash_bool(st, keyboard->loopback);
  ap_hash_u32(st, keyboard->rx_message);
  hash_bool(st, keyboard->keystate_mode);
  hash_bool(st, keyboard->caps_lock_led);
  ap_hash_u32(st, (uint32_t)keyboard->held);
  hash_bool(st, keyboard->repeating);
  ap_hash_time(st, keyboard->repeat_at);
  ap_hash_time(st, keyboard->beeper_until);
}

/* ## The nine that had no hasher
 *
 * See the header. Each of these holds state a run changes, and none of it was
 * in the identity hash. Diagnostic counters stay out, as they do above.
 */

void ap_board_hash_parity(ap_hash_t *st, const ap_parity_t *parity) {
  ap_hash_scope(st, "parity");
  /* The extent is configuration and the bits are state -- hashed the way main
   * memory is, for the same reason: a lent buffer whose *contents* a program
   * changes. Forcing bad parity is a documented control-register function, so
   * which bytes carry it is exactly what a divergence would show up in. */
  ap_hash_u32(st, parity->ram_bytes);
  ap_hash_u32(st, parity->bad_bytes);
  if (parity->bad != NULL) {
    ap_hash_u8(st, 0x01u);
    ap_hash_bytes(st, parity->bad, parity->bad_bytes);
  } else {
    ap_hash_u8(st, 0xFFu);
  }
  /* `forced_writes`, `unstorable_writes`, `errors` and `first_error_offset`
   * are the report's, not the machine's. */
}

void ap_board_hash_dma_page(ap_hash_t *st, const ap_dmapage_t *page) {
  ap_hash_scope(st, "dma_page");
  /* The DS3000's counterpart to the translation map: registers a program
   * writes, and every one of them is state. */
  for (unsigned i = 0; i < AP_DMAPAGE_REGISTERS; i++) {
    ap_hash_u8(st, page->page[i]);
  }
}

static void hash_cpu_arb(ap_hash_t *st, const ap_m68030_arb_t *arb) {
  ap_hash_u8(st, (uint8_t)arb->state);
  hash_bool(st, arb->br);
  hash_bool(st, arb->bgack);
  ap_hash_u8(st, arb->br_sync);
  ap_hash_u8(st, arb->bgack_sync);
  hash_bool(st, arb->r);
  hash_bool(st, arb->a);
  hash_bool(st, arb->bg);
  hash_bool(st, arb->three_state);
  ap_hash_u8(st, (uint8_t)arb->rmc);
}

void ap_board_hash_arbiter(ap_hash_t *st, const ap_arbiter_t *arbiter) {
  ap_hash_scope(st, "arbiter");
  /* Live bus arbitration: which channels are asking, which is selected, and
   * who holds the bus. It changes on the machine's own clock, which is what
   * makes its absence from the hash the most surprising of the nine. */
  hash_cpu_arb(st, &arbiter->cpu);
  ap_hash_u8(st, arbiter->request);
  ap_hash_u32(st, (uint32_t)arbiter->selected);
  ap_hash_u32(st, (uint32_t)arbiter->master);
}

void ap_board_hash_master(ap_hash_t *st, const ap_master_t *master) {
  ap_hash_scope(st, "master");
  /* `unit`, `channel` and `drq` say which device this master *is* -- fitting,
   * not state -- and are hashed for the same reason the AT bus series is: two
   * machines wired differently must not hash alike. The rest is the handshake
   * in flight. */
  ap_hash_u32(st, (uint32_t)master->unit);
  ap_hash_u32(st, (uint32_t)master->channel);
  ap_hash_u32(st, (uint32_t)master->drq);
  hash_bool(st, master->request);
  hash_bool(st, master->master_l);
  ap_hash_u8(st, (uint8_t)master->state);
}

void ap_board_hash_matrox(ap_hash_t *st, const ap_matrox_t *matrox) {
  ap_hash_scope(st, "matrox");
  /* The DN4500's graphics board. Its frame is a lent buffer the board's own
   * ROM draws into -- the item that landed it verified the picture by decoding
   * a PNG, which is exactly the state a hash should be able to speak for. */
  ap_hash_u32(st, (uint32_t)matrox->microcode_words);
  ap_hash_u32(st, matrox->last_transfer);
  hash_bool(st, matrox->transfer_armed);
  ap_hash_u16(st, matrox->data_latch);
  ap_hash_u32(st, matrox->frame_bytes);
  if (matrox->frame != NULL) {
    ap_hash_u8(st, 0x01u);
    ap_hash_bytes(st, matrox->frame, matrox->frame_bytes);
  } else {
    ap_hash_u8(st, 0xFFu);
  }
  /* `frame_writes` is a counter. */
}

void ap_board_hash_ethernet(ap_hash_t *st, const ap_3c505_t *card,
                            const ap_3c505_adapter_t *adapter) {
  ap_hash_scope(st, "ethernet");
  /* The **card**: the eight registers and the FIFO the host sees, which is all
   * of `ap_3c505_t`. */
  ap_hash_u8(st, card->hcr);
  ap_hash_u8(st, card->acr);
  ap_hash_u8(st, card->aux_dma);
  ap_hash_u8(st, card->to_adapter);
  hash_bool(st, card->to_adapter_full);
  ap_hash_u8(st, card->to_host);
  hash_bool(st, card->to_host_full);
  /* The FIFO to its count and no further: bytes past it are stale, and hashing
   * them would make two identical cards differ over what neither can read. */
  ap_hash_u32(st, card->fifo_count);
  for (unsigned i = 0; i < card->fifo_count && i < AP_3C505_DATA_FIFO; i++) {
    ap_hash_u8(st, card->fifo[i]);
  }
  hash_bool(st, card->dma_done);
  /* The jumper and the slot width are how the card is *fitted*, hashed for the
   * same reason `ap_master_t::unit` is. */
  hash_bool(st, card->test_jumper);
  hash_bool(st, card->sixteen_bit);
  ap_hash_u32(st, card->dma_since_pause);
  hash_bool(st, card->dma_pause_owed);
  hash_bool(st, card->adapter_initialising);

  ap_board_hash_ethernet_adapter(st, adapter);
}

static void hash_3c505_pcb(ap_hash_t *st, const ap_3c505_pcb_t *pcb) {
  ap_hash_u8(st, pcb->command);
  ap_hash_u8(st, pcb->length);
  /* To the declared length: bytes past it are whatever the last PCB left, and
   * two adapters agreeing on every readable byte must hash alike. */
  for (unsigned i = 0; i < pcb->length && i < AP_3C505_PCB_DATA_MAX; i++) {
    ap_hash_u8(st, pcb->data[i]);
  }
}

/* Whether a host callback is attached, and **never the pointer**. `ap_hash.h`
 * has no `ap_hash_ptr` so that a host address cannot make two identical
 * machines differ; a wire that is present and one that is not are genuinely
 * different machines, and that much is a boolean. */
static void hash_3c505_wire(ap_hash_t *st, const ap_3c505_wire_t *wire) {
  hash_bool(st, wire->transmit != NULL);
  hash_bool(st, wire->context != NULL);
}

void ap_board_hash_ethernet_adapter(ap_hash_t *st,
                                    const ap_3c505_adapter_t *adapter) {
  ap_hash_scope(st, "ethernet.adapter");

  /* The address PROM: which card this *is*, hashed for the same reason
   * `ap_master_t::unit` is -- two machines wired differently must not hash
   * alike. */
  for (unsigned i = 0; i < AP_3C505_ADDRESS_BYTES; i++) {
    ap_hash_u8(st, adapter->address[i]);
  }
  ap_hash_u16(st, adapter->receive_mode);
  ap_hash_u32(st, adapter->multicast_count);
  for (unsigned i = 0; i < adapter->multicast_count &&
                       i < AP_3C505_MULTICAST_MAX; i++) {
    for (unsigned b = 0; b < AP_3C505_ADDRESS_BYTES; b++) {
      ap_hash_u8(st, adapter->multicast[i][b]);
    }
  }

  /* **The statistics are state, not diagnostics**, and they are the exception
   * that shows where this file draws the line: a driver reads them back with a
   * `Network Statistics` command, so two adapters differing in them behave
   * differently. `parity`'s error tallies and the ring station's frame counters
   * stay out because nothing in the machine can read those -- they exist for
   * the run report. */
  ap_hash_u32(st, adapter->receive_packets);
  ap_hash_u32(st, adapter->transmit_packets);
  ap_hash_u16(st, adapter->crc_errors);
  ap_hash_u16(st, adapter->alignment_errors);
  ap_hash_u16(st, adapter->no_resource_errors);
  ap_hash_u16(st, adapter->overrun_errors);

  hash_3c505_wire(st, &adapter->wire);

  /* The PCB coming in from the host. `written` is a *total* and the write
   * position is that modulo the buffer, so every byte of the buffer can be
   * live and all of it is hashed -- unlike the frame buffers below, which have
   * a length that says where the live part ends. */
  ap_hash_u32(st, adapter->incoming.written);
  ap_hash_bytes(st, adapter->incoming.buffer, AP_3C505_PCB_MAX);

  /* And the one going out. */
  hash_3c505_pcb(st, &adapter->pending.pcb);
  ap_hash_u32(st, adapter->pending.sent);
  hash_bool(st, adapter->pending.active);
  hash_bool(st, adapter->pending_total);

  /* `09H`'s armed transmit. */
  hash_bool(st, adapter->transmitting);
  ap_hash_u32(st, adapter->transmit_length);
  ap_hash_u32(st, adapter->transmit_received);
  ap_hash_u16(st, adapter->transmit_offset);
  ap_hash_u16(st, adapter->transmit_segment);
  /* To what has been received, not the whole frame buffer: the tail is the
   * previous frame's. */
  for (unsigned i = 0; i < adapter->transmit_received &&
                       i < AP_3C505_FRAME_MAX; i++) {
    ap_hash_u8(st, adapter->transmit_buffer[i]);
  }

  /* `08H`'s armed receive, and the frame staged for upload. */
  hash_bool(st, adapter->receive_armed);
  ap_hash_u16(st, adapter->receive_offset);
  ap_hash_u16(st, adapter->receive_segment);
  ap_hash_u16(st, adapter->receive_buffer_length);
  ap_hash_u16(st, adapter->receive_timeout);
  ap_hash_u32(st, adapter->staged_length);
  ap_hash_u32(st, adapter->staged_read);
  for (unsigned i = 0; i < adapter->staged_length && i < AP_3C505_FRAME_MAX;
       i++) {
    ap_hash_u8(st, adapter->staged[i]);
  }
}

void ap_board_hash_ring_station(ap_hash_t *st, const ap_ring_station_t *s) {
  ap_hash_scope(st, "ring_station");
  /* The card, as against `ap_board_hash_ring`'s controller. Where it sits on
   * the cable, what it is transmitting, and how far into a passing frame its
   * receiver has got -- all of it changes on the ring's own bit clock. */
  ap_hash_u32(st, (uint32_t)s->slot);
  ap_hash_u32(st, s->address);
  hash_bool(st, s->receive_enabled);

  ap_hash_u32(st, (uint32_t)s->tx_capacity);
  ap_hash_u32(st, (uint32_t)s->tx_bit_count);
  ap_hash_u32(st, (uint32_t)s->tx_bit_pos);
  hash_bool(st, s->tx_armed);
  if (s->tx_bits != NULL) {
    ap_hash_u8(st, 0x01u);
    /* Only the bits the assembled frame occupies: the rest of the buffer is
     * whatever the last frame left, which two identical rings may differ in. */
    const size_t bytes = (s->tx_bit_count + 7u) / 8u;
    ap_hash_bytes(st, s->tx_bits, bytes < s->tx_capacity ? bytes
                                                         : s->tx_capacity);
  } else {
    ap_hash_u8(st, 0xFFu);
  }

  ap_hash_u32(st, s->rx_state);
  ap_hash_u32(st, s->rx_ones_run);
  ap_hash_u32(st, s->rx_bit_count);
  ap_hash_u8(st, s->rx_byte);
  for (unsigned i = 0; i < sizeof s->rx_header; i++) {
    ap_hash_u8(st, s->rx_header[i]);
  }
  ap_hash_u32(st, s->rx_header_len);
  hash_bool(st, s->rx_addressed);
  ap_hash_u32(st, (uint32_t)s->rx_capacity);
  ap_hash_u32(st, (uint32_t)s->rx_bytes);
  ap_hash_u32(st, (uint32_t)s->rx_header_bytes);
  hash_bool(st, s->rx_overrun);
  ap_hash_u32(st, s->rx_header_bits);
  hash_bool(st, s->rx_flipped_parity);
  ap_hash_u32(st, s->rx_separators);
  ap_hash_u32(st, s->rx_fcs_bits);
  ap_hash_u32(st, s->rx_late_bits);
  ap_hash_u8(st, s->rx_late);
  hash_bool(st, s->rx_frame_error);
  if (s->rx_buffer != NULL) {
    ap_hash_u8(st, 0x01u);
    ap_hash_bytes(st, s->rx_buffer,
                  s->rx_bytes < s->rx_capacity ? s->rx_bytes : s->rx_capacity);
  } else {
    ap_hash_u8(st, 0xFFu);
  }
  /* `frames_copied`, `frames_wacked` and `frames_seen` are counters -- the
   * two-node runner's report, not the machine's state. */
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
  ap_board_hash_parity(st, &board->parity);
  ap_board_hash_dma_page(st, &board->dma_page);
  ap_board_hash_arbiter(st, &board->arbiter);
  ap_board_hash_master(st, &board->master);
  ap_board_hash_matrox(st, &board->matrox);
  ap_board_hash_ethernet(st, &board->ethernet, &board->ethernet_adapter);
  ap_board_hash_ring_station(st, &board->ring_station);
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

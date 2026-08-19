/* Core board state hashing.
 *
 * The failure this module must not have is the same one `state_suite` guards
 * for the processor: a field that changes while the hash does not. The identity
 * harness would then report two diverging machines as the same machine, and
 * every optimisation checked under it would be checked against nothing. So the
 * bulk of this suite is a sweep -- perturb one field, demand the hash moves --
 * and a device added without a sweep entry is a gap visible in this file rather
 * than one nobody can see.
 *
 * The sweep is one assertion per *field*, not one per device. A device fed as a
 * whole struct would pass a per-device test while quietly omitting half its
 * members, which is exactly how a hash goes hollow.
 */

#include "board/ap_board_state.h"
#include <string.h>

#include "image/ap_ct.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Static rather than automatic: a board carries the translation map, the
 * calendar's RAM and a tape block buffer, and every test here builds two. */
static uint8_t ram[0x1000];
static ap_board_t scratch;
static ap_board_t other;

static const ap_mc146818_time_t epoch = {
    .year = 1987u, .month = 7u, .day = 31u, .day_of_week = 6u,
    .hour = 21u, .minute = 9u, .second = 21u,
};

static void make_board(ap_board_t *board) {
  TEST_ASSERT_TRUE(ap_board_init(board, ram, (uint32_t)sizeof ram, &epoch,
                                 0x012345u));
}

/* Perturb one field of a freshly built board and demand the hash moves. The
 * board is rebuilt for each, so one assertion cannot be carried by a change an
 * earlier one made. */
#define MOVES_THE_HASH(statement)                                              \
  do {                                                                         \
    make_board(&scratch);                                                      \
    const uint64_t before = ap_board_state_hash(&scratch);                     \
    statement;                                                                 \
    TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_board_state_hash(&scratch));       \
  } while (0)

/* The property everything else rests on, and the one that fails first if a host
 * pointer ever reaches the hash: two boards built the same way, at two
 * different addresses, must agree. */
static void test_two_identically_built_boards_hash_alike(void) {
  make_board(&scratch);
  make_board(&other);
  TEST_ASSERT_EQUAL_HEX64(ap_board_state_hash(&scratch),
                          ap_board_state_hash(&other));

  /* And hashing twice does not move it, which a hash carrying uninitialised
   * padding would fail even against itself. */
  TEST_ASSERT_EQUAL_HEX64(ap_board_state_hash(&scratch),
                          ap_board_state_hash(&scratch));
}

static void test_every_board_register_field_moves_the_hash(void) {
  MOVES_THE_HASH(scratch.registers.cpu_status ^= 0x0100u);
  MOVES_THE_HASH(scratch.registers.cpu_control ^= 0x0001u);
  MOVES_THE_HASH(scratch.registers.cache_control ^= 0x01u);
  MOVES_THE_HASH(scratch.registers.latch_page_on_parity ^= 0x8000u);
}

/* Every entry, including the 64 an 8-bit transfer can never reach: software
 * writes them and a later 16-bit transfer reads them, so an entry outside one
 * transfer width's range is still live state. A loop that hashed only the
 * reachable half would pass a first-and-last check. */
static void test_every_translation_map_entry_moves_the_hash(void) {
  for (unsigned i = 0; i < AP_ATMAP_ENTRIES; i++) {
    MOVES_THE_HASH(scratch.translation_map.entry[i] ^= 0xFFFFu);
  }
}

static void perturb_i8259(ap_i8259_t *pic, unsigned field) {
  switch (field) {
  case 0: pic->irr ^= 0x01u; break;
  case 1: pic->isr ^= 0x02u; break;
  case 2: pic->imr ^= 0x04u; break;
  case 3: pic->pins ^= 0x08u; break;
  case 4: pic->highest_priority ^= 0x03u; break;
  case 5: pic->vector_base ^= 0x08u; break;
  case 6: pic->cascade ^= 0x08u; break;
  case 7: pic->single = !pic->single; break;
  case 8: pic->level_triggered = !pic->level_triggered; break;
  case 9: pic->auto_eoi = !pic->auto_eoi; break;
  case 10: pic->x86_mode = !pic->x86_mode; break;
  case 11: pic->buffered = !pic->buffered; break;
  case 12: pic->master = !pic->master; break;
  case 13: pic->special_fully_nested = !pic->special_fully_nested; break;
  case 14: pic->auto_rotate = !pic->auto_rotate; break;
  case 15: pic->special_mask = !pic->special_mask; break;
  case 16: pic->read_isr = !pic->read_isr; break;
  case 17: pic->poll_pending = !pic->poll_pending; break;
  case 18: pic->init_state = AP_I8259_INIT_ICW4; break;
  case 19: pic->expect_icw4 = !pic->expect_icw4; break;
  case 20: pic->acknowledging = !pic->acknowledging; break;
  default: pic->acknowledged_level ^= 0x05u; break;
  }
}

#define I8259_FIELDS 22u

/* Both controllers separately: a hash feeding only the master would pass every
 * test written against the master alone, and the slave carries eight of the
 * machine's sixteen interrupt levels. */
static void test_every_interrupt_controller_field_moves_the_hash(void) {
  for (unsigned field = 0; field < I8259_FIELDS; field++) {
    MOVES_THE_HASH(perturb_i8259(&scratch.interrupts.master, field));
    MOVES_THE_HASH(perturb_i8259(&scratch.interrupts.slave, field));
  }
}

/* And the two are told apart. Feeding them in a fixed order is what makes a
 * machine with the pair exchanged hash differently from one without -- the same
 * rule the processor applies to its two caches. */
static void test_the_master_and_the_slave_are_told_apart(void) {
  make_board(&scratch);
  scratch.interrupts.master.imr = 0xF0u;
  scratch.interrupts.slave.imr = 0x0Fu;
  const uint64_t before = ap_board_state_hash(&scratch);

  scratch.interrupts.master.imr = 0x0Fu;
  scratch.interrupts.slave.imr = 0xF0u;
  TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_board_state_hash(&scratch));
}

static void test_every_timer_field_moves_the_hash(void) {
  for (unsigned i = 0; i < AP_MC6840_TIMERS; i++) {
    MOVES_THE_HASH(scratch.timer.ptm.timer[i].latch ^= 0x0100u);
    MOVES_THE_HASH(scratch.timer.ptm.timer[i].counter ^= 0x0001u);
    MOVES_THE_HASH(scratch.timer.ptm.timer[i].control ^= 0x40u);
    MOVES_THE_HASH(scratch.timer.ptm.timer[i].interrupt_flag =
                       !scratch.timer.ptm.timer[i].interrupt_flag);
    MOVES_THE_HASH(scratch.timer.ptm.timer[i].output =
                       !scratch.timer.ptm.timer[i].output);
    MOVES_THE_HASH(scratch.timer.ptm.timer[i].gate =
                       !scratch.timer.ptm.timer[i].gate);
    MOVES_THE_HASH(scratch.timer.ptm.timer[i].lsb_counter ^= 0x11u);
    MOVES_THE_HASH(scratch.timer.ptm.timer[i].single_shot_fired =
                       !scratch.timer.ptm.timer[i].single_shot_fired);
    MOVES_THE_HASH(scratch.timer.ptm.timer[i].prescale_count ^= 0x01u);

    /* The clock and how far it has been consumed. Two machines whose counters
     * agree but which have taken different amounts of time to get there
     * diverge on the very next advance. */
    MOVES_THE_HASH(scratch.timer.clock[i].hz ^= 0x1000u);
    MOVES_THE_HASH(scratch.timer.clock[i].period ^= 0x10u);
    MOVES_THE_HASH(scratch.timer.clocked_to[i] += 1u);
  }

  MOVES_THE_HASH(scratch.timer.ptm.msb_buffer ^= 0x55u);
  MOVES_THE_HASH(scratch.timer.ptm.lsb_buffer ^= 0xAAu);
  MOVES_THE_HASH(scratch.timer.ptm.control2 ^= 0x01u);

  /* §3.11's clearing rule needs memory: a part whose status has been read is in
   * a different state from one whose has not, with every register identical. */
  MOVES_THE_HASH(scratch.timer.ptm.status_snapshot ^= 0x01u);
  MOVES_THE_HASH(scratch.timer.ptm.status_was_read =
                     !scratch.timer.ptm.status_was_read);
}

static void test_every_calendar_field_moves_the_hash(void) {
  for (unsigned i = 0; i < AP_MC146818_BYTES; i++) {
    MOVES_THE_HASH(scratch.calendar.rtc.ram[i] ^= 0xFFu);
  }

  MOVES_THE_HASH(scratch.calendar.rtc.now.year += 1u);
  MOVES_THE_HASH(scratch.calendar.rtc.now.month += 1u);
  MOVES_THE_HASH(scratch.calendar.rtc.now.day += 1u);
  MOVES_THE_HASH(scratch.calendar.rtc.now.day_of_week += 1u);
  MOVES_THE_HASH(scratch.calendar.rtc.now.hour += 1u);
  MOVES_THE_HASH(scratch.calendar.rtc.now.minute += 1u);
  MOVES_THE_HASH(scratch.calendar.rtc.now.second += 1u);

  MOVES_THE_HASH(scratch.calendar.rtc.updated_to += 1u);
  MOVES_THE_HASH(scratch.calendar.rtc.second_clock.hz ^= 0x01u);
  MOVES_THE_HASH(scratch.calendar.rtc.second_clock.period ^= 0x01u);

  /* The periodic interrupt keeps its own cursor and its own clock, because it
   * runs at its own rate and must not be quantised to the one-second update. A
   * hash folding the two together would lose a programmed rate entirely. */
  MOVES_THE_HASH(scratch.calendar.rtc.periodic_to += 1u);
  MOVES_THE_HASH(scratch.calendar.rtc.periodic_clock.hz ^= 0x01u);
  MOVES_THE_HASH(scratch.calendar.rtc.periodic_clock.period ^= 0x01u);
}

static void test_every_dma_field_moves_the_hash(void) {
  for (unsigned c = 0; c < 2u; c++) {
    for (unsigned i = 0; i < AP_I8237_CHANNELS; i++) {
      /* Base and current both: autoinitialise reloads one from the other, so a
       * channel that has counted down is not one that has not, even though the
       * next transfer would restore it. */
      MOVES_THE_HASH(scratch.dma.controller[c].channel[i].base_address ^=
                     0x1000u);
      MOVES_THE_HASH(scratch.dma.controller[c].channel[i].base_count ^= 0x0100u);
      MOVES_THE_HASH(scratch.dma.controller[c].channel[i].current_address ^=
                     0x0010u);
      MOVES_THE_HASH(scratch.dma.controller[c].channel[i].current_count ^=
                     0x0001u);
      MOVES_THE_HASH(scratch.dma.controller[c].channel[i].mode ^= 0x04u);
    }
    MOVES_THE_HASH(scratch.dma.controller[c].command ^= 0x01u);
    MOVES_THE_HASH(scratch.dma.controller[c].status ^= 0x10u);
    MOVES_THE_HASH(scratch.dma.controller[c].request ^= 0x01u);
    MOVES_THE_HASH(scratch.dma.controller[c].mask ^= 0x01u);
    MOVES_THE_HASH(scratch.dma.controller[c].temporary ^= 0xFFu);
    MOVES_THE_HASH(scratch.dma.controller[c].high_byte =
                       !scratch.dma.controller[c].high_byte);
    MOVES_THE_HASH(scratch.dma.controller[c].dreq ^= 0x02u);
  }
}

static void test_every_serial_field_moves_the_hash(void) {
  for (unsigned p = 0; p < 2u; p++) {
    for (unsigned c = 0; c < AP_MC68681_CHANNELS; c++) {
      MOVES_THE_HASH(scratch.sio.port[p].channel[c].mr[0] ^= 0x13u);
      MOVES_THE_HASH(scratch.sio.port[p].channel[c].mr[1] ^= 0x07u);
      MOVES_THE_HASH(scratch.sio.port[p].channel[c].mr_pointer =
                         !scratch.sio.port[p].channel[c].mr_pointer);
      MOVES_THE_HASH(scratch.sio.port[p].channel[c].csr ^= 0x77u);
      MOVES_THE_HASH(scratch.sio.port[p].channel[c].sr ^= 0x40u);
      MOVES_THE_HASH(scratch.sio.port[p].channel[c].rx_enabled =
                         !scratch.sio.port[p].channel[c].rx_enabled);
      MOVES_THE_HASH(scratch.sio.port[p].channel[c].tx_enabled =
                         !scratch.sio.port[p].channel[c].tx_enabled);
      MOVES_THE_HASH(scratch.sio.port[p].channel[c].tx_holding ^= 0x41u);
      MOVES_THE_HASH(scratch.sio.port[p].channel[c].tx_holding_full =
                         !scratch.sio.port[p].channel[c].tx_holding_full);

      /* A character in the FIFO, which is what a console read is waiting for. */
      MOVES_THE_HASH(scratch.sio.port[p].channel[c].fifo[0] = 0x0Du;
                     scratch.sio.port[p].channel[c].fifo_count = 1u);
    }
    MOVES_THE_HASH(scratch.sio.port[p].acr ^= 0xE0u);
    MOVES_THE_HASH(scratch.sio.port[p].imr ^= 0x02u);
    MOVES_THE_HASH(scratch.sio.port[p].isr ^= 0x02u);
    MOVES_THE_HASH(scratch.sio.port[p].ivr ^= 0x0Fu);
    MOVES_THE_HASH(scratch.sio.port[p].ipcr ^= 0x10u);
    MOVES_THE_HASH(scratch.sio.port[p].opcr ^= 0x04u);
    MOVES_THE_HASH(scratch.sio.port[p].opr ^= 0x01u);
    MOVES_THE_HASH(scratch.sio.port[p].input ^= 0x01u);
    MOVES_THE_HASH(scratch.sio.port[p].preload ^= 0x0100u);
    MOVES_THE_HASH(scratch.sio.port[p].counter ^= 0x0001u);
    MOVES_THE_HASH(scratch.sio.port[p].counter_running =
                       !scratch.sio.port[p].counter_running);
    MOVES_THE_HASH(scratch.sio.port[p].counter_output =
                       !scratch.sio.port[p].counter_output);
    MOVES_THE_HASH(scratch.sio.port[p].counter_second_half =
                       !scratch.sio.port[p].counter_second_half);
  }
}

/* The other half of the FIFO rule, and it is the direction that goes wrong
 * silently: entries past the count hold whatever the last occupant left, no
 * read can reach them, and hashing them would make two ports that behave
 * identically disagree. That is a false positive, and a harness that rejects
 * identical machines cannot be used at all. */
static void test_a_serial_fifo_entry_beyond_the_count_does_not_move_the_hash(
    void) {
  make_board(&scratch);
  scratch.sio.port[0].channel[0].fifo[0] = 0x41u;
  scratch.sio.port[0].channel[0].fifo_count = 1u;
  const uint64_t before = ap_board_state_hash(&scratch);

  scratch.sio.port[0].channel[0].fifo[1] = 0xFFu;
  scratch.sio.port[0].channel[0].fifo[2] = 0xFFu;
  TEST_ASSERT_EQUAL_HEX64(before, ap_board_state_hash(&scratch));

  /* The control: the byte that *is* reachable still counts, so this is not a
   * hash that has simply stopped looking at the FIFO. */
  scratch.sio.port[0].channel[0].fifo[0] = 0x42u;
  TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_board_state_hash(&scratch));
}

/* A device whose whole purpose is to be unique per machine. Two nodes on one
 * ring hashing alike is the one thing this must never say. */
static void test_the_node_id_moves_the_hash(void) {
  MOVES_THE_HASH(scratch.node_id.id = 0x054321u);
}

static void test_every_disk_controller_field_moves_the_hash(void) {
  MOVES_THE_HASH(scratch.disk.controller.data ^= 0x0102u);
  MOVES_THE_HASH(scratch.disk.controller.status ^= 0x0Fu);
  MOVES_THE_HASH(scratch.disk.controller.configuration ^= 0x01u);
  MOVES_THE_HASH(scratch.disk.controller.mask ^= 0x01u);
  MOVES_THE_HASH(scratch.disk.controller.dor ^= 0x08u);
  MOVES_THE_HASH(scratch.disk.controller.fdc_status ^= 0x80u);
  MOVES_THE_HASH(scratch.disk.controller.fdc_data ^= 0x55u);
  MOVES_THE_HASH(scratch.disk.controller.fdc_control ^= 0x03u);
  MOVES_THE_HASH(scratch.disk.controller.fdc_rate ^= 0x03u);
  MOVES_THE_HASH(scratch.disk.controller.disk_change =
                     !scratch.disk.controller.disk_change);
}

static void test_every_tape_field_moves_the_hash(void) {
  MOVES_THE_HASH(scratch.tape.controller.control ^= 0x01u);
  MOVES_THE_HASH(scratch.tape.controller.data ^= 0x55u);
  MOVES_THE_HASH(scratch.tape.controller.ready =
                     !scratch.tape.controller.ready);
  MOVES_THE_HASH(scratch.tape.controller.exception =
                     !scratch.tape.controller.exception);
  MOVES_THE_HASH(scratch.tape.controller.done = !scratch.tape.controller.done);
  MOVES_THE_HASH(scratch.tape.controller.direction =
                     !scratch.tape.controller.direction);
  MOVES_THE_HASH(scratch.tape.controller.dma_active =
                     !scratch.tape.controller.dma_active);

  MOVES_THE_HASH(scratch.tape.drive.image.data = ram);
  MOVES_THE_HASH(scratch.tape.drive.image.size += 512u);
  MOVES_THE_HASH(scratch.tape.drive.image.blocks += 1u);
  MOVES_THE_HASH(scratch.tape.drive.loaded = !scratch.tape.drive.loaded);
  MOVES_THE_HASH(scratch.tape.drive.cartridge = AP_QIC_CARTRIDGE_DC600A);
  MOVES_THE_HASH(scratch.tape.drive.selected = !scratch.tape.drive.selected);
  MOVES_THE_HASH(scratch.tape.drive.soft_lock = !scratch.tape.drive.soft_lock);
  MOVES_THE_HASH(scratch.tape.drive.q24_format = !scratch.tape.drive.q24_format);
  MOVES_THE_HASH(scratch.tape.drive.position += 1u);
  MOVES_THE_HASH(scratch.tape.drive.reading = !scratch.tape.drive.reading);
  /* The drive's own latches and counters. `reading` was hashed and `writing`
   * was not; neither were the two conditions a driver reads out of the status
   * block, so two drives differing only in whether they had acknowledged their
   * power-on reset hashed alike. */
  MOVES_THE_HASH(scratch.tape.drive.writing = !scratch.tape.drive.writing);
  MOVES_THE_HASH(scratch.tape.drive.status_pending =
                     !scratch.tape.drive.status_pending);
  MOVES_THE_HASH(scratch.tape.drive.power_on = !scratch.tape.drive.power_on);
  MOVES_THE_HASH(scratch.tape.drive.illegal_command =
                     !scratch.tape.drive.illegal_command);
  MOVES_THE_HASH(scratch.tape.drive.data_errors += 1u);
  MOVES_THE_HASH(scratch.tape.drive.underruns += 1u);

  MOVES_THE_HASH(scratch.tape.offset += 1u);
  MOVES_THE_HASH(scratch.tape.block_valid = !scratch.tape.block_valid);
}

/* The buffered block counts when it holds something and not otherwise. Same
 * rule as the serial FIFO and as the processor's caches: an invalid buffer
 * keeps whatever the last block left, and no read can reach it. */
static void test_a_buffered_tape_block_counts_and_an_unbuffered_one_does_not(
    void) {
  make_board(&scratch);
  const uint64_t empty = ap_board_state_hash(&scratch);

  scratch.tape.block[0] = 0xA5u;
  TEST_ASSERT_EQUAL_HEX64(empty, ap_board_state_hash(&scratch));

  scratch.tape.block_valid = true;
  const uint64_t buffered = ap_board_state_hash(&scratch);
  scratch.tape.block[0] = 0x5Au;
  TEST_ASSERT_NOT_EQUAL_UINT64(buffered, ap_board_state_hash(&scratch));
}

/* The frame buffer is the machine's output, so a run that drew a different
 * picture is a different run -- and nothing else covers it, since the graphics
 * memories hang off the display controller rather than off the machine.
 *
 * Two boards with two different buffers holding the same picture must still
 * agree, which is where a leaked host pointer would show. */
/* ## The frame buffer is only half the picture
 *
 * A frame buffer holds palette *indices*, so two runs that drew identical bits
 * through different palettes drew different pictures -- and used to hash alike,
 * because the buffers were the only thing here that was hashed. So did two
 * controllers with the same memory and different raster operations or plane
 * selects.
 *
 * Named per field rather than by a memory compare so that a field added and not
 * hashed fails here, which is how the omission was found in the first place. */
static void test_every_display_controller_field_moves_the_hash(void) {
  ap_graphics_init(&scratch.graphics, AP_SCREEN_COLOUR_4_PLANE);

  MOVES_THE_HASH(scratch.graphics.reg.cr0 ^= 0xE0u);
  MOVES_THE_HASH(scratch.graphics.reg.cr1 ^= 0x01u);
  MOVES_THE_HASH(scratch.graphics.reg.cr2 ^= 0x0Fu);
  MOVES_THE_HASH(scratch.graphics.reg.cr2b ^= 0x07u);
  MOVES_THE_HASH(scratch.graphics.reg.cr3a ^= 0x80u);
  MOVES_THE_HASH(scratch.graphics.reg.cr3b ^= 0x04u);
  MOVES_THE_HASH(scratch.graphics.reg.write_enable ^= 0xF00Fu);
  MOVES_THE_HASH(scratch.graphics.reg.rop ^= 0x12345678u);
  MOVES_THE_HASH(scratch.graphics.lut_control ^= 0x20u);
  MOVES_THE_HASH(scratch.graphics.lut_data ^= 0x5Au);
  MOVES_THE_HASH(scratch.graphics.lut_fifo_count = 1u);
  MOVES_THE_HASH(scratch.graphics.diag_channel ^= 0x06u);
  MOVES_THE_HASH(scratch.graphics.diag_refresh_request ^= 0x01u);
  MOVES_THE_HASH(scratch.graphics.guard_latch[3] ^= 0xDEADBEEFu);
  MOVES_THE_HASH(scratch.graphics.blt_cycle ^= 1u);
  MOVES_THE_HASH(scratch.graphics.h_clock ^= 7u);
  MOVES_THE_HASH(scratch.graphics.v_clock ^= 7u);
  MOVES_THE_HASH(scratch.graphics.p_clock ^= 7u);

  /* Both palettes: the 4-plane board's three-register table and the 8-plane
   * board's Bt458, which is reached through the part rather than a field. */
  MOVES_THE_HASH(scratch.graphics.lut4[1][9] ^= 0x0Fu);
  /* Three writes, because the part takes red, green and blue in turn and an
   * entry is not changed until all three have arrived. */
  MOVES_THE_HASH(ap_bt458_write(&scratch.graphics.lut, AP_BT458_ADDRESS, 0x00u);
                 ap_bt458_write(&scratch.graphics.lut, AP_BT458_PALETTE, 0x7Fu);
                 ap_bt458_write(&scratch.graphics.lut, AP_BT458_PALETTE, 0x11u);
                 ap_bt458_write(&scratch.graphics.lut, AP_BT458_PALETTE, 0x22u));

  /* The FIFO is hashed by its *pending window*, not by the whole array: a byte
   * a driver has already committed is not state the controller still holds, and
   * two machines that reached the same empty FIFO by different routes are the
   * same machine. So a byte inside the window counts and one beyond it does
   * not, and both halves are asserted -- the second is what stops the window
   * quietly becoming the array again. */
  make_board(&scratch);
  scratch.graphics.lut_fifo_count = 1u;
  const uint64_t pending = ap_board_state_hash(&scratch);
  scratch.graphics.lut_fifo[0] = 0x33u;
  TEST_ASSERT_NOT_EQUAL_UINT64(pending, ap_board_state_hash(&scratch));
  const uint64_t inside = ap_board_state_hash(&scratch);
  scratch.graphics.lut_fifo[1] = 0x44u;
  TEST_ASSERT_EQUAL_HEX64(inside, ap_board_state_hash(&scratch));

  /* And the counters are *not* in it, for the reason the disk controller's are
   * not: they are this core watching the machine. */
  const uint64_t before = ap_board_state_hash(&scratch);
  scratch.graphics.lut_fifo_overruns++;
  scratch.graphics.lut_ad_accesses++;
  scratch.graphics.diag_refresh_requests++;
  TEST_ASSERT_EQUAL_HEX64(before, ap_board_state_hash(&scratch));
}

static void test_the_graphics_memory_is_hashed_and_its_address_is_not(void) {
  static uint8_t first_memory[256];
  static uint8_t second_memory[256];
  for (unsigned i = 0; i < sizeof first_memory; i++) {
    first_memory[i] = (uint8_t)i;
    second_memory[i] = (uint8_t)i;
  }

  make_board(&scratch);
  make_board(&other);
  ap_graphics_init(&scratch.graphics, AP_SCREEN_COLOUR_8_PLANE);
  ap_graphics_init(&other.graphics, AP_SCREEN_COLOUR_8_PLANE);
  ap_graphics_attach_memory(&scratch.graphics, first_memory,
                            (uint32_t)sizeof first_memory, NULL, 0u);
  ap_graphics_attach_memory(&other.graphics, second_memory,
                            (uint32_t)sizeof second_memory, NULL, 0u);
  TEST_ASSERT_EQUAL_HEX64(ap_board_state_hash(&scratch),
                          ap_board_state_hash(&other));

  second_memory[128] ^= 0xFFu;
  TEST_ASSERT_NOT_EQUAL_UINT64(ap_board_state_hash(&scratch),
                               ap_board_state_hash(&other));
}

/* Which screen is fitted is state in its own right: the controllers answer
 * different device IDs and the firmware branches on them. */
static void test_the_screen_kind_moves_the_hash(void) {
  MOVES_THE_HASH(ap_graphics_init(&scratch.graphics, AP_SCREEN_MONO_19_INCH));
}

static void test_the_keyboard_matrix_moves_the_hash(void) {
  for (unsigned key = 0; key < AP_KBD_KEYS; key++) {
    MOVES_THE_HASH(scratch.keyboard.down[key] = true);
  }
}

/* **The matrix was the whole of the keyboard's hasher**, so a keyboard in the
 * other code set, still in loopback, part-way through a command, sounding its
 * beeper or lit at CAPS LOCK hashed the same as one that was none of those.
 * Three of the six are changed by a host command directly.
 *
 * `loopback` is the sharpest: a keyboard taken out of loopback answers commands
 * where one still in it echoes them. `caps_lock_led` joined the model in the
 * `008778-03` chapter 12 walk, which is what put a lamp here at all. */
static void test_every_other_keyboard_field_moves_the_hash(void) {
  MOVES_THE_HASH(scratch.keyboard.loopback = !scratch.keyboard.loopback);
  MOVES_THE_HASH(scratch.keyboard.rx_message = 0xFF1221u);
  MOVES_THE_HASH(scratch.keyboard.keystate_mode = true);
  MOVES_THE_HASH(scratch.keyboard.caps_lock_led = true);
  MOVES_THE_HASH(scratch.keyboard.held = 3u);
  MOVES_THE_HASH(scratch.keyboard.repeating = true);
  MOVES_THE_HASH(scratch.keyboard.repeat_at = 12345u);
  MOVES_THE_HASH(scratch.keyboard.beeper_until = 67890u);
}

/* Which firmware is running is the largest single fact about a boot. */
static void test_the_prom_contents_move_the_hash(void) {
  static uint8_t first_prom[64];
  static uint8_t second_prom[64];
  for (unsigned i = 0; i < sizeof first_prom; i++) {
    first_prom[i] = (uint8_t)(i * 3u);
    second_prom[i] = (uint8_t)(i * 3u);
  }

  make_board(&scratch);
  make_board(&other);
  TEST_ASSERT_TRUE(
      ap_board_load_prom(&scratch, first_prom, (uint32_t)sizeof first_prom));
  TEST_ASSERT_TRUE(
      ap_board_load_prom(&other, second_prom, (uint32_t)sizeof second_prom));
  TEST_ASSERT_EQUAL_HEX64(ap_board_state_hash(&scratch),
                          ap_board_state_hash(&other));

  second_prom[7] ^= 0xFFu;
  TEST_ASSERT_NOT_EQUAL_UINT64(ap_board_state_hash(&scratch),
                               ap_board_state_hash(&other));
}

/* A machine with no PROM is a real configuration -- the region answers unmapped
 * -- and must not hash as one with a blank PROM fitted. Absence is fed as a
 * marker for the same reason the processor marks an absent access context. */
static void test_no_prom_and_a_blank_prom_hash_differently(void) {
  static uint8_t blank[64] = {0};

  make_board(&scratch);
  const uint64_t absent = ap_board_state_hash(&scratch);
  TEST_ASSERT_TRUE(ap_board_load_prom(&scratch, blank, (uint32_t)sizeof blank));
  TEST_ASSERT_NOT_EQUAL_UINT64(absent, ap_board_state_hash(&scratch));
}

/* Main memory's *extent* is the board's, its contents are the machine's. 8
 * Mbyte fitted is a different machine from 16 however well the bytes in common
 * agree; the bytes themselves are hashed once, by `ap_machine_hash`, because
 * the board and the machine share one buffer. */
static void test_the_memory_extent_counts_and_its_contents_are_hashed_elsewhere(
    void) {
  make_board(&scratch);
  const uint64_t before = ap_board_state_hash(&scratch);

  ram[0] ^= 0xFFu;
  TEST_ASSERT_EQUAL_HEX64(before, ap_board_state_hash(&scratch));
  ram[0] ^= 0xFFu;

  scratch.ram_bytes /= 2u;
  TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_board_state_hash(&scratch));
}

/* The counters are our record of *watching* the machine, not state it has. In
 * the hash they would make adding an instrument change every golden with no
 * emulated behaviour changing, and would make two machines that behave
 * identically compare unequal because one was watched more closely. They are
 * reported beside the hash instead -- `ap_machine_state` and the frontend's
 * boot summary -- so nothing is lost by leaving them out. */
static void test_the_diagnostic_counters_do_not_move_the_hash(void) {
  make_board(&scratch);
  const uint64_t before = ap_board_state_hash(&scratch);

  scratch.unmapped_reads = 129u;
  scratch.unmapped_writes = 7u;
  scratch.first_unmapped_read = 0xFFF90000u;
  scratch.first_unmapped_write = 0x00011600u;
  scratch.rom_writes = 3u;
  scratch.first_rom_write = 0x00000000u;
  scratch.atbus_empty_reads = 11u;
  scratch.atbus_empty_writes = 2u;
  scratch.first_atbus_empty_read = 0x00090000u;
  scratch.first_atbus_empty_write = 0x00090004u;
  for (unsigned r = 0; r < AP_BOARD_REGIONS; r++) {
    scratch.region_reads[r] = r + 1u;
    scratch.region_writes[r] = r + 2u;
  }
  for (unsigned unit = 0; unit < 2u; unit++) {
    for (unsigned reg = 0; reg < AP_MC68681_REGISTERS; reg++) {
      scratch.sio.register_reads[unit][reg] = reg + 1u;
      scratch.sio.register_writes[unit][reg] = reg + 2u;
    }
  }

  TEST_ASSERT_EQUAL_HEX64(before, ap_board_state_hash(&scratch));
}

/* Running a board -- the whole point. Two boards given the same accesses in the
 * same order must agree at every step, and the hash must *move* as they go: one
 * that never changed would satisfy the first property perfectly and detect
 * nothing. */
static void test_two_boards_given_the_same_accesses_agree_at_every_step(void) {
  make_board(&scratch);
  make_board(&other);

  static const uint32_t writes[] = {
      0x010401u, /* serial 1, channel A mode register */
      0x010403u, 0x010801u, /* the interval timer's control register */
      0x011000u,            /* the master interrupt controller */
      0x017000u,            /* the address translation map */
  };

  uint64_t previous = ap_board_state_hash(&scratch);
  unsigned moves = 0u;
  for (unsigned pass = 0; pass < 4u; pass++) {
    for (unsigned i = 0; i < sizeof writes / sizeof writes[0]; i++) {
      const uint8_t value = (uint8_t)(0x11u * (pass + 1u) + i);
      bool ok = false;
      ap_board_write(&scratch, writes[i], value, &ok);
      ap_board_write(&other, writes[i], value, &ok);

      const uint64_t now = ap_board_state_hash(&scratch);
      TEST_ASSERT_EQUAL_HEX64(now, ap_board_state_hash(&other));
      if (now != previous) {
        moves++;
        previous = now;
      }
    }
  }
  /* The hash moved as the run proceeded. A hash that never changed would agree
   * with itself at every step and detect nothing at all. */
  TEST_ASSERT_GREATER_THAN_UINT32(1u, moves);
}

/* Two cartridges of exactly equal size and different contents must hash apart.
 *
 * They did not: the drive contributed its image by *extent* only, because
 * re-reading up to a hundred megabytes of read-only media on every hash would
 * cost more than the run being measured. The approximation was named in
 * `ap_board_state.h` with its closing route -- a digest taken once at load --
 * and this is that route asserted. */
static void test_two_cartridges_of_equal_size_hash_apart(void) {
  static uint8_t one[AP_CT_BLOCK_SIZE * 2u];
  static uint8_t two[AP_CT_BLOCK_SIZE * 2u];
  memset(one, 0xA5, sizeof one);
  memset(two, 0xA5, sizeof two);
  /* One byte, in the second block, so the difference is nowhere near the
   * header and could only be found by reading the medium. */
  two[AP_CT_BLOCK_SIZE + 17u] = 0x5Au;

  ap_ct_t a, b;
  TEST_ASSERT_TRUE(ap_ct_open(&a, one, sizeof one, false));
  TEST_ASSERT_TRUE(ap_ct_open(&b, two, sizeof two, false));

  TEST_ASSERT_EQUAL_UINT64(a.size, b.size);
  TEST_ASSERT_EQUAL_UINT64(a.blocks, b.blocks);
  TEST_ASSERT_TRUE(a.digest != b.digest);

  /* And a writable cartridge keeps its digest current, so a run that changed
   * the medium is distinguishable from one that did not. */
  ap_ct_t w;
  TEST_ASSERT_TRUE(ap_ct_open(&w, two, sizeof two, true));
  const uint64_t before = w.digest;
  static uint8_t block[AP_CT_BLOCK_SIZE];
  memset(block, 0x11, sizeof block);
  TEST_ASSERT_TRUE(ap_ct_write_block(&w, 0u, block));
  TEST_ASSERT_TRUE(w.digest != before);
}

/* ## The seven members that had no hasher, and the two registers that had none
 *
 * The `MOVES_THE_HASH` discipline above is thorough per *struct* and was silent
 * about which structs existed: nine of the board's members had no hasher at
 * all, so there was nothing to write a `test_every_..._moves_the_hash` against
 * and nobody noticed one was missing. These are those tests.
 */

static void test_the_last_two_board_registers_move_the_hash(void) {
  /* Table 2-8's remaining pair. Writable storage, and outside the hash until
   * 2026-08-19 -- the reference boot writes one of them. */
  MOVES_THE_HASH(scratch.registers.master_request ^= 0x40u);
  MOVES_THE_HASH(scratch.registers.task_alias ^= 0x01u);
}

static void test_every_parity_bit_moves_the_hash(void) {
  /* The bad-parity bitmap is a lent buffer whose contents the control
   * register's force-bad-parity function writes, so it is hashed the way main
   * memory is. Only meaningful with one attached. */
  static uint8_t bad[(sizeof ram + 7u) / 8u];
  make_board(&scratch);
  TEST_ASSERT_TRUE(ap_board_attach_parity(&scratch, bad, sizeof bad));
  const uint64_t before = ap_board_state_hash(&scratch);
  bad[0] ^= 0x01u;
  TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_board_state_hash(&scratch));
  /* And the far end, so a hash covering only the first byte fails here. */
  bad[0] ^= 0x01u;
  bad[sizeof bad - 1u] ^= 0x80u;
  TEST_ASSERT_NOT_EQUAL_UINT64(before, ap_board_state_hash(&scratch));
}

static void test_every_dma_page_register_moves_the_hash(void) {
  for (unsigned i = 0; i < AP_DMAPAGE_REGISTERS; i++) {
    MOVES_THE_HASH(scratch.dma_page.page[i] ^= 0xFFu);
  }
}

static void test_every_arbiter_field_moves_the_hash(void) {
  /* Live bus arbitration, which changes on the machine's own clock. */
  MOVES_THE_HASH(scratch.arbiter.request ^= 0x02u);
  MOVES_THE_HASH(scratch.arbiter.selected = 3);
  MOVES_THE_HASH(scratch.arbiter.master = 1);
  MOVES_THE_HASH(scratch.arbiter.cpu.br = !scratch.arbiter.cpu.br);
  MOVES_THE_HASH(scratch.arbiter.cpu.bgack = !scratch.arbiter.cpu.bgack);
  MOVES_THE_HASH(scratch.arbiter.cpu.br_sync ^= 0x01u);
  MOVES_THE_HASH(scratch.arbiter.cpu.bgack_sync ^= 0x01u);
  MOVES_THE_HASH(scratch.arbiter.cpu.r = !scratch.arbiter.cpu.r);
  MOVES_THE_HASH(scratch.arbiter.cpu.a = !scratch.arbiter.cpu.a);
  MOVES_THE_HASH(scratch.arbiter.cpu.bg = !scratch.arbiter.cpu.bg);
  MOVES_THE_HASH(scratch.arbiter.cpu.three_state =
                     !scratch.arbiter.cpu.three_state);
}

static void test_every_external_master_field_moves_the_hash(void) {
  MOVES_THE_HASH(scratch.master.unit ^= 1u);
  MOVES_THE_HASH(scratch.master.channel ^= 1u);
  MOVES_THE_HASH(scratch.master.drq ^= 1u);
  MOVES_THE_HASH(scratch.master.request = !scratch.master.request);
  MOVES_THE_HASH(scratch.master.master_l = !scratch.master.master_l);
  MOVES_THE_HASH(scratch.master.state =
                     (ap_master_state_t)(scratch.master.state + 1));
}

static void test_every_matrox_field_moves_the_hash(void) {
  MOVES_THE_HASH(scratch.matrox.microcode_words ^= 1u);
  MOVES_THE_HASH(scratch.matrox.last_transfer ^= 0x01u);
  MOVES_THE_HASH(scratch.matrox.transfer_armed =
                     !scratch.matrox.transfer_armed);
  MOVES_THE_HASH(scratch.matrox.data_latch ^= 0x0001u);
}

static void test_every_ethernet_card_field_moves_the_hash(void) {
  MOVES_THE_HASH(scratch.ethernet.hcr ^= 0x01u);
  MOVES_THE_HASH(scratch.ethernet.acr ^= 0x01u);
  MOVES_THE_HASH(scratch.ethernet.aux_dma ^= 0x01u);
  MOVES_THE_HASH(scratch.ethernet.to_adapter ^= 0x01u);
  MOVES_THE_HASH(scratch.ethernet.to_adapter_full =
                     !scratch.ethernet.to_adapter_full);
  MOVES_THE_HASH(scratch.ethernet.to_host ^= 0x01u);
  MOVES_THE_HASH(scratch.ethernet.to_host_full =
                     !scratch.ethernet.to_host_full);
  MOVES_THE_HASH(scratch.ethernet.dma_done = !scratch.ethernet.dma_done);
  MOVES_THE_HASH(scratch.ethernet.test_jumper = !scratch.ethernet.test_jumper);
  MOVES_THE_HASH(scratch.ethernet.sixteen_bit = !scratch.ethernet.sixteen_bit);
  MOVES_THE_HASH(scratch.ethernet.dma_since_pause ^= 1u);
  MOVES_THE_HASH(scratch.ethernet.dma_pause_owed =
                     !scratch.ethernet.dma_pause_owed);
  MOVES_THE_HASH(scratch.ethernet.adapter_initialising =
                     !scratch.ethernet.adapter_initialising);
  /* The FIFO counts to its count and no further, the same rule the serial
   * FIFO already follows. */
  MOVES_THE_HASH(scratch.ethernet.fifo_count = 1u;
                 scratch.ethernet.fifo[0] ^= 0xFFu);
}

/* A FIFO byte past the count is stale, and two cards agreeing on everything
 * readable must hash alike. The serial port has this test; the 3c505 did not,
 * because it had no hasher. */
static void test_an_ethernet_fifo_byte_beyond_the_count_does_not_move_the_hash(
    void) {
  make_board(&scratch);
  scratch.ethernet.fifo_count = 1u;
  const uint64_t before = ap_board_state_hash(&scratch);
  scratch.ethernet.fifo[AP_3C505_DATA_FIFO - 1u] ^= 0xFFu;
  TEST_ASSERT_EQUAL_HEX64(before, ap_board_state_hash(&scratch));
}

static void test_every_ethernet_adapter_field_moves_the_hash(void) {
  MOVES_THE_HASH(scratch.ethernet_adapter.address[0] ^= 0x01u);
  MOVES_THE_HASH(scratch.ethernet_adapter.address[AP_3C505_ADDRESS_BYTES - 1u]
                 ^= 0x01u);
  MOVES_THE_HASH(scratch.ethernet_adapter.receive_mode ^= 0x0001u);
  MOVES_THE_HASH(scratch.ethernet_adapter.multicast_count = 1u;
                 scratch.ethernet_adapter.multicast[0][0] ^= 0xFFu);
  /* Host-readable statistics: state, because a driver reads them back with a
   * Network Statistics command. */
  MOVES_THE_HASH(scratch.ethernet_adapter.receive_packets ^= 1u);
  MOVES_THE_HASH(scratch.ethernet_adapter.transmit_packets ^= 1u);
  MOVES_THE_HASH(scratch.ethernet_adapter.crc_errors ^= 1u);
  MOVES_THE_HASH(scratch.ethernet_adapter.alignment_errors ^= 1u);
  MOVES_THE_HASH(scratch.ethernet_adapter.no_resource_errors ^= 1u);
  MOVES_THE_HASH(scratch.ethernet_adapter.overrun_errors ^= 1u);
  MOVES_THE_HASH(scratch.ethernet_adapter.incoming.written ^= 1u);
  MOVES_THE_HASH(scratch.ethernet_adapter.incoming.buffer[0] ^= 0xFFu);
  MOVES_THE_HASH(
      scratch.ethernet_adapter.incoming.buffer[AP_3C505_PCB_MAX - 1u] ^= 0xFFu);
  MOVES_THE_HASH(scratch.ethernet_adapter.pending.pcb.command ^= 0x01u);
  MOVES_THE_HASH(scratch.ethernet_adapter.pending.pcb.length = 1u;
                 scratch.ethernet_adapter.pending.pcb.data[0] ^= 0xFFu);
  MOVES_THE_HASH(scratch.ethernet_adapter.pending.sent ^= 1u);
  MOVES_THE_HASH(scratch.ethernet_adapter.pending.active =
                     !scratch.ethernet_adapter.pending.active);
  MOVES_THE_HASH(scratch.ethernet_adapter.pending_total =
                     !scratch.ethernet_adapter.pending_total);
  MOVES_THE_HASH(scratch.ethernet_adapter.transmitting =
                     !scratch.ethernet_adapter.transmitting);
  MOVES_THE_HASH(scratch.ethernet_adapter.transmit_length ^= 1u);
  MOVES_THE_HASH(scratch.ethernet_adapter.transmit_received = 1u;
                 scratch.ethernet_adapter.transmit_buffer[0] ^= 0xFFu);
  MOVES_THE_HASH(scratch.ethernet_adapter.transmit_offset ^= 1u);
  MOVES_THE_HASH(scratch.ethernet_adapter.transmit_segment ^= 1u);
  MOVES_THE_HASH(scratch.ethernet_adapter.receive_armed =
                     !scratch.ethernet_adapter.receive_armed);
  MOVES_THE_HASH(scratch.ethernet_adapter.receive_offset ^= 1u);
  MOVES_THE_HASH(scratch.ethernet_adapter.receive_segment ^= 1u);
  MOVES_THE_HASH(scratch.ethernet_adapter.receive_buffer_length ^= 1u);
  MOVES_THE_HASH(scratch.ethernet_adapter.receive_timeout ^= 1u);
  MOVES_THE_HASH(scratch.ethernet_adapter.staged_read ^= 1u);
  MOVES_THE_HASH(scratch.ethernet_adapter.staged_length = 1u;
                 scratch.ethernet_adapter.staged[0] ^= 0xFFu);
}

/* **The property the whole file rests on, for the part most able to break it.**
 * The adapter carries a `transmit` function pointer and a `void *context`, and
 * a hash that fed either would make two identical machines differ by where the
 * host happened to put them. Whether one is *attached* is state; its address
 * is not. */
static void test_the_ethernet_wire_is_hashed_by_presence_not_by_address(void) {
  static int context_a = 0;
  static int context_b = 0;
  make_board(&scratch);
  make_board(&other);

  scratch.ethernet_adapter.wire.context = &context_a;
  other.ethernet_adapter.wire.context = &context_b;
  TEST_ASSERT_EQUAL_HEX64(ap_board_state_hash(&scratch),
                          ap_board_state_hash(&other));

  /* But present and absent are different machines. */
  other.ethernet_adapter.wire.context = nullptr;
  TEST_ASSERT_NOT_EQUAL_UINT64(ap_board_state_hash(&scratch),
                               ap_board_state_hash(&other));
}

static void test_every_ring_station_field_moves_the_hash(void) {
  MOVES_THE_HASH(scratch.ring_station.slot ^= 1);
  MOVES_THE_HASH(scratch.ring_station.address ^= 0x00000001u);
  MOVES_THE_HASH(scratch.ring_station.receive_enabled =
                     !scratch.ring_station.receive_enabled);
  MOVES_THE_HASH(scratch.ring_station.tx_armed =
                     !scratch.ring_station.tx_armed);
  MOVES_THE_HASH(scratch.ring_station.tx_bit_pos ^= 1u);
  MOVES_THE_HASH(scratch.ring_station.rx_state ^= 1u);
  MOVES_THE_HASH(scratch.ring_station.rx_ones_run ^= 1u);
  MOVES_THE_HASH(scratch.ring_station.rx_bit_count ^= 1u);
  MOVES_THE_HASH(scratch.ring_station.rx_byte ^= 0xFFu);
  MOVES_THE_HASH(scratch.ring_station.rx_header[0] ^= 0xFFu);
  MOVES_THE_HASH(scratch.ring_station.rx_header[7] ^= 0xFFu);
  MOVES_THE_HASH(scratch.ring_station.rx_header_len ^= 1u);
  MOVES_THE_HASH(scratch.ring_station.rx_addressed =
                     !scratch.ring_station.rx_addressed);
  MOVES_THE_HASH(scratch.ring_station.rx_overrun =
                     !scratch.ring_station.rx_overrun);
  MOVES_THE_HASH(scratch.ring_station.rx_header_bits ^= 1u);
  MOVES_THE_HASH(scratch.ring_station.rx_flipped_parity =
                     !scratch.ring_station.rx_flipped_parity);
  MOVES_THE_HASH(scratch.ring_station.rx_separators ^= 1u);
  MOVES_THE_HASH(scratch.ring_station.rx_fcs_bits ^= 1u);
  MOVES_THE_HASH(scratch.ring_station.rx_late_bits ^= 1u);
  MOVES_THE_HASH(scratch.ring_station.rx_late ^= 0xFFu);
  MOVES_THE_HASH(scratch.ring_station.rx_frame_error =
                     !scratch.ring_station.rx_frame_error);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_two_cartridges_of_equal_size_hash_apart);
  RUN_TEST(test_two_identically_built_boards_hash_alike);
  RUN_TEST(test_every_board_register_field_moves_the_hash);
  RUN_TEST(test_every_translation_map_entry_moves_the_hash);
  RUN_TEST(test_every_interrupt_controller_field_moves_the_hash);
  RUN_TEST(test_the_master_and_the_slave_are_told_apart);
  RUN_TEST(test_every_timer_field_moves_the_hash);
  RUN_TEST(test_every_calendar_field_moves_the_hash);
  RUN_TEST(test_every_dma_field_moves_the_hash);
  RUN_TEST(test_every_serial_field_moves_the_hash);
  RUN_TEST(test_a_serial_fifo_entry_beyond_the_count_does_not_move_the_hash);
  RUN_TEST(test_the_node_id_moves_the_hash);
  RUN_TEST(test_every_disk_controller_field_moves_the_hash);
  RUN_TEST(test_every_tape_field_moves_the_hash);
  RUN_TEST(test_a_buffered_tape_block_counts_and_an_unbuffered_one_does_not);
  RUN_TEST(test_every_display_controller_field_moves_the_hash);
  RUN_TEST(test_the_graphics_memory_is_hashed_and_its_address_is_not);
  RUN_TEST(test_the_screen_kind_moves_the_hash);
  RUN_TEST(test_the_keyboard_matrix_moves_the_hash);
  RUN_TEST(test_every_other_keyboard_field_moves_the_hash);
  RUN_TEST(test_the_prom_contents_move_the_hash);
  RUN_TEST(test_no_prom_and_a_blank_prom_hash_differently);
  RUN_TEST(test_the_memory_extent_counts_and_its_contents_are_hashed_elsewhere);
  RUN_TEST(test_the_diagnostic_counters_do_not_move_the_hash);
  RUN_TEST(test_two_boards_given_the_same_accesses_agree_at_every_step);
  RUN_TEST(test_the_last_two_board_registers_move_the_hash);
  RUN_TEST(test_every_parity_bit_moves_the_hash);
  RUN_TEST(test_every_dma_page_register_moves_the_hash);
  RUN_TEST(test_every_arbiter_field_moves_the_hash);
  RUN_TEST(test_every_external_master_field_moves_the_hash);
  RUN_TEST(test_every_matrox_field_moves_the_hash);
  RUN_TEST(test_every_ethernet_card_field_moves_the_hash);
  RUN_TEST(test_an_ethernet_fifo_byte_beyond_the_count_does_not_move_the_hash);
  RUN_TEST(test_every_ethernet_adapter_field_moves_the_hash);
  RUN_TEST(test_the_ethernet_wire_is_hashed_by_presence_not_by_address);
  RUN_TEST(test_every_ring_station_field_moves_the_hash);
  return UNITY_END();
}

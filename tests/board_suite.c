/* The DN3500 core board's address map, `008778-03` Table 2-8. */

#include "unity.h"

#include <string.h>

#include "board/ap_board.h"
#include "model/ap_model.h"
#include "device/ap_mc68681.h"
#include "board/ap_sio.h"
#include "board/ap_dmapage.h"
#include "board/ap_atmap.h"
#include "board/ap_boardreg.h"

void setUp(void) {}
void tearDown(void) {}

static const ap_mc146818_time_t START = {
    .year = 1987u, .month = 7u, .day = 31u, .day_of_week = 6u,
    .hour = 21u, .minute = 9u, .second = 21u,
};

static uint8_t ram[0x2000];
static uint8_t other_ram[0x2000];

/* A board for the tests that only ask where an address falls. The region is a
 * *model's* answer now, and these all speak the DN3500's map. */
static ap_board_t region_board;
static void init_region_board(void) {
  TEST_ASSERT_TRUE(
      ap_board_init(&region_board, ram, sizeof ram, &START, 0x012345u));
}

static void init(ap_board_t *b) {
  TEST_ASSERT_TRUE(ap_board_init(b, ram, sizeof ram, &START, 0x012345u));
}

static void test_every_device_lands_in_its_documented_region(void) {
  /* Table 2-8, walked. Each address is the one the device's own module carries,
   * so a placement corrected there cannot drift from the map. */
  static const struct {
    uint32_t address;
    ap_board_region_t region;
  } cases[] = {
      {0x000000u, AP_BOARD_REGION_PROM},
      {0x010000u, AP_BOARD_REGION_CORE_REGISTER},
      {0x010300u, AP_BOARD_REGION_CORE_REGISTER},
      {0x010400u, AP_BOARD_REGION_SIO},
      {0x010500u, AP_BOARD_REGION_SIO},
      {0x010800u, AP_BOARD_REGION_TIMER},
      {0x010900u, AP_BOARD_REGION_CALENDAR},
      {0x010C00u, AP_BOARD_REGION_DMA},
      {0x010D00u, AP_BOARD_REGION_DMA},
      {0x011000u, AP_BOARD_REGION_INTERRUPT},
      {0x011100u, AP_BOARD_REGION_INTERRUPT},
      {0x011200u, AP_BOARD_REGION_NODE_ID},
      {0x017000u, AP_BOARD_REGION_TRANSLATION_MAP},
      {0x04D000u, AP_BOARD_REGION_DISK},
      {0x05F800u, AP_BOARD_REGION_DISK},
      {0x050000u, AP_BOARD_REGION_TAPE},
      {0x1000000u, AP_BOARD_REGION_RAM},
  };
  for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    TEST_ASSERT_EQUAL_UINT(cases[i].region, ap_board_region(&region_board, cases[i].address));
  }
}

static void test_an_unclaimed_address_is_unmapped_not_zero(void) {
  ap_board_t b;
  bool ok = true;
  init(&b);

  /* The distinction C28 turned on. Flat RAM made every device address read as
   * zero, which hid thousands of accesses that should have been visible -- an
   * emulator that answers everything cannot say what the firmware wanted. */
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_UNMAPPED, ap_board_region(&region_board, 0x020000u));
  (void)ap_board_read(&b, 0x020000u, &ok);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_UINT(1u, b.unmapped_reads);
  TEST_ASSERT_EQUAL_HEX32(0x020000u, b.first_unmapped_read);
}

static void test_main_memory_is_where_table_two_eight_puts_it(void) {
  ap_board_t b;
  bool ok = false;
  init(&b);

  /* `1000000`, not zero. The boot image's own load address of `0013D800` is
   * *below* this, among the devices -- which is why flat-RAM-from-zero was the
   * wrong shape and why the firmware reached high thousands of times. */
  ap_board_write(&b, AP_BOARD_RAM_BASE + 4u, 0x5Au, &ok);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_HEX8(0x5Au, ap_board_read(&b, AP_BOARD_RAM_BASE + 4u, &ok));
  TEST_ASSERT_TRUE(ok);

  /* And past the memory actually fitted is unmapped, not a wrap. */
  (void)ap_board_read(&b, AP_BOARD_RAM_BASE + sizeof ram, &ok);
  TEST_ASSERT_FALSE(ok);
}

static void test_reads_reach_the_devices_themselves(void) {
  ap_board_t b;
  bool ok = false;
  init(&b);

  /* Each device's own measured idle value, through the map rather than
   * directly -- so the map is checked against the same dumps the devices are. */
  TEST_ASSERT_EQUAL_HEX8(0xC0u, ap_board_read(&b, 0x04D001u, &ok)); /* disk */
  TEST_ASSERT_TRUE(ok);
  /* `70`, not the `40` the oracle reads: RDY and EXC are asserted low so both
   * bits stand at one on an idle controller, and DONE is set by the reset
   * because `[SC499]` says RSTDMA "sets DONE to 1". See `ap_sc499_reset`. */
  TEST_ASSERT_EQUAL_HEX8(0x70u, ap_board_read(&b, 0x050001u, &ok)); /* tape */
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_HEX8(0x80u, ap_board_read(&b, 0x05F807u, &ok)); /* floppy */
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_HEX8(0x01u, ap_board_read(&b, 0x011202u, &ok)); /* node ID */
  TEST_ASSERT_TRUE(ok);
}

/* A read-only memory **absorbs** a write rather than refusing it. Something
 * decodes the address and terminates the cycle; the storage simply cannot
 * change, so the processor sees an ordinary completed write and no bus error.
 *
 * The oracle settles this: MAME's DN3500 maps the boot ROM for write as well as
 * read, to a handler that only logs — and its source names the very image we
 * boot as one that writes to address 4 from PC 2c1c. Real firmware does this and
 * real hardware shrugs, so a board that faulted here would break a program the
 * machine runs. */
static void test_the_read_only_memories_absorb_writes_rather_than_faulting(void) {
  ap_board_t b;
  bool ok = false;
  init(&b);
  static const uint8_t prom[4] = {0x01u, 0x00u, 0x01u, 0x80u};
  TEST_ASSERT_TRUE(ap_board_load_prom(&b, prom, sizeof prom));

  ap_board_write(&b, 0x000002u, 0x5Au, &ok);
  TEST_ASSERT_TRUE(ok);
  ap_board_write(&b, 0x011202u, 0x5Au, &ok);
  TEST_ASSERT_TRUE(ok);

  /* Not unmapped -- an unmapped write is an address nothing answers, and these
   * are answered. Counted apart, because a driver writing to a PROM stays worth
   * knowing even though it is not an error. */
  TEST_ASSERT_EQUAL_UINT(0u, b.unmapped_writes);
  TEST_ASSERT_EQUAL_UINT(2u, b.rom_writes);
  /* The *first*, so a second write cannot overwrite the lead. */
  TEST_ASSERT_EQUAL_HEX32(0x000002u, b.first_rom_write);

  /* Absorbed, not stored: both still read what they held. */
  TEST_ASSERT_EQUAL_HEX8(0x01u, ap_board_read(&b, 0x000002u, &ok));
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_HEX8(0x01u, ap_board_read(&b, 0x011202u, &ok));
  TEST_ASSERT_TRUE(ok);
}

/* With no PROM fitted the region is absent, and both directions have to say so.
 * A board whose missing PROM refuses reads but absorbs writes describes no
 * hardware, and the absorb rule above is exactly the kind that grows such a
 * hole if it is applied by region name rather than by what is present. */
static void test_a_missing_prom_is_absent_for_writes_too(void) {
  ap_board_t b;
  bool ok = true;
  init(&b);

  ap_board_write(&b, 0x000100u, 0x5Au, &ok);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_UINT(1u, b.unmapped_writes);
  TEST_ASSERT_EQUAL_UINT(0u, b.rom_writes);
  TEST_ASSERT_EQUAL_HEX32(0x000100u, b.first_unmapped_write);
}

/* Both AT bus windows are decoded by the **board**, not by whatever card sits
 * in them. An address in a window with no card behind it reads `FF` -- the bus
 * is pulled up and the cycle terminates normally -- and does not fault.
 *
 * This is the display controller's lesson again, and it is worth a test of its
 * own because the failure is invisible until firmware walks a window: the boot
 * PROM jumps into AT bus memory at `00090000` to scan for an expansion ROM, and
 * a board that faulted on an empty window would turn "found nothing" into a
 * crash. */
static void test_an_empty_at_bus_window_reads_ff_rather_than_faulting(void) {
  ap_board_t b;
  bool ok = false;
  init(&b);

  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_ATBUS, ap_board_region(&region_board, 0x090000u));
  TEST_ASSERT_EQUAL_HEX8(0xFFu, ap_board_read(&b, 0x090000u, &ok));
  TEST_ASSERT_TRUE(ok);
  ap_board_write(&b, 0x090000u, 0x5Au, &ok);
  TEST_ASSERT_TRUE(ok);

  /* Counted apart from unmapped, because they mean different things: an empty
   * slot answers, an unmapped address does not. */
  TEST_ASSERT_EQUAL_UINT(0u, b.unmapped_reads);
  TEST_ASSERT_EQUAL_UINT(0u, b.unmapped_writes);
  TEST_ASSERT_EQUAL_UINT(1u, b.atbus_empty_reads);
  TEST_ASSERT_EQUAL_UINT(1u, b.atbus_empty_writes);

  /* And *where*, not only how many. C33's rule: a count cannot tell a firmware
   * self-test from a device that is missing, and an address can. */
  TEST_ASSERT_EQUAL_HEX32(0x090000u, b.first_atbus_empty_read);
  TEST_ASSERT_EQUAL_HEX32(0x090000u, b.first_atbus_empty_write);
}

/* The windows must not swallow the devices inside them. The tape, the disk and
 * the display controller all sit within the AT I/O window, so a window checked
 * before them would answer `FF` for every one -- and every device test would
 * still pass, because they call the devices directly. */
static void test_the_windows_do_not_swallow_the_devices_inside_them(void) {
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_TAPE, ap_board_region(&region_board, 0x050000u));
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_DISK, ap_board_region(&region_board, 0x04D000u));
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_DISK, ap_board_region(&region_board, 0x05F800u));
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_GRAPHICS, ap_board_region(&region_board, 0x05D800u));
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_GRAPHICS, ap_board_region(&region_board, 0x05E800u));

  /* And the window still claims what no device does. */
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_ATBUS, ap_board_region(&region_board, 0x040000u));
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_ATBUS, ap_board_region(&region_board, 0x05FFFFu));
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_ATBUS, ap_board_region(&region_board, 0x080000u));
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_ATBUS, ap_board_region(&region_board, 0xFFFFFFu));

  /* And the graphics memories are not empty slots, though both sit inside the
   * AT bus memory window. A window matched before them would report the
   * machine's own frame buffer as an unoccupied expansion slot. */
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_GRAPHICS, ap_board_region(&region_board, 0x0A0000u));
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_GRAPHICS, ap_board_region(&region_board, 0x0BFFFFu));
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_GRAPHICS, ap_board_region(&region_board, 0xFA0000u));
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_GRAPHICS, ap_board_region(&region_board, 0xFDFFFFu));

  /* Between the two windows is neither. */
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_UNMAPPED, ap_board_region(&region_board, 0x070000u));
}

/* The region enum exists to answer "what did the firmware reach for", so a
 * confident wrong name is worse than none. Main memory's *space* ends at the
 * largest RAM a DN3500 takes; above that is unmapped, not memory.
 *
 * Caught by a run whose final PC printed as "main memory" at `FFFF060E`. The
 * read path had refused it correctly all along -- only the name was wrong, and
 * the name is what a reader acts on. */
static void test_main_memory_s_name_stops_where_its_address_space_does(void) {
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_RAM, ap_board_region(&region_board, AP_BOARD_RAM_BASE));
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_RAM, ap_board_region(&region_board, AP_BOARD_RAM_LIMIT));
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_UNMAPPED,
                         ap_board_region(&region_board, AP_BOARD_RAM_LIMIT + 1u));
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_UNMAPPED, ap_board_region(&region_board, 0xFFFF060Eu));

  /* Inside the space but past the memory fitted is still *named* main memory --
   * the address decodes to memory, there is simply no SIMM there -- and the
   * read still refuses it. Same shape as an empty AT bus slot. */
  ap_board_t b;
  bool ok = true;
  init(&b);
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_RAM,
                         ap_board_region(&region_board, AP_BOARD_RAM_BASE + sizeof ram));
  (void)ap_board_read(&b, AP_BOARD_RAM_BASE + sizeof ram, &ok);
  TEST_ASSERT_FALSE(ok);
}

/* All six core-board registers are reachable, not just the four that happen to
 * be contiguous. `ap_boardreg.h` has defined the latch-page register at
 * `011300` and the master request register at `011600` since it was written,
 * and the map routed only `010000-0103FF` -- so two registers existed, had
 * their own tests in `boardreg_suite`, and could not be reached through the
 * machine.
 *
 * That is the failure a contiguous range invites: it looks like it covers a
 * device and silently covers only the contiguous part. The boot PROM's
 * `CLR.B $00011600` bus errored on every pass through its reset path, and each
 * fault drained a frame off a 384-byte supervisor stack until it ran out --
 * 2788 instructions later, and thousands of instructions from the cause. */
static void test_every_core_board_register_is_reachable_through_the_map(void) {
  const uint32_t registers[] = {
      AP_BOARDREG_CPU_STATUS_ADDR,     AP_BOARDREG_CPU_CONTROL_ADDR,
      AP_BOARDREG_CACHE_CONTROL_ADDR,  AP_BOARDREG_TASK_ALIAS_ADDR,
      AP_BOARDREG_LATCH_PAGE_ADDR,     AP_BOARDREG_MASTER_REQUEST_ADDR,
  };
  ap_board_t b;
  bool ok = false;
  init(&b);

  for (unsigned i = 0; i < sizeof registers / sizeof registers[0]; i++) {
    TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_CORE_REGISTER,
                           ap_board_region(&region_board, registers[i]));
    (void)ap_board_read(&b, registers[i], &ok);
    TEST_ASSERT_TRUE(ok);
    ap_board_write(&b, registers[i], 0x00u, &ok);
    TEST_ASSERT_TRUE(ok);
  }
  TEST_ASSERT_EQUAL_UINT(0u, b.unmapped_reads);
  TEST_ASSERT_EQUAL_UINT(0u, b.unmapped_writes);
}

/* The keyboard reaches serial 1 channel A — the port both the oracle's machine
 * configuration and the boot PROM's own poll loop identify. A scan code
 * delivered here is what the firmware's translation table searches for. */
static void test_a_key_press_reaches_serial_one_channel_a(void) {
  ap_board_t b;
  bool ok = false;
  init(&b);
  /* Program the port to the **keyboard's** framing, the way a driver does, and
   * through the registers. Eight bits is not optional: `MR1` resets to a
   * five-bit link and a release code has bit 7 set, so on an unconfigured port
   * `4B` arrives as `0B` and `CB` cannot arrive at all.
   *
   * The rate and parity are not optional either, and that is newer: the
   * keyboard sends **1200 baud 8E1** and does not follow the port, so the two
   * ends have to be told the same thing. Delivering at whatever the port
   * happened to be set to -- which this board did -- is a machine where the
   * cable always agrees. */
  ap_board_write(&b, AP_SIO1_ADDR + (AP_MC68681_MR_A * 2u),
                 AP_SIO_KEYBOARD_MR1, &ok);
  ap_board_write(&b, AP_SIO1_ADDR + (AP_MC68681_SR_CSR_A * 2u),
                 AP_SIO_KEYBOARD_CSR, &ok);
  ap_board_write(&b, AP_SIO1_ADDR + (AP_MC68681_CR_A * 2u), 0x01u, &ok);

  TEST_ASSERT_TRUE(ap_board_key_press(&b, 0x4Bu));

  TEST_ASSERT_EQUAL_HEX8(
      0x4Bu, ap_board_read(&b, AP_SIO1_ADDR + (AP_MC68681_RB_TB_A * 2u), &ok));
  TEST_ASSERT_TRUE(ok);

  /* And the release carries bit 7, which is how the firmware tells them
   * apart. */
  TEST_ASSERT_TRUE(ap_board_key_release(&b, 0x4Bu));
  TEST_ASSERT_EQUAL_HEX8(
      0xCBu, ap_board_read(&b, AP_SIO1_ADDR + (AP_MC68681_RB_TB_A * 2u), &ok));
}

/* A non-transition delivers nothing, and must not reach the port at all — not
 * merely be filtered later. A second press with a byte on the wire would be a
 * key the hardware never reported. */
static void test_a_repeated_press_puts_nothing_on_the_port(void) {
  ap_board_t b;
  bool ok = false;
  init(&b);
  ap_board_write(&b, AP_SIO1_ADDR + (AP_MC68681_MR_A * 2u), 0x07u, &ok);
  ap_board_write(&b, AP_SIO1_ADDR + (AP_MC68681_CR_A * 2u), 0x01u, &ok);

  TEST_ASSERT_TRUE(ap_board_key_press(&b, 0x4Bu));
  (void)ap_board_read(&b, AP_SIO1_ADDR + (AP_MC68681_RB_TB_A * 2u), &ok);

  TEST_ASSERT_FALSE(ap_board_key_press(&b, 0x4Bu));
  /* Nothing waiting. */
  TEST_ASSERT_FALSE(
      (ap_board_read(&b, AP_SIO1_ADDR + (AP_MC68681_SR_CSR_A * 2u), &ok) &
       AP_MC68681_SR_RXRDY) != 0u);
}

static void test_the_boot_prom_region_is_reported_absent(void) {
  ap_board_t b;
  bool ok = true;
  init(&b);

  /* No PROM image is loaded, and the region answers unmapped rather than zero:
   * a machine answering the PROM with zeros looks like one with a blank PROM
   * rather than one without a PROM at all. */
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_PROM, ap_board_region(&region_board, 0x000000u));
  (void)ap_board_read(&b, 0x000000u, &ok);
  TEST_ASSERT_FALSE(ok);
}

static void test_every_region_has_a_name(void) {
  /* The names are what make a trace answer "what did the firmware reach for",
   * which is the question C28 could not. An unnamed region would print as
   * nothing at exactly the moment it mattered. */
  for (unsigned r = 0; r <= AP_BOARD_REGION_RAM; r++) {
    const char *name = ap_board_region_name((ap_board_region_t)r);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_TRUE(strlen(name) > 0u);
  }
}

/* The seven eighths of the translation map's region that no manual describes.
 *
 * `019411-A00` §4.2.1.4 is the only source naming the DS3500 and says nothing
 * about it; `008778-03` §1.2 and Table 2-8 give the range and no decode; the web
 * step found nothing; and the oracle cannot witness it, masking the whole region
 * with `& 0x3ff` and so modelling 1024 entries where the manual gives 128. So
 * the aliasing decode is an assumption, and this counter is what will answer the
 * question if anything real ever touches the region.
 *
 * Zero after a run is the informative answer, not a missing one: it says nothing
 * went anywhere the assumption could be wrong. */
/* ## The map's "undescribed" bytes are entries, and the counter stays
 *
 * Both of these asserted the opposite: that the region's last seven eighths
 * were outside the 128 entries, counted as undescribed, and aliased onto the
 * first eighth. `019411-A00` counts what a *transfer* indexes, not what the map
 * holds, and `SELF_TEST`'s DMA test requires all 1024 words to be distinct.
 *
 * The counter is kept rather than deleted. It answers "did anything touch a
 * part of this region that is not storage", and the answer being permanently
 * "no" is a fact about this map -- a smaller one, or a region that grew, would
 * need it back and would have nowhere to put it.
 */
static void test_the_whole_map_region_is_entries_and_none_are_undescribed(void) {
  ap_board_t b;
  init(&b);

  bool ok = false;
  (void)ap_board_read(&b, AP_ATMAP_BASE + 0x0FFu, &ok);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_UINT(1u, b.region_reads[AP_BOARD_REGION_TRANSLATION_MAP]);

  /* The byte that used to be the first past the entries. */
  (void)ap_board_read(&b, AP_ATMAP_BASE + 0x100u, &ok);
  TEST_ASSERT_TRUE(ok);
  (void)ap_board_read(&b, AP_ATMAP_LIMIT - 1u, &ok);
  TEST_ASSERT_TRUE(ok);
  ap_board_write(&b, AP_ATMAP_LIMIT - 1u, 0x5Au, &ok);
  TEST_ASSERT_TRUE(ok);

  TEST_ASSERT_EQUAL_UINT(0u, b.atmap_undescribed_reads);
  TEST_ASSERT_EQUAL_UINT(0u, b.atmap_undescribed_writes);
}

/* And they do not alias: a write at one offset is not readable at another a
 * quarter of the region away, which is what the aliased model did and what the
 * diagnostic's walk detects. */
static void test_the_map_does_not_alias_within_its_region(void) {
  ap_board_t b;
  init(&b);

  bool ok = false;
  ap_board_write(&b, AP_ATMAP_BASE + 0x001u, 0x5Au, &ok);
  TEST_ASSERT_TRUE(ok);

  const uint8_t elsewhere = ap_board_read(&b, AP_ATMAP_BASE + 0x101u, &ok);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_NOT_EQUAL_HEX8(0x5Au, elsewhere);

  /* And the byte written is still there, so the write went somewhere real. */
  TEST_ASSERT_EQUAL_HEX8(0x5Au, ap_board_read(&b, AP_ATMAP_BASE + 0x001u, &ok));
}

/* The two registers Table 2-8 names and this core declines. Counted apart
 * because they are in different states of evidence: the boot PROMs testify to
 * the master request register -- 29 byte writes across three images and not one
 * read -- and say nothing at all about task alias, which appears at no absolute
 * address in any firmware in hand. A shared counter would hide which of the two
 * a run had touched, which is the only question a run can answer. */
static void test_the_two_declined_registers_are_counted_apart(void) {
  ap_board_t b;
  init(&b);

  bool ok = false;
  (void)ap_board_read(&b, AP_BOARDREG_TASK_ALIAS_ADDR, &ok);
  ap_board_write(&b, AP_BOARDREG_MASTER_REQUEST_ADDR, 0x40u, &ok);
  ap_board_write(&b, AP_BOARDREG_MASTER_REQUEST_ADDR + 0x0FFu, 0x00u, &ok);

  TEST_ASSERT_EQUAL_UINT(1u, b.task_alias_reads);
  TEST_ASSERT_EQUAL_UINT(0u, b.task_alias_writes);
  TEST_ASSERT_EQUAL_UINT(0u, b.master_request_reads);
  /* Both, because Table 2-8 gives each register a 256-byte range and this board
   * aliases within a range -- measured, for every register that could be. */
  TEST_ASSERT_EQUAL_UINT(2u, b.master_request_writes);

  /* A register that is modelled is not counted here, or the counters would
   * report the whole core-register region rather than the declined part of it. */
  ap_board_write(&b, AP_BOARDREG_CPU_CONTROL_ADDR, 0x11u, &ok);
  TEST_ASSERT_EQUAL_UINT(0u, b.task_alias_writes);
  TEST_ASSERT_EQUAL_UINT(2u, b.master_request_writes);
}

/* ---------------------------------------------------------------------------
 * The DS3000's map, `008778-03` Table 2-6
 *
 * A different board, not a shifted one: the device block moves from `010000` to
 * `008000` and *within* it the DMA, interrupt and node-ID placements move again,
 * so no single offset describes the difference.
 * ------------------------------------------------------------------------- */

static ap_board_t ds3000;
static void init_ds3000(void) {
  TEST_ASSERT_TRUE(ap_board_init_model(&ds3000, ram, sizeof ram, &START,
                                       0x012345u, AP_MODEL_DN3000));
}

static void test_the_ds3000_places_its_devices_where_table_two_six_does(void) {
  init_ds3000();
  static const struct {
    uint32_t address;
    ap_board_region_t region;
  } cases[] = {
      {0x000000u, AP_BOARD_REGION_PROM},
      {0x007FFFu, AP_BOARD_REGION_PROM},
      {0x008000u, AP_BOARD_REGION_CORE_REGISTER},
      {0x008100u, AP_BOARD_REGION_CORE_REGISTER},
      {0x008400u, AP_BOARD_REGION_SIO},
      {0x008800u, AP_BOARD_REGION_TIMER},
      {0x008900u, AP_BOARD_REGION_CALENDAR},
      {0x009000u, AP_BOARD_REGION_DMA},
      {0x009100u, AP_BOARD_REGION_DMA},
      {0x009200u, AP_BOARD_REGION_DMA_PAGE},
      {0x009300u, AP_BOARD_REGION_CORE_REGISTER},
      {0x009400u, AP_BOARD_REGION_INTERRUPT},
      {0x009500u, AP_BOARD_REGION_INTERRUPT},
      {0x009600u, AP_BOARD_REGION_NODE_ID},
      {0x100000u, AP_BOARD_REGION_RAM},
      {0x8FFFFFu, AP_BOARD_REGION_RAM},
  };
  for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    TEST_ASSERT_EQUAL_UINT(cases[i].region,
                           ap_board_region(&ds3000, cases[i].address));
  }

  /* And the DN3500's placements are *not* the DS3000's, which is the half that
   * would pass on a map that had simply been copied. */
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_UNMAPPED,
                         ap_board_region(&ds3000, 0x010400u));
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_UNMAPPED,
                         ap_board_region(&ds3000, 0x1000000u));
}

/* §1.2: "The Series 4000, unlike the Series 3000, incorporates an address
 * translation map in its architecture." So a DS3000 does not decode the map's
 * window at all -- its DMA reaches physical memory directly, and the DMA page
 * register is what extends the address instead. */
static void test_the_ds3000_has_no_translation_map(void) {
  init_ds3000();
  TEST_ASSERT_FALSE(ds3000.map->has_translation_map);
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_UNMAPPED,
                         ap_board_region(&ds3000, AP_ATMAP_BASE));
  /* The DN3500 does, which is what makes the absence a property of the model
   * rather than of this test. */
  TEST_ASSERT_TRUE(region_board.map->has_translation_map);
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_TRANSLATION_MAP,
                         ap_board_region(&region_board, AP_ATMAP_BASE));
}

/* §1.3: "In the Series 3000, the virtual address appears to 'wrap' at 26 bits,
 * the five high-order (27:31) bits are simply ignored. The Series 4000 makes
 * use of all virtual address bits."
 *
 * The boot PROM writes `08000000` thirty-eight thousand times; on a machine that
 * kept the bit that is an unmapped write and a fault, and on the real one it is
 * address zero. */
static void test_the_ds3000_ignores_the_five_high_address_bits(void) {
  init_ds3000();
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_PROM,
                         ap_board_region(&ds3000, 0x08000000u));
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_RAM,
                         ap_board_region(&ds3000, 0xF8100000u));

  /* A Series 4000 keeps them, so the same address is nothing there. */
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_UNMAPPED,
                         ap_board_region(&region_board, 0x08000000u));
}

/* Table 2-6 gives the PROM `000000-007FFF`, half what Table 2-8 gives it. An
 * image that does not fit is not this machine's PROM: truncating a 64 KB image
 * into 32 would run whatever happened to be in the first half. */
static void test_the_ds3000_takes_a_32k_prom_and_refuses_a_64k_one(void) {
  init_ds3000();
  static uint8_t image[0x10000];
  TEST_ASSERT_TRUE(ap_board_load_prom(&ds3000, image, 0x8000u));
  TEST_ASSERT_FALSE(ap_board_load_prom(&ds3000, image, 0x10000u));

  /* And the DN3500 takes both. */
  ap_board_t ds4000;
  TEST_ASSERT_TRUE(
      ap_board_init(&ds4000, ram, sizeof ram, &START, 0x012345u));
  TEST_ASSERT_TRUE(ap_board_load_prom(&ds4000, image, 0x10000u));
}

/* The device modules are addressed in the DN3500's space whatever the board's
 * is, and the map translates. So a write to the DS3000's serial port reaches
 * the same register a write to the DN3500's does -- which is the whole reason
 * the placement variance can live in one table. */
static void test_a_ds3000_device_write_reaches_the_same_register(void) {
  init_ds3000();
  ap_board_t ds4000;
  TEST_ASSERT_TRUE(
      ap_board_init(&ds4000, other_ram, sizeof other_ram, &START, 0x012345u));

  bool ok = false;
  /* Register 2 of serial 1 channel A -- the command register -- at each board's
   * own address for it. */
  ap_board_write(&ds3000, 0x008400u + AP_MC68681_CR_A * 2u, 0x05u, &ok);
  TEST_ASSERT_TRUE(ok);
  ap_board_write(&ds4000, AP_SIO1_ADDR + AP_MC68681_CR_A * 2u, 0x05u, &ok);
  TEST_ASSERT_TRUE(ok);

  /* Same effect on the same part: both receivers enabled. */
  TEST_ASSERT_TRUE(ap_sio_receiver_enabled(&ds3000.sio, 0u, 0u));
  TEST_ASSERT_TRUE(ap_sio_receiver_enabled(&ds4000.sio, 0u, 0u));
}

/* The DMA page register: what a machine without a translation map uses to
 * extend a DMA address. Storage only -- Table 2-6 names it and no manual here
 * gives its bits -- and it has to exist because the boot PROM writes it five
 * times before it does anything else. */
static void test_the_dma_page_registers_store(void) {
  init_ds3000();
  bool ok = false;
  ap_board_write(&ds3000, AP_DMAPAGE_ADDR + 7u, 0x5Au, &ok);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_HEX8(0x5Au, ap_board_read(&ds3000, AP_DMAPAGE_ADDR + 7u,
                                              &ok));
  /* Aliased through the block, as every byte-wide range on this board is. */
  TEST_ASSERT_EQUAL_HEX8(0x5Au, ap_board_read(&ds3000, AP_DMAPAGE_ADDR + 0x17u,
                                              &ok));
  /* And on the DN3500 that address is **boot PROM**, because its PROM is 64 KB
   * where the DS3000's is 32 -- the whole of the DS3000's device block lives
   * inside the space the DN3500 gives its firmware. Which is the sharpest way
   * to say these are two boards rather than one board shifted. */
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_PROM,
                         ap_board_region(&region_board, AP_DMAPAGE_ADDR));
}

/* ---- The FPA address space, which must keep faulting ---------------------- */

/* `F8000000`-`FFFFFFFF` is the floating-point accelerator's space, and no FPA
 * is fitted. The oracle carries a handler for exactly this range inside
 * `#if 0` -- `apollo_f8_r`, returning `FFFFFFFF` -- and four commented-out map
 * lines that would install it. It was tried and not kept, and the reason is the
 * one thing that matters here: **it does not raise a bus error**. The catch-all
 * it falls through to instead, `apollo_unmapped_r`, returns the same
 * `FFFFFFFF` *and* calls `apollo_bus_error()`.
 *
 * So the two differ in nothing a data bus can show and everything a machine
 * acts on. The firmware probes this space to find out whether an FPA is there;
 * the fault is the negative answer. Install the quiet handler and a machine
 * with no accelerator reports one, then dispatches floating point to it. */
static void test_the_fpa_space_is_unmapped_on_both_models(void) {
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_UNMAPPED,
                         ap_board_region(&region_board, 0xF8000000u));
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_UNMAPPED,
                         ap_board_region(&region_board, 0xFFFFFFFFu));

  /* And on the DN3000, which reaches the same answer by a different route: its
   * five high-order bits "are simply ignored", so `FFF90000` folds to
   * `07F90000` -- still above everything that board decodes. A mask that
   * happened to land the probe on a device would answer it. */
  ap_board_t dn3000;
  TEST_ASSERT_TRUE(ap_board_init_model(&dn3000, ram, sizeof ram, &START,
                                       0x012345u, AP_MODEL_DN3000));
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_UNMAPPED,
                         ap_board_region(&dn3000, 0xFFF90000u));
}

/* `FFF90000` is the address the firmware actually probes -- the oracle names it
 * "FPA trial access" and silences its log there while still faulting. This core
 * found it independently as the first unmapped read of a PROM boot. */
static void test_the_fpa_trial_access_faults_rather_than_answering(void) {
  ap_board_t b;
  bool ok = true;
  init(&b);

  (void)ap_board_read(&b, 0xFFF90000u, &ok);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_UINT(1u, b.unmapped_reads);
  TEST_ASSERT_EQUAL_HEX32(0xFFF90000u, b.first_unmapped_read);

  /* A write there is refused too. An FPA space that swallowed writes while
   * faulting reads would be a stranger machine than either choice. */
  ok = true;
  ap_board_write(&b, 0xFFF90000u, 0x5Au, &ok);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_UINT(1u, b.unmapped_writes);
}


/* And a port programmed for something else **does not** receive it cleanly.
 * That is the other half of the same fact: the keyboard has one framing, so a
 * driver that mis-programs the port sees the damage rather than the code. A
 * board delivering at the port's own rate could never show this. */
static void test_a_key_press_into_a_mismatched_port_is_damaged(void) {
  ap_board_t b;
  bool ok = false;
  init(&b);
  /* Eight bits and the receiver on, but the **wrong rate** -- 9600 against the
   * keyboard's 1200. */
  ap_board_write(&b, AP_SIO1_ADDR + (AP_MC68681_MR_A * 2u),
                 AP_SIO_KEYBOARD_MR1, &ok);
  ap_board_write(&b, AP_SIO1_ADDR + (AP_MC68681_SR_CSR_A * 2u), 0xBBu, &ok);
  ap_board_write(&b, AP_SIO1_ADDR + (AP_MC68681_CR_A * 2u), 0x01u, &ok);

  TEST_ASSERT_TRUE(ap_board_key_press(&b, 0x4Bu));
  const uint8_t got =
      ap_board_read(&b, AP_SIO1_ADDR + (AP_MC68681_RB_TB_A * 2u), &ok);
  TEST_ASSERT_TRUE(ok);
  /* The byte still enters the FIFO -- the part does not discard it -- but it is
   * not what was sent. */
  TEST_ASSERT_NOT_EQUAL_HEX8(0x4Bu, got);
}

/* ## Three bytes is a transfer size, and the display controller is the reason
 * ## it cannot simply be waved through
 *
 * `[030]` Table 7-2 gives four sizes and `11` is 3 Bytes. The byte loop serves
 * any of them; what needed care is the graphics fast path, which turns an
 * access into the controller's own 16-bit word cycles. A 3-byte transfer is not
 * a whole number of words, and the word loop would have run two cycles and
 * written a byte past the end of the operand.
 */
static void test_a_three_byte_access_is_served_and_round_trips(void) {
  static ap_board_t width_board;
  ap_board_t *b = &width_board;
  init(b);

  TEST_ASSERT_TRUE(ap_board_write_access(b, AP_BOARD_RAM_BASE + 1u, 3u,
                                         0x00ABCDEFu));
  uint32_t value = 0xFFFFFFFFu;
  TEST_ASSERT_TRUE(
      ap_board_read_access(b, AP_BOARD_RAM_BASE + 1u, 3u, &value));
  TEST_ASSERT_EQUAL_HEX32(0x00ABCDEFu, value);

  /* Byte for byte, so a model that shifted the operand into the wrong lanes
   * fails here rather than round-tripping its own mistake. */
  for (unsigned i = 0; i < 3u; i++) {
    bool ok = false;
    const uint8_t byte = ap_board_read(b, AP_BOARD_RAM_BASE + 1u + i, &ok);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(0xABCDEFu >> ((2u - i) * 8u)), byte);
  }

  /* And the neighbours are untouched: three bytes means three. */
  bool ok = false;
  TEST_ASSERT_EQUAL_HEX8(0u, ap_board_read(b, AP_BOARD_RAM_BASE, &ok));
  TEST_ASSERT_EQUAL_HEX8(0u, ap_board_read(b, AP_BOARD_RAM_BASE + 4u, &ok));

  /* Nothing else is a size this part has. */
  TEST_ASSERT_FALSE(ap_board_write_access(b, AP_BOARD_RAM_BASE, 0u, 0u));
  TEST_ASSERT_FALSE(ap_board_write_access(b, AP_BOARD_RAM_BASE, 5u, 0u));
  TEST_ASSERT_FALSE(ap_board_read_access(b, AP_BOARD_RAM_BASE, 5u, &value));
}

int main(void) {
  UNITY_BEGIN();
  init_region_board();
  RUN_TEST(test_a_three_byte_access_is_served_and_round_trips);
  RUN_TEST(test_the_ds3000_places_its_devices_where_table_two_six_does);
  RUN_TEST(test_the_ds3000_has_no_translation_map);
  RUN_TEST(test_the_ds3000_ignores_the_five_high_address_bits);
  RUN_TEST(test_the_ds3000_takes_a_32k_prom_and_refuses_a_64k_one);
  RUN_TEST(test_a_ds3000_device_write_reaches_the_same_register);
  RUN_TEST(test_the_dma_page_registers_store);
  RUN_TEST(test_the_two_declined_registers_are_counted_apart);
  RUN_TEST(test_the_whole_map_region_is_entries_and_none_are_undescribed);
  RUN_TEST(test_the_map_does_not_alias_within_its_region);
  RUN_TEST(test_every_device_lands_in_its_documented_region);
  RUN_TEST(test_an_unclaimed_address_is_unmapped_not_zero);
  RUN_TEST(test_main_memory_is_where_table_two_eight_puts_it);
  RUN_TEST(test_reads_reach_the_devices_themselves);
  RUN_TEST(test_the_read_only_memories_absorb_writes_rather_than_faulting);
  RUN_TEST(test_a_missing_prom_is_absent_for_writes_too);
  RUN_TEST(test_an_empty_at_bus_window_reads_ff_rather_than_faulting);
  RUN_TEST(test_the_windows_do_not_swallow_the_devices_inside_them);
  RUN_TEST(test_main_memory_s_name_stops_where_its_address_space_does);
  RUN_TEST(test_the_fpa_space_is_unmapped_on_both_models);
  RUN_TEST(test_the_fpa_trial_access_faults_rather_than_answering);
  RUN_TEST(test_every_core_board_register_is_reachable_through_the_map);
  RUN_TEST(test_a_key_press_reaches_serial_one_channel_a);
  RUN_TEST(test_a_key_press_into_a_mismatched_port_is_damaged);
  RUN_TEST(test_a_repeated_press_puts_nothing_on_the_port);
  RUN_TEST(test_the_boot_prom_region_is_reported_absent);
  RUN_TEST(test_every_region_has_a_name);
  return UNITY_END();
}

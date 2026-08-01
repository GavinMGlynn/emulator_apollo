/* The DN3500 core board's address map, `008778-03` Table 2-8. */

#include "unity.h"

#include <string.h>

#include "board/ap_board.h"

void setUp(void) {}
void tearDown(void) {}

static const ap_mc146818_time_t START = {
    .year = 1987u, .month = 7u, .day = 31u, .day_of_week = 6u,
    .hour = 21u, .minute = 9u, .second = 21u,
};

static uint8_t ram[0x2000];

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
    TEST_ASSERT_EQUAL_UINT(cases[i].region, ap_board_region(cases[i].address));
  }
}

static void test_an_unclaimed_address_is_unmapped_not_zero(void) {
  ap_board_t b;
  bool ok = true;
  init(&b);

  /* The distinction C28 turned on. Flat RAM made every device address read as
   * zero, which hid thousands of accesses that should have been visible -- an
   * emulator that answers everything cannot say what the firmware wanted. */
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_UNMAPPED, ap_board_region(0x020000u));
  (void)ap_board_read(&b, 0x020000u, &ok);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_UINT(1u, b.unmapped_reads);
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
  TEST_ASSERT_EQUAL_HEX8(0x40u, ap_board_read(&b, 0x050001u, &ok)); /* tape */
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

  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_ATBUS, ap_board_region(0x090000u));
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
}

/* The windows must not swallow the devices inside them. The tape, the disk and
 * the display controller all sit within the AT I/O window, so a window checked
 * before them would answer `FF` for every one -- and every device test would
 * still pass, because they call the devices directly. */
static void test_the_windows_do_not_swallow_the_devices_inside_them(void) {
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_TAPE, ap_board_region(0x050000u));
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_DISK, ap_board_region(0x04D000u));
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_DISK, ap_board_region(0x05F800u));
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_GRAPHICS, ap_board_region(0x05D800u));
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_GRAPHICS, ap_board_region(0x05E800u));

  /* And the window still claims what no device does. */
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_ATBUS, ap_board_region(0x040000u));
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_ATBUS, ap_board_region(0x05FFFFu));
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_ATBUS, ap_board_region(0x080000u));
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_ATBUS, ap_board_region(0xFFFFFFu));

  /* Between the two windows is neither. */
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_UNMAPPED, ap_board_region(0x070000u));
}

static void test_the_boot_prom_region_is_reported_absent(void) {
  ap_board_t b;
  bool ok = true;
  init(&b);

  /* No PROM image is loaded, and the region answers unmapped rather than zero:
   * a machine answering the PROM with zeros looks like one with a blank PROM
   * rather than one without a PROM at all. */
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_REGION_PROM, ap_board_region(0x000000u));
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

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_every_device_lands_in_its_documented_region);
  RUN_TEST(test_an_unclaimed_address_is_unmapped_not_zero);
  RUN_TEST(test_main_memory_is_where_table_two_eight_puts_it);
  RUN_TEST(test_reads_reach_the_devices_themselves);
  RUN_TEST(test_the_read_only_memories_absorb_writes_rather_than_faulting);
  RUN_TEST(test_a_missing_prom_is_absent_for_writes_too);
  RUN_TEST(test_an_empty_at_bus_window_reads_ff_rather_than_faulting);
  RUN_TEST(test_the_windows_do_not_swallow_the_devices_inside_them);
  RUN_TEST(test_the_boot_prom_region_is_reported_absent);
  RUN_TEST(test_every_region_has_a_name);
  return UNITY_END();
}

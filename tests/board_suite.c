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

static void test_the_read_only_memories_refuse_writes(void) {
  ap_board_t b;
  bool ok = true;
  init(&b);

  /* A PROM does not store, and counting the attempt is how a driver writing to
   * one becomes visible rather than appearing to succeed. */
  ap_board_write(&b, 0x000100u, 0x5Au, &ok);
  TEST_ASSERT_FALSE(ok);
  ap_board_write(&b, 0x011202u, 0x5Au, &ok);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_UINT(2u, b.unmapped_writes);

  /* The node ID still reads what it was given. */
  TEST_ASSERT_EQUAL_HEX8(0x01u, ap_board_read(&b, 0x011202u, &ok));
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
  RUN_TEST(test_the_read_only_memories_refuse_writes);
  RUN_TEST(test_the_boot_prom_region_is_reported_absent);
  RUN_TEST(test_every_region_has_a_name);
  return UNITY_END();
}

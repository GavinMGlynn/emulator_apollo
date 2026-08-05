/* The memory array's parity circuit, and the boot PROM's self-test 7.
 *
 * Every figure here comes from a page: `008778-03` §3.2 for the level 7
 * autovectored interrupt and what a status write does to it, §3.3 for the four
 * F280 checkers that make the lane field four bits wide, `019411-A00` §4.2.1
 * for the Clear Parity Error Flag location. The one thing with no source is
 * which lane bit is which byte, and `ap_parity.h` says so; nothing here asserts
 * an assignment beyond "each byte of a longword gets a different bit". */

#include "unity.h"

#include <string.h>

#include "board/ap_board.h"
#include "board/ap_parity.h"

void setUp(void) {}
void tearDown(void) {}

#define RAM_BYTES 0x10000u
#define TEST_ADDR (AP_BOARD_RAM_BASE + 0xA000u)

static uint8_t ram[RAM_BYTES];
static uint8_t parity_ram[RAM_BYTES / 8u];
static ap_board_t board;

static const ap_mc146818_time_t EPOCH = {
    .year = 1987u, .month = 7u, .day = 31u, .day_of_week = 6u,
    .hour = 21u, .minute = 9u, .second = 21u,
};

static void build(void) {
  memset(ram, 0, sizeof ram);
  TEST_ASSERT_TRUE(ap_board_init(&board, ram, RAM_BYTES, &EPOCH, 0x012345u));
  TEST_ASSERT_TRUE(
      ap_board_attach_parity(&board, parity_ram, sizeof parity_ram));
}

/* What self-test 7 does, in the order it does it: force bad parity on, write
 * the longword, force off, clear the status register, enable the interrupt. */
static void control(uint16_t value) {
  ap_boardreg_write8(&board.registers, AP_BOARDREG_CPU_CONTROL_ADDR + 1u,
                     (uint8_t)value);
}

static void write_byte(uint32_t address, uint8_t value) {
  bool ok = false;
  ap_board_write(&board, address, value, &ok);
  TEST_ASSERT_TRUE(ok);
}

static uint8_t read_byte(uint32_t address) {
  bool ok = false;
  const uint8_t value = ap_board_read(&board, address, &ok);
  TEST_ASSERT_TRUE(ok);
  return value;
}

/* ## The parity RAM is fitted, or it is not
 *
 * The core allocates nothing, so a caller supplies the store as it supplies
 * main memory. One bit per byte, and a store too small for the array is refused
 * rather than covering part of it -- parity that stops halfway through memory
 * describes no memory board.
 */
static void test_a_parity_store_smaller_than_the_array_is_refused(void) {
  memset(ram, 0, sizeof ram);
  TEST_ASSERT_TRUE(ap_board_init(&board, ram, RAM_BYTES, &EPOCH, 0x012345u));

  TEST_ASSERT_FALSE(
      ap_board_attach_parity(&board, parity_ram, RAM_BYTES / 8u - 1u));
  TEST_ASSERT_TRUE(ap_board_attach_parity(&board, parity_ram, RAM_BYTES / 8u));
}

/* ## A forced write, then a read: the check fails and the data still arrives
 *
 * The F280s sit beside the array and not in front of it, so a parity error does
 * not withhold the byte. Self-test 7 depends on exactly that -- it reads the
 * longword into `d0` and its failure reporter prints what it read.
 */
static void test_a_forced_write_fails_its_check_and_still_returns_the_byte(void) {
  build();

  control(AP_BOARDREG_CONTROL_FORCE_BAD_PARITY);
  write_byte(TEST_ADDR, 0x5Au);
  control(0u);

  TEST_ASSERT_EQUAL_UINT(1u, board.parity.forced_writes);
  TEST_ASSERT_EQUAL_UINT(0u, board.parity.errors);

  TEST_ASSERT_EQUAL_HEX8(0x5Au, read_byte(TEST_ADDR));
  TEST_ASSERT_EQUAL_UINT(1u, board.parity.errors);
}

/* ## An ordinary write regenerates parity, which is the whole of the clearing
 *
 * The firmware never tells the hardware to un-force a byte: its handler simply
 * writes the location again at `00749A`. Parity is generated on every write, so
 * writing correct data is writing correct parity.
 */
static void test_an_ordinary_write_regenerates_parity(void) {
  build();

  control(AP_BOARDREG_CONTROL_FORCE_BAD_PARITY);
  write_byte(TEST_ADDR, 0x00u);
  control(0u);
  TEST_ASSERT_TRUE(ap_parity_check(&board.parity, TEST_ADDR -
                                                      AP_BOARD_RAM_BASE));

  write_byte(TEST_ADDR, 0x00u);
  TEST_ASSERT_FALSE(ap_parity_check(&board.parity, TEST_ADDR -
                                                       AP_BOARD_RAM_BASE));
}

/* ## The status register takes the lane's bit, and the latch takes the page
 *
 * `019411-A00` §4.2.1.4 gives the address translation map entry as a physical
 * page number, bits `<25:10>`; the latch-page register holds the same field for
 * the address that failed. Which of bits 4-7 belongs to which byte is
 * `PROVISIONAL` and not asserted here -- what is asserted is that the four
 * bytes of a longword take four *different* bits, which is what "one checker
 * per byte lane" means and is true under any assignment.
 */
static void test_a_parity_error_names_its_lane_and_latches_its_page(void) {
  build();

  control(AP_BOARDREG_CONTROL_FORCE_BAD_PARITY);
  for (uint32_t i = 0; i < 4u; i++) {
    write_byte(TEST_ADDR + i, 0x00u);
  }
  control(0u);
  ap_boardreg_write16(&board.registers, AP_BOARDREG_CPU_STATUS_ADDR, 0x0000);

  uint16_t seen = 0;
  for (uint32_t i = 0; i < 4u; i++) {
    (void)read_byte(TEST_ADDR + i);
    const uint16_t bit = ap_parity_lane_bit(TEST_ADDR + i - AP_BOARD_RAM_BASE);
    /* A bit inside the four-bit field, and one no earlier byte has claimed. */
    TEST_ASSERT_EQUAL_HEX16(bit, (uint16_t)(bit &
                                            AP_BOARDREG_STATUS_PARITY_MASK));
    TEST_ASSERT_EQUAL_HEX16(0u, (uint16_t)(bit & seen));
    seen |= bit;
    TEST_ASSERT_EQUAL_HEX16(seen,
                            (uint16_t)(board.registers.cpu_status &
                                       AP_BOARDREG_STATUS_PARITY_MASK));
  }
  TEST_ASSERT_EQUAL_HEX16(AP_BOARDREG_STATUS_PARITY_MASK, seen);

  TEST_ASSERT_EQUAL_HEX16((uint16_t)(TEST_ADDR >> 10),
                          board.registers.latch_page_on_parity);
}

/* ## Level 7, and only while the control register enables it
 *
 * `008778-03` §3.2: "The parity error interrupt is a non-maskable interrupt to
 * the CPU. It generates a Level 7 interrupt to the CPU."
 */
static void test_a_parity_error_raises_level_seven_only_when_enabled(void) {
  build();

  control(AP_BOARDREG_CONTROL_FORCE_BAD_PARITY);
  write_byte(TEST_ADDR, 0x00u);

  /* Interrupt disabled: the status bit is set and the line is not. */
  control(0u);
  (void)read_byte(TEST_ADDR);
  TEST_ASSERT_TRUE((board.registers.cpu_status &
                    AP_BOARDREG_STATUS_PARITY_MASK) != 0u);
  TEST_ASSERT_FALSE(ap_board_parity_interrupt(&board));
  TEST_ASSERT_EQUAL_UINT(0u, ap_board_interrupt_level(&board));

  control(AP_BOARDREG_CONTROL_INTERRUPT_ENABLE);
  TEST_ASSERT_TRUE(ap_board_parity_interrupt(&board));
  TEST_ASSERT_EQUAL_UINT(AP_BOARD_PARITY_LEVEL,
                         ap_board_interrupt_level(&board));
}

/* ## Two ways to drop it, and neither is wired for separately
 *
 * `008778-03` §3.2 -- "Writing to the status register clears the interrupt
 * status" -- and `019411-A00`'s Clear Parity Error Flag at `016406`. The
 * request is derived from the two registers rather than latched, so both work
 * by themselves.
 */
static void test_the_interrupt_is_a_level_that_either_clear_drops(void) {
  for (unsigned by_location = 0; by_location < 2u; by_location++) {
    build();
    control(AP_BOARDREG_CONTROL_FORCE_BAD_PARITY);
    write_byte(TEST_ADDR, 0x00u);
    control(AP_BOARDREG_CONTROL_INTERRUPT_ENABLE);
    (void)read_byte(TEST_ADDR);
    TEST_ASSERT_TRUE(ap_board_parity_interrupt(&board));

    if (by_location) {
      ap_boardreg_write16(&board.registers,
                          AP_BOARDREG_SELECTIVE_CLEAR_ADDR +
                              AP_BOARDREG_CLEAR_PARITY_OFFSET,
                          0x0000);
    } else {
      ap_boardreg_write16(&board.registers, AP_BOARDREG_CPU_STATUS_ADDR,
                          0x0000);
    }
    TEST_ASSERT_FALSE(ap_board_parity_interrupt(&board));
  }
}

/* ## The lane bits are active low on this family and not on the DN3000's
 *
 * Settled from firmware rather than from the oracle: both DN3000 PROMs write
 * `F8` to force bad parity on all four lanes and the three Series 4000 PROMs
 * write `08`, and each then requires all four status bits back.
 */
static void test_the_lane_bits_are_inverted_on_this_family_only(void) {
  ap_boardreg_t regs;

  ap_boardreg_init(&regs); /* the DN3500's, the reference superset */
  regs.cpu_control = 0x0008u;
  TEST_ASSERT_EQUAL_HEX16(0x00F0u, ap_boardreg_forced_lanes(&regs));
  regs.cpu_control = 0x00F8u;
  TEST_ASSERT_EQUAL_HEX16(0x0000u, ap_boardreg_forced_lanes(&regs));

  ap_boardreg_set_active_low_lanes(&regs, false); /* a Series 3000 */
  regs.cpu_control = 0x00F8u;
  TEST_ASSERT_EQUAL_HEX16(0x00F0u, ap_boardreg_forced_lanes(&regs));
  regs.cpu_control = 0x0008u;
  TEST_ASSERT_EQUAL_HEX16(0x0000u, ap_boardreg_forced_lanes(&regs));

  /* And bit 3 is the gate either way: without it the lane bits say nothing. */
  ap_boardreg_init(&regs);
  regs.cpu_control = 0x0001u;
  TEST_ASSERT_EQUAL_HEX16(0x0000u, ap_boardreg_forced_lanes(&regs));
}

/* ## A board with no parity RAM fitted says so
 *
 * It cannot fail a check, which is a describable machine and not a pass. The
 * count of writes it could not store is what keeps a self-test from passing for
 * the wrong reason.
 */
static void test_a_board_without_parity_ram_counts_what_it_cannot_do(void) {
  memset(ram, 0, sizeof ram);
  TEST_ASSERT_TRUE(ap_board_init(&board, ram, RAM_BYTES, &EPOCH, 0x012345u));

  control(AP_BOARDREG_CONTROL_FORCE_BAD_PARITY);
  write_byte(TEST_ADDR, 0x5Au);
  control(AP_BOARDREG_CONTROL_INTERRUPT_ENABLE);

  TEST_ASSERT_EQUAL_HEX8(0x5Au, read_byte(TEST_ADDR));
  TEST_ASSERT_EQUAL_UINT(1u, board.parity.forced_writes);
  TEST_ASSERT_EQUAL_UINT(1u, board.parity.unstorable_writes);
  TEST_ASSERT_EQUAL_UINT(0u, board.parity.errors);
  TEST_ASSERT_FALSE(ap_board_parity_interrupt(&board));
}

/* ## The LED byte and the parity byte are different halves
 *
 * `008778-03` §3.7 puts the diagnostic LEDs in the control register's *upper*
 * byte, and the firmware writes them at `010100` and the parity control at
 * `010101`. Treating the two addresses alike put the parity test's `08`, `00`
 * and `01` into the boot's posted-code list, where they read as diagnostic
 * codes.
 */
static void test_the_led_byte_and_the_parity_byte_do_not_overwrite_each_other(void) {
  ap_boardreg_t regs;
  ap_boardreg_init(&regs);

  ap_boardreg_write8(&regs, AP_BOARDREG_CPU_CONTROL_ADDR, 0x8Fu);
  ap_boardreg_write8(&regs, AP_BOARDREG_CPU_CONTROL_ADDR + 1u, 0x08u);

  TEST_ASSERT_EQUAL_HEX16(0x8F08u,
                          ap_boardreg_read16(&regs,
                                             AP_BOARDREG_CPU_CONTROL_ADDR));
  /* Only the LED write was posted. */
  TEST_ASSERT_EQUAL_UINT(1u, regs.posted_count);
  TEST_ASSERT_EQUAL_HEX8(0x8Fu, regs.posted[0]);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_parity_store_smaller_than_the_array_is_refused);
  RUN_TEST(test_a_forced_write_fails_its_check_and_still_returns_the_byte);
  RUN_TEST(test_an_ordinary_write_regenerates_parity);
  RUN_TEST(test_a_parity_error_names_its_lane_and_latches_its_page);
  RUN_TEST(test_a_parity_error_raises_level_seven_only_when_enabled);
  RUN_TEST(test_the_interrupt_is_a_level_that_either_clear_drops);
  RUN_TEST(test_the_lane_bits_are_inverted_on_this_family_only);
  RUN_TEST(test_a_board_without_parity_ram_counts_what_it_cannot_do);
  RUN_TEST(test_the_led_byte_and_the_parity_byte_do_not_overwrite_each_other);
  return UNITY_END();
}

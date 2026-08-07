/* Apollo calendar as the board wires it. Placement and aliasing are measured;
 * `FINDINGS.md` C12 and `ap_calendar.h` record the evidence. */

#include "unity.h"

#include "board/ap_calendar.h"
#include "board/ap_intr.h"

void setUp(void) {}
void tearDown(void) {}

static const ap_mc146818_time_t START = {
    .year = 1987u, .month = 7u, .day = 31u, .day_of_week = 6u,
    .hour = 21u, .minute = 9u, .second = 21u,
};

static void test_the_calendar_is_byte_consecutive_unlike_the_timer(void) {
  uint8_t reg;

  /* Stride 1, not the timer's odd-address stride 2. Two byte-wide peripherals
   * side by side with different placements, so neither could be inferred from
   * the other. */
  TEST_ASSERT_TRUE(ap_calendar_decode(0x010900u, &reg));
  TEST_ASSERT_EQUAL_UINT(0u, reg);
  TEST_ASSERT_TRUE(ap_calendar_decode(0x010901u, &reg));
  TEST_ASSERT_EQUAL_UINT(1u, reg);
  TEST_ASSERT_TRUE(ap_calendar_decode(0x010908u, &reg));
  TEST_ASSERT_EQUAL_UINT(AP_MC146818_MONTH, reg);

  TEST_ASSERT_FALSE(ap_calendar_decode(0x010800u, &reg)); /* the timer */
  TEST_ASSERT_FALSE(ap_calendar_decode(0x010A00u, &reg));
}

static void test_the_registers_alias_through_the_range(void) {
  ap_calendar_t calendar;
  TEST_ASSERT_TRUE(ap_calendar_reset(&calendar, &START));

  /* Measured: the month reads alike at `+08`, `+48`, `+88` and `+C8`, and a
   * RAM byte written at `+10` reappears at `+50` and `+90`. */
  uint8_t month = ap_calendar_read(&calendar, AP_CALENDAR_ADDR + 0x08u);
  TEST_ASSERT_EQUAL_HEX8(0x07, month);
  TEST_ASSERT_EQUAL_HEX8(month, ap_calendar_read(&calendar, AP_CALENDAR_ADDR + 0x48u));
  TEST_ASSERT_EQUAL_HEX8(month, ap_calendar_read(&calendar, AP_CALENDAR_ADDR + 0x88u));
  TEST_ASSERT_EQUAL_HEX8(month, ap_calendar_read(&calendar, AP_CALENDAR_ADDR + 0xC8u));

  ap_calendar_write(&calendar, AP_CALENDAR_ADDR + 0x10u, 0x5A);
  TEST_ASSERT_EQUAL_HEX8(0x5A, ap_calendar_read(&calendar, AP_CALENDAR_ADDR + 0x50u));
  TEST_ASSERT_EQUAL_HEX8(0x5A, ap_calendar_read(&calendar, AP_CALENDAR_ADDR + 0x90u));
}

static void test_the_dumped_register_layout_is_reproduced(void) {
  ap_calendar_t calendar;
  TEST_ASSERT_TRUE(ap_calendar_reset(&calendar, &START));

  /* The dump that identified the stride, reproduced from this core: the first
   * ten bytes read as a coherent clock, with the alarm bytes zero. If this
   * stops holding, the placement the code was derived from is no longer what
   * the code implements. */
  static const uint8_t expected[10] = {0x21, 0x00, 0x09, 0x00, 0x89,
                                       0x00, 0x06, 0x31, 0x07, 0x87};
  for (unsigned i = 0; i < 10; i++) {
    TEST_ASSERT_EQUAL_HEX8(expected[i],
                           ap_calendar_read(&calendar, AP_CALENDAR_ADDR + i));
  }
}

static void test_the_calendar_raises_the_first_slave_interrupt(void) {
  ap_calendar_t calendar;
  ap_intr_t intr;
  TEST_ASSERT_TRUE(ap_calendar_reset(&calendar, &START));
  ap_intr_reset(&intr);

  /* The firmware's own initialization, then unmasked as a driver would. */
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 0u, 0x11);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 1u, 0xA0);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 1u, 0x08);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 1u, 0x01);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 1u, 0x00);
  ap_intr_write(&intr, AP_INTR_SLAVE_ADDR + 0u, 0x11);
  ap_intr_write(&intr, AP_INTR_SLAVE_ADDR + 1u, 0xA8);
  ap_intr_write(&intr, AP_INTR_SLAVE_ADDR + 1u, 0x03);
  ap_intr_write(&intr, AP_INTR_SLAVE_ADDR + 1u, 0x01);
  ap_intr_write(&intr, AP_INTR_SLAVE_ADDR + 1u, 0x00);

  /* Update-ended interrupt enabled, twenty-four hour. */
  ap_calendar_write(&calendar, AP_CALENDAR_ADDR + AP_MC146818_REGISTER_B,
                    AP_MC146818_B_24HOUR | AP_MC146818_B_UIE);
  ap_calendar_advance(&calendar, AP_TIME_BASE_HZ);
  TEST_ASSERT_TRUE(ap_calendar_irq(&calendar));

  ap_intr_set_request(&intr, AP_CALENDAR_IRQ, ap_calendar_irq(&calendar));
  TEST_ASSERT_TRUE(ap_intr_pending(&intr));

  /* Vector `A8`: the slave's measured base plus its line 0. `008778-03`
   * Table 2-3 gives the calendar IRQ8, the first slave line. */
  TEST_ASSERT_EQUAL_HEX8(0xA8, ap_intr_acknowledge(&intr));
}

static void test_the_calendar_outranks_every_line_below_the_cascade(void) {
  ap_calendar_t calendar;
  ap_intr_t intr;
  TEST_ASSERT_TRUE(ap_calendar_reset(&calendar, &START));
  ap_intr_reset(&intr);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 0u, 0x11);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 1u, 0xA0);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 1u, 0x08);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 1u, 0x01);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 1u, 0x00);
  ap_intr_write(&intr, AP_INTR_SLAVE_ADDR + 0u, 0x11);
  ap_intr_write(&intr, AP_INTR_SLAVE_ADDR + 1u, 0xA8);
  ap_intr_write(&intr, AP_INTR_SLAVE_ADDR + 1u, 0x03);
  ap_intr_write(&intr, AP_INTR_SLAVE_ADDR + 1u, 0x01);
  ap_intr_write(&intr, AP_INTR_SLAVE_ADDR + 1u, 0x00);

  /* Table 2-3 puts the calendar at 4+1 and IRQ4 at 5, so the calendar wins --
   * which is only true because the cascade is on IR3. */
  ap_intr_set_request(&intr, 4u, false);
  ap_intr_set_request(&intr, 4u, true);
  ap_intr_set_request(&intr, AP_CALENDAR_IRQ, true);
  TEST_ASSERT_EQUAL_HEX8(0xA8, ap_intr_acknowledge(&intr));
}

/* `002398-04` p. 12-3's configuration table, asserted as a layout: contiguous
 * from the RAM's first byte, in the order the handbook prints it, ending where
 * the part does.
 *
 * The field that matters is `VALID PATTERN` at `12`. The boot PROM was measured
 * making exactly one calendar access in a hundred million instructions -- a
 * 32-bit read at `010912` -- and the handbook independently puts a four-byte
 * validity field at that offset. This pins the two together, so a later edit
 * that moves either has to face the other. */
static void test_the_configuration_table_is_the_handbooks_layout(void) {
  /* The battery RAM starts where the four registers end, and the table starts
   * with it: there is no gap for the checksum to sit above. */
  TEST_ASSERT_EQUAL_HEX8(AP_MC146818_RAM_BASE, AP_CALENDAR_CONFIG_CHECKSUM);

  /* Field widths, as the ranges the handbook prints: 0E-11, 12-15, 16-1D,
   * 1E-21, 22-25, then three single bytes. */
  TEST_ASSERT_EQUAL_HEX8(4u, AP_CALENDAR_CONFIG_VALID_PATTERN -
                                 AP_CALENDAR_CONFIG_CHECKSUM);
  TEST_ASSERT_EQUAL_HEX8(4u, AP_CALENDAR_CONFIG_MEM_BOARD_ARRAY -
                                 AP_CALENDAR_CONFIG_VALID_PATTERN);
  TEST_ASSERT_EQUAL_HEX8(8u, AP_CALENDAR_CONFIG_NODEID -
                                 AP_CALENDAR_CONFIG_MEM_BOARD_ARRAY);
  TEST_ASSERT_EQUAL_HEX8(4u, AP_CALENDAR_CONFIG_DEV_BIT_ARRAY -
                                 AP_CALENDAR_CONFIG_NODEID);
  TEST_ASSERT_EQUAL_HEX8(4u, AP_CALENDAR_CONFIG_RING_TYPE -
                                 AP_CALENDAR_CONFIG_DEV_BIT_ARRAY);
  TEST_ASSERT_EQUAL_HEX8(1u, AP_CALENDAR_CONFIG_DISP_TYPE -
                                 AP_CALENDAR_CONFIG_RING_TYPE);
  TEST_ASSERT_EQUAL_HEX8(1u, AP_CALENDAR_CONFIG_DISK_TYPE -
                                 AP_CALENDAR_CONFIG_DISP_TYPE);
  TEST_ASSERT_EQUAL_HEX8(1u, AP_CALENDAR_CONFIG_UNUSED -
                                 AP_CALENDAR_CONFIG_DISK_TYPE);
  /* And `29-3F` is the last of the sixty-four registers. */
  TEST_ASSERT_EQUAL_HEX8(AP_MC146818_BYTES, AP_CALENDAR_CONFIG_UNUSED + 0x17u);

  /* The address the PROM was measured reading is the validity field's. */
  TEST_ASSERT_EQUAL_HEX32(0x00010912u, AP_CALENDAR_ADDR +
                                           AP_CALENDAR_CONFIG_VALID_PATTERN);
}

/* Nothing has been written into the table, and that is the state to hold on to
 * until the valid pattern's *value* comes from somewhere. A blank battery RAM
 * is a real machine whose battery has died, and it is what the PROM is
 * complaining about -- inventing a pattern to quiet it would be inventing the
 * machine's identity. */
static void test_a_reset_calendar_leaves_the_configuration_blank(void) {
  ap_calendar_t calendar;
  TEST_ASSERT_TRUE(ap_calendar_reset(&calendar, &START));
  for (uint8_t reg = AP_CALENDAR_CONFIG_CHECKSUM; reg < AP_MC146818_BYTES;
       reg++) {
    TEST_ASSERT_EQUAL_HEX8(0u, ap_calendar_read(&calendar,
                                                AP_CALENDAR_ADDR + reg));
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_configuration_table_is_the_handbooks_layout);
  RUN_TEST(test_a_reset_calendar_leaves_the_configuration_blank);
  RUN_TEST(test_the_calendar_is_byte_consecutive_unlike_the_timer);
  RUN_TEST(test_the_registers_alias_through_the_range);
  RUN_TEST(test_the_dumped_register_layout_is_reproduced);
  RUN_TEST(test_the_calendar_raises_the_first_slave_interrupt);
  RUN_TEST(test_the_calendar_outranks_every_line_below_the_cascade);
  return UNITY_END();
}

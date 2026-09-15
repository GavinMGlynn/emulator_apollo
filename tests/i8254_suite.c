/* Intel 8254 programmable interval timer, `[8254]` (1983 Intel Microprocessors
 * and Peripherals Handbook, ch. 6).
 *
 * The acceptance test for this part is the ring controller's own firmware:
 * `RING.md` finding 41 identifies two 8254s on the board from the exact
 * sequence the ROM writes, and that sequence is the first test below. The rest
 * follow the chapter, walked whole in `docs/references/I8254_WALK.md`, one per
 * mode figure. */

#include "unity.h"

#include "device/ap_i8254.h"

void setUp(void) {}
void tearDown(void) {}

static void write_pair(ap_i8254_t *pit, unsigned index, uint16_t count) {
  ap_i8254_write(pit, (ap_i8254_reg_t)index, (uint8_t)(count & 0xFFu));
  ap_i8254_write(pit, (ap_i8254_reg_t)index, (uint8_t)(count >> 8));
}

/* A figure's row of count values and OUT levels, one per CLK pulse. */
static void expect_pulses(ap_i8254_t *pit, unsigned index, unsigned pulses,
                          const uint16_t *counts, const bool *outs) {
  for (unsigned i = 0; i < pulses; i++) {
    ap_i8254_clock_counter(pit, index);
    TEST_ASSERT_EQUAL_HEX16(counts[i], pit->counter[index].counter);
    TEST_ASSERT_EQUAL_INT((int)outs[i], (int)ap_i8254_out(pit, index));
  }
}

/* The ring ROM's own initialisation, which is what identified the part.
 *
 * `$30`, `$70`, `$B0` are counters 0, 1 and 2, each "LSB then MSB", each mode
 * 0. `$E4` is the read-back command latching **status** for counter 1 -- a
 * command the 8253 does not have, which is what makes the identification a
 * confirmation rather than a guess. The firmware then tests the NULL COUNT bit
 * of what it reads back. */
static void test_the_ring_firmwares_own_sequence(void) {
  ap_i8254_t pit;
  ap_i8254_reset(&pit);

  ap_i8254_write(&pit, AP_I8254_CONTROL, 0x30u);
  ap_i8254_write(&pit, AP_I8254_CONTROL, 0x70u);
  ap_i8254_write(&pit, AP_I8254_CONTROL, 0xB0u);

  for (unsigned i = 0; i < AP_I8254_COUNTERS; i++) {
    TEST_ASSERT_EQUAL_UINT(0u, ap_i8254_mode(&pit, i));
    /* "Write to the control word register: NULL COUNT = 1." */
    TEST_ASSERT_TRUE(pit.counter[i].null_count);
  }

  /* Read back counter 1's status: `E4` is 11 100 100 -- read-back, D5 set so no
   * count, D4 clear so status, D2 set for counter 1. */
  ap_i8254_write(&pit, AP_I8254_CONTROL, 0xE4u);
  const uint8_t status = ap_i8254_read(&pit, AP_I8254_COUNTER_1);
  TEST_ASSERT_EQUAL_HEX8(AP_I8254_STATUS_NULL_COUNT,
                         status & AP_I8254_STATUS_NULL_COUNT);
  /* And the low six bits are the control word as written: `70 & 3F` = `30`. */
  TEST_ASSERT_EQUAL_HEX8(0x30u, status & 0x3Fu);
}

/* Figure 12, exactly: NULL COUNT goes to 0 on "new count is loaded into CE (CR
 * -> CE)" -- which is the next CLK pulse, not the write. And for a two-byte
 * count it "goes to 1 when the second byte is written", so the first byte moves
 * nothing but counting. */
static void test_null_count_clears_only_when_the_count_reaches_the_element(
    void) {
  ap_i8254_t pit;
  ap_i8254_reset(&pit);

  ap_i8254_write(&pit, AP_I8254_CONTROL, 0x30u); /* counter 0, LSB then MSB */
  TEST_ASSERT_TRUE(pit.counter[0].null_count);

  ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x34u); /* first byte */
  TEST_ASSERT_TRUE(pit.counter[0].null_count);
  /* "Writing the first byte disables counting." */
  TEST_ASSERT_FALSE(pit.counter[0].counting);

  ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x12u); /* second byte */
  /* In CR, not yet in CE. */
  TEST_ASSERT_TRUE(pit.counter[0].null_count);
  TEST_ASSERT_FALSE(pit.counter[0].counting);

  ap_i8254_clock_counter(&pit, 0u);
  TEST_ASSERT_FALSE(pit.counter[0].null_count);
  TEST_ASSERT_TRUE(pit.counter[0].counting);
  TEST_ASSERT_EQUAL_HEX16(0x1234u, pit.counter[0].counter);
}

/* **Mode 0, Figure 15's first row.** "After the Control Word and initial count
 * are written to a Counter, the initial count will be loaded on the next CLK
 * pulse. This CLK pulse does not decrement the count, so for an initial count
 * of N, OUT does not go high until N + 1 CLK pulses after the initial count is
 * written." CW = 10, LSB = 4: 4 3 2 1 0 FFFF FFFE, OUT high from the 0 on --
 * "the Counter does not stop when it reaches zero" (p. 6-161). */
static void test_a_count_is_loaded_on_the_next_clk_pulse_which_does_not_decrement_it(
    void) {
  ap_i8254_t pit;
  ap_i8254_reset(&pit);
  ap_i8254_write(&pit, AP_I8254_CONTROL, 0x10u); /* counter 0, LSB, mode 0 */
  TEST_ASSERT_FALSE(ap_i8254_out(&pit, 0u));
  ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x04u);

  static const uint16_t counts[] = {4u, 3u, 2u, 1u, 0u, 0xFFFFu, 0xFFFEu};
  static const bool outs[] = {false, false, false, false, true, true, true};
  expect_pulses(&pit, 0u, 7u, counts, outs);
}

/* Mode 0, Figure 15's second row: "If an initial count is written while GATE =
 * 0, it will still be loaded on the next CLK pulse. When GATE goes high, OUT
 * will go high N CLK pulses later; no CLK pulse is needed to load the Counter
 * as this has already been done." */
static void test_a_count_written_with_the_gate_low_is_still_loaded(void) {
  ap_i8254_t pit;
  ap_i8254_reset(&pit);
  ap_i8254_write(&pit, AP_I8254_CONTROL, 0x10u);
  ap_i8254_set_gate(&pit, 0u, false);
  ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x03u);

  ap_i8254_clock_counter(&pit, 0u);
  TEST_ASSERT_EQUAL_HEX16(3u, pit.counter[0].counter);
  TEST_ASSERT_FALSE(pit.counter[0].null_count);
  ap_i8254_clock_counter(&pit, 0u);
  TEST_ASSERT_EQUAL_HEX16(3u, pit.counter[0].counter);

  ap_i8254_set_gate(&pit, 0u, true);
  static const uint16_t counts[] = {2u, 1u, 0u};
  static const bool outs[] = {false, false, true};
  expect_pulses(&pit, 0u, 3u, counts, outs);
}

/* Mode 0's two-byte rewrite: "1) Writing the first byte disables counting. OUT
 * is set low immediately (no clock pulse required) 2) Writing the second byte
 * allows the new count to be loaded on the next CLK pulse." And OUT "remains
 * high until a new count or a new Mode 0 Control Word is written". */
static void test_the_first_byte_of_a_mode_zero_count_stops_it_and_sets_out_low(
    void) {
  ap_i8254_t pit;
  ap_i8254_reset(&pit);
  ap_i8254_write(&pit, AP_I8254_CONTROL, 0x30u);
  write_pair(&pit, 0u, 2u);
  for (unsigned i = 0; i < 3u; i++) {
    ap_i8254_clock_counter(&pit, 0u);
  }
  TEST_ASSERT_TRUE(ap_i8254_out(&pit, 0u));

  ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x05u);
  TEST_ASSERT_FALSE(ap_i8254_out(&pit, 0u));
  ap_i8254_clock_counter(&pit, 0u);
  TEST_ASSERT_EQUAL_HEX16(0u, pit.counter[0].counter); /* held */

  ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x00u);
  static const uint16_t counts[] = {5u, 4u};
  static const bool outs[] = {false, false};
  expect_pulses(&pit, 0u, 2u, counts, outs);
}

/* p. 6-152: "CR_M and CR_L are cleared when the Counter is programmed. In this
 * way, if the Counter has been programmed for one byte counts ... the other
 * byte will be zero." */
static void test_a_control_word_clears_the_count_register(void) {
  ap_i8254_t pit;
  ap_i8254_reset(&pit);
  ap_i8254_write(&pit, AP_I8254_CONTROL, 0x30u);
  write_pair(&pit, 0u, 0x1234u);
  ap_i8254_clock_counter(&pit, 0u);
  TEST_ASSERT_EQUAL_HEX16(0x1234u, pit.counter[0].counter);

  ap_i8254_write(&pit, AP_I8254_CONTROL, 0x10u); /* LSB only */
  ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x05u);
  ap_i8254_clock_counter(&pit, 0u);
  TEST_ASSERT_EQUAL_HEX16(0x0005u, pit.counter[0].counter);
}

/* Figure 9's counter latch command reads the count "on the fly": it latches
 * without reprogramming, so counting is unaffected and the value read is the
 * one at the moment of the command. */
static void test_the_counter_latch_command_freezes_a_running_count(void) {
  ap_i8254_t pit;
  ap_i8254_reset(&pit);
  ap_i8254_write(&pit, AP_I8254_CONTROL, 0x30u);
  write_pair(&pit, 0u, 0x0020u);

  ap_i8254_clock_counter(&pit, 0u); /* the load */
  ap_i8254_clock_counter(&pit, 0u);
  /* Counter latch: counter 0, RW field zero, rest don't-care. */
  ap_i8254_write(&pit, AP_I8254_CONTROL, 0x00u);
  const uint16_t at_latch = pit.counter[0].counter;
  TEST_ASSERT_EQUAL_HEX16(0x001Fu, at_latch);

  /* Counting continues while the latch holds. */
  ap_i8254_clock_counter(&pit, 0u);
  ap_i8254_clock_counter(&pit, 0u);
  TEST_ASSERT_TRUE(pit.counter[0].counter != at_latch);

  const uint8_t lsb = ap_i8254_read(&pit, AP_I8254_COUNTER_0);
  const uint8_t msb = ap_i8254_read(&pit, AP_I8254_COUNTER_0);
  TEST_ASSERT_EQUAL_HEX16(at_latch, (uint16_t)((uint16_t)(msb << 8) | lsb));

  /* "The count is then unlatched automatically" -- after the whole count, so
   * the next pair follows the counting element again. */
  const uint8_t live_lsb = ap_i8254_read(&pit, AP_I8254_COUNTER_0);
  TEST_ASSERT_EQUAL_HEX8((uint8_t)(pit.counter[0].counter & 0xFFu), live_lsb);
}

/* "If both count and status of a counter are latched, the first read operation
 * of that counter will return the latched status, regardless of which was
 * latched first." */
static void test_a_read_back_of_both_returns_status_first(void) {
  ap_i8254_t pit;
  ap_i8254_reset(&pit);
  ap_i8254_write(&pit, AP_I8254_CONTROL, 0x30u);
  write_pair(&pit, 0u, 0x0055u);
  ap_i8254_clock_counter(&pit, 0u);

  /* D5 and D4 both clear: latch count *and* status, counter 0. */
  ap_i8254_write(&pit, AP_I8254_CONTROL,
                 (uint8_t)(AP_I8254_READ_BACK | AP_I8254_RB_COUNTER_0));
  const uint8_t first = ap_i8254_read(&pit, AP_I8254_COUNTER_0);
  /* The status byte: the mode as written, and the count now available. */
  TEST_ASSERT_EQUAL_HEX8(0x30u, first & 0x3Fu);
  TEST_ASSERT_EQUAL_HEX8(0u, first & AP_I8254_STATUS_NULL_COUNT);

  /* Then the latched count, LSB first. */
  TEST_ASSERT_EQUAL_HEX8(0x55u, ap_i8254_read(&pit, AP_I8254_COUNTER_0));
  TEST_ASSERT_EQUAL_HEX8(0x00u, ap_i8254_read(&pit, AP_I8254_COUNTER_0));
}

/* Modes 1, 4 and 5 need a GATE edge or a strobe this board does not drive, and
 * are reported as such. */
static void test_the_gate_triggered_modes_are_reported(void) {
  ap_i8254_t pit;
  ap_i8254_reset(&pit);

  static const struct { uint8_t control; unsigned mode; bool gated; } cases[] = {
      {0x30u, 0u, false}, {0x32u, 1u, true},  {0x34u, 2u, false},
      {0x36u, 3u, false}, {0x38u, 4u, true},  {0x3Au, 5u, true},
      /* Figure 7: `X10` and `X11` alias to modes 2 and 3. */
      {0x3Cu, 2u, false}, {0x3Eu, 3u, false},
  };
  for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    ap_i8254_write(&pit, AP_I8254_CONTROL, cases[i].control);
    TEST_ASSERT_EQUAL_UINT(cases[i].mode, ap_i8254_mode(&pit, 0u));
    TEST_ASSERT_EQUAL_INT((int)cases[i].gated,
                          (int)ap_i8254_mode_gated(&pit, 0u));
  }
}

/* **Mode 1, Figure 16.** "OUT will be initially high. OUT will go low on the CLK
 * pulse following a trigger to begin the one-shot pulse, and will remain low
 * until the Counter reaches zero." Nothing loads without a trigger; and "The
 * one-shot is retriggerable". CW = 12, LSB = 3: 3 2 1 0 FFFF, OUT low for three
 * pulses. */
static void test_mode_one_is_a_retriggerable_one_shot(void) {
  ap_i8254_t pit;
  ap_i8254_reset(&pit);
  ap_i8254_write(&pit, AP_I8254_CONTROL, 0x12u);
  ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x03u);
  TEST_ASSERT_TRUE(ap_i8254_out(&pit, 0u));
  for (unsigned i = 0; i < 3u; i++) {
    ap_i8254_clock_counter(&pit, 0u);
  }
  TEST_ASSERT_TRUE(pit.counter[0].null_count); /* no trigger, no load */

  ap_i8254_set_gate(&pit, 0u, false);
  ap_i8254_set_gate(&pit, 0u, true);
  static const uint16_t counts[] = {3u, 2u, 1u, 0u, 0xFFFFu};
  static const bool outs[] = {false, false, false, true, true};
  expect_pulses(&pit, 0u, 5u, counts, outs);

  ap_i8254_set_gate(&pit, 0u, false);
  ap_i8254_set_gate(&pit, 0u, true);
  ap_i8254_clock_counter(&pit, 0u);
  TEST_ASSERT_EQUAL_HEX16(3u, pit.counter[0].counter);
  TEST_ASSERT_FALSE(ap_i8254_out(&pit, 0u));
}

/* **Mode 2, Figure 17.** "When the initial count has decremented to 1, OUT goes
 * low for one CLK pulse. OUT then goes high again, the Counter reloads the
 * initial count". CW = 14, LSB = 3: 3 2 1 3 2 1, OUT low on each 1. "Writing a
 * new count while counting does not affect the current counting sequence" --
 * it arrives with the reload. And "If GATE goes low during an output pulse, OUT
 * is set high immediately." */
static void test_mode_two_takes_out_low_for_one_pulse_at_one(void) {
  ap_i8254_t pit;
  ap_i8254_reset(&pit);
  ap_i8254_write(&pit, AP_I8254_CONTROL, 0x14u);
  ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x03u);
  TEST_ASSERT_TRUE(ap_i8254_out(&pit, 0u));

  static const uint16_t counts[] = {3u, 2u, 1u, 3u, 2u, 1u};
  static const bool outs[] = {true, true, false, true, true, false};
  expect_pulses(&pit, 0u, 6u, counts, outs);

  ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x05u);
  ap_i8254_clock_counter(&pit, 0u);
  TEST_ASSERT_EQUAL_HEX16(5u, pit.counter[0].counter);
  TEST_ASSERT_TRUE(ap_i8254_out(&pit, 0u));

  for (unsigned i = 0; i < 4u; i++) {
    ap_i8254_clock_counter(&pit, 0u);
  }
  TEST_ASSERT_FALSE(ap_i8254_out(&pit, 0u)); /* at 1 */
  ap_i8254_set_gate(&pit, 0u, false);
  TEST_ASSERT_TRUE(ap_i8254_out(&pit, 0u));
}

/* **Mode 3, Figure 18.** Even counts decrement by two and OUT changes at each
 * expiry: CW = 16, LSB = 4 gives 4 2 4 2, high then low. Odd counts load the
 * count minus one, and OUT goes low "one CLK pulse *after* the count expires":
 * LSB = 5 gives 4 2 0 4 2 4, high for (N+1)/2 = 3 pulses and low for
 * (N-1)/2 = 2. */
static void test_mode_three_is_high_for_the_larger_half_of_an_odd_count(void) {
  ap_i8254_t pit;
  ap_i8254_reset(&pit);
  ap_i8254_write(&pit, AP_I8254_CONTROL, 0x16u);
  ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x04u);
  static const uint16_t even_counts[] = {4u, 2u, 4u, 2u, 4u};
  static const bool even_outs[] = {true, true, false, false, true};
  expect_pulses(&pit, 0u, 5u, even_counts, even_outs);

  ap_i8254_write(&pit, AP_I8254_CONTROL, 0x16u);
  ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x05u);
  static const uint16_t odd_counts[] = {4u, 2u, 0u, 4u, 2u, 4u, 2u, 0u};
  static const bool odd_outs[] = {true,  true,  true, false,
                                  false, true,  true, true};
  expect_pulses(&pit, 0u, 8u, odd_counts, odd_outs);
}

/* **Mode 4, Figure 19.** "When the initial count expires, OUT will go low for
 * one CLK pulse and then go high again", and the load pulse "does not
 * decrement the count, so for an initial count of N, OUT does not strobe low
 * until N + 1 CLK pulses after the initial count is written." CW = 18, LSB = 3:
 * 3 2 1 0 FFFF FFFE, OUT low on the 0 alone. */
static void test_mode_four_strobes_once_n_plus_one_pulses_after_the_write(void) {
  ap_i8254_t pit;
  ap_i8254_reset(&pit);
  ap_i8254_write(&pit, AP_I8254_CONTROL, 0x18u);
  ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x03u);
  static const uint16_t counts[] = {3u, 2u, 1u, 0u, 0xFFFFu, 0xFFFEu};
  static const bool outs[] = {true, true, true, false, true, true};
  expect_pulses(&pit, 0u, 6u, counts, outs);
}

/* **Mode 5, Figure 20.** "After writing the Control Word and initial count, the
 * Counter will not be loaded until the CLK pulse after a trigger", then mode
 * 4's strobe. CW = 1A, LSB = 3. */
static void test_mode_five_strobes_after_a_trigger(void) {
  ap_i8254_t pit;
  ap_i8254_reset(&pit);
  ap_i8254_write(&pit, AP_I8254_CONTROL, 0x1Au);
  ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x03u);
  ap_i8254_clock_counter(&pit, 0u);
  TEST_ASSERT_TRUE(pit.counter[0].null_count);

  ap_i8254_set_gate(&pit, 0u, false);
  ap_i8254_set_gate(&pit, 0u, true);
  static const uint16_t counts[] = {3u, 2u, 1u, 0u, 0xFFFFu};
  static const bool outs[] = {true, true, true, false, true};
  expect_pulses(&pit, 0u, 5u, counts, outs);
}

/* Figure 7's BCD bit, "Binary Coded Decimal (BCD) Counter (4 Decades)", and
 * p. 6-161: the counter "'wraps around' to the highest count, either FFFF hex
 * for binary counting or 9999 for BCD counting". */
static void test_a_bcd_counter_counts_decades_and_wraps_to_9999(void) {
  ap_i8254_t pit;
  ap_i8254_reset(&pit);
  ap_i8254_write(&pit, AP_I8254_CONTROL, 0x31u); /* mode 0, BCD */
  write_pair(&pit, 0u, 0x0010u);
  ap_i8254_clock_counter(&pit, 0u);
  TEST_ASSERT_EQUAL_HEX16(0x0010u, pit.counter[0].counter);
  ap_i8254_clock_counter(&pit, 0u);
  TEST_ASSERT_EQUAL_HEX16(0x0009u, pit.counter[0].counter);
  for (unsigned i = 0; i < 9u; i++) {
    ap_i8254_clock_counter(&pit, 0u);
  }
  TEST_ASSERT_EQUAL_HEX16(0x0000u, pit.counter[0].counter);
  TEST_ASSERT_TRUE(ap_i8254_out(&pit, 0u));
  ap_i8254_clock_counter(&pit, 0u);
  TEST_ASSERT_EQUAL_HEX16(0x9999u, pit.counter[0].counter);
}

/* **The GATE pin**, Figure 21 and p. 6-161. Mode 0 is level-sensitive: a low
 * gate holds the count and releasing it resumes. Modes 2 and 3 are "both edge-
 * and level-sensitive": a low gate sets OUT high at once, and a rising edge
 * sets the trigger flip-flop, which "is then sampled on the next rising edge of
 * CLK" -- so the reload happens on that pulse, not at the edge. A gate that was
 * already high is no edge. */
static void test_the_gate_stops_a_count_and_a_rising_edge_reloads_it(void) {
  ap_i8254_t pit;
  ap_i8254_reset(&pit);

  /* Mode 0, level sensitive: a low gate holds the count. */
  ap_i8254_write(&pit, AP_I8254_CONTROL, 0x30u);
  write_pair(&pit, 0u, 5u);
  ap_i8254_clock_counter(&pit, 0u); /* the load */
  ap_i8254_clock_counter(&pit, 0u);
  TEST_ASSERT_EQUAL_HEX16(4u, pit.counter[0].counter);
  ap_i8254_set_gate(&pit, 0u, false);
  ap_i8254_clock_counter(&pit, 0u);
  ap_i8254_clock_counter(&pit, 0u);
  TEST_ASSERT_EQUAL_HEX16(4u, pit.counter[0].counter);
  ap_i8254_set_gate(&pit, 0u, true);
  ap_i8254_clock_counter(&pit, 0u);
  TEST_ASSERT_EQUAL_HEX16(3u, pit.counter[0].counter);

  /* Modes 2 and 3: the count after the load and one pulse differs -- mode 3
   * decrements by two -- and both reload on the pulse after a rising edge. */
  static const struct { uint8_t control; uint16_t after_two; } gated[] = {
      {0x34u, 5u}, /* mode 2: 6, 5 */
      {0x36u, 4u}, /* mode 3: 6, 4 */
  };
  for (unsigned pass = 0; pass < 2u; pass++) {
    ap_i8254_reset(&pit);
    ap_i8254_write(&pit, AP_I8254_CONTROL, gated[pass].control);
    write_pair(&pit, 0u, 6u);
    ap_i8254_clock_counter(&pit, 0u);
    ap_i8254_clock_counter(&pit, 0u);
    TEST_ASSERT_EQUAL_HEX16(gated[pass].after_two, pit.counter[0].counter);
    ap_i8254_set_gate(&pit, 0u, false);
    TEST_ASSERT_TRUE(ap_i8254_out(&pit, 0u));
    ap_i8254_set_gate(&pit, 0u, true);
    TEST_ASSERT_EQUAL_HEX16(gated[pass].after_two, pit.counter[0].counter);
    ap_i8254_clock_counter(&pit, 0u);
    TEST_ASSERT_EQUAL_HEX16(6u, pit.counter[0].counter);
  }

  ap_i8254_reset(&pit);
  ap_i8254_write(&pit, AP_I8254_CONTROL, 0x34u);
  write_pair(&pit, 0u, 6u);
  ap_i8254_clock_counter(&pit, 0u);
  ap_i8254_clock_counter(&pit, 0u);
  ap_i8254_set_gate(&pit, 0u, true);
  ap_i8254_clock_counter(&pit, 0u);
  TEST_ASSERT_EQUAL_HEX16(4u, pit.counter[0].counter);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_ring_firmwares_own_sequence);
  RUN_TEST(test_null_count_clears_only_when_the_count_reaches_the_element);
  RUN_TEST(
      test_a_count_is_loaded_on_the_next_clk_pulse_which_does_not_decrement_it);
  RUN_TEST(test_a_count_written_with_the_gate_low_is_still_loaded);
  RUN_TEST(
      test_the_first_byte_of_a_mode_zero_count_stops_it_and_sets_out_low);
  RUN_TEST(test_a_control_word_clears_the_count_register);
  RUN_TEST(test_the_counter_latch_command_freezes_a_running_count);
  RUN_TEST(test_a_read_back_of_both_returns_status_first);
  RUN_TEST(test_the_gate_triggered_modes_are_reported);
  RUN_TEST(test_mode_one_is_a_retriggerable_one_shot);
  RUN_TEST(test_mode_two_takes_out_low_for_one_pulse_at_one);
  RUN_TEST(test_mode_three_is_high_for_the_larger_half_of_an_odd_count);
  RUN_TEST(test_mode_four_strobes_once_n_plus_one_pulses_after_the_write);
  RUN_TEST(test_mode_five_strobes_after_a_trigger);
  RUN_TEST(test_a_bcd_counter_counts_decades_and_wraps_to_9999);
  RUN_TEST(test_the_gate_stops_a_count_and_a_rising_edge_reloads_it);
  return UNITY_END();
}

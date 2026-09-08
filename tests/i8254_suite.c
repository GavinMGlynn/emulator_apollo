/* Intel 8254 programmable interval timer, `[8254]` (1983 Intel Microprocessors
 * and Peripherals Handbook, ch. 6).
 *
 * The acceptance test for this part is the ring controller's own firmware:
 * `RING.md` finding 41 identifies two 8254s on the board from the exact
 * sequence the ROM writes, and that sequence is the first test below. */

#include "unity.h"

#include "device/ap_i8254.h"

void setUp(void) {}
void tearDown(void) {}

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

/* Figure 12, exactly. The footnote is the part a model gets wrong: for a
 * two-byte count the flag "goes to 1 when the second byte is written", so it
 * must not clear on the first. */
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
  TEST_ASSERT_FALSE(pit.counter[0].null_count);
  TEST_ASSERT_TRUE(pit.counter[0].counting);
  TEST_ASSERT_EQUAL_HEX16(0x1234u, pit.counter[0].counter);
}

/* Mode 0: "OUT will be initially low ... and it will go high when the Counter
 * reaches zero", and stay there. */
static void test_mode_zero_raises_out_at_terminal_count(void) {
  ap_i8254_t pit;
  ap_i8254_reset(&pit);
  ap_i8254_write(&pit, AP_I8254_CONTROL, 0x30u);
  TEST_ASSERT_FALSE(ap_i8254_out(&pit, 0u));

  ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x03u);
  ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x00u);
  TEST_ASSERT_FALSE(ap_i8254_out(&pit, 0u));

  ap_i8254_clock(&pit);
  ap_i8254_clock(&pit);
  TEST_ASSERT_FALSE(ap_i8254_out(&pit, 0u));
  ap_i8254_clock(&pit);
  TEST_ASSERT_TRUE(ap_i8254_out(&pit, 0u));

  /* And it stays high: mode 0 fires once per count written. */
  for (unsigned i = 0; i < 8u; i++) {
    ap_i8254_clock(&pit);
  }
  TEST_ASSERT_TRUE(ap_i8254_out(&pit, 0u));
}

/* Figure 9's counter latch command reads the count "on the fly": it latches
 * without reprogramming, so counting is unaffected and the value read is the
 * one at the moment of the command. */
static void test_the_counter_latch_command_freezes_a_running_count(void) {
  ap_i8254_t pit;
  ap_i8254_reset(&pit);
  ap_i8254_write(&pit, AP_I8254_CONTROL, 0x30u);
  ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x20u);
  ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x00u);

  ap_i8254_clock(&pit);
  ap_i8254_clock(&pit);
  /* Counter latch: counter 0, RW field zero, rest don't-care. */
  ap_i8254_write(&pit, AP_I8254_CONTROL, 0x00u);
  const uint16_t at_latch = pit.counter[0].counter;

  /* Counting continues while the latch holds. */
  ap_i8254_clock(&pit);
  ap_i8254_clock(&pit);
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
  ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x55u);
  ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x00u);

  /* D5 and D4 both clear: latch count *and* status, counter 0. */
  ap_i8254_write(&pit, AP_I8254_CONTROL,
                 (uint8_t)(AP_I8254_READ_BACK | AP_I8254_RB_COUNTER_0));
  const uint8_t first = ap_i8254_read(&pit, AP_I8254_COUNTER_0);
  TEST_ASSERT_EQUAL_HEX8(0x30u, first & 0x3Fu); /* the status byte */

  /* Then the latched count, LSB first. */
  TEST_ASSERT_EQUAL_HEX8(0x55u, ap_i8254_read(&pit, AP_I8254_COUNTER_0));
  TEST_ASSERT_EQUAL_HEX8(0x00u, ap_i8254_read(&pit, AP_I8254_COUNTER_0));
}

/* Modes 1, 4 and 5 need a GATE edge this board does not drive. They are
 * reported as such rather than being silently given mode 0's waveform. */
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

/* Mode 2 is periodic without a new write, and mode 3 is the square wave whose
 * half period is half the count. Both are what a timer on a controller board
 * would actually be programmed for once past initialisation. */
static void test_mode_two_reloads_and_mode_three_halves(void) {
  ap_i8254_t pit;
  ap_i8254_reset(&pit);

  ap_i8254_write(&pit, AP_I8254_CONTROL, 0x34u); /* counter 0, mode 2 */
  ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x03u);
  ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x00u);
  TEST_ASSERT_TRUE(ap_i8254_out(&pit, 0u)); /* initially high */
  ap_i8254_clock(&pit);
  ap_i8254_clock(&pit);
  ap_i8254_clock(&pit);
  TEST_ASSERT_FALSE(ap_i8254_out(&pit, 0u)); /* low for one clock at zero */
  TEST_ASSERT_EQUAL_HEX16(3u, pit.counter[0].counter); /* and reloaded */

  ap_i8254_write(&pit, AP_I8254_CONTROL, 0x36u); /* counter 0, mode 3 */
  ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x04u);
  ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x00u);
  const bool level = ap_i8254_out(&pit, 0u);
  ap_i8254_clock(&pit);
  ap_i8254_clock(&pit);
  TEST_ASSERT_TRUE(ap_i8254_out(&pit, 0u) != level); /* toggled at N/2 */
}

/* **The GATE pin, which nothing in this core drives and nothing tested.**
 *
 * `ap_i8254_set_gate` was found by the "what is called by nobody" audit: no
 * board wires a gate, which `ap_i8254.h` states deliberately -- "this board's
 * undriven gate leaves them reported rather than approximated" -- and the
 * counters come out of reset with their gates **high** for exactly that reason.
 * Deliberately uncalled is a fine answer; deliberately untested is not, because
 * a part nobody exercises is a part nobody knows works.
 *
 * Two documented behaviours, one per direction. `[8254]`: "In Modes 0, 2, 3 and
 * 4 the GATE input is level sensitive", so a low gate stops the count where it
 * stands; and modes 2 and 3 **reload on a rising gate edge**, which is the
 * trigger the three gate-triggered modes need to start at all. */
static void test_the_gate_stops_a_count_and_a_rising_edge_reloads_it(void) {
  ap_i8254_t pit;
  ap_i8254_reset(&pit);

  /* Mode 0, level sensitive: a low gate holds the count. */
  ap_i8254_write(&pit, AP_I8254_CONTROL, 0x30u);
  ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x05u);
  ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x00u);
  ap_i8254_clock(&pit);
  const uint16_t after_one = pit.counter[0].counter;
  TEST_ASSERT_EQUAL_HEX16(4u, after_one);
  ap_i8254_set_gate(&pit, 0u, false);
  ap_i8254_clock(&pit);
  ap_i8254_clock(&pit);
  TEST_ASSERT_EQUAL_HEX16(after_one, pit.counter[0].counter);
  /* And released, it goes on from where it was rather than restarting: mode 0
   * "merely resumes". */
  ap_i8254_set_gate(&pit, 0u, true);
  ap_i8254_clock(&pit);
  TEST_ASSERT_EQUAL_HEX16(3u, pit.counter[0].counter);

  /* Modes 2 and 3 reload on the **rising edge**, which mode 0 does not.
   *
   * The two count at different rates and the expectation says so rather than
   * splitting the difference: mode 3 is the square wave, so it decrements by
   * **two** a clock -- `[8254]`'s "half period" -- where mode 2 decrements by
   * one. Asserting 4 for both was this test's own first mistake, and the model
   * was right. */
  static const struct { uint8_t control; uint16_t after_two; } gated[] = {
      {0x34u, 4u}, /* mode 2: 6, 5, 4 */
      {0x36u, 2u}, /* mode 3: 6, 4, 2 */
  };
  for (unsigned pass = 0; pass < 2u; pass++) {
    ap_i8254_reset(&pit);
    ap_i8254_write(&pit, AP_I8254_CONTROL, gated[pass].control);
    ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x06u);
    ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x00u);
    ap_i8254_clock(&pit);
    ap_i8254_clock(&pit);
    TEST_ASSERT_EQUAL_HEX16(gated[pass].after_two, pit.counter[0].counter);
    ap_i8254_set_gate(&pit, 0u, false);
    ap_i8254_set_gate(&pit, 0u, true);
    TEST_ASSERT_EQUAL_HEX16(6u, pit.counter[0].counter);
  }

  /* A gate that was already high is not an edge, so nothing reloads -- the
   * assertion that separates "level" from "edge" and the one a model that
   * reloaded on every write would fail. */
  ap_i8254_reset(&pit);
  ap_i8254_write(&pit, AP_I8254_CONTROL, 0x34u);
  ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x06u);
  ap_i8254_write(&pit, AP_I8254_COUNTER_0, 0x00u);
  ap_i8254_clock(&pit);
  ap_i8254_set_gate(&pit, 0u, true);
  TEST_ASSERT_EQUAL_HEX16(5u, pit.counter[0].counter);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_ring_firmwares_own_sequence);
  RUN_TEST(test_null_count_clears_only_when_the_count_reaches_the_element);
  RUN_TEST(test_mode_zero_raises_out_at_terminal_count);
  RUN_TEST(test_the_counter_latch_command_freezes_a_running_count);
  RUN_TEST(test_a_read_back_of_both_returns_status_first);
  RUN_TEST(test_the_gate_triggered_modes_are_reported);
  RUN_TEST(test_mode_two_reloads_and_mode_three_halves);
  RUN_TEST(test_the_gate_stops_a_count_and_a_rising_edge_reloads_it);
  return UNITY_END();
}

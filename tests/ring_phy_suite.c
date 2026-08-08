/* Apollo Token Ring physical layer, `[MAC]` ch. 3. No oracle; the citations
 * carry the weight, as in the other two ring suites. */

#include "ring/ap_ring_phy.h"
#include "time/ap_time.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* `[MAC]` §3.2 p. 3-3: a bit cell is 83.33 nsec and holds two windows. The
 * exact period is 1/12 MHz, and both clock domains must divide the time base
 * exactly or a ring node could not be scheduled against the rest of the
 * machine -- which is the constraint that set `AP_TIME_BASE_HZ`. */
static void test_both_ring_clocks_divide_the_time_base_exactly(void) {
  TEST_ASSERT_EQUAL_UINT64(0u, AP_TIME_BASE_HZ % AP_RING_DATA_HZ);
  TEST_ASSERT_EQUAL_UINT64(0u, AP_TIME_BASE_HZ % AP_RING_LINE_HZ);
  /* Two windows to a cell, exactly. */
  TEST_ASSERT_EQUAL_UINT64(AP_RING_BIT_CELL_TICKS, 2u * AP_RING_WINDOW_TICKS);
  /* And the cell really is the manual's 83.33 ns: 83.33e-9 * 336.6e9 = 28050. */
  TEST_ASSERT_EQUAL_UINT64(28050u, AP_RING_BIT_CELL_TICKS);
}

/* "In each clock window, a transition ... must always be present." So the
 * clock window's level is always the inverse of what the previous cell left,
 * whatever the bit is and whichever polarity the line happens to be in. */
static void test_every_cell_begins_with_a_clock_transition(void) {
  for (unsigned prev = 0; prev < 2u; prev++) {
    for (unsigned bit = 0; bit < 2u; bit++) {
      const ap_ring_cell_t cell =
          ap_ring_biphase_encode(bit != 0u, prev != 0u);
      TEST_ASSERT_TRUE(cell.clock_window != (prev != 0u));
    }
  }
}

/* "A transition within the data window indicates a bit value of One; no
 * transition within the data window signals a bit value of Zero." */
static void test_a_data_window_transition_is_a_one(void) {
  const ap_ring_cell_t one = ap_ring_biphase_encode(true, false);
  TEST_ASSERT_TRUE(one.data_window != one.clock_window);

  const ap_ring_cell_t zero = ap_ring_biphase_encode(false, false);
  TEST_ASSERT_TRUE(zero.data_window == zero.clock_window);
}

/* Round trip, both polarities. The encoding is differential, so a node must
 * decode the same bits whichever level the line happens to be sitting at --
 * which is what lets a node be inserted into the ring without agreeing on
 * polarity with anyone. */
static void test_a_bit_stream_round_trips_from_either_polarity(void) {
  static const bool bits[] = {true, false, false, true, true, true, false};
  for (unsigned start = 0; start < 2u; start++) {
    bool level = start != 0u;
    ap_ring_cell_t cells[sizeof bits / sizeof bits[0]];
    for (unsigned i = 0; i < sizeof bits / sizeof bits[0]; i++) {
      cells[i] = ap_ring_biphase_encode(bits[i], level);
      level = ap_ring_cell_trailing_level(cells[i]);
    }

    bool previous = start != 0u;
    for (unsigned i = 0; i < sizeof bits / sizeof bits[0]; i++) {
      bool error = true;
      const bool got = ap_ring_biphase_decode(cells[i], previous, &error);
      TEST_ASSERT_FALSE(error);
      TEST_ASSERT_EQUAL_INT((int)bits[i], (int)got);
      previous = ap_ring_cell_trailing_level(cells[i]);
    }
  }
}

/* "or a bi-phase error will occur and the corresponding data will be
 * interpreted as having a bit value of Zero". Both halves matter: the error is
 * reported *and* a bit is still produced, so one bad cell costs a bit rather
 * than the byte framing. */
static void test_a_missing_clock_transition_is_an_error_and_a_zero(void) {
  ap_ring_cell_t cell = ap_ring_biphase_encode(true, false);
  /* Break the clock transition by making the clock window match what came
   * before it. */
  cell.clock_window = false;

  bool error = false;
  const bool got = ap_ring_biphase_decode(cell, false, &error);
  TEST_ASSERT_TRUE(error);
  TEST_ASSERT_FALSE(got);
}

/* `[MAC]` §3.3.2 p. 3-4: nominally a 1-bit delay, range 0.5 to 1.5 bits, and
 * the failures are stated inclusively -- "0.5 bit-times or less", "1.5
 * bit-times or more" -- so a node exactly on a bound has already failed. */
static void test_the_elastic_store_bounds_are_inclusive_failures(void) {
  TEST_ASSERT_EQUAL_INT(AP_RING_ESB_OK,
                        ap_ring_esb_classify(AP_RING_ESB_NOMINAL_CENTIBITS));
  TEST_ASSERT_EQUAL_INT(AP_RING_ESB_OK, ap_ring_esb_classify(51));
  TEST_ASSERT_EQUAL_INT(AP_RING_ESB_OK, ap_ring_esb_classify(149));

  TEST_ASSERT_EQUAL_INT(AP_RING_ESB_UNDERFLOW, ap_ring_esb_classify(50));
  TEST_ASSERT_EQUAL_INT(AP_RING_ESB_UNDERFLOW, ap_ring_esb_classify(0));
  TEST_ASSERT_EQUAL_INT(AP_RING_ESB_OVERFLOW, ap_ring_esb_classify(150));
  TEST_ASSERT_EQUAL_INT(AP_RING_ESB_OVERFLOW, ap_ring_esb_classify(200));
}

/* §3.3.1 p. 3-4 gives two endpoints and the word "linearly": 0.5 bit-times at
 * 24 MHz -3 kHz rising to 1.5 at +3 kHz. The centre must therefore land on the
 * nominal 1-bit delay §3.3.2 states independently, and those two sections
 * agreeing is the only cross-check either has. */
static void test_the_phase_offset_is_linear_between_the_stated_endpoints(void) {
  TEST_ASSERT_EQUAL_INT(AP_RING_ESB_MIN_CENTIBITS,
                        ap_ring_pll_phase_offset_centibits(
                            -AP_RING_PLL_DEVIATION_HZ));
  TEST_ASSERT_EQUAL_INT(AP_RING_ESB_MAX_CENTIBITS,
                        ap_ring_pll_phase_offset_centibits(
                            AP_RING_PLL_DEVIATION_HZ));
  /* The centre, which §3.3.2 gives independently as the 1-bit nominal. */
  TEST_ASSERT_EQUAL_INT(AP_RING_ESB_NOMINAL_CENTIBITS,
                        ap_ring_pll_phase_offset_centibits(0));
  /* Halfway up, and symmetric about the centre -- a rounding bias would show
   * as one side missing by a centibit. */
  TEST_ASSERT_EQUAL_INT(125, ap_ring_pll_phase_offset_centibits(1500));
  TEST_ASSERT_EQUAL_INT(75, ap_ring_pll_phase_offset_centibits(-1500));

  /* Beyond the stated interval the manual says "or less" and "or more", so the
   * relation clamps rather than extrapolating into a delay no buffer has. */
  TEST_ASSERT_EQUAL_INT(AP_RING_ESB_MAX_CENTIBITS,
                        ap_ring_pll_phase_offset_centibits(9000));
  TEST_ASSERT_EQUAL_INT(AP_RING_ESB_MIN_CENTIBITS,
                        ap_ring_pll_phase_offset_centibits(-9000));
}

/* And the two layers join: an offset outside the interval is exactly the
 * condition that makes the buffer fail. Clamping means the endpoints
 * themselves classify as failures, which is the manual's reading -- a ring
 * running at 24 MHz ±3 kHz is *at* its limit, not inside it. */
static void test_the_deviation_limits_are_where_the_buffer_fails(void) {
  TEST_ASSERT_EQUAL_INT(
      AP_RING_ESB_UNDERFLOW,
      ap_ring_esb_classify(
          ap_ring_pll_phase_offset_centibits(-AP_RING_PLL_DEVIATION_HZ)));
  TEST_ASSERT_EQUAL_INT(
      AP_RING_ESB_OVERFLOW,
      ap_ring_esb_classify(
          ap_ring_pll_phase_offset_centibits(AP_RING_PLL_DEVIATION_HZ)));
  /* Just inside, the ring is stable. */
  TEST_ASSERT_EQUAL_INT(AP_RING_ESB_OK, ap_ring_esb_classify(
      ap_ring_pll_phase_offset_centibits(AP_RING_PLL_DEVIATION_HZ - 100)));
}

/* `[MAC]` §3.5 p. 3-5: the relays do two things at once -- they join input coax
 * to output coax, taking the node out of the ring, *and* they join the node's
 * transmit output to its receive input. The second half is what lets a
 * bypassed node run loopback self-tests, which is the ring firmware's own test
 * and the first real check this controller will get. */
static void test_bypass_removes_the_node_and_loops_it_back_together(void) {
  const ap_ring_bypass_t in = {.bypassed = false};
  TEST_ASSERT_TRUE(ap_ring_node_in_ring(in));
  TEST_ASSERT_FALSE(ap_ring_node_loopback(in));

  const ap_ring_bypass_t out = {.bypassed = true};
  TEST_ASSERT_FALSE(ap_ring_node_in_ring(out));
  TEST_ASSERT_TRUE(ap_ring_node_loopback(out));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_both_ring_clocks_divide_the_time_base_exactly);
  RUN_TEST(test_every_cell_begins_with_a_clock_transition);
  RUN_TEST(test_a_data_window_transition_is_a_one);
  RUN_TEST(test_a_bit_stream_round_trips_from_either_polarity);
  RUN_TEST(test_a_missing_clock_transition_is_an_error_and_a_zero);
  RUN_TEST(test_the_elastic_store_bounds_are_inclusive_failures);
  RUN_TEST(test_the_phase_offset_is_linear_between_the_stated_endpoints);
  RUN_TEST(test_the_deviation_limits_are_where_the_buffer_fails);
  RUN_TEST(test_bypass_removes_the_node_and_loops_it_back_together);
  return UNITY_END();
}

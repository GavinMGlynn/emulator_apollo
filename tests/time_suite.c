/* The time base is the keystone of a multi-node, mixed-model machine: if a
 * clock period is not exact, every ring measurement drifts. These tests state
 * the arithmetic facts the rest of the emulator relies on. */

#include "time/ap_time.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_the_time_base_exactly_divides_every_apollo_cpu_clock(void) {
  TEST_ASSERT_TRUE(ap_time_base_divides(12000000u)); /* DN3000 */
  TEST_ASSERT_TRUE(ap_time_base_divides(20000000u)); /* DN2500 */
  TEST_ASSERT_TRUE(ap_time_base_divides(25000000u)); /* DN3500, DN5500 */
  TEST_ASSERT_TRUE(ap_time_base_divides(33000000u)); /* DN4500 */
}

/* The ring is bi-phase encoded, so it has two clock domains and the base must
 * represent both: 010005-00 s3.2 p.3-3. The 24 MHz line clock is what forced the
 * base up from 3.3 GHz, which divides 24 MHz only as 137.5. */
static void test_the_time_base_exactly_divides_both_ring_clocks(void) {
  TEST_ASSERT_TRUE(ap_time_base_divides(AP_RING_DATA_HZ));
  TEST_ASSERT_TRUE(ap_time_base_divides(AP_RING_LINE_HZ));
}

/* ## A period is asserted as a *quotient*, not as a literal
 *
 * These used to name the number -- "a 12 MHz cycle is 1650 base units" -- and
 * every one of them failed the moment the base was recomputed for the video dot
 * clock, all by exactly the same factor of 17. That is the recomputation
 * working, not a defect, but a test that has to be rewritten each time the unit
 * changes is testing the unit rather than the model.
 *
 * What the model actually promises is that a period is the base **divided
 * exactly** by the frequency: no rounding, no remainder. Asserted that way
 * these survive the next recomputation, and they still fail if `ap_clock_init`
 * rounds, truncates or refuses a frequency it should accept.
 *
 * The literal is not lost. `test_the_base_is_the_lcm_it_claims_to_be` below
 * pins the base itself once, which is where a wrong base belongs -- one place,
 * not scattered through every clock's test. */
static void check_exact_period(uint32_t hz) {
  ap_clock_t clk;
  TEST_ASSERT_TRUE(ap_clock_init(&clk, hz));
  TEST_ASSERT_EQUAL_UINT64(0u, AP_TIME_BASE_HZ % hz);
  TEST_ASSERT_EQUAL_UINT64(AP_TIME_BASE_HZ / hz, clk.period);
  /* And the period times the rate is a whole second, which is the property a
   * rounded one loses. */
  TEST_ASSERT_EQUAL_UINT64(AP_TIME_BASE_HZ, clk.period * hz);
}

static void test_a_12mhz_cycle_divides_the_base_exactly(void) {
  check_exact_period(12000000u);
}

static void test_a_25mhz_cycle_divides_the_base_exactly(void) {
  check_exact_period(25000000u);
}

static void test_a_33mhz_cycle_divides_the_base_exactly(void) {
  check_exact_period(33000000u);
}

/* The display's dot clock, and the reason the base is what it is. 68 MHz did
 * **not** divide 19.8 GHz -- 291.18 units -- so `ap_clock_init` refused it and
 * the raster could not be a clock domain at all until the base was recomputed.
 * `FINDINGS.md` C112. */
static void test_the_video_dot_clock_divides_the_base_exactly(void) {
  check_exact_period(68000000u);
}

/* The base is the LCM times the calendar's power of two, pinned in one place.
 * Every frequency the machine has must divide it, and the LCM must divide it
 * exactly -- the multiplier is 2^6 and nothing else, so a base that had drifted
 * to some other multiple would fail here rather than silently re-scale every
 * period in the machine.
 *
 * It is no longer the *least* such base, and that is deliberate: `[146818]`
 * Table 5's six fastest periodic rates need 2^15 where the LCM carries 2^9. */
static void test_the_base_is_the_lcm_times_the_calendar_power_of_two(void) {
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(21542400000000), AP_TIME_BASE_HZ);
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(0),
                           AP_TIME_BASE_HZ % UINT64_C(336600000000));
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(64),
                           AP_TIME_BASE_HZ / UINT64_C(336600000000));
  /* And every one of the calendar's fifteen rates is now exact. */
  for (unsigned n = 0; n < 15u; n++) {
    TEST_ASSERT_TRUE(ap_time_base_divides(32768u >> n));
  }
  const uint32_t clocks[] = {3600000u,  12000000u, 20000000u, 24000000u,
                             25000000u, 33000000u, 68000000u};
  uint64_t lcm = 1u;
  for (unsigned i = 0; i < sizeof clocks / sizeof clocks[0]; i++) {
    uint64_t a = lcm, b = clocks[i];
    while (b != 0u) { const uint64_t t = a % b; a = b; b = t; }
    lcm = lcm / a * clocks[i];
  }
  /* The LCM is what the base is built *from*, not what it equals: the base is
   * that LCM times 2^6. Asserting equality here is what this test did before
   * the calendar's rates were representable. */
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(336600000000), lcm);
  TEST_ASSERT_EQUAL_UINT64(lcm * 64u, AP_TIME_BASE_HZ);
}

static void test_a_ring_bit_cell_divides_the_base_exactly(void) {
  check_exact_period(AP_RING_DATA_HZ);
}

/* "In the time it takes to transmit one bit (this is a bit cell, or 83.33 nsec),
 * two windows exist" -- 010005-00 s3.2 p.3-3. Exactly two, with no remainder. */
static void test_a_ring_bit_cell_is_exactly_two_line_windows(void) {
  ap_clock_t bit, window;
  TEST_ASSERT_TRUE(ap_clock_init(&bit, AP_RING_DATA_HZ));
  TEST_ASSERT_TRUE(ap_clock_init(&window, AP_RING_LINE_HZ));
  /* Exactly two, with no remainder -- stated as the relation rather than as
   * the two numbers, so the recomputation of the base cannot break it. */
  TEST_ASSERT_EQUAL_UINT64(ap_clock_duration(&bit, 1u),
                           ap_clock_duration(&window, 2u));
  TEST_ASSERT_EQUAL_UINT64(0u, ap_clock_duration(&bit, 1u) % window.period);
}

/* The reason a CPU cycle cannot be the machine's unit of account: a 25 MHz
 * 68030 and the ring share no common cycle until 25 CPU cycles and 12 ring bits
 * have both elapsed. Anything that rounds this ratio drifts. */
static void test_a_25mhz_cpu_and_the_ring_realign_every_12_ring_bits(void) {
  ap_clock_t cpu, ring;
  TEST_ASSERT_TRUE(ap_clock_init(&cpu, 25000000u));
  TEST_ASSERT_TRUE(ap_clock_init(&ring, AP_RING_DATA_HZ));
  TEST_ASSERT_EQUAL_UINT64(ap_clock_duration(&cpu, 25u),
                           ap_clock_duration(&ring, 12u));
  /* and not before */
  for (uint64_t bits = 1u; bits < 12u; ++bits) {
    ap_time_t t = ap_clock_duration(&ring, bits);
    TEST_ASSERT_NOT_EQUAL_UINT64(0u, t % cpu.period);
  }
}

static void test_a_clock_the_time_base_cannot_represent_is_rejected(void) {
  ap_clock_t clk;
  TEST_ASSERT_FALSE(ap_clock_init(&clk, 7000000u));
  TEST_ASSERT_EQUAL_UINT32(0u, clk.hz);
  TEST_ASSERT_EQUAL_UINT64(0u, clk.period);
}

static void test_a_zero_frequency_clock_is_rejected(void) {
  ap_clock_t clk;
  TEST_ASSERT_FALSE(ap_clock_init(&clk, 0u));
}

static void test_align_up_leaves_an_instant_already_on_a_boundary_unchanged(void) {
  ap_clock_t clk;
  TEST_ASSERT_TRUE(ap_clock_init(&clk, 25000000u));
  ap_time_t t = ap_clock_duration(&clk, 1000u);
  TEST_ASSERT_EQUAL_UINT64(t, ap_clock_align_up(&clk, t));
}

static void test_align_up_advances_a_mid_cycle_instant_to_the_next_boundary(void) {
  ap_clock_t clk, ring;
  TEST_ASSERT_TRUE(ap_clock_init(&clk, 25000000u));
  TEST_ASSERT_TRUE(ap_clock_init(&ring, AP_RING_DATA_HZ));
  /* One ring bit cell lands part-way into the 25 MHz node's **third** cycle --
   * it is longer than two and shorter than three -- so the next boundary the
   * node may execute on is the third. Stated as cycle counts rather than as
   * unit totals, which is what makes it survive a recomputation of the base
   * while still failing if the ratio is wrong. */
  const ap_time_t cell = ap_clock_duration(&ring, 1u);
  TEST_ASSERT_TRUE(cell > ap_clock_duration(&clk, 2u));
  TEST_ASSERT_TRUE(cell < ap_clock_duration(&clk, 3u));
  TEST_ASSERT_EQUAL_UINT64(ap_clock_duration(&clk, 3u),
                           ap_clock_align_up(&clk, cell));
}

static void test_align_down_truncates_to_the_previous_boundary(void) {
  ap_clock_t clk, ring;
  TEST_ASSERT_TRUE(ap_clock_init(&clk, 25000000u));
  TEST_ASSERT_TRUE(ap_clock_init(&ring, AP_RING_DATA_HZ));
  const ap_time_t cell = ap_clock_duration(&ring, 1u);
  TEST_ASSERT_EQUAL_UINT64(ap_clock_duration(&clk, 2u),
                           ap_clock_align_down(&clk, cell));
  /* One unit short of a whole cycle is no whole cycles. */
  TEST_ASSERT_EQUAL_UINT64(0u, ap_clock_align_down(&clk, clk.period - 1u));
}

/* A node may only execute whole cycles: a partial cycle has not happened. */
static void test_cycles_in_truncates_a_partial_cycle(void) {
  ap_clock_t clk, ring;
  TEST_ASSERT_TRUE(ap_clock_init(&clk, 25000000u));
  TEST_ASSERT_TRUE(ap_clock_init(&ring, AP_RING_DATA_HZ));
  TEST_ASSERT_EQUAL_UINT64(2u,
                           ap_clock_cycles_in(&clk, ap_clock_duration(&ring, 1u)));
  TEST_ASSERT_EQUAL_UINT64(0u, ap_clock_cycles_in(&clk, clk.period - 1u));
  TEST_ASSERT_EQUAL_UINT64(1u, ap_clock_cycles_in(&clk, clk.period));
}

/* A one-second run must be the same number of units whichever clock counts it,
 * which is what makes cross-node timing comparable at all. */
static void test_one_emulated_second_is_the_base_frequency_in_units(void) {
  ap_clock_t cpu, ring;
  TEST_ASSERT_TRUE(ap_clock_init(&cpu, 25000000u));
  TEST_ASSERT_TRUE(ap_clock_init(&ring, AP_RING_DATA_HZ));
  TEST_ASSERT_EQUAL_UINT64(AP_TIME_BASE_HZ, ap_clock_duration(&cpu, 25000000u));
  TEST_ASSERT_EQUAL_UINT64(AP_TIME_BASE_HZ, ap_clock_duration(&ring, AP_RING_DATA_HZ));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_time_base_exactly_divides_every_apollo_cpu_clock);
  RUN_TEST(test_the_time_base_exactly_divides_both_ring_clocks);
  RUN_TEST(test_a_12mhz_cycle_divides_the_base_exactly);
  RUN_TEST(test_a_25mhz_cycle_divides_the_base_exactly);
  RUN_TEST(test_a_33mhz_cycle_divides_the_base_exactly);
  RUN_TEST(test_the_video_dot_clock_divides_the_base_exactly);
  RUN_TEST(test_the_base_is_the_lcm_times_the_calendar_power_of_two);
  RUN_TEST(test_a_ring_bit_cell_divides_the_base_exactly);
  RUN_TEST(test_a_ring_bit_cell_is_exactly_two_line_windows);
  RUN_TEST(test_a_25mhz_cpu_and_the_ring_realign_every_12_ring_bits);
  RUN_TEST(test_a_clock_the_time_base_cannot_represent_is_rejected);
  RUN_TEST(test_a_zero_frequency_clock_is_rejected);
  RUN_TEST(test_align_up_leaves_an_instant_already_on_a_boundary_unchanged);
  RUN_TEST(test_align_up_advances_a_mid_cycle_instant_to_the_next_boundary);
  RUN_TEST(test_align_down_truncates_to_the_previous_boundary);
  RUN_TEST(test_cycles_in_truncates_a_partial_cycle);
  RUN_TEST(test_one_emulated_second_is_the_base_frequency_in_units);
  return UNITY_END();
}

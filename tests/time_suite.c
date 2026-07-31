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

static void test_a_12mhz_cycle_is_550_base_units(void) {
  ap_clock_t clk;
  TEST_ASSERT_TRUE(ap_clock_init(&clk, 12000000u));
  TEST_ASSERT_EQUAL_UINT64(550u, clk.period);
}

static void test_a_25mhz_cycle_is_264_base_units(void) {
  ap_clock_t clk;
  TEST_ASSERT_TRUE(ap_clock_init(&clk, 25000000u));
  TEST_ASSERT_EQUAL_UINT64(264u, clk.period);
}

static void test_a_33mhz_cycle_is_200_base_units(void) {
  ap_clock_t clk;
  TEST_ASSERT_TRUE(ap_clock_init(&clk, 33000000u));
  TEST_ASSERT_EQUAL_UINT64(200u, clk.period);
}

static void test_a_ring_bit_cell_is_550_base_units(void) {
  ap_clock_t ring;
  TEST_ASSERT_TRUE(ap_clock_init(&ring, AP_RING_DATA_HZ));
  TEST_ASSERT_EQUAL_UINT64(550u, ring.period);
}

/* "In the time it takes to transmit one bit (this is a bit cell, or 83.33 nsec),
 * two windows exist" -- 010005-00 s3.2 p.3-3. Exactly two, with no remainder. */
static void test_a_ring_bit_cell_is_exactly_two_line_windows(void) {
  ap_clock_t bit, window;
  TEST_ASSERT_TRUE(ap_clock_init(&bit, AP_RING_DATA_HZ));
  TEST_ASSERT_TRUE(ap_clock_init(&window, AP_RING_LINE_HZ));
  TEST_ASSERT_EQUAL_UINT64(275u, window.period);
  TEST_ASSERT_EQUAL_UINT64(ap_clock_duration(&bit, 1u),
                           ap_clock_duration(&window, 2u));
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
  ap_clock_t clk;
  TEST_ASSERT_TRUE(ap_clock_init(&clk, 25000000u));
  /* One ring bit cell (550 units) lands 22 units into the third 264-unit
   * cycle, so the next boundary a 25 MHz node may execute on is 792. */
  TEST_ASSERT_EQUAL_UINT64(792u, ap_clock_align_up(&clk, 550u));
}

static void test_align_down_truncates_to_the_previous_boundary(void) {
  ap_clock_t clk;
  TEST_ASSERT_TRUE(ap_clock_init(&clk, 25000000u));
  TEST_ASSERT_EQUAL_UINT64(528u, ap_clock_align_down(&clk, 550u));
  TEST_ASSERT_EQUAL_UINT64(0u, ap_clock_align_down(&clk, 263u));
}

/* A node may only execute whole cycles: a partial cycle has not happened. */
static void test_cycles_in_truncates_a_partial_cycle(void) {
  ap_clock_t clk;
  TEST_ASSERT_TRUE(ap_clock_init(&clk, 25000000u));
  TEST_ASSERT_EQUAL_UINT64(2u, ap_clock_cycles_in(&clk, 550u));
  TEST_ASSERT_EQUAL_UINT64(0u, ap_clock_cycles_in(&clk, 263u));
  TEST_ASSERT_EQUAL_UINT64(1u, ap_clock_cycles_in(&clk, 264u));
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
  RUN_TEST(test_a_12mhz_cycle_is_550_base_units);
  RUN_TEST(test_a_25mhz_cycle_is_264_base_units);
  RUN_TEST(test_a_33mhz_cycle_is_200_base_units);
  RUN_TEST(test_a_ring_bit_cell_is_550_base_units);
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

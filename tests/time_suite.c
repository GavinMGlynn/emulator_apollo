/* The time base is the keystone of a multi-node, mixed-model machine: if a
 * clock period is not exact, every ring measurement drifts. These tests state
 * the arithmetic facts the rest of the emulator relies on. */

#include "time/ap_time.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* 12 Mbit/s Apollo Token Ring, per 010005-00 chapter 3. */
#define RING_BIT_HZ 12000000u

static void test_the_time_base_exactly_divides_every_apollo_cpu_clock(void) {
  TEST_ASSERT_TRUE(ap_time_base_divides(12000000u)); /* DN3000 */
  TEST_ASSERT_TRUE(ap_time_base_divides(20000000u)); /* DN2500 */
  TEST_ASSERT_TRUE(ap_time_base_divides(25000000u)); /* DN3500, DN5500 */
  TEST_ASSERT_TRUE(ap_time_base_divides(33000000u)); /* DN4500 */
}

static void test_a_12mhz_cycle_is_275_base_units(void) {
  ap_clock_t clk;
  TEST_ASSERT_TRUE(ap_clock_init(&clk, 12000000u));
  TEST_ASSERT_EQUAL_UINT64(275u, clk.period);
}

static void test_a_25mhz_cycle_is_132_base_units(void) {
  ap_clock_t clk;
  TEST_ASSERT_TRUE(ap_clock_init(&clk, 25000000u));
  TEST_ASSERT_EQUAL_UINT64(132u, clk.period);
}

static void test_a_33mhz_cycle_is_100_base_units(void) {
  ap_clock_t clk;
  TEST_ASSERT_TRUE(ap_clock_init(&clk, 33000000u));
  TEST_ASSERT_EQUAL_UINT64(100u, clk.period);
}

static void test_a_ring_bit_time_is_275_base_units(void) {
  ap_clock_t ring;
  TEST_ASSERT_TRUE(ap_clock_init(&ring, RING_BIT_HZ));
  TEST_ASSERT_EQUAL_UINT64(275u, ring.period);
}

/* The reason a CPU cycle cannot be the machine's unit of account: a 25 MHz
 * 68030 and the ring share no common cycle until 25 CPU cycles and 12 ring bits
 * have both elapsed. Anything that rounds this ratio drifts. */
static void test_a_25mhz_cpu_and_the_ring_realign_every_12_ring_bits(void) {
  ap_clock_t cpu, ring;
  TEST_ASSERT_TRUE(ap_clock_init(&cpu, 25000000u));
  TEST_ASSERT_TRUE(ap_clock_init(&ring, RING_BIT_HZ));
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
  /* One ring bit (275 units) lands 11 units into the third 132-unit cycle. */
  TEST_ASSERT_EQUAL_UINT64(396u, ap_clock_align_up(&clk, 275u));
}

static void test_align_down_truncates_to_the_previous_boundary(void) {
  ap_clock_t clk;
  TEST_ASSERT_TRUE(ap_clock_init(&clk, 25000000u));
  TEST_ASSERT_EQUAL_UINT64(264u, ap_clock_align_down(&clk, 275u));
  TEST_ASSERT_EQUAL_UINT64(0u, ap_clock_align_down(&clk, 131u));
}

/* A node may only execute whole cycles: a partial cycle has not happened. */
static void test_cycles_in_truncates_a_partial_cycle(void) {
  ap_clock_t clk;
  TEST_ASSERT_TRUE(ap_clock_init(&clk, 25000000u));
  TEST_ASSERT_EQUAL_UINT64(2u, ap_clock_cycles_in(&clk, 275u));
  TEST_ASSERT_EQUAL_UINT64(0u, ap_clock_cycles_in(&clk, 131u));
  TEST_ASSERT_EQUAL_UINT64(1u, ap_clock_cycles_in(&clk, 132u));
}

/* A one-second run must be the same number of units whichever clock counts it,
 * which is what makes cross-node timing comparable at all. */
static void test_one_emulated_second_is_the_base_frequency_in_units(void) {
  ap_clock_t cpu, ring;
  TEST_ASSERT_TRUE(ap_clock_init(&cpu, 25000000u));
  TEST_ASSERT_TRUE(ap_clock_init(&ring, RING_BIT_HZ));
  TEST_ASSERT_EQUAL_UINT64(AP_TIME_BASE_HZ, ap_clock_duration(&cpu, 25000000u));
  TEST_ASSERT_EQUAL_UINT64(AP_TIME_BASE_HZ, ap_clock_duration(&ring, RING_BIT_HZ));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_time_base_exactly_divides_every_apollo_cpu_clock);
  RUN_TEST(test_a_12mhz_cycle_is_275_base_units);
  RUN_TEST(test_a_25mhz_cycle_is_132_base_units);
  RUN_TEST(test_a_33mhz_cycle_is_100_base_units);
  RUN_TEST(test_a_ring_bit_time_is_275_base_units);
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

/* MC68040 `MOVE` timing, `[68040]` §10.4, from the page images of pages 10-9
 * and 10-10.
 *
 * A hundred and eighty cells is too many to check one by one, so this mixes
 * spot values against the printed table with structural properties that would
 * catch a whole row or column being displaced.
 */

#include "cpu/m68040/ap_m68040_move_timing.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static ap_m68040_move_timing_t at(ap_m68040_move_source_t s,
                                  ap_m68040_move_destination_t d) {
  return ap_m68040_move_timing(s, d);
}

/* ---------------------------------------------------------------------------
 * Spot checks: the four corners and a few interior cells.
 * ------------------------------------------------------------------------- */

static void test_the_cheapest_move(void) {
  /* `MOVE Dn,Dn`: one clock in each stage, the floor of the table. */
  const ap_m68040_move_timing_t t =
      at(AP_M68040_MOVE_SRC_DN, AP_M68040_MOVE_DST_DN);
  TEST_ASSERT_EQUAL_UINT(1u, t.calculate);
  TEST_ASSERT_EQUAL_UINT(0u, t.execute.lead);
  TEST_ASSERT_EQUAL_UINT(1u, t.execute.base);
}

static void test_the_most_expensive_move(void) {
  /* `MOVE ([bd,BR],Xn,od),([bd,An],Xn,od)`: 23 and `3L + 20`, the table's
   * bottom-right corner and the most addressing a single instruction can
   * express. */
  const ap_m68040_move_timing_t t =
      at(AP_M68040_MOVE_SRC_MEMORY_POSTINDEXED_OD,
         AP_M68040_MOVE_DST_MEMORY_POSTINDEXED_OD);
  TEST_ASSERT_EQUAL_UINT(23u, t.calculate);
  TEST_ASSERT_EQUAL_UINT(3u, t.execute.lead);
  TEST_ASSERT_EQUAL_UINT(20u, t.execute.base);
}

static void test_the_other_two_corners(void) {
  /* Top-right: `MOVE ([bd,BR],Xn,od),Dn` is 12 and `3L + 9`.
   * Bottom-left: `MOVE Dn,([bd,An],Xn,od)` is 12 and `3L + 9` too -- the table
   * is not symmetric in general, and these two corners happening to agree is
   * worth pinning rather than assuming. */
  const ap_m68040_move_timing_t top_right =
      at(AP_M68040_MOVE_SRC_MEMORY_POSTINDEXED_OD, AP_M68040_MOVE_DST_DN);
  TEST_ASSERT_EQUAL_UINT(12u, top_right.calculate);
  TEST_ASSERT_EQUAL_UINT(3u, top_right.execute.lead);
  TEST_ASSERT_EQUAL_UINT(9u, top_right.execute.base);

  const ap_m68040_move_timing_t bottom_left =
      at(AP_M68040_MOVE_SRC_DN, AP_M68040_MOVE_DST_MEMORY_POSTINDEXED_OD);
  TEST_ASSERT_EQUAL_UINT(12u, bottom_left.calculate);
  TEST_ASSERT_EQUAL_UINT(3u, bottom_left.execute.lead);
  TEST_ASSERT_EQUAL_UINT(9u, bottom_left.execute.base);
}

static void test_a_postincrement_destination_costs_more_than_a_plain_one(void) {
  /* `MOVE (An),(An)` is 1/1 while `MOVE (An),(An)+` is 2 and `1L + 1`: the
   * increment has to be written back, which the plain indirect form skips. */
  const ap_m68040_move_timing_t plain =
      at(AP_M68040_MOVE_SRC_INDIRECT, AP_M68040_MOVE_DST_INDIRECT);
  TEST_ASSERT_EQUAL_UINT(1u, plain.calculate);
  TEST_ASSERT_EQUAL_UINT(1u, ap_m68040_execute_total(plain.execute));

  const ap_m68040_move_timing_t post =
      at(AP_M68040_MOVE_SRC_INDIRECT, AP_M68040_MOVE_DST_POSTINCREMENT);
  TEST_ASSERT_EQUAL_UINT(2u, post.calculate);
  TEST_ASSERT_EQUAL_UINT(1u, post.execute.lead);
  TEST_ASSERT_EQUAL_UINT(1u, post.execute.base);
}

static void test_an_interior_cell(void) {
  /* `MOVE (d8,PC,Xn),(bd,An,Xn)` is 11 and `1L + 10` -- chosen because it sits
   * where both a source index and a destination base displacement apply, so a
   * row or column slip would move it. */
  const ap_m68040_move_timing_t t = at(AP_M68040_MOVE_SRC_PC_INDEXED,
                                       AP_M68040_MOVE_DST_BASE_DISPLACEMENT);
  TEST_ASSERT_EQUAL_UINT(11u, t.calculate);
  TEST_ASSERT_EQUAL_UINT(1u, t.execute.lead);
  TEST_ASSERT_EQUAL_UINT(10u, t.execute.base);
}

/* ---------------------------------------------------------------------------
 * Structural properties.
 * ------------------------------------------------------------------------- */

static void test_every_cell_is_populated(void) {
  /* A designated initialiser left out would leave a zeroed cell, and no cell in
   * this table is free. */
  for (unsigned d = 0; d < AP_M68040_MOVE_DST_COUNT; d++) {
    for (unsigned s = 0; s < AP_M68040_MOVE_SRC_COUNT; s++) {
      const ap_m68040_move_timing_t t = at((ap_m68040_move_source_t)s,
                                           (ap_m68040_move_destination_t)d);
      TEST_ASSERT_TRUE(t.calculate >= 1u);
      TEST_ASSERT_TRUE(ap_m68040_execute_total(t.execute) >= 1u);
    }
  }
}

static void test_the_unindexed_sources_agree_for_complex_destinations(void) {
  /* From `(d8,An,Xn)` onward the destination's own address calculation
   * dominates, and the eight source rows with no index register carry
   * identical figures -- except the PC-relative one, which supposition 1
   * charges separately.
   *
   * This is the property most likely to break under a transcription slip: one
   * displaced row in a column of eight identical values shows up here and
   * nowhere else. */
  const ap_m68040_move_source_t unindexed[] = {
      AP_M68040_MOVE_SRC_DN,          AP_M68040_MOVE_SRC_INDIRECT,
      AP_M68040_MOVE_SRC_POSTINCREMENT, AP_M68040_MOVE_SRC_PREDECREMENT,
      AP_M68040_MOVE_SRC_DISPLACEMENT, AP_M68040_MOVE_SRC_ABSOLUTE,
      AP_M68040_MOVE_SRC_IMMEDIATE};

  for (unsigned d = AP_M68040_MOVE_DST_BASE_DISPLACEMENT;
       d < AP_M68040_MOVE_DST_COUNT; d++) {
    const ap_m68040_move_timing_t first =
        at(unindexed[0], (ap_m68040_move_destination_t)d);
    for (unsigned i = 1; i < sizeof unindexed / sizeof unindexed[0]; i++) {
      const ap_m68040_move_timing_t t =
          at(unindexed[i], (ap_m68040_move_destination_t)d);
      TEST_ASSERT_EQUAL_UINT(first.calculate, t.calculate);
      TEST_ASSERT_EQUAL_UINT(first.execute.lead, t.execute.lead);
      TEST_ASSERT_EQUAL_UINT(first.execute.base, t.execute.base);
    }
  }
}

static void test_a_pc_relative_source_always_costs_more(void) {
  /* Supposition 1: "for BR = PC, 1 and 1L clocks to the <ea> calculate and
   * execution times". So `(d16,PC)` beats `(d16,An)` in every column, and by
   * more than one clock where the interlock compounds it. */
  for (unsigned d = 0; d < AP_M68040_MOVE_DST_COUNT; d++) {
    const ap_m68040_move_timing_t an =
        at(AP_M68040_MOVE_SRC_DISPLACEMENT, (ap_m68040_move_destination_t)d);
    const ap_m68040_move_timing_t pc =
        at(AP_M68040_MOVE_SRC_PC_DISPLACEMENT, (ap_m68040_move_destination_t)d);
    TEST_ASSERT_TRUE(pc.calculate > an.calculate);
    TEST_ASSERT_TRUE(pc.execute.lead > an.execute.lead);
  }
}

static void test_cost_grows_with_destination_complexity(void) {
  /* Holding the source at `Dn`, each destination from `(d8,An,Xn)` onward costs
   * at least as much to calculate as the one before it. The table is ordered by
   * addressing complexity, so a column swapped with its neighbour would break
   * the monotonicity. */
  unsigned previous = 0;
  for (unsigned d = AP_M68040_MOVE_DST_INDEXED; d < AP_M68040_MOVE_DST_COUNT;
       d++) {
    const ap_m68040_move_timing_t t =
        at(AP_M68040_MOVE_SRC_DN, (ap_m68040_move_destination_t)d);
    TEST_ASSERT_TRUE(t.calculate >= previous);
    previous = t.calculate;
  }
}

static void test_cost_grows_with_source_complexity(void) {
  /* The same, along the other axis, from the first indexed source onward. */
  unsigned previous = 0;
  for (unsigned s = AP_M68040_MOVE_SRC_INDEXED; s < AP_M68040_MOVE_SRC_COUNT;
       s++) {
    const ap_m68040_move_timing_t t =
        at((ap_m68040_move_source_t)s, AP_M68040_MOVE_DST_DN);
    TEST_ASSERT_TRUE(t.calculate >= previous);
    previous = t.calculate;
  }
}

static void test_the_postindexed_forms_carry_a_three_clock_lead(void) {
  /* Every `([bd,...],Xn)` and `([bd,...],Xn,od)` cell has a lead of three where
   * the preindexed forms have one -- the extra indirection has to complete
   * before the index can be applied, and the lead is where that shows. */
  for (unsigned d = 0; d < AP_M68040_MOVE_DST_COUNT; d++) {
    TEST_ASSERT_EQUAL_UINT(
        3u, at(AP_M68040_MOVE_SRC_MEMORY_POSTINDEXED,
               (ap_m68040_move_destination_t)d)
                .execute.lead);
    TEST_ASSERT_EQUAL_UINT(
        1u, at(AP_M68040_MOVE_SRC_MEMORY_PREINDEXED,
               (ap_m68040_move_destination_t)d)
                .execute.lead);
  }
}

static void test_only_the_last_seven_sources_are_indexed(void) {
  for (unsigned s = 0; s < AP_M68040_MOVE_SRC_COUNT; s++) {
    const bool indexed = s >= AP_M68040_MOVE_SRC_INDEXED;
    TEST_ASSERT_EQUAL_INT(
        indexed, ap_m68040_move_source_is_indexed((ap_m68040_move_source_t)s));
  }
}

static void test_an_out_of_range_request_returns_zero(void) {
  const ap_m68040_move_timing_t t =
      ap_m68040_move_timing(AP_M68040_MOVE_SRC_COUNT, AP_M68040_MOVE_DST_DN);
  TEST_ASSERT_EQUAL_UINT(0u, t.calculate);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_cheapest_move);
  RUN_TEST(test_the_most_expensive_move);
  RUN_TEST(test_the_other_two_corners);
  RUN_TEST(test_a_postincrement_destination_costs_more_than_a_plain_one);
  RUN_TEST(test_an_interior_cell);
  RUN_TEST(test_every_cell_is_populated);
  RUN_TEST(test_the_unindexed_sources_agree_for_complex_destinations);
  RUN_TEST(test_a_pc_relative_source_always_costs_more);
  RUN_TEST(test_cost_grows_with_destination_complexity);
  RUN_TEST(test_cost_grows_with_source_complexity);
  RUN_TEST(test_the_postindexed_forms_carry_a_three_clock_lead);
  RUN_TEST(test_only_the_last_seven_sources_are_indexed);
  RUN_TEST(test_an_out_of_range_request_returns_zero);
  return UNITY_END();
}

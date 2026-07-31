/* MC68030 instruction overlap.
 *
 * The module holds the composition rule and none of the per-instruction
 * figures, so these tests check it against the manual's own worked example and
 * against the properties the manual states in prose — not against numbers this
 * project produced.
 */

#include "cpu/m68030/ap_m68030_overlap.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* "The total overlap time between instructions A and B consists of the lesser
 * of the tail of instruction A or the head of instruction B." */
static void test_the_overlap_is_the_lesser_of_the_tail_and_the_head(void) {
  TEST_ASSERT_EQUAL_UINT(2u, ap_m68030_overlap(2u, 4u));
  TEST_ASSERT_EQUAL_UINT(2u, ap_m68030_overlap(4u, 2u));
  TEST_ASSERT_EQUAL_UINT(0u, ap_m68030_overlap(0u, 4u));
  TEST_ASSERT_EQUAL_UINT(3u, ap_m68030_overlap(3u, 3u));
}

/* §11.3.4's worked example, verbatim:
 *
 *              Head  Tail  CC
 *   ADD.L A1,D1   2     0   2
 *   SUBA.L D1,A2  4     0   4
 *
 *   Execution Time = CC1 + [CC2 - min(H2,T1)]
 *                  = 2 + [4 - min(4,0)]
 *                  = 6 clocks
 *
 * An external check rather than one of our own numbers, which is the only kind
 * worth having here. */
static void test_the_manuals_worked_example_comes_to_six_clocks(void) {
  const ap_m68030_timing_t add = {.head = 2u, .tail = 0u, .cache_case = 2u};
  const ap_m68030_timing_t suba = {.head = 4u, .tail = 0u, .cache_case = 4u};

  ap_m68030_overlap_state_t state = ap_m68030_overlap_begin();
  ap_m68030_overlap_add(&state, &add);
  ap_m68030_overlap_add(&state, &suba);

  TEST_ASSERT_EQUAL_UINT64(6u, ap_m68030_overlap_total(&state));
}

/* The pairing is directional: the *following* instruction's head against the
 * *preceding* instruction's tail. Reversing it reads plausibly and is wrong,
 * and no single instruction reveals it -- only a sequence whose two entries
 * have asymmetric heads and tails, which is most of them.
 *
 * Here A has a tail of 4 and B a head of 0. The correct rule saves nothing;
 * the reversed one would save min(H_A,T_B) = min(0,3) = 0 too, so the pair is
 * chosen to make the two differ: A tail 4, B head 1, A head 0, B tail 3.
 * Correct: min(4,1) = 1. Reversed: min(0,3) = 0. */
static void test_the_head_and_tail_are_taken_from_different_instructions(void) {
  const ap_m68030_timing_t first = {.head = 0u, .tail = 4u, .cache_case = 6u};
  const ap_m68030_timing_t second = {.head = 1u, .tail = 3u, .cache_case = 5u};

  ap_m68030_overlap_state_t state = ap_m68030_overlap_begin();
  ap_m68030_overlap_add(&state, &first);
  ap_m68030_overlap_add(&state, &second);

  /* 6 + (5 - min(4,1)) = 6 + 4 = 10. The reversed rule would give 11. */
  TEST_ASSERT_EQUAL_UINT64(10u, ap_m68030_overlap_total(&state));
}

/* "The nature of the instruction overlap and the fact that the heads of some
 * instructions equal the total instruction-cache-case time for those
 * instructions makes a zero net execution time possible. The execution time of
 * an instruction is completely absorbed by overlap with the previous
 * instruction."
 *
 * A model clamping every instruction to at least one clock would be wrong, and
 * wrong in the direction that hides a fast mode's error -- it would make the
 * reference core slower than the hardware, so a fast mode that skipped work
 * would look closer to correct rather than further from it. */
static void test_an_instruction_can_cost_nothing_at_all(void) {
  const ap_m68030_timing_t first = {.head = 0u, .tail = 4u, .cache_case = 4u};
  /* Head equal to the whole cache case, fully absorbed by the tail above. */
  const ap_m68030_timing_t absorbed = {.head = 3u, .tail = 0u,
                                       .cache_case = 3u};

  ap_m68030_overlap_state_t state = ap_m68030_overlap_begin();
  ap_m68030_overlap_add(&state, &first);
  const uint64_t after_first = ap_m68030_overlap_total(&state);
  ap_m68030_overlap_add(&state, &absorbed);

  TEST_ASSERT_EQUAL_UINT64(after_first, ap_m68030_overlap_total(&state));
}

/* The first instruction contributes its whole cache-case time: "CC1 + ...",
 * with nothing before it to overlap with. A model that applied an overlap to
 * the first instruction would need a tail from nowhere. */
static void test_the_first_instruction_overlaps_with_nothing(void) {
  const ap_m68030_timing_t only = {.head = 4u, .tail = 2u, .cache_case = 6u};

  ap_m68030_overlap_state_t state = ap_m68030_overlap_begin();
  ap_m68030_overlap_add(&state, &only);

  TEST_ASSERT_EQUAL_UINT64(6u, ap_m68030_overlap_total(&state));
}

/* An empty sequence is zero clocks, and adding to it later still starts from
 * the first instruction's whole cache case rather than from an overlap against
 * a tail of zero that was never there. Those give the same number here, which
 * is why the state carries `started` rather than relying on the tail. */
static void test_an_empty_sequence_costs_nothing(void) {
  ap_m68030_overlap_state_t state = ap_m68030_overlap_begin();
  TEST_ASSERT_EQUAL_UINT64(0u, ap_m68030_overlap_total(&state));
}

/* A longer sequence, composed pairwise: each overlap is against the immediately
 * preceding instruction only. Overlap does not accumulate across three
 * instructions -- §11.3.2's figure shows A/B and B/C as separate overlaps, so
 * B's tail is spent on C and not shared back with A. */
static void test_overlap_is_pairwise_and_not_cumulative(void) {
  const ap_m68030_timing_t a = {.head = 0u, .tail = 2u, .cache_case = 4u};
  const ap_m68030_timing_t b = {.head = 2u, .tail = 3u, .cache_case = 5u};
  const ap_m68030_timing_t c = {.head = 4u, .tail = 0u, .cache_case = 6u};

  ap_m68030_overlap_state_t state = ap_m68030_overlap_begin();
  ap_m68030_overlap_add(&state, &a);
  ap_m68030_overlap_add(&state, &b);
  ap_m68030_overlap_add(&state, &c);

  /* 4 + (5 - min(2,2)) + (6 - min(3,4)) = 4 + 3 + 3 = 10. */
  TEST_ASSERT_EQUAL_UINT64(10u, ap_m68030_overlap_total(&state));
}

/* "the heads of some instructions equal the total instruction-cache-case time",
 * so head may equal the cache case but cannot exceed it. An entry that fails
 * this was mis-transcribed, and catching it at the table is cheaper than
 * watching a sequence total come out wrong somewhere downstream. */
static void test_a_head_or_tail_longer_than_the_instruction_is_inconsistent(
    void) {
  const ap_m68030_timing_t equal = {.head = 3u, .tail = 3u, .cache_case = 3u};
  TEST_ASSERT_TRUE(ap_m68030_timing_consistent(&equal));

  const ap_m68030_timing_t long_head = {.head = 4u, .tail = 0u,
                                        .cache_case = 3u};
  TEST_ASSERT_FALSE(ap_m68030_timing_consistent(&long_head));

  const ap_m68030_timing_t long_tail = {.head = 0u, .tail = 4u,
                                        .cache_case = 3u};
  TEST_ASSERT_FALSE(ap_m68030_timing_consistent(&long_tail));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_overlap_is_the_lesser_of_the_tail_and_the_head);
  RUN_TEST(test_the_manuals_worked_example_comes_to_six_clocks);
  RUN_TEST(test_the_head_and_tail_are_taken_from_different_instructions);
  RUN_TEST(test_an_instruction_can_cost_nothing_at_all);
  RUN_TEST(test_the_first_instruction_overlaps_with_nothing);
  RUN_TEST(test_an_empty_sequence_costs_nothing);
  RUN_TEST(test_overlap_is_pairwise_and_not_cumulative);
  RUN_TEST(test_a_head_or_tail_longer_than_the_instruction_is_inconsistent);
  return UNITY_END();
}

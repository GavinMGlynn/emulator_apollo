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
 * worth having here.
 *
 * **Do not "correct" these against §11.6.8.** That table gives `SUBA.L Rn,An`
 * as 2/0/2, not 4/0/4 -- the example is mislabelled, and its numbers are the
 * `SUBA.W` row's. `docs/references/M68030_TIMING.md` records the evidence: the
 * word forms cost 4 and the long forms 2 in six rows across `ADDA`, `SUBA` and
 * `CMPA`, and the word form is the one that sign-extends.
 *
 * The mislabelling does not weaken this test, because what is being checked is
 * the *arithmetic*: 2 + [4 - min(4,0)] = 6 is a correct demonstration of
 * Equation (11-1) whatever instruction those numbers belong to. Reconciling the
 * inputs with the table would destroy that check while appearing to tidy it. */
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

/* Microcode and bus run concurrently, so an instruction's cost is the two
 * scheduled rather than summed. The tables establish this and the header quotes
 * them; these are the two rows worked through.
 *
 * `ADD Rn,Dn` is CC 2(0/0/0) and NCC 2(0/1/0): one more bus cycle, worth two
 * clocks, and the same total -- so the prefetch cost nothing, having happened
 * while the microcode ran. */
static void test_a_bus_cycle_hidden_under_microcode_costs_nothing(void) {
  /* Cache case: no bus at all. */
  TEST_ASSERT_EQUAL_UINT(2u, ap_m68030_schedule(2u, 0u));
  /* No-cache case: one prefetch, two clocks, fully hidden. */
  TEST_ASSERT_EQUAL_UINT(2u, ap_m68030_schedule(2u, 2u));
}

/* `ADD Dn,EA` is CC 3(0/0/1) against NCC 4(0/1/1): the extra prefetch adds one
 * clock, not zero and not two. That is what rules out both "add the two" and
 * "the bus is free" -- the bus time is counted whole, and only the part that
 * fits under the microcode is free. */
static void test_bus_time_beyond_the_microcode_is_what_costs(void) {
  /* Cache case: a write, two clocks, under three of microcode. */
  TEST_ASSERT_EQUAL_UINT(3u, ap_m68030_schedule(3u, 2u));
  /* No-cache case: write plus prefetch, four clocks, now exceeding it. */
  TEST_ASSERT_EQUAL_UINT(4u, ap_m68030_schedule(3u, 4u));

  /* And the sum would have been 5 and 7 -- the model that was implemented,
   * measured and backed out. */
  TEST_ASSERT_NOT_EQUAL_UINT(3u + 2u, ap_m68030_schedule(3u, 2u));
}

/* A long instruction hides everything the bus does. DIVU.W is CC 44(0/0/0) and
 * NCC 44(0/1/0) -- forty-four clocks of microcode swallow a two-clock prefetch
 * without trace, which is the same fact as the first test at a scale where a
 * summing model would be obviously wrong. */
static void test_a_long_instruction_hides_its_whole_fetch(void) {
  TEST_ASSERT_EQUAL_UINT(44u, ap_m68030_schedule(44u, 0u));
  TEST_ASSERT_EQUAL_UINT(44u, ap_m68030_schedule(44u, 2u));
}

/* An instruction with no microcode figure is its bus time, unchanged. That is
 * the honest state for everything not yet transcribed: a lower bound, not a
 * guess dressed as a measurement. */
static void test_an_untranscribed_instruction_is_its_bus_time(void) {
  TEST_ASSERT_EQUAL_UINT(2u, ap_m68030_schedule(0u, 2u));
  TEST_ASSERT_EQUAL_UINT(0u, ap_m68030_schedule(0u, 0u));
}

/* ---------------------------------------------------------------------------
 * The no-cache case, against §11.3.3's own worked example.
 *
 * The section works `MOVE.L (d16,An,Dn),Dn` followed by `CMPI.W #(data).W,
 * (d16,An)` with both caches missing on every access, and states three numbers
 * this can be checked against: the MOVE's average no-cache case is "2 + 7 = 9
 * clocks" -- the operation's figure plus the effective address's -- the CMPI's
 * is seven, and the pair together is "9 + 7 = 16 clocks".
 *
 * All three are Motorola's arithmetic on Motorola's figures, which is the only
 * kind of check available for a composition rule with no measurement behind it.
 * ------------------------------------------------------------------------- */

static void test_the_no_cache_case_composes_by_addition(void) {
  /* In Equation (11-2)'s order: the effective address then the operation. The
   * full-format `fea (d16,An,Xn)` is `6(1/0/0)` and `7(1/1/0)` in §11.6.1, and
   * `MOVE Source,Dn` is 2 in §11.6.6 -- the two rows §11.3.3 names, giving its
   * "2 + 7 = 9". */
  const ap_m68030_timing_t move[] = {
      {.head = 0, .tail = 0, .cache_case = 6, .no_cache_case = 7, .prefetches = 1},
      {.head = 0, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1},
  };
  TEST_ASSERT_EQUAL_UINT64(9u, ap_m68030_no_cache_total(move, 2u));

  /* And the two instructions of the example together, the CMPI's own average
   * being seven. */
  const ap_m68030_timing_t sequence[] = {
      move[0],
      move[1],
      {.head = 0, .tail = 0, .cache_case = 7, .no_cache_case = 7, .prefetches = 1},
  };
  TEST_ASSERT_EQUAL_UINT64(16u, ap_m68030_no_cache_total(sequence, 3u));
}

/* The two columns compose by two different rules, and this is the assertion
 * that they are not quietly the same function. Given components with a real
 * head and tail, the cache case saves the overlap and the no-cache case does
 * not -- "the no-cache-case time assumes no overlap".
 *
 * Without this, an implementation that ran NCC figures through the overlap
 * accumulator would pass every test above whose components happen to have a
 * zero head. */
static void test_the_no_cache_case_takes_no_overlap(void) {
  const ap_m68030_timing_t components[] = {
      {.head = 0, .tail = 2, .cache_case = 4, .no_cache_case = 4, .prefetches = 1},
      {.head = 2, .tail = 0, .cache_case = 4, .no_cache_case = 4, .prefetches = 1},
  };

  ap_m68030_overlap_state_t state = ap_m68030_overlap_begin();
  ap_m68030_overlap_add(&state, &components[0]);
  ap_m68030_overlap_add(&state, &components[1]);
  TEST_ASSERT_EQUAL_UINT64(6u, ap_m68030_overlap_total(&state));

  TEST_ASSERT_EQUAL_UINT64(8u, ap_m68030_no_cache_total(components, 2u));
}

/* §11.3.3's example makes one further statement, and it is the one that decides
 * what a per-instruction figure from this core can be compared against: the
 * MOVE "is eight clocks for even alignment and 10 clocks for odd alignment, an
 * average of nine clocks", while "the total execution time of the two
 * instructions ... is 16 clocks for both even and odd alignment".
 *
 * So alignment moves cost *between* adjacent instructions rather than adding
 * it, and the published 9 is a number that instruction never takes. This
 * asserts the relationship the numbers must satisfy -- the average of the two
 * alignment cases is the published figure, and the pair is alignment-invariant
 * -- so that the shape of the check on this core is pinned even before it can
 * produce the figures. Our own core exhibits exactly this alternation
 * (`FINDINGS.md` C7), which is why a per-instruction comparison against a
 * published NCC is the wrong comparison and a sequence is the right one. */
static void test_the_published_figure_is_the_mean_of_two_alignments(void) {
  const unsigned even = 8u;
  const unsigned odd = 10u;
  TEST_ASSERT_EQUAL_UINT(9u, (even + odd) / 2u);

  /* The CMPI takes the other half of each alignment's cost: 16 both ways. */
  const unsigned cmpi_even = 16u - even;
  const unsigned cmpi_odd = 16u - odd;
  TEST_ASSERT_EQUAL_UINT(7u, (cmpi_even + cmpi_odd) / 2u);
  TEST_ASSERT_EQUAL_UINT(16u, even + cmpi_even);
  TEST_ASSERT_EQUAL_UINT(16u, odd + cmpi_odd);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_no_cache_case_composes_by_addition);
  RUN_TEST(test_the_no_cache_case_takes_no_overlap);
  RUN_TEST(test_the_published_figure_is_the_mean_of_two_alignments);
  RUN_TEST(test_the_overlap_is_the_lesser_of_the_tail_and_the_head);
  RUN_TEST(test_the_manuals_worked_example_comes_to_six_clocks);
  RUN_TEST(test_the_head_and_tail_are_taken_from_different_instructions);
  RUN_TEST(test_an_instruction_can_cost_nothing_at_all);
  RUN_TEST(test_the_first_instruction_overlaps_with_nothing);
  RUN_TEST(test_an_empty_sequence_costs_nothing);
  RUN_TEST(test_overlap_is_pairwise_and_not_cumulative);
  RUN_TEST(test_a_head_or_tail_longer_than_the_instruction_is_inconsistent);
  RUN_TEST(test_a_bus_cycle_hidden_under_microcode_costs_nothing);
  RUN_TEST(test_bus_time_beyond_the_microcode_is_what_costs);
  RUN_TEST(test_a_long_instruction_hides_its_whole_fetch);
  RUN_TEST(test_an_untranscribed_instruction_is_its_bus_time);
  return UNITY_END();
}

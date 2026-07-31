/* MC68030 published instruction timings, `[030]` §11.6.
 *
 * A transcription cannot be checked by re-reading it, so these tests check it
 * against *structure*: the patterns that repeat across the table, the internal
 * consistency rule the overlap module already enforces, and the markers the
 * table itself carries. A row mistyped in a way that breaks none of those is
 * still possible — but a row mistyped at random almost certainly breaks one.
 */

#include "cpu/m68030/ap_m68030_timing_table.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Every row must satisfy the rule §11.3.2 states in prose: "the heads of some
 * instructions equal the total instruction-cache-case time", so a head may
 * equal the cache case but never exceed it, and the same for the tail. A digit
 * dropped or doubled in transcription usually breaks this. */
static void test_every_transcribed_row_is_internally_consistent(void) {
  unsigned count = 0;
  const ap_m68030_table_entry_t *table = ap_m68030_timing_table(&count);
  TEST_ASSERT_TRUE(count > 0u);

  for (unsigned i = 0; i < count; i++) {
    TEST_ASSERT_TRUE_MESSAGE(
        ap_m68030_timing_consistent(&table[i].timing), table[i].form);
    /* A zero cache case would mean an instruction that takes no time at all,
     * which no row in §11.6 shows -- overlap can absorb an instruction's cost
     * entirely, but its own CC is never zero. */
    TEST_ASSERT_TRUE_MESSAGE(table[i].timing.cache_case > 0u, table[i].form);
  }
}

/* The pattern that appears six times across ADDA, SUBA and CMPA: the word-size
 * address forms cost 4 and the long forms cost 2. It is also the direction that
 * makes physical sense, since the word form sign-extends its source to 32 bits
 * and the long form does not.
 *
 * This is the pattern that showed §11.3.4's worked example to be mislabelled --
 * it gives `SUBA.L` the word form's 4 -- so pinning it here is what stops the
 * table drifting back towards that example. */
static void test_the_word_address_forms_cost_more_than_the_long_ones(void) {
  const ap_m68030_table_entry_t *adda_word =
      ap_m68030_timing_for_word(0xD0C0u); /* ADDA.W D0,A0 */
  const ap_m68030_table_entry_t *adda_long =
      ap_m68030_timing_for_word(0xD1C0u); /* ADDA.L D0,A0 */
  TEST_ASSERT_NOT_NULL(adda_word);
  TEST_ASSERT_NOT_NULL(adda_long);
  TEST_ASSERT_EQUAL_UINT(4u, adda_word->timing.cache_case);
  TEST_ASSERT_EQUAL_UINT(2u, adda_long->timing.cache_case);
  TEST_ASSERT_EQUAL_UINT(4u, adda_word->timing.head);
  TEST_ASSERT_EQUAL_UINT(2u, adda_long->timing.head);

  const ap_m68030_table_entry_t *suba_word =
      ap_m68030_timing_for_word(0x90C0u); /* SUBA.W D0,A0 */
  const ap_m68030_table_entry_t *suba_long =
      ap_m68030_timing_for_word(0x91C0u); /* SUBA.L D0,A0 */
  TEST_ASSERT_NOT_NULL(suba_word);
  TEST_ASSERT_NOT_NULL(suba_long);
  TEST_ASSERT_EQUAL_UINT(4u, suba_word->timing.cache_case);
  /* The value §11.3.4's example contradicts. The table says 2 and six rows
   * agree with it. */
  TEST_ASSERT_EQUAL_UINT(2u, suba_long->timing.cache_case);

  const ap_m68030_table_entry_t *cmpa =
      ap_m68030_timing_for_word(0xB0C0u); /* CMPA.W D0,A0 */
  TEST_ASSERT_NOT_NULL(cmpa);
  TEST_ASSERT_EQUAL_UINT(4u, cmpa->timing.cache_case);
}

/* The ordinary register-to-register operations all cost two clocks with a head
 * of two and no tail. That uniformity is itself a check: a row mistyped among
 * them stands out against the other six. */
static void test_the_register_operations_agree_with_each_other(void) {
  const uint16_t words[] = {
      0xD200u, /* ADD.B D0,D1  */
      0x9200u, /* SUB.B D0,D1  */
      0xC200u, /* AND.B D0,D1  */
      0x8200u, /* OR.B  D0,D1  */
      0xB200u, /* CMP.B D0,D1  */
      0xB300u, /* EOR.B D1,D0  */
      0x7000u, /* MOVEQ #0,D0  */
      0x5200u, /* ADDQ.B #1,D0 */
      0x5300u, /* SUBQ.B #1,D0 */
  };

  for (unsigned i = 0; i < sizeof words / sizeof words[0]; i++) {
    const ap_m68030_table_entry_t *entry = ap_m68030_timing_for_word(words[i]);
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(2u, entry->timing.cache_case, entry->form);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(2u, entry->timing.head, entry->form);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, entry->timing.tail, entry->form);
  }
}

/* "+ Indicates Maximum Time (Actual time is data dependent)". The divides carry
 * that marker, and it must survive into the table: a caller using 56 clocks for
 * every DIVS.W would be slow by a data-dependent amount rather than wrong by a
 * fixed one, which is far harder to notice. */
static void test_the_divides_are_marked_data_dependent(void) {
  const ap_m68030_table_entry_t *divu =
      ap_m68030_timing_for_word(0x80C0u); /* DIVU.W D0,D0 */
  const ap_m68030_table_entry_t *divs =
      ap_m68030_timing_for_word(0x81C0u); /* DIVS.W D0,D0 */

  TEST_ASSERT_NOT_NULL(divu);
  TEST_ASSERT_NOT_NULL(divs);
  TEST_ASSERT_TRUE(divu->data_dependent);
  TEST_ASSERT_TRUE(divs->data_dependent);
  TEST_ASSERT_EQUAL_UINT(44u, divu->timing.cache_case);
  TEST_ASSERT_EQUAL_UINT(56u, divs->timing.cache_case);

  /* And a signed divide costing more than an unsigned one is the direction to
   * expect, which is a weak but real check on not having swapped the pair. */
  TEST_ASSERT_TRUE(divs->timing.cache_case > divu->timing.cache_case);

  /* Nothing else is marked: a marker applied too widely would make every figure
   * look provisional and none of them actionable. */
  unsigned count = 0;
  const ap_m68030_table_entry_t *table = ap_m68030_timing_table(&count);
  unsigned marked = 0;
  for (unsigned i = 0; i < count; i++) {
    if (table[i].data_dependent) {
      marked++;
    }
  }
  TEST_ASSERT_EQUAL_UINT(4u, marked); /* DIVS.W, DIVS.L, DIVU.W, DIVU.L */
}

/* The lookup returns NULL for what is not transcribed, and that is the honest
 * answer rather than a gap to paper over. A memory form's published figure
 * needs an effective address time this module does not carry, so returning the
 * register row for it would under-count by a whole memory access. */
static void test_what_is_not_transcribed_is_reported_as_absent(void) {
  /* ADD.B (A0),D0 -- a memory source, whose row is footnoted "Add Fetch
   * Effective Address Time". */
  TEST_ASSERT_NULL(ap_m68030_timing_for_word(0xD010u));
  /* MULU.W D0,D0 -- family 1100's wide form, not transcribed. */
  TEST_ASSERT_NULL(ap_m68030_timing_for_word(0xC0C0u));
  /* SWAP, which lives in §11.6.13's miscellaneous table and has not been read
   * yet. NOP stood here until §11.6.16 was transcribed -- a placeholder for
   * "not covered" needs replacing whenever coverage grows, which is the right
   * kind of churn. */
  TEST_ASSERT_NULL(ap_m68030_timing_for_word(0x4840u)); /* SWAP D0 */
  /* A register-count shift, whose cost the table marks as count-dependent. */
  TEST_ASSERT_NULL(ap_m68030_timing_for_word(0xE2A8u)); /* LSR.L D1,D0 */
}

/* Every row names the form as §11.6 writes it, so a figure can be traced back
 * to a line in the manual rather than to someone's reading of it. */
static void test_every_row_names_its_form(void) {
  unsigned count = 0;
  const ap_m68030_table_entry_t *table = ap_m68030_timing_table(&count);

  for (unsigned i = 0; i < count; i++) {
    TEST_ASSERT_NOT_NULL(table[i].form);
    TEST_ASSERT_TRUE(table[i].form[0] != '\0');
  }
}

/* The transcribed figures compose through Equation (11-1), which is the whole
 * reason they were transcribed. Two register operations back to back: the
 * second's head of 2 meets the first's tail of 0, so nothing overlaps and the
 * total is the plain sum. */
static void test_the_figures_compose_through_the_overlap_rule(void) {
  const ap_m68030_table_entry_t *add = ap_m68030_timing_for_word(0xD200u);
  const ap_m68030_table_entry_t *sub = ap_m68030_timing_for_word(0x9200u);
  TEST_ASSERT_NOT_NULL(add);
  TEST_ASSERT_NOT_NULL(sub);

  ap_m68030_overlap_state_t state = ap_m68030_overlap_begin();
  ap_m68030_overlap_add(&state, &add->timing);
  ap_m68030_overlap_add(&state, &sub->timing);

  /* 2 + [2 - min(2,0)] = 4. The tail of zero is what makes these instructions
   * unable to absorb the next one's head, which is why a run of register
   * operations costs the sum of its parts. */
  TEST_ASSERT_EQUAL_UINT64(4u, ap_m68030_overlap_total(&state));
}

/* §11.6.16's control rows, identified by whole instruction words rather than by
 * family, since each is a single encoding. The `LINK`/`UNLK` pair splits on bit
 * 3 of `$4E5x` — the same split `ap_m68030_control_decode` makes, so the two
 * modules agree about where the boundary is rather than each having its own. */
static void test_the_control_instructions_are_found_by_their_encodings(void) {
  const ap_m68030_table_entry_t *nop = ap_m68030_timing_for_word(0x4E71u);
  TEST_ASSERT_NOT_NULL(nop);
  TEST_ASSERT_EQUAL_UINT(2u, nop->timing.cache_case);
  /* NOP's head is zero, unlike most register operations: an instruction that
   * does nothing still cannot be overlapped away by its predecessor. */
  TEST_ASSERT_EQUAL_UINT(0u, nop->timing.head);

  const ap_m68030_table_entry_t *rts = ap_m68030_timing_for_word(0x4E75u);
  TEST_ASSERT_NOT_NULL(rts);
  TEST_ASSERT_EQUAL_UINT(9u, rts->timing.cache_case);
  TEST_ASSERT_EQUAL_UINT(11u, rts->timing.no_cache_case);

  /* LINK.W is $4E5x with bit 3 clear and UNLK the same range with it set. */
  for (unsigned reg = 0; reg < 8u; reg++) {
    const ap_m68030_table_entry_t *link =
        ap_m68030_timing_for_word((uint16_t)(0x4E50u + reg));
    const ap_m68030_table_entry_t *unlk =
        ap_m68030_timing_for_word((uint16_t)(0x4E58u + reg));
    TEST_ASSERT_NOT_NULL(link);
    TEST_ASSERT_NOT_NULL(unlk);
    TEST_ASSERT_EQUAL_UINT(4u, link->timing.cache_case);
    TEST_ASSERT_EQUAL_UINT(5u, unlk->timing.cache_case);
  }

  /* And LINK.L is a different encoding entirely, at $480x, costing more. */
  const ap_m68030_table_entry_t *link_long =
      ap_m68030_timing_for_word(0x4808u);
  TEST_ASSERT_NOT_NULL(link_long);
  TEST_ASSERT_EQUAL_UINT(6u, link_long->timing.cache_case);
}

/* Writing the status register costs 12 clocks — six times the same logical
 * operation on a data register. That is the pipe refilling, and it is the same
 * fact §8.1.7 gives as the reason these instructions count as a change of flow
 * for tracing: "the processor must re-prefetch instruction words to fill the
 * pipe again any time an instruction that can modify the status register is
 * executed."
 *
 * Two independent parts of the manual agreeing about one instruction's
 * behaviour is worth pinning: a transcription that had this at 2, matching its
 * data-register sibling, would contradict the trace rule this core already
 * implements. */
static void test_a_status_register_write_costs_a_pipe_refill(void) {
  unsigned count = 0;
  const ap_m68030_table_entry_t *table = ap_m68030_timing_table(&count);

  const ap_m68030_table_entry_t *status = nullptr;
  for (unsigned i = 0; i < count; i++) {
    if (table[i].timing.cache_case == 12u) {
      status = &table[i];
    }
  }
  TEST_ASSERT_NOT_NULL(status);
  TEST_ASSERT_EQUAL_UINT(14u, status->timing.no_cache_case);

  /* Six times the register-operand form of the same logical operation. */
  const ap_m68030_table_entry_t *ordinary = ap_m68030_timing_for_word(0xC200u);
  TEST_ASSERT_NOT_NULL(ordinary);
  TEST_ASSERT_EQUAL_UINT(6u * ordinary->timing.cache_case,
                         status->timing.cache_case);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_every_transcribed_row_is_internally_consistent);
  RUN_TEST(test_the_word_address_forms_cost_more_than_the_long_ones);
  RUN_TEST(test_the_register_operations_agree_with_each_other);
  RUN_TEST(test_the_divides_are_marked_data_dependent);
  RUN_TEST(test_what_is_not_transcribed_is_reported_as_absent);
  RUN_TEST(test_every_row_names_its_form);
  RUN_TEST(test_the_control_instructions_are_found_by_their_encodings);
  RUN_TEST(test_a_status_register_write_costs_a_pipe_refill);
  RUN_TEST(test_the_figures_compose_through_the_overlap_rule);
  return UNITY_END();
}

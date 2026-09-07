/* MC68882 instruction execution timing and concurrency.
 *
 * `[881]` §8.5. The figures are Table 8-3's and the composition rule is
 * §8.5.1.3's, and the check on both is **Table 8-5's own worked example** --
 * the manual composing eight instructions and printing what they cost. A
 * transcription checked against numbers this project produced would be checked
 * against itself.
 */

#include "cpu/m68882/ap_m68882_timing.h"

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* ---------------------------------------------------------------------------
 * Table 8-3, the figures.
 * ------------------------------------------------------------------------- */

/* **`FSIN` is 394 clocks and `FMOVE` is 21**, which is the whole reason this
 * module exists: before it they cost the same, because the only thing charged
 * for a floating-point instruction was its operand bus time.
 *
 * Spot values rather than the whole table, chosen at the two ends and at the
 * two rows whose `H` is not 17. */
static void test_table_8_3_totals(void) {
  TEST_ASSERT_EQUAL_UINT(394u, ap_m68882_operation_clocks(AP_M68882_OP_FSIN));
  TEST_ASSERT_EQUAL_UINT(21u,
                         ap_m68882_operation_clocks(AP_M68882_OP_FMOVE_TO_FPN));
  TEST_ASSERT_EQUAL_UINT(56u, ap_m68882_operation_clocks(AP_M68882_OP_FADD));
  TEST_ASSERT_EQUAL_UINT(108u, ap_m68882_operation_clocks(AP_M68882_OP_FDIV));
  /* The longest and the shortest arithmetic rows in the column. */
  TEST_ASSERT_EQUAL_UINT(696u, ap_m68882_operation_clocks(AP_M68882_OP_FATANH));
  TEST_ASSERT_EQUAL_UINT(34u,
                         ap_m68882_operation_clocks(AP_M68882_OP_FGETMAN));
}

/* **Total = H + T + 4 for every arithmetic row**, which is a property of the
 * transcription rather than of the part: it holds across all thirty-seven of
 * them and nowhere else, so a digit mistyped in any of the three columns breaks
 * it. That makes it a checksum over the table, which is what a transcription of
 * a scanned page most needs and what no individual assertion provides.
 *
 * `FMOVE to FPn` is excluded because it has no tail at all, and `FMOVECR`
 * because Table 8-3 gives it `10/0/32` -- neither is an arithmetic row. */
static void test_head_plus_tail_plus_four_is_the_total(void) {
  static const ap_m68882_operation_t arithmetic[] = {
      AP_M68882_OP_FABS,    AP_M68882_OP_FACOS,   AP_M68882_OP_FADD,
      AP_M68882_OP_FASIN,   AP_M68882_OP_FATAN,   AP_M68882_OP_FATANH,
      AP_M68882_OP_FCMP,    AP_M68882_OP_FCOS,    AP_M68882_OP_FCOSH,
      AP_M68882_OP_FDIV,    AP_M68882_OP_FETOX,   AP_M68882_OP_FETOXM1,
      AP_M68882_OP_FGETEXP, AP_M68882_OP_FGETMAN, AP_M68882_OP_FINT,
      AP_M68882_OP_FINTRZ,  AP_M68882_OP_FLOGN,   AP_M68882_OP_FLOGNP1,
      AP_M68882_OP_FLOG10,  AP_M68882_OP_FLOG2,   AP_M68882_OP_FMOD,
      AP_M68882_OP_FMUL,    AP_M68882_OP_FNEG,    AP_M68882_OP_FREM,
      AP_M68882_OP_FSCALE,  AP_M68882_OP_FSGLDIV, AP_M68882_OP_FSGLMUL,
      AP_M68882_OP_FSIN,    AP_M68882_OP_FSINCOS, AP_M68882_OP_FSINH,
      AP_M68882_OP_FSQRT,   AP_M68882_OP_FSUB,    AP_M68882_OP_FTAN,
      AP_M68882_OP_FTANH,   AP_M68882_OP_FTENTOX, AP_M68882_OP_FTST,
      AP_M68882_OP_FTWOTOX};
  const unsigned count = sizeof arithmetic / sizeof arithmetic[0];
  TEST_ASSERT_EQUAL_UINT(37u, count);

  for (unsigned i = 0; i < count; i++) {
    const ap_m68882_concurrency_t c =
        ap_m68882_operation_concurrency(arithmetic[i]);
    const unsigned total = ap_m68882_operation_clocks(arithmetic[i]);
    TEST_ASSERT_TRUE_MESSAGE(c.has_tail, "an arithmetic row lost its tail");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(17u, c.head,
                                   "an arithmetic row's head is not 17");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(total, c.head + c.tail + 4u,
                                   "H + T + 4 != Total for an arithmetic row");
  }
}

/* **`FMOVE` has no tail, and that is not a tail of zero.** Table 8-3 prints
 * `T = *` and its footnote says "these instruction do not have a tail time. The
 * next instruction's head can be added to determine the effective head time."
 * A zero tail would overlap with nothing; no tail merges into the next head. */
static void test_fmove_has_no_tail(void) {
  const ap_m68882_concurrency_t move =
      ap_m68882_operation_concurrency(AP_M68882_OP_FMOVE_TO_FPN);
  TEST_ASSERT_FALSE_MESSAGE(move.has_tail, "FMOVE was given a tail");
  TEST_ASSERT_EQUAL_UINT(21u, move.head);

  /* And every other row does have one, so the flag distinguishes rather than
   * being always false. */
  TEST_ASSERT_TRUE(ap_m68882_operation_concurrency(AP_M68882_OP_FADD).has_tail);
}

/* ---------------------------------------------------------------------------
 * §8.5.1.3, the rule -- checked against Table 8-5.
 * ------------------------------------------------------------------------- */

/* **Table 8-5, *Timing Calculation Example*, composed whole.**
 *
 * Eight instructions whose MC68882 totals add to 470 and whose effective
 * addresses add to 36. The manual's arithmetic at the foot of the table:
 *
 *     overall 881 time:  557 + 36       = 593
 *     overall 882 time:  470 + 36 - 175 = 331
 *     ratio:             593/331        = 1.80
 *
 * The figures below are the manual's own, taken from Table 8-5's rows rather
 * than from this project's table, because the example uses `.D` memory operands
 * and `ap_m68882_timing.c` transcribes the `FPn to FPm` column. What is under
 * test here is the **rule**, and the rule has to reproduce the manual's 331.
 *
 * The `<ea>` time is added to the total and to the head, except on the two
 * `FMOVE` to memory rows -- Table 8-3's footnote `***` and §8.5.1.3 both
 * exclude it there, and Table 8-5's own cells add it anyway. The header records
 * why that disagreement changes nothing: the tails are below the heads in every
 * pair, so the `min` is the tail whichever head is used. */
static void test_table_8_5_worked_example(void) {
  typedef struct {
    const char *form;
    unsigned total; /* Table 8-5's MC68882 "Times" column */
    unsigned ea;    /* its "<ea> Time" column */
    unsigned head;  /* Table 8-3's H */
    unsigned tail;  /* Table 8-3's T */
    bool has_tail;
    bool to_memory; /* opclass 011, where the <ea> is not added to the head */
  } row_t;

  static const row_t rows[] = {
      {"FMUL.D <ea>,FP1",   95, 6, 36, 58, true,  false},
      {"FMOVE.D FP2,<ea>",  44, 6, 44,  0, false, true},
      {"FADD.D <ea>,FP1",   75, 6, 36, 38, true,  false},
      {"FMOVE.X FP0,FP2",   21, 0, 21,  0, false, false},
      {"FMUL.D <ea>,FP2",   95, 6, 36, 58, true,  false},
      {"FMOVE.D FP1,<ea>",  44, 6, 44,  0, false, true},
      {"FADD.D <ea>,FP2",   75, 6, 36, 38, true,  false},
      {"FMOVE.X FP0,FP1",   21, 0, 21,  0, false, false},
  };
  const unsigned count = sizeof rows / sizeof rows[0];

  /* The two column totals the table prints, so a mistyped row is caught before
   * it can be absorbed into the composition. */
  unsigned times = 0;
  unsigned ea = 0;
  for (unsigned i = 0; i < count; i++) {
    times += rows[i].total;
    ea += rows[i].ea;
  }
  TEST_ASSERT_EQUAL_UINT_MESSAGE(470u, times, "Table 8-5's Times column");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(36u, ea, "Table 8-5's <ea> column");

  ap_m68882_overlap_state_t state = ap_m68882_overlap_begin();
  for (unsigned i = 0; i < count; i++) {
    const unsigned head =
        rows[i].to_memory ? rows[i].head : rows[i].head + rows[i].ea;
    ap_m68882_overlap_add(&state, rows[i].total + rows[i].ea, head,
                          rows[i].tail, rows[i].has_tail);
  }

  /* "overall 882 time: 470 + 36 - 175 = 331". */
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      331u, ap_m68882_overlap_total(&state),
      "Table 8-5's composed MC68882 time");

  /* And the overlap the table attributes to it, recovered by difference:
   * `min` picked 58, 38, 58 and 21 across the four pairs. */
  TEST_ASSERT_EQUAL_UINT64(175u,
                           (uint64_t)(times + ea) -
                               ap_m68882_overlap_total(&state));
}

/* **The same eight instructions with no tails cost their plain sum**, which is
 * §8.5.1.3's other half: "this formula applies to both the MC68881 and the
 * MC68882; **for the MC68881, the overlap between floating-point instructions
 * is zero**."
 *
 * The control for the worked example above -- identical rows, one thing
 * changed -- so an overlap appearing here would have come from the accumulator
 * rather than from the tails.
 *
 * It deliberately does **not** assert Table 8-5's printed MC68881 total. That
 * column's eight row values (98, 86, 78, 33, 98, 86, 78, 33) sum to **590**
 * where the table prints **557**, and the difference is exactly one `FMOVE.X`
 * row's 33. The printed total is the half that is right: the manual's own
 * ratio, `593/331 = 1.80`, is computed from 557 + 36, and 590 + 36 would give
 * 1.89. So a row value is misprinted -- a fourth arithmetic slip in a table
 * that already carries three, and the fourth to leave the MC68882 answer
 * untouched. The MC68882 column, by contrast, sums to its printed 470 exactly,
 * which is what the assertion above checks before composing anything. */
static void test_no_tails_means_no_overlap(void) {
  static const unsigned totals[] = {95, 44, 75, 21, 95, 44, 75, 21};
  static const unsigned eas[] = {6, 6, 6, 0, 6, 6, 6, 0};

  ap_m68882_overlap_state_t state = ap_m68882_overlap_begin();
  unsigned sum = 0;
  for (unsigned i = 0; i < 8u; i++) {
    /* A head still arrives on every one of them, so this also checks that a
     * head alone cannot manufacture an overlap. */
    ap_m68882_overlap_add(&state, totals[i] + eas[i], 36u, 0u, false);
    sum += totals[i] + eas[i];
  }
  TEST_ASSERT_EQUAL_UINT(506u, sum);
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(506u, ap_m68882_overlap_total(&state),
                                   "an overlap appeared with no tails");
}

/* **The overlap is the lesser of the two, and the last row of Table 8-5 is
 * where that matters.**
 *
 * Its `FADD` has a tail of 38 and the trailing `FMOVE` an effective head of 21,
 * and the table's overlap is **21**. A model that took the tail whenever one
 * was available would answer 38 there and total 348 -- close enough to 331 to
 * look plausible and wrong by exactly this row. Checked directly, on two
 * instructions, so the failure names itself. */
static void test_the_overlap_is_the_lesser_of_the_two(void) {
  /* Tail 38 against head 21: the head wins. */
  ap_m68882_overlap_state_t small_head = ap_m68882_overlap_begin();
  ap_m68882_overlap_add(&small_head, 75u, 36u, 38u, true);
  ap_m68882_overlap_add(&small_head, 21u, 21u, 0u, false);
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(75u + 21u - 21u,
                                   ap_m68882_overlap_total(&small_head),
                                   "the head did not bound the overlap");

  /* Tail 38 against head 100: the tail wins. Same pair, one number changed. */
  ap_m68882_overlap_state_t small_tail = ap_m68882_overlap_begin();
  ap_m68882_overlap_add(&small_tail, 75u, 36u, 38u, true);
  ap_m68882_overlap_add(&small_tail, 200u, 100u, 50u, true);
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(75u + 200u - 38u,
                                   ap_m68882_overlap_total(&small_tail),
                                   "the tail did not bound the overlap");

  /* **And the head bounding it in the middle of a sequence**, not only at the
   * end. Both cases above settle through a different path from this one -- the
   * first through the end-of-sequence branch, the second through the tail --
   * so without this the `min` could be replaced by "always the tail" and every
   * assertion here would still hold. A probe found exactly that, and the two
   * copies of the rule it found are now one. */
  ap_m68882_overlap_state_t mid = ap_m68882_overlap_begin();
  ap_m68882_overlap_add(&mid, 100u, 36u, 50u, true); /* tail 50 */
  ap_m68882_overlap_add(&mid, 40u, 10u, 20u, true);  /* head 10, and a tail */
  ap_m68882_overlap_add(&mid, 30u, 5u, 0u, false);
  /* min(50, 10) = 10 at the first boundary, then min(20, 5) = 5 at the end. */
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(100u + 40u + 30u - 10u - 5u,
                                   ap_m68882_overlap_total(&mid),
                                   "the head did not bound a mid-sequence "
                                   "overlap");
}

/* **A no-tail instruction merges its head into the next one's**, which is the
 * `T = *` rule and the part a copy of the 68030's accumulator would miss.
 *
 * Table 8-5's second pair is the case: an `FMUL` with tail 58, then an
 * `FMOVE` with head 44 and no tail, then an `FADD` with head 42. The overlap
 * belongs to the `FMUL`/`FMOVE` boundary and is measured against 44 + 42 = 86,
 * not against 44 -- and here the difference is made visible by giving the
 * `FMUL` a tail of 60, between the two. */
static void test_a_no_tail_instruction_merges_into_the_next_head(void) {
  ap_m68882_overlap_state_t state = ap_m68882_overlap_begin();
  ap_m68882_overlap_add(&state, 100u, 36u, 60u, true); /* tail 60 */
  ap_m68882_overlap_add(&state, 44u, 44u, 0u, false);  /* no tail, head 44 */
  ap_m68882_overlap_add(&state, 75u, 42u, 38u, true);  /* head 42 */

  /* Effective head is 44 + 42 = 86, so the overlap is min(60, 86) = 60.
   * Without the merge it would be min(60, 44) = 44, and the total 231. */
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(100u + 44u + 75u - 60u,
                                   ap_m68882_overlap_total(&state),
                                   "the FMOVE's head did not merge forward");
}

/* **Two no-tail instructions in a row: a reading, pinned as one.**
 *
 * §8.5.1.3 says "the effective head time is the sum of the FMOVE H time plus
 * the H time of **the** subsequent instruction", and Table 8-5 never puts two
 * `FMOVE`s together, so the manual does not say what a run of them does. This
 * core accumulates: the tail before the run overlaps with the whole of it,
 * until something with a tail arrives.
 *
 * The argument for accumulating rather than keeping only the last head is that
 * each instruction's *total* was charged, so dropping a head would leave time
 * accounted for in one column and not the other -- and a no-tail instruction is
 * precisely one that cannot end an overlap. It is still an extension of the
 * rule and not the rule, which is why this test exists: to stop the choice
 * changing silently, not to claim the manual made it. */
static void test_consecutive_no_tail_instructions_accumulate(void) {
  ap_m68882_overlap_state_t state = ap_m68882_overlap_begin();
  ap_m68882_overlap_add(&state, 100u, 36u, 90u, true); /* tail 90 */
  ap_m68882_overlap_add(&state, 21u, 21u, 0u, false);  /* no tail, head 21 */
  ap_m68882_overlap_add(&state, 21u, 21u, 0u, false);  /* no tail, head 21 */
  ap_m68882_overlap_add(&state, 75u, 42u, 38u, true);  /* head 42 */

  /* Effective head 21 + 21 + 42 = 84, so the overlap is min(90, 84) = 84.
   * Keeping only the last no-tail head would give 21 + 42 = 63. The tail of 90
   * is deliberately above both, so the assertion turns on the head alone. */
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(100u + 21u + 21u + 75u - 84u,
                                   ap_m68882_overlap_total(&state),
                                   "consecutive no-tail heads did not "
                                   "accumulate");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_table_8_3_totals);
  RUN_TEST(test_head_plus_tail_plus_four_is_the_total);
  RUN_TEST(test_fmove_has_no_tail);
  RUN_TEST(test_table_8_5_worked_example);
  RUN_TEST(test_no_tails_means_no_overlap);
  RUN_TEST(test_the_overlap_is_the_lesser_of_the_two);
  RUN_TEST(test_a_no_tail_instruction_merges_into_the_next_head);
  RUN_TEST(test_consecutive_no_tail_instructions_accumulate);
  return UNITY_END();
}

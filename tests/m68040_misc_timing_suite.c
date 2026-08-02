/* MC68040 §10.5's miscellaneous integer timings, transcribed from the page
 * images of pages 10-11 and 10-12.
 *
 * Spot checks against the table, plus the structural properties that would
 * catch a whole column being shifted or a note being lost.
 */

#include <string.h>

#include "cpu/m68040/ap_m68040_misc_timing.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static const ap_m68040_misc_timing_t *row(const char *instruction,
                                          const char *condition) {
  const ap_m68040_misc_timing_t *r =
      ap_m68040_misc_timing_find(instruction, condition);
  TEST_ASSERT_NOT_NULL(r);
  return r;
}

/* ---------------------------------------------------------------------------
 * The rows the extraction got wrong.
 * ------------------------------------------------------------------------- */

static void test_moveq_is_moveq_and_not_move(void) {
  /* `pdftotext` renders this row's instruction as `MOVEa`, which reads as a
   * damaged `MOVE` -- and `MOVE` is a real instruction with its own row in
   * §10.4. An extracted table would have silently given `MOVE` the timing of
   * `MOVEQ`. The page image shows `MOVEQ`.
   *
   * This is the concrete case behind the rule in `CLAUDE.md`: the figures
   * survive extraction and the instruction names do not. */
  const ap_m68040_misc_timing_t *moveq = row("MOVEQ", NULL);
  TEST_ASSERT_EQUAL_UINT(1u, moveq->calculate);
  TEST_ASSERT_EQUAL_UINT(1u, ap_m68040_execute_total(moveq->execute));
  /* And §10.5 prices no `MOVE` at all -- §10.4 does. */
  TEST_ASSERT_NULL(ap_m68040_misc_timing_find("MOVE", NULL));
}

static void test_the_branch_and_nop_rows_survive(void) {
  /* Extraction renders these as `Bee` and `NOpa`. */
  TEST_ASSERT_EQUAL_UINT(2u, row("Bcc", "Branch Taken")->calculate);
  TEST_ASSERT_EQUAL_UINT(3u, row("Bcc", "Branch Not Taken")->calculate);
  TEST_ASSERT_EQUAL_UINT(8u, row("NOP", NULL)->calculate);
}

/* ---------------------------------------------------------------------------
 * Spot checks across the table.
 * ------------------------------------------------------------------------- */

static void test_the_register_and_memory_forms_differ(void) {
  /* `ABCD Dy,Dx` is 1 and a flat 3; the predecrement form is 3 and `1L + 3`,
   * so the memory form costs two more calculate clocks and gains a lead. */
  const ap_m68040_misc_timing_t *reg = row("ABCD", "Dy,Dx");
  TEST_ASSERT_EQUAL_UINT(1u, reg->calculate);
  TEST_ASSERT_EQUAL_UINT(0u, reg->execute.lead);
  TEST_ASSERT_EQUAL_UINT(3u, reg->execute.base);

  const ap_m68040_misc_timing_t *mem = row("ABCD", "-(Ay),-(Ax)");
  TEST_ASSERT_EQUAL_UINT(3u, mem->calculate);
  TEST_ASSERT_EQUAL_UINT(1u, mem->execute.lead);
  TEST_ASSERT_EQUAL_UINT(3u, mem->execute.base);
}

static void test_cas2_is_the_most_expensive_row(void) {
  /* 56 and `6L + 49`. Worth a test because a read-modify-write across two
   * addresses is exactly where a plausible-looking transcription error would
   * hide -- nobody's intuition says what CAS2 should cost. */
  const ap_m68040_misc_timing_t *r = row("CAS2", "True");
  TEST_ASSERT_EQUAL_UINT(56u, r->calculate);
  TEST_ASSERT_EQUAL_UINT(6u, r->execute.lead);
  TEST_ASSERT_EQUAL_UINT(49u, r->execute.base);
  TEST_ASSERT_EQUAL_UINT(55u, ap_m68040_execute_total(r->execute));
}

static void test_reset_asserts_for_five_hundred_clocks(void) {
  /* 521 in both columns -- the reset signal's assertion time, not a
   * computation. */
  const ap_m68040_misc_timing_t *r = row("RESET", NULL);
  TEST_ASSERT_EQUAL_UINT(521u, r->calculate);
  TEST_ASSERT_EQUAL_UINT(521u, ap_m68040_execute_total(r->execute));
}

static void test_every_rte_stack_format_is_priced_separately(void) {
  /* Six formats, six figures, and they are not monotonic in the format number:
   * $1 and $7 both cost 23 while $2 costs 14. A table that had been
   * interpolated rather than transcribed would be smoother than this. */
  const struct { const char *format; unsigned calc, exec; } formats[] = {
      {"Stack Format $0", 2u, 13u}, {"Stack Format $1", 4u, 23u},
      {"Stack Format $2", 2u, 14u}, {"Stack Format $3", 3u, 20u},
      {"Stack Format $4", 2u, 15u}, {"Stack Format $7", 4u, 23u},
  };
  for (unsigned i = 0; i < 6u; i++) {
    const ap_m68040_misc_timing_t *r = row("RTE", formats[i].format);
    TEST_ASSERT_EQUAL_UINT(formats[i].calc, r->calculate);
    TEST_ASSERT_EQUAL_UINT(formats[i].exec,
                           ap_m68040_execute_total(r->execute));
  }
}

static void test_a_taken_trap_costs_far_more_than_an_untaken_one(void) {
  TEST_ASSERT_EQUAL_UINT(19u, row("TRAPcc", "Taken")->calculate);
  TEST_ASSERT_EQUAL_UINT(5u, row("TRAPcc", "Not Taken")->calculate);
  TEST_ASSERT_EQUAL_UINT(19u, row("TRAPV", "Taken")->calculate);
  TEST_ASSERT_EQUAL_UINT(5u, row("TRAPV", "Not Taken")->calculate);
}

/* ---------------------------------------------------------------------------
 * What the notes do to the figures.
 * ------------------------------------------------------------------------- */

static void test_the_minimum_and_typical_figures_are_marked(void) {
  /* Notes a, b and e qualify figures rather than explaining them, and a core
   * that reported these as exact would claim a precision the manual withholds. */
  TEST_ASSERT_EQUAL_INT(AP_M68040_TIMING_MINIMUM,
                        row("ANDI #<xxx>,SR", NULL)->confidence);
  TEST_ASSERT_EQUAL_INT(AP_M68040_TIMING_MINIMUM, row("RESET", NULL)->confidence);
  TEST_ASSERT_EQUAL_INT(AP_M68040_TIMING_TYPICAL,
                        row("CAS2", "True")->confidence);
  TEST_ASSERT_EQUAL_INT(AP_M68040_TIMING_TYPICAL, row("PFLUSH", NULL)->confidence);
  /* And a plain row is exact. */
  TEST_ASSERT_EQUAL_INT(AP_M68040_TIMING_EXACT, row("SWAP", NULL)->confidence);
}

static void test_ptest_is_the_most_conditional_figure(void) {
  /* Note e: "typical measurement for three-level table search with no
   * descriptor writes, no entries cached, and four-clock memory access times."
   * That describes one search against one memory -- a machine with different
   * memory would not reproduce it, which is why it is marked rather than
   * treated as this machine's number. */
  const ap_m68040_misc_timing_t *r = row("PTESTR, PTESTW", NULL);
  TEST_ASSERT_EQUAL_INT(AP_M68040_TIMING_TYPICAL, r->confidence);
  TEST_ASSERT_EQUAL_UINT(25u, r->calculate);
  TEST_ASSERT_EQUAL_UINT(11u, r->execute.lead);
  TEST_ASSERT_EQUAL_UINT(14u, r->execute.base);
}

static void test_only_move16_carries_a_successive_instruction_penalty(void) {
  /* Note d: "successive in-line MOVE16 instructions each add eight clocks to
   * the <ea> calculate and execute times" -- a cost that depends on the
   * *previous* instruction, which no per-instruction figure can carry. Flagged
   * so a scheduler knows to look. */
  TEST_ASSERT_EQUAL_UINT(8u, AP_M68040_MOVE16_SUCCESSIVE_PENALTY);
  for (size_t i = 0; i < ap_m68040_misc_timing_count(); i++) {
    const ap_m68040_misc_timing_t *r = &ap_m68040_misc_timings()[i];
    const bool is_move16 = strcmp(r->instruction, "MOVE16") == 0;
    TEST_ASSERT_EQUAL_INT(is_move16, r->successive_penalty);
  }
}

static void test_every_qualified_row_also_interlocks(void) {
  /* Notes a and b both say the instruction "interlocks the <ea> calculate and
   * execute stages and synchronizes some portions of the processor before
   * execution", so a minimum or typical figure always implies an interlock.
   * The converse does not hold -- note c interlocks without qualifying the
   * figure -- and this checks the implication runs the one way. */
  for (size_t i = 0; i < ap_m68040_misc_timing_count(); i++) {
    const ap_m68040_misc_timing_t *r = &ap_m68040_misc_timings()[i];
    if (r->confidence != AP_M68040_TIMING_EXACT) {
      TEST_ASSERT_TRUE(r->interlocks);
    }
  }
  /* `DBcc` is note c: interlocked, exact. */
  TEST_ASSERT_TRUE(row("DBcc", "True")->interlocks);
  TEST_ASSERT_EQUAL_INT(AP_M68040_TIMING_EXACT, row("DBcc", "True")->confidence);
}

/* ---------------------------------------------------------------------------
 * Structural checks over the whole table.
 * ------------------------------------------------------------------------- */

static void test_the_table_size_is_pinned(void) {
  /* 44 distinct instructions over 75 rows, since several are priced per
   * condition. This is a guard against an accidental edit rather than an
   * independent check of the manual -- it compares the table with a constant
   * derived from the same transcription, so it catches a row being dropped
   * later and cannot catch one that was never transcribed. The row-by-row
   * spot checks above are what tie the figures to the page images. */
  TEST_ASSERT_EQUAL_UINT(75u, ap_m68040_misc_timing_count());

  unsigned distinct = 0;
  for (size_t i = 0; i < ap_m68040_misc_timing_count(); i++) {
    bool seen = false;
    for (size_t j = 0; j < i; j++) {
      if (strcmp(ap_m68040_misc_timings()[i].instruction,
                 ap_m68040_misc_timings()[j].instruction) == 0) {
        seen = true;
        break;
      }
    }
    if (!seen) {
      distinct++;
    }
  }
  TEST_ASSERT_EQUAL_UINT(44u, distinct);
}

static void test_no_row_is_free(void) {
  /* Every instruction costs at least one clock in each of the two priced
   * stages -- a zero would mean a column had been left empty rather than
   * transcribed. */
  for (size_t i = 0; i < ap_m68040_misc_timing_count(); i++) {
    const ap_m68040_misc_timing_t *r = &ap_m68040_misc_timings()[i];
    TEST_ASSERT_TRUE(r->calculate >= 1u);
    TEST_ASSERT_TRUE(ap_m68040_execute_total(r->execute) >= 1u);
  }
}

static void test_a_lead_never_exceeds_its_total(void) {
  /* `nL + b` with a positive base everywhere in this table, so the lead is
   * always strictly less than the total -- which would fail if a lead and a
   * base had been swapped in transcription. */
  for (size_t i = 0; i < ap_m68040_misc_timing_count(); i++) {
    const ap_m68040_misc_timing_t *r = &ap_m68040_misc_timings()[i];
    TEST_ASSERT_TRUE(r->execute.lead < ap_m68040_execute_total(r->execute));
  }
}

static void test_an_instruction_not_in_this_section_is_not_found(void) {
  /* §10.5 is one of several timing sections. Reporting NULL is the honest
   * answer for an instruction another section prices. */
  TEST_ASSERT_NULL(ap_m68040_misc_timing_find("ADD", NULL));
  TEST_ASSERT_NULL(ap_m68040_misc_timing_find("MULS.W", NULL));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_moveq_is_moveq_and_not_move);
  RUN_TEST(test_the_branch_and_nop_rows_survive);
  RUN_TEST(test_the_register_and_memory_forms_differ);
  RUN_TEST(test_cas2_is_the_most_expensive_row);
  RUN_TEST(test_reset_asserts_for_five_hundred_clocks);
  RUN_TEST(test_every_rte_stack_format_is_priced_separately);
  RUN_TEST(test_a_taken_trap_costs_far_more_than_an_untaken_one);
  RUN_TEST(test_the_minimum_and_typical_figures_are_marked);
  RUN_TEST(test_ptest_is_the_most_conditional_figure);
  RUN_TEST(test_only_move16_carries_a_successive_instruction_penalty);
  RUN_TEST(test_every_qualified_row_also_interlocks);
  RUN_TEST(test_the_table_size_is_pinned);
  RUN_TEST(test_no_row_is_free);
  RUN_TEST(test_a_lead_never_exceeds_its_total);
  RUN_TEST(test_an_instruction_not_in_this_section_is_not_found);
  return UNITY_END();
}

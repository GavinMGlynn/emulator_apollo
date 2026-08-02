/* MC68040 §10.7.1 and §10.7.2: what the integer unit spends supporting the FPU.
 *
 * Every test names a fact about the hardware or about the table's shape. The
 * module header carries the semantics; these pin the figures. */

#include "cpu/m68040/ap_m68040_fpu_timing.h"

#include <string.h>

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static unsigned total(ap_m68040_fpu_cell_t c) {
  return ap_m68040_execute_total(c.execute);
}

/* ---------------------------------------------------------------------------
 * §10.7.2's shape.
 * ------------------------------------------------------------------------- */

static void test_a_floating_point_operand_comes_from_fpn_and_never_from_an(void) {
  /* §10.6's `An` row is replaced by an `FPn` row -- the two tables index
   * different things at that position. There is no addressing mode by which a
   * floating-point instruction reads an address register, and every mode by
   * which it reads a floating-point one is `FPn`. */
  bool priced = false;
  for (unsigned f = 0; f < AP_M68040_FPU_FORMAT_COUNT; f++) {
    if (ap_m68040_fpu_support((ap_m68040_fpu_format_t)f, AP_M68040_FPU_FPN)
            .valid)
      priced = true;
  }
  TEST_ASSERT_TRUE(priced);
}

static void test_an_fp_register_is_priced_only_as_extended(void) {
  /* `FPn` is dashed for every format but extended, and the hardware reason is
   * that there is no other case: §9's register file holds extended precision
   * only, so an `FPn` source *is* an extended source however the size field
   * reads. A table entry for `FPn` at single precision would be pricing an
   * encoding the register file cannot produce. */
  for (unsigned f = 0; f < AP_M68040_FPU_FORMAT_COUNT; f++) {
    const bool expected = f == AP_M68040_FPU_FORMAT_EXTENDED;
    TEST_ASSERT_EQUAL_MESSAGE(
        expected,
        ap_m68040_fpu_support((ap_m68040_fpu_format_t)f, AP_M68040_FPU_FPN)
            .valid,
        "FPn should be priced at extended precision and nowhere else");
  }
  const ap_m68040_fpu_cell_t fpn = ap_m68040_fpu_support(
      AP_M68040_FPU_FORMAT_EXTENDED, AP_M68040_FPU_FPN);
  TEST_ASSERT_EQUAL_UINT(2u, fpn.calculate);
  TEST_ASSERT_EQUAL_UINT(1u, fpn.execute.lead);
  TEST_ASSERT_EQUAL_UINT(2u, fpn.execute.base);
}

static void test_a_data_register_cannot_hold_more_than_thirty_two_bits(void) {
  /* The mirror of the previous fact. `Dn` is priced for byte/word, long word
   * and single precision -- everything that fits in 32 bits -- and dashed for
   * double and extended, which do not. The table encodes the register width,
   * not a decision about which operations are useful. */
  const bool expected[AP_M68040_FPU_FORMAT_COUNT] = {true, true, true, false,
                                                     false};
  for (unsigned f = 0; f < AP_M68040_FPU_FORMAT_COUNT; f++) {
    TEST_ASSERT_EQUAL_MESSAGE(
        expected[f],
        ap_m68040_fpu_support((ap_m68040_fpu_format_t)f, AP_M68040_FPU_DN)
            .valid,
        "Dn should be priced exactly for the formats that fit in 32 bits");
  }
}

static void test_no_format_and_mode_pair_is_priced_twice_over(void) {
  /* Structural: `FPn` and `Dn` are the only rows any format dashes. Every
   * memory mode is priced in all five formats, because a memory operand of any
   * format can be reached by any of them. */
  for (unsigned f = 0; f < AP_M68040_FPU_FORMAT_COUNT; f++) {
    for (unsigned m = 0; m < AP_M68040_FPU_MODE_COUNT; m++) {
      const bool dashable =
          m == AP_M68040_FPU_FPN || m == AP_M68040_FPU_DN;
      const bool valid =
          ap_m68040_fpu_support((ap_m68040_fpu_format_t)f,
                                (ap_m68040_fpu_mode_t)m)
              .valid;
      if (!dashable)
        TEST_ASSERT_TRUE_MESSAGE(valid, "a memory mode was left unpriced");
    }
  }
}

static void test_a_long_word_and_a_single_cost_exactly_the_same(void) {
  /* Two columns, identical in all seventeen rows. Both move 32 bits, and the
   * integer unit's job ends at moving them: converting an integer long word to
   * extended and reinterpreting a single as extended are both the FPU's work,
   * priced in §10.7.3 and not here. This is the clearest demonstration that
   * §10.7.2 measures transfer and not arithmetic. */
  for (unsigned m = 0; m < AP_M68040_FPU_MODE_COUNT; m++) {
    const ap_m68040_fpu_cell_t l = ap_m68040_fpu_support(
        AP_M68040_FPU_FORMAT_LONG, (ap_m68040_fpu_mode_t)m);
    const ap_m68040_fpu_cell_t s = ap_m68040_fpu_support(
        AP_M68040_FPU_FORMAT_SINGLE, (ap_m68040_fpu_mode_t)m);
    TEST_ASSERT_EQUAL(l.valid, s.valid);
    TEST_ASSERT_EQUAL_UINT(l.calculate, s.calculate);
    TEST_ASSERT_EQUAL_UINT(l.execute.lead, s.execute.lead);
    TEST_ASSERT_EQUAL_UINT(l.execute.base, s.execute.base);
  }
}

static void test_byte_and_word_differ_from_long_only_as_an_immediate(void) {
  /* The byte/word column and the long-word column agree everywhere except
   * `#<xxx>`, where byte/word prints `5/3L + 2` against long word's
   * `3/1L + 2`. An immediate is fetched from the instruction stream, so its
   * *encoded* width is what costs -- and this is the one place in the table
   * where a narrower operand is the dearer one, because a byte or word
   * immediate must be extracted and widened where a long word is already
   * aligned to fetch. */
  unsigned differing = 0;
  for (unsigned m = 0; m < AP_M68040_FPU_MODE_COUNT; m++) {
    const ap_m68040_fpu_cell_t bw = ap_m68040_fpu_support(
        AP_M68040_FPU_FORMAT_BYTE_WORD, (ap_m68040_fpu_mode_t)m);
    const ap_m68040_fpu_cell_t l = ap_m68040_fpu_support(
        AP_M68040_FPU_FORMAT_LONG, (ap_m68040_fpu_mode_t)m);
    if (bw.calculate != l.calculate || bw.execute.lead != l.execute.lead ||
        bw.execute.base != l.execute.base) {
      differing++;
      TEST_ASSERT_EQUAL_INT_MESSAGE(AP_M68040_FPU_IMMEDIATE, (int)m,
                                    "only the immediate row should differ");
    }
  }
  TEST_ASSERT_EQUAL_UINT(1u, differing);

  const ap_m68040_fpu_cell_t bw = ap_m68040_fpu_support(
      AP_M68040_FPU_FORMAT_BYTE_WORD, AP_M68040_FPU_IMMEDIATE);
  TEST_ASSERT_EQUAL_UINT(5u, bw.calculate);
  TEST_ASSERT_EQUAL_UINT(3u, bw.execute.lead);
  TEST_ASSERT_EQUAL_UINT(2u, bw.execute.base);
  TEST_ASSERT_TRUE(bw.calculate > ap_m68040_fpu_support(
                                      AP_M68040_FPU_FORMAT_LONG,
                                      AP_M68040_FPU_IMMEDIATE)
                                      .calculate);
}

static void test_extended_costs_one_more_clock_than_double_in_memory(void) {
  /* Ten bytes against eight, and the table charges exactly one clock for the
   * difference across the simple memory modes: `(An)` and its kin are 3/3 at
   * extended against 2/2 at double. Not two clocks, though extended is a
   * third larger -- the transfer is wide enough that the extra bytes cost one
   * more beat and no more. */
  const ap_m68040_fpu_mode_t simple[] = {
      AP_M68040_FPU_INDIRECT, AP_M68040_FPU_POSTINCREMENT,
      AP_M68040_FPU_PREDECREMENT, AP_M68040_FPU_DISPLACEMENT};
  for (unsigned i = 0; i < sizeof simple / sizeof simple[0]; i++) {
    const ap_m68040_fpu_cell_t d =
        ap_m68040_fpu_support(AP_M68040_FPU_FORMAT_DOUBLE, simple[i]);
    const ap_m68040_fpu_cell_t e =
        ap_m68040_fpu_support(AP_M68040_FPU_FORMAT_EXTENDED, simple[i]);
    TEST_ASSERT_EQUAL_UINT(2u, d.calculate);
    TEST_ASSERT_EQUAL_UINT(2u, total(d));
    TEST_ASSERT_EQUAL_UINT(3u, e.calculate);
    TEST_ASSERT_EQUAL_UINT(3u, total(e));
  }
}

static void test_extended_is_never_cheaper_than_any_narrower_format(void) {
  /* The widest operand cannot cost less to move than a narrower one, in any
   * mode either can reach. Checked rather than assumed, because it is the
   * invariant a mistyped row would break first. */
  for (unsigned m = 0; m < AP_M68040_FPU_MODE_COUNT; m++) {
    const ap_m68040_fpu_cell_t e = ap_m68040_fpu_support(
        AP_M68040_FPU_FORMAT_EXTENDED, (ap_m68040_fpu_mode_t)m);
    if (!e.valid) continue;
    for (unsigned f = 0; f < AP_M68040_FPU_FORMAT_EXTENDED; f++) {
      const ap_m68040_fpu_cell_t n = ap_m68040_fpu_support(
          (ap_m68040_fpu_format_t)f, (ap_m68040_fpu_mode_t)m);
      if (!n.valid) continue;
      TEST_ASSERT_TRUE_MESSAGE(e.calculate >= n.calculate,
                               "extended calculated faster than a narrower "
                               "format");
      TEST_ASSERT_TRUE_MESSAGE(total(e) >= total(n),
                               "extended executed faster than a narrower "
                               "format");
    }
  }
}

/* ---------------------------------------------------------------------------
 * The BR = PC footnote.
 * ------------------------------------------------------------------------- */

static void test_a_pc_base_costs_one_clock_on_both_stages(void) {
  /* "For BR = PC, add one clock to both <ea> calculate and execute times."
   * §10.6 priced a PC base as its own row; §10.7.2 prices it as a penalty on
   * the `An` row, so the same hardware case is a row there and an adjustment
   * here. */
  const ap_m68040_fpu_cell_t base = ap_m68040_fpu_support(
      AP_M68040_FPU_FORMAT_EXTENDED, AP_M68040_FPU_BASE_INDEXED);
  const ap_m68040_fpu_cell_t pc = ap_m68040_fpu_with_pc_base(base);
  TEST_ASSERT_EQUAL_UINT(base.calculate + 1u, pc.calculate);
  TEST_ASSERT_EQUAL_UINT(total(base) + 1u, total(pc));
  /* The clock lands on the base, not the lead. A lead is stall tolerance, and
   * forming a longer address does not make the next instruction more able to
   * overlap this one. */
  TEST_ASSERT_EQUAL_UINT(base.execute.lead, pc.execute.lead);
  TEST_ASSERT_EQUAL_UINT(base.execute.base + 1u, pc.execute.base);
}

static void test_only_the_deep_modes_have_a_base_register_to_be_the_pc(void) {
  /* The footnote applies where there is a `BR` field to hold the PC, which is
   * the six modes §10.6 spells with `BR` and §10.7.2 spells with `An`. Charging
   * the clock anywhere else would be a fabricated figure -- `(An)` has a base
   * register in the ordinary sense, but not one the encoding lets be the PC. */
  const ap_m68040_fpu_mode_t deep[] = {
      AP_M68040_FPU_BASE_INDEXED,       AP_M68040_FPU_BASE_DISPLACEMENT,
      AP_M68040_FPU_MEMORY_PREINDEXED,  AP_M68040_FPU_MEMORY_PREINDEXED_OD,
      AP_M68040_FPU_MEMORY_POSTINDEXED, AP_M68040_FPU_MEMORY_POSTINDEXED_OD};
  unsigned found = 0;
  for (unsigned m = 0; m < AP_M68040_FPU_MODE_COUNT; m++) {
    bool expected = false;
    for (unsigned i = 0; i < 6; i++)
      if (deep[i] == (ap_m68040_fpu_mode_t)m) expected = true;
    if (expected) found++;
    TEST_ASSERT_EQUAL_MESSAGE(
        expected,
        ap_m68040_fpu_mode_has_base_register((ap_m68040_fpu_mode_t)m),
        "wrong set of modes carries a base register");
  }
  TEST_ASSERT_EQUAL_UINT(6u, found);
  /* `(d16,PC)` and `(d8,PC,Xn)` already *are* the PC-relative modes and are
   * priced as such, so the footnote must not apply to them a second time. */
  TEST_ASSERT_FALSE(
      ap_m68040_fpu_mode_has_base_register(AP_M68040_FPU_PC_DISPLACEMENT));
  TEST_ASSERT_FALSE(
      ap_m68040_fpu_mode_has_base_register(AP_M68040_FPU_PC_INDEXED));
}

static void test_an_invalid_cell_is_not_given_a_penalty(void) {
  /* Adding a clock to a dash would turn an unpriced encoding into a priced
   * one, which is how a decoder ends up costing an instruction that cannot be
   * encoded. */
  const ap_m68040_fpu_cell_t dash =
      ap_m68040_fpu_support(AP_M68040_FPU_FORMAT_DOUBLE, AP_M68040_FPU_DN);
  TEST_ASSERT_FALSE(dash.valid);
  const ap_m68040_fpu_cell_t penalised = ap_m68040_fpu_with_pc_base(dash);
  TEST_ASSERT_FALSE(penalised.valid);
  TEST_ASSERT_EQUAL_UINT(0u, penalised.calculate);
  TEST_ASSERT_EQUAL_UINT(0u, total(penalised));
}

static void test_an_out_of_range_query_is_refused_rather_than_read(void) {
  TEST_ASSERT_FALSE(
      ap_m68040_fpu_support(AP_M68040_FPU_FORMAT_COUNT, AP_M68040_FPU_INDIRECT)
          .valid);
  TEST_ASSERT_FALSE(ap_m68040_fpu_support(AP_M68040_FPU_FORMAT_LONG,
                                          AP_M68040_FPU_MODE_COUNT)
                        .valid);
  TEST_ASSERT_FALSE(ap_m68040_fpu_misc(AP_M68040_FPU_MISC_COUNT).valid);
  TEST_ASSERT_NULL(ap_m68040_fpu_misc_instruction(AP_M68040_FPU_MISC_COUNT));
  TEST_ASSERT_NULL(ap_m68040_fpu_support_name(ap_m68040_fpu_support_count()));
  TEST_ASSERT_FALSE(ap_m68040_fpu_support_prices(NULL));
}

/* ---------------------------------------------------------------------------
 * The instruction group.
 * ------------------------------------------------------------------------- */

static void test_ten_instructions_share_the_support_column(void) {
  /* "FABS, FADD, FCMP, FDIV, FMOVE, FMUL, FNEG, FSQRT, FSUB, FTST <ea>,FPn".
   * To the integer unit these are one instruction: fetch an operand of some
   * format and hand it over. A divide and a negate differ only in what the FPU
   * does afterwards, which is why `FDIV` and `FNEG` share a figure here and
   * will not in §10.7.3. */
  TEST_ASSERT_EQUAL_UINT(10u, (unsigned)ap_m68040_fpu_support_count());
  const char *const expected[] = {"FABS", "FADD", "FCMP",  "FDIV", "FMOVE",
                                  "FMUL", "FNEG", "FSQRT", "FSUB", "FTST"};
  for (unsigned i = 0; i < 10; i++) {
    TEST_ASSERT_EQUAL_STRING(expected[i], ap_m68040_fpu_support_name(i));
    TEST_ASSERT_TRUE(ap_m68040_fpu_support_prices(expected[i]));
  }
  /* `FMOVEM` is not among them, and must not be: the preamble says "all FMOVEM
   * instructions wait for the pipe to idle before starting", so its cost is not
   * a table lookup at all. */
  TEST_ASSERT_FALSE(ap_m68040_fpu_support_prices("FMOVEM"));
  TEST_ASSERT_FALSE(ap_m68040_fpu_support_prices("FNOP"));
}

/* ---------------------------------------------------------------------------
 * §10.7.1.
 * ------------------------------------------------------------------------- */

static void test_a_taken_float_branch_costs_one_clock_more_than_a_skipped_one(void) {
  /* `FBcc` taken is 7/7 and not taken is 6/6 -- the branch costs one clock, and
   * both stages pay it. */
  const ap_m68040_fpu_cell_t taken =
      ap_m68040_fpu_misc(AP_M68040_FPU_MISC_FBCC_TAKEN);
  const ap_m68040_fpu_cell_t not_taken =
      ap_m68040_fpu_misc(AP_M68040_FPU_MISC_FBCC_NOT_TAKEN);
  TEST_ASSERT_EQUAL_UINT(7u, taken.calculate);
  TEST_ASSERT_EQUAL_UINT(7u, total(taken));
  TEST_ASSERT_EQUAL_UINT(6u, not_taken.calculate);
  TEST_ASSERT_EQUAL_UINT(6u, total(not_taken));
  /* Neither carries a lead: a branch resolves the instruction stream, so there
   * is nothing behind it to overlap with. */
  TEST_ASSERT_EQUAL_UINT(0u, taken.execute.lead);
  TEST_ASSERT_EQUAL_UINT(0u, not_taken.execute.lead);
}

static void test_fdbcc_is_dearer_when_the_condition_is_false(void) {
  /* The reversal that catches readers: `FDBcc` *continues the loop* when the
   * condition is false, so `cc False` is the branch-taken case. It prints
   * `11/1L + 9` against `cc True`'s `9/1L + 7` -- two clocks dearer, where
   * `FBcc`'s taken case costs only one more than its not-taken case.
   *
   * Reading `FDBcc` as though it branched on truth gets both figures backwards
   * and would make a loop's cost the cheap one on every iteration but the
   * last. */
  const ap_m68040_fpu_cell_t t =
      ap_m68040_fpu_misc(AP_M68040_FPU_MISC_FDBCC_TRUE);
  const ap_m68040_fpu_cell_t f =
      ap_m68040_fpu_misc(AP_M68040_FPU_MISC_FDBCC_FALSE);
  TEST_ASSERT_EQUAL_UINT(9u, t.calculate);
  TEST_ASSERT_EQUAL_UINT(1u, t.execute.lead);
  TEST_ASSERT_EQUAL_UINT(7u, t.execute.base);
  TEST_ASSERT_EQUAL_UINT(11u, f.calculate);
  TEST_ASSERT_EQUAL_UINT(1u, f.execute.lead);
  TEST_ASSERT_EQUAL_UINT(9u, f.execute.base);
  TEST_ASSERT_EQUAL_UINT(t.calculate + 2u, f.calculate);

  /* And unlike `FBcc`, both `FDBcc` cases carry a lead -- the decrement is
   * work the following instruction can overlap, where a plain branch's target
   * resolution is not. */
  TEST_ASSERT_TRUE(t.execute.lead > 0u);
  TEST_ASSERT_TRUE(f.execute.lead > 0u);
}

static void test_a_float_nop_is_not_free_and_says_why(void) {
  /* `FNOP` costs 6/6 with an idle FPU, the same as `FBcc` not taken. It is not
   * a no-operation to the integer unit: the instruction still has to reach the
   * FPU and be seen to complete, which is precisely what programs use it for --
   * `FNOP` is the documented way to wait for the floating-point pipe. */
  const ap_m68040_fpu_cell_t nop =
      ap_m68040_fpu_misc(AP_M68040_FPU_MISC_FNOP_IDLE);
  TEST_ASSERT_EQUAL_UINT(6u, nop.calculate);
  TEST_ASSERT_EQUAL_UINT(6u, total(nop));
  TEST_ASSERT_EQUAL_UINT(
      total(ap_m68040_fpu_misc(AP_M68040_FPU_MISC_FBCC_NOT_TAKEN)), total(nop));
  TEST_ASSERT_EQUAL_STRING(
      "FPU Idle", ap_m68040_fpu_misc_condition(AP_M68040_FPU_MISC_FNOP_IDLE));
}

static void test_only_the_untaken_trap_is_priced(void) {
  /* `FTRAPcc` appears with one row, "Not Taken", at `6/1L + 5`. Taking the trap
   * costs the exception, which §10.7 does not price -- so there is no entry,
   * rather than an entry holding a number nobody measured. */
  const ap_m68040_fpu_cell_t trap =
      ap_m68040_fpu_misc(AP_M68040_FPU_MISC_FTRAPCC_NOT_TAKEN);
  TEST_ASSERT_EQUAL_UINT(6u, trap.calculate);
  TEST_ASSERT_EQUAL_UINT(1u, trap.execute.lead);
  TEST_ASSERT_EQUAL_UINT(5u, trap.execute.base);
  TEST_ASSERT_EQUAL_STRING("FTRAPcc", ap_m68040_fpu_misc_instruction(
                                          AP_M68040_FPU_MISC_FTRAPCC_NOT_TAKEN));
  TEST_ASSERT_EQUAL_STRING("Not Taken",
                           ap_m68040_fpu_misc_condition(
                               AP_M68040_FPU_MISC_FTRAPCC_NOT_TAKEN));

  unsigned ftrapcc_rows = 0;
  for (unsigned i = 0; i < AP_M68040_FPU_MISC_COUNT; i++)
    if (strcmp(ap_m68040_fpu_misc_instruction((ap_m68040_fpu_misc_t)i),
               "FTRAPcc") == 0)
      ftrapcc_rows++;
  TEST_ASSERT_EQUAL_UINT(1u, ftrapcc_rows);
}

static void test_every_miscellaneous_row_is_priced_and_named(void) {
  /* Six rows across four instructions, and none of them a placeholder. */
  TEST_ASSERT_EQUAL_UINT(6u, (unsigned)AP_M68040_FPU_MISC_COUNT);
  for (unsigned i = 0; i < AP_M68040_FPU_MISC_COUNT; i++) {
    const ap_m68040_fpu_cell_t c = ap_m68040_fpu_misc((ap_m68040_fpu_misc_t)i);
    TEST_ASSERT_TRUE(c.valid);
    TEST_ASSERT_TRUE_MESSAGE(c.calculate > 0u,
                             "a priced row calculated in zero clocks");
    TEST_ASSERT_TRUE_MESSAGE(total(c) > 0u,
                             "a priced row executed in zero clocks");
    TEST_ASSERT_NOT_NULL(
        ap_m68040_fpu_misc_instruction((ap_m68040_fpu_misc_t)i));
    TEST_ASSERT_NOT_NULL(ap_m68040_fpu_misc_condition((ap_m68040_fpu_misc_t)i));
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_floating_point_operand_comes_from_fpn_and_never_from_an);
  RUN_TEST(test_an_fp_register_is_priced_only_as_extended);
  RUN_TEST(test_a_data_register_cannot_hold_more_than_thirty_two_bits);
  RUN_TEST(test_no_format_and_mode_pair_is_priced_twice_over);
  RUN_TEST(test_a_long_word_and_a_single_cost_exactly_the_same);
  RUN_TEST(test_byte_and_word_differ_from_long_only_as_an_immediate);
  RUN_TEST(test_extended_costs_one_more_clock_than_double_in_memory);
  RUN_TEST(test_extended_is_never_cheaper_than_any_narrower_format);
  RUN_TEST(test_a_pc_base_costs_one_clock_on_both_stages);
  RUN_TEST(test_only_the_deep_modes_have_a_base_register_to_be_the_pc);
  RUN_TEST(test_an_invalid_cell_is_not_given_a_penalty);
  RUN_TEST(test_an_out_of_range_query_is_refused_rather_than_read);
  RUN_TEST(test_ten_instructions_share_the_support_column);
  RUN_TEST(test_a_taken_float_branch_costs_one_clock_more_than_a_skipped_one);
  RUN_TEST(test_fdbcc_is_dearer_when_the_condition_is_false);
  RUN_TEST(test_a_float_nop_is_not_free_and_says_why);
  RUN_TEST(test_only_the_untaken_trap_is_priced);
  RUN_TEST(test_every_miscellaneous_row_is_priced_and_named);
  return UNITY_END();
}

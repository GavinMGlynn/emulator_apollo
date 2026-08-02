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

static void test_the_load_table_prices_every_memory_mode(void) {
  /* Structural: `FPn`, `Dn` and `An` are the only rows the load table dashes --
   * `An` in every format, since page 10-30 has no `An` row at all. Every memory
   * mode is priced in all five formats, because a memory operand of any format
   * can be reached by any of them. */
  for (unsigned f = 0; f < AP_M68040_FPU_FORMAT_COUNT; f++) {
    TEST_ASSERT_FALSE_MESSAGE(
        ap_m68040_fpu_support((ap_m68040_fpu_format_t)f, AP_M68040_FPU_AN)
            .valid,
        "the load table has no An row");
    for (unsigned m = 0; m < AP_M68040_FPU_MODE_COUNT; m++) {
      if (m == AP_M68040_FPU_FPN || m == AP_M68040_FPU_DN ||
          m == AP_M68040_FPU_AN)
        continue;
      TEST_ASSERT_TRUE_MESSAGE(
          ap_m68040_fpu_support((ap_m68040_fpu_format_t)f,
                                (ap_m68040_fpu_mode_t)m)
              .valid,
          "a memory mode was left unpriced");
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

/* ---------------------------------------------------------------------------
 * Page 10-31: the store direction.
 * ------------------------------------------------------------------------- */

static void test_storing_to_an_integer_is_far_dearer_than_loading_one(void) {
  /* The largest asymmetry in §10.7.2, and the one that says most about the
   * hardware. Loading a long word into an FPU register costs `2/2` at `(An)`.
   * Storing one back out costs `8/9L + 2` -- four times the calculate and five
   * times the execute.
   *
   * The direction is not symmetric because the work is not. A load hands the
   * FPU a bit pattern and lets it convert at leisure inside its own pipeline;
   * a store must have the *converted* integer in hand before the bus cycle can
   * begin, so the extended-to-integer conversion lands in the integer unit's
   * figure. The `9L` lead is that conversion: nine clocks of work a following
   * instruction may overlap, which is exactly what a lead is for. */
  const ap_m68040_fpu_cell_t load =
      ap_m68040_fpu_support(AP_M68040_FPU_FORMAT_LONG, AP_M68040_FPU_INDIRECT);
  const ap_m68040_fpu_cell_t store = ap_m68040_fpu_store(
      AP_M68040_FPU_STORE_BYTE_WORD_LONG, AP_M68040_FPU_INDIRECT);
  TEST_ASSERT_EQUAL_UINT(2u, load.calculate);
  TEST_ASSERT_EQUAL_UINT(2u, total(load));
  TEST_ASSERT_EQUAL_UINT(8u, store.calculate);
  TEST_ASSERT_EQUAL_UINT(9u, store.execute.lead);
  TEST_ASSERT_EQUAL_UINT(2u, store.execute.base);

  /* Storing a *floating-point* format is cheap again, because no conversion is
   * needed -- a single or double is a repack of the extended value, not an
   * arithmetic change. `2/1L + 2` against the integer store's `8/9L + 2`. */
  const ap_m68040_fpu_cell_t fp = ap_m68040_fpu_store(
      AP_M68040_FPU_STORE_SINGLE_DOUBLE, AP_M68040_FPU_INDIRECT);
  TEST_ASSERT_EQUAL_UINT(2u, fp.calculate);
  TEST_ASSERT_EQUAL_UINT(1u, fp.execute.lead);
  TEST_ASSERT_EQUAL_UINT(2u, fp.execute.base);
  TEST_ASSERT_TRUE(store.execute.lead > fp.execute.lead * 4u);
}

static void test_the_formats_regroup_between_the_two_directions(void) {
  /* The load table's five columns are `B/W`, `L`, `S`, `D`, `E`; the store
   * table's three are `B/W/L`, `S/D`, `E`. The regrouping is the finding, not
   * an editorial choice.
   *
   * Going *in*, a long word and a single cost the same and byte/word differs --
   * what matters is how many bytes are fetched, and the conversion is the FPU's
   * problem. Going *out*, all three integer widths cost the same and the single
   * joins the double -- what matters is whether a conversion happened at all,
   * and every integer width needs one while no floating format does. Two
   * different questions, so two different groupings. */
  TEST_ASSERT_EQUAL_UINT(5u, (unsigned)AP_M68040_FPU_FORMAT_COUNT);
  TEST_ASSERT_EQUAL_UINT(3u, (unsigned)AP_M68040_FPU_STORE_FORMAT_COUNT);

  /* Going out: single and double are one column, so they agree everywhere. */
  for (unsigned m = 0; m < AP_M68040_FPU_MODE_COUNT; m++) {
    const ap_m68040_fpu_cell_t s =
        ap_m68040_fpu_store(AP_M68040_FPU_STORE_SINGLE_DOUBLE,
                            (ap_m68040_fpu_mode_t)m);
    const ap_m68040_fpu_cell_t bwl =
        ap_m68040_fpu_store(AP_M68040_FPU_STORE_BYTE_WORD_LONG,
                            (ap_m68040_fpu_mode_t)m);
    TEST_ASSERT_EQUAL_MESSAGE(bwl.valid, s.valid,
                              "the store columns should dash the same rows");
  }
  /* Going in, single and double do *not* agree -- `#<xxx>` is `3/1L + 2` at
   * single and `4/2L + 2` at double, the eight-byte immediate costing more to
   * fetch. That difference is what makes them separate columns on the way in
   * and irrelevant on the way out. */
  const ap_m68040_fpu_cell_t si = ap_m68040_fpu_support(
      AP_M68040_FPU_FORMAT_SINGLE, AP_M68040_FPU_IMMEDIATE);
  const ap_m68040_fpu_cell_t di = ap_m68040_fpu_support(
      AP_M68040_FPU_FORMAT_DOUBLE, AP_M68040_FPU_IMMEDIATE);
  TEST_ASSERT_EQUAL_UINT(3u, si.calculate);
  TEST_ASSERT_EQUAL_UINT(4u, di.calculate);
}

static void test_a_store_has_no_source_register_row_and_no_program_space(void) {
  /* `FMOVE FPn,<ea>` writes, so the destination cannot be program space or an
   * immediate. And there is no `FPn` row: the source is already named in the
   * mnemonic, so an `FPn` destination would be register-to-register, which is
   * the *load* table's `FPn` row and not this one's. */
  for (unsigned f = 0; f < AP_M68040_FPU_STORE_FORMAT_COUNT; f++) {
    const ap_m68040_fpu_store_format_t sf = (ap_m68040_fpu_store_format_t)f;
    TEST_ASSERT_FALSE(ap_m68040_fpu_store(sf, AP_M68040_FPU_FPN).valid);
    TEST_ASSERT_FALSE(ap_m68040_fpu_store(sf, AP_M68040_FPU_AN).valid);
    TEST_ASSERT_FALSE(
        ap_m68040_fpu_store(sf, AP_M68040_FPU_PC_DISPLACEMENT).valid);
    TEST_ASSERT_FALSE(ap_m68040_fpu_store(sf, AP_M68040_FPU_PC_INDEXED).valid);
    TEST_ASSERT_FALSE(ap_m68040_fpu_store(sf, AP_M68040_FPU_IMMEDIATE).valid);
  }
  /* `Dn` is a legal destination for the integer and 32-bit floating formats and
   * not for extended, which does not fit -- the same register-width fact the
   * load table encodes, seen from the other side. */
  TEST_ASSERT_TRUE(ap_m68040_fpu_store(AP_M68040_FPU_STORE_BYTE_WORD_LONG,
                                       AP_M68040_FPU_DN)
                       .valid);
  TEST_ASSERT_TRUE(ap_m68040_fpu_store(AP_M68040_FPU_STORE_SINGLE_DOUBLE,
                                       AP_M68040_FPU_DN)
                       .valid);
  TEST_ASSERT_FALSE(
      ap_m68040_fpu_store(AP_M68040_FPU_STORE_EXTENDED, AP_M68040_FPU_DN)
          .valid);
}

/* ---------------------------------------------------------------------------
 * Page 10-32: the control register, FMOVEM and FScc.
 * ------------------------------------------------------------------------- */

static void test_a_control_register_move_is_the_only_one_taking_an(void) {
  /* `FMOVE/FMOVEM to/from 1 Control Register` is the only §10.7.2 table that
   * prices an `An` row, at `2/1L + 2` -- the same as `Dn`. `FPCR`, `FPSR` and
   * `FPIAR` are ordinary 32-bit registers, so moving one is an ordinary 32-bit
   * move and an address register is as good a partner as a data register.
   * Nothing else here takes `An`, because nothing else moves a plain long
   * word. */
  const ap_m68040_fpu_cell_t an = ap_m68040_fpu_control(AP_M68040_FPU_AN);
  const ap_m68040_fpu_cell_t dn = ap_m68040_fpu_control(AP_M68040_FPU_DN);
  TEST_ASSERT_TRUE(an.valid);
  TEST_ASSERT_EQUAL_UINT(dn.calculate, an.calculate);
  TEST_ASSERT_EQUAL_UINT(total(dn), total(an));
  TEST_ASSERT_EQUAL_UINT(2u, an.calculate);
  TEST_ASSERT_EQUAL_UINT(1u, an.execute.lead);
  TEST_ASSERT_EQUAL_UINT(2u, an.execute.base);

  TEST_ASSERT_FALSE(ap_m68040_fpu_movem(AP_M68040_FPU_AN).valid);
  TEST_ASSERT_FALSE(ap_m68040_fpu_scc(AP_M68040_FPU_AN).valid);
  TEST_ASSERT_FALSE(
      ap_m68040_fpu_save(AP_M68040_FPU_FRAME_LONG, AP_M68040_FPU_AN).valid);
  TEST_ASSERT_FALSE(
      ap_m68040_fpu_restore(AP_M68040_FPU_FRAME_LONG, AP_M68040_FPU_AN).valid);

  /* And it takes an immediate and both PC modes, because loading a control
   * register from a constant or from program space is ordinary. */
  TEST_ASSERT_TRUE(ap_m68040_fpu_control(AP_M68040_FPU_IMMEDIATE).valid);
  TEST_ASSERT_TRUE(ap_m68040_fpu_control(AP_M68040_FPU_PC_DISPLACEMENT).valid);
  TEST_ASSERT_TRUE(ap_m68040_fpu_control(AP_M68040_FPU_PC_INDEXED).valid);
}

static void test_each_extra_floating_register_costs_three_clocks(void) {
  /* Note b, page 10-32: "add three clocks to both <ea> calculate and execute
   * times for each additional floating-point register. Add one clock to both
   * <ea> calculate and execute times for dynamic register list."
   *
   * The printed cell is the one-register case, so the count starts at one and
   * the eight-register move costs seven increments, not eight. Getting that off
   * by one would misprice every full save by three clocks. */
  const ap_m68040_fpu_cell_t base = ap_m68040_fpu_movem(AP_M68040_FPU_INDIRECT);
  TEST_ASSERT_EQUAL_UINT(17u, base.calculate);
  TEST_ASSERT_EQUAL_UINT(2u, base.execute.lead);
  TEST_ASSERT_EQUAL_UINT(15u, base.execute.base);

  const ap_m68040_fpu_cell_t one =
      ap_m68040_fpu_movem_with_list(base, 1u, false);
  TEST_ASSERT_EQUAL_UINT(base.calculate, one.calculate);
  TEST_ASSERT_EQUAL_UINT(total(base), total(one));

  const ap_m68040_fpu_cell_t all =
      ap_m68040_fpu_movem_with_list(base, 8u, false);
  TEST_ASSERT_EQUAL_UINT(17u + 7u * 3u, all.calculate);
  TEST_ASSERT_EQUAL_UINT(15u + 7u * 3u, all.execute.base);
  /* The lead does not grow with the list: it is stall tolerance, and moving
   * more registers does not give the next instruction more room to overlap. */
  TEST_ASSERT_EQUAL_UINT(base.execute.lead, all.execute.lead);

  const ap_m68040_fpu_cell_t dynamic =
      ap_m68040_fpu_movem_with_list(base, 8u, true);
  TEST_ASSERT_EQUAL_UINT(all.calculate + 1u, dynamic.calculate);
  TEST_ASSERT_EQUAL_UINT(all.execute.base + 1u, dynamic.execute.base);
}

static void test_an_empty_register_list_is_refused_not_discounted(void) {
  /* The cell already prices the first register, so a zero-register `FMOVEM` is
   * not the cheap case -- it is an encoding that does not exist. Returning a
   * discounted figure would let a decoder price an instruction it should
   * reject, which is the same failure a dash exists to prevent. */
  const ap_m68040_fpu_cell_t base = ap_m68040_fpu_movem(AP_M68040_FPU_INDIRECT);
  const ap_m68040_fpu_cell_t none =
      ap_m68040_fpu_movem_with_list(base, 0u, false);
  TEST_ASSERT_FALSE(none.valid);
  TEST_ASSERT_EQUAL_UINT(0u, none.calculate);

  /* And an adjustment on a dashed mode stays dashed. */
  const ap_m68040_fpu_cell_t dashed = ap_m68040_fpu_movem(AP_M68040_FPU_DN);
  TEST_ASSERT_FALSE(dashed.valid);
  TEST_ASSERT_FALSE(ap_m68040_fpu_movem_with_list(dashed, 4u, false).valid);
}

static void test_a_register_list_cannot_go_to_a_register(void) {
  /* `FMOVEM` dashes `Dn` and `An` in both directions: a list of floating-point
   * registers needs somewhere to put more than one value, and a single
   * register is not it. */
  TEST_ASSERT_FALSE(ap_m68040_fpu_movem(AP_M68040_FPU_DN).valid);
  TEST_ASSERT_FALSE(ap_m68040_fpu_movem(AP_M68040_FPU_AN).valid);
  TEST_ASSERT_FALSE(ap_m68040_fpu_movem(AP_M68040_FPU_FPN).valid);
}

static void test_fmovem_dashes_one_program_space_mode_and_prices_the_other(void) {
  /* Transcribed as printed, and flagged. The column covers both directions,
   * `<list>,<ea>` and `<ea>,<list>`, so its mode set is the union -- and the
   * load direction legitimately takes the control modes, both PC forms among
   * them. Yet `(d16,PC)` is dashed while `(d8,PC,Xn)` is priced at
   * `20/1L + 18`.
   *
   * The two stand or fall together: there is no reading of `FMOVEM` under
   * which a PC displacement is illegal and a PC index is legal. One cell is
   * wrong and the page does not say which, so both are kept as printed. Same
   * class as §10.6's `MOVE to SR (BR,Xn)`, which dashes a mode the
   * Programmer's Reference Manual allows.
   *
   * This test exists so the inconsistency is loud. If a later reader
   * "corrects" one of them, they should have to delete a stated finding to do
   * it. */
  TEST_ASSERT_FALSE(ap_m68040_fpu_movem(AP_M68040_FPU_PC_DISPLACEMENT).valid);
  const ap_m68040_fpu_cell_t indexed =
      ap_m68040_fpu_movem(AP_M68040_FPU_PC_INDEXED);
  TEST_ASSERT_TRUE(indexed.valid);
  TEST_ASSERT_EQUAL_UINT(20u, indexed.calculate);
  TEST_ASSERT_EQUAL_UINT(1u, indexed.execute.lead);
  TEST_ASSERT_EQUAL_UINT(18u, indexed.execute.base);

  /* The neighbouring columns are each internally consistent, which is what
   * isolates `FMOVEM` as the odd one. `FScc` writes and dashes both; the
   * control-register move reads and prices both. */
  TEST_ASSERT_FALSE(ap_m68040_fpu_scc(AP_M68040_FPU_PC_DISPLACEMENT).valid);
  TEST_ASSERT_FALSE(ap_m68040_fpu_scc(AP_M68040_FPU_PC_INDEXED).valid);
  TEST_ASSERT_TRUE(ap_m68040_fpu_control(AP_M68040_FPU_PC_DISPLACEMENT).valid);
  TEST_ASSERT_TRUE(ap_m68040_fpu_control(AP_M68040_FPU_PC_INDEXED).valid);
}

static void test_fscc_writes_a_byte_like_its_integer_namesake(void) {
  /* `FScc` costs `5/6` to a data register and `4/5` to `(An)` -- dearer in a
   * register, like §10.6's `NBCD` and unlike almost everything else. It refuses
   * `An`, the immediate and both PC modes, which is the byte-write mode set
   * exactly as integer `Scc` has it. The condition is floating-point; the
   * destination handling is not. */
  const ap_m68040_fpu_cell_t reg = ap_m68040_fpu_scc(AP_M68040_FPU_DN);
  const ap_m68040_fpu_cell_t mem = ap_m68040_fpu_scc(AP_M68040_FPU_INDIRECT);
  TEST_ASSERT_EQUAL_UINT(5u, reg.calculate);
  TEST_ASSERT_EQUAL_UINT(6u, total(reg));
  TEST_ASSERT_EQUAL_UINT(4u, mem.calculate);
  TEST_ASSERT_EQUAL_UINT(5u, total(mem));
  TEST_ASSERT_TRUE_MESSAGE(total(reg) > total(mem),
                           "FScc should be dearer in a register");
  TEST_ASSERT_FALSE(ap_m68040_fpu_scc(AP_M68040_FPU_AN).valid);
  TEST_ASSERT_FALSE(ap_m68040_fpu_scc(AP_M68040_FPU_IMMEDIATE).valid);
  TEST_ASSERT_FALSE(ap_m68040_fpu_scc(AP_M68040_FPU_FPN).valid);
}

/* ---------------------------------------------------------------------------
 * Pages 10-33 and 10-34: FSAVE and FRESTORE.
 * ------------------------------------------------------------------------- */

static void test_a_save_pushes_and_a_restore_pops(void) {
  /* The cleanest mirror in §10.7. `FSAVE` takes `-(An)` and dashes `(An)+`;
   * `FRESTORE` takes `(An)+` and dashes `-(An)`. A state frame is pushed onto a
   * stack and popped off it, and the two instructions are hard-wired to the
   * only direction that makes sense for each -- a stack that grows downward is
   * saved by predecrementing and restored by postincrementing.
   *
   * Neither takes a register of any kind, in any frame size: a state frame is
   * far larger than any register can hold. */
  for (unsigned f = 0; f < AP_M68040_FPU_FRAME_COUNT; f++) {
    const ap_m68040_fpu_frame_t frame = (ap_m68040_fpu_frame_t)f;
    TEST_ASSERT_TRUE(
        ap_m68040_fpu_save(frame, AP_M68040_FPU_PREDECREMENT).valid);
    TEST_ASSERT_FALSE(
        ap_m68040_fpu_save(frame, AP_M68040_FPU_POSTINCREMENT).valid);
    TEST_ASSERT_TRUE(
        ap_m68040_fpu_restore(frame, AP_M68040_FPU_POSTINCREMENT).valid);
    TEST_ASSERT_FALSE(
        ap_m68040_fpu_restore(frame, AP_M68040_FPU_PREDECREMENT).valid);

    const ap_m68040_fpu_mode_t registers[] = {
        AP_M68040_FPU_FPN, AP_M68040_FPU_DN, AP_M68040_FPU_AN};
    for (unsigned i = 0; i < 3; i++) {
      TEST_ASSERT_FALSE(ap_m68040_fpu_save(frame, registers[i]).valid);
      TEST_ASSERT_FALSE(ap_m68040_fpu_restore(frame, registers[i]).valid);
    }
  }
  /* `FSAVE` writes, so it takes no program space; `FRESTORE` reads, and yet the
   * table dashes the PC modes for it too -- a restore's source is a stack frame
   * the program just wrote, which program space cannot be. */
  TEST_ASSERT_FALSE(ap_m68040_fpu_restore(AP_M68040_FPU_FRAME_LONG,
                                          AP_M68040_FPU_PC_DISPLACEMENT)
                        .valid);
}

static void test_the_frame_size_costs_the_same_whatever_the_addressing(void) {
  /* A state frame's size is how many bytes cross the bus; addressing is how the
   * address is formed. The two are independent, so the step from one frame size
   * to the next ought to be a constant across every mode -- and `FRESTORE`'s
   * `<ea> calculate` is exactly that, in all eleven modes it accepts: short
   * costs 13 more than idle, long 27 more, without exception.
   *
   * That is the invariant this table is built on. Where it breaks -- and it
   * breaks in both tables, always in the deepest addressing mode -- the two
   * tests below pin the break rather than smoothing it. */
  unsigned checked = 0;
  for (unsigned m = 0; m < AP_M68040_FPU_MODE_COUNT; m++) {
    const ap_m68040_fpu_mode_t mode = (ap_m68040_fpu_mode_t)m;
    const ap_m68040_fpu_cell_t idle =
        ap_m68040_fpu_restore(AP_M68040_FPU_FRAME_IDLE_OR_NULL, mode);
    if (!idle.valid) continue;
    checked++;
    TEST_ASSERT_EQUAL_UINT_MESSAGE(
        idle.calculate + 13u,
        ap_m68040_fpu_restore(AP_M68040_FPU_FRAME_SHORT, mode).calculate,
        "FRESTORE short calculate offset is not constant");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(
        idle.calculate + 27u,
        ap_m68040_fpu_restore(AP_M68040_FPU_FRAME_LONG, mode).calculate,
        "FRESTORE long calculate offset is not constant");
  }
  TEST_ASSERT_EQUAL_UINT(11u, checked);
}

static void test_the_deepest_row_is_unreliable_in_both_save_tables(void) {
  /* Tenth suspect entry, and it is one *row* across two facing pages rather
   * than one cell. `([bd,An],Xn,od)` -- the deepest addressing mode there is --
   * breaks the constant frame offset in `FSAVE` and in `FRESTORE` alike, on
   * different stages:
   *
   *   `FSAVE` calculate: +21/+38 in ten modes, +24/+43 in this one.
   *   `FRESTORE` execute: +13/+27 in ten modes, +12/+26 in this one, because
   *   its short and long columns fail to increment from the row above where the
   *   idle column does.
   *
   * Each table is its own witness against the other. `FRESTORE`'s calculate
   * holds its constant in all eleven modes, so the structure is real; `FSAVE`'s
   * calculate holds it in ten, so the exception is not the structure. An
   * `<ea> calculate` is address formation and cannot depend on how many bytes
   * of internal state follow it, and an execute that stops incrementing at the
   * one row where the address grows longest has no mechanism behind it either.
   *
   * `FSAVE`'s excess differs between its two columns -- +3 short, +5 long --
   * and both stages move together, so it is not one digit slipping but a row
   * derived differently from the ten above it. Which figures are right is not
   * recoverable from the page. All of them are transcribed as printed. */
  const ap_m68040_fpu_mode_t odd = AP_M68040_FPU_MEMORY_POSTINDEXED_OD;

  const unsigned save_idle =
      ap_m68040_fpu_save(AP_M68040_FPU_FRAME_IDLE_OR_NULL, odd).calculate;
  TEST_ASSERT_EQUAL_UINT(
      save_idle + 24u,
      ap_m68040_fpu_save(AP_M68040_FPU_FRAME_SHORT, odd).calculate);
  TEST_ASSERT_EQUAL_UINT(
      save_idle + 43u,
      ap_m68040_fpu_save(AP_M68040_FPU_FRAME_LONG, odd).calculate);

  const unsigned restore_idle =
      total(ap_m68040_fpu_restore(AP_M68040_FPU_FRAME_IDLE_OR_NULL, odd));
  TEST_ASSERT_EQUAL_UINT(
      restore_idle + 12u,
      total(ap_m68040_fpu_restore(AP_M68040_FPU_FRAME_SHORT, odd)));
  TEST_ASSERT_EQUAL_UINT(
      restore_idle + 26u,
      total(ap_m68040_fpu_restore(AP_M68040_FPU_FRAME_LONG, odd)));

  /* `FRESTORE`'s short and long columns print the same execute for the deepest
   * two rows, where the idle column increments between them. That is the
   * mechanism of its break, stated exactly. */
  TEST_ASSERT_EQUAL_UINT(
      total(ap_m68040_fpu_restore(AP_M68040_FPU_FRAME_SHORT,
                                  AP_M68040_FPU_MEMORY_POSTINDEXED)),
      total(ap_m68040_fpu_restore(AP_M68040_FPU_FRAME_SHORT, odd)));
  TEST_ASSERT_EQUAL_UINT(
      total(ap_m68040_fpu_restore(AP_M68040_FPU_FRAME_IDLE_OR_NULL,
                                  AP_M68040_FPU_MEMORY_POSTINDEXED)) +
          1u,
      total(ap_m68040_fpu_restore(AP_M68040_FPU_FRAME_IDLE_OR_NULL, odd)));

  /* Every other mode keeps both constants, so each anomaly is one row and not
   * a whole column. */
  unsigned constant = 0;
  for (unsigned m = 0; m < AP_M68040_FPU_MODE_COUNT; m++) {
    const ap_m68040_fpu_mode_t mode = (ap_m68040_fpu_mode_t)m;
    const ap_m68040_fpu_cell_t base =
        ap_m68040_fpu_save(AP_M68040_FPU_FRAME_IDLE_OR_NULL, mode);
    if (!base.valid || mode == odd) continue;
    TEST_ASSERT_EQUAL_UINT_MESSAGE(
        base.calculate + 21u,
        ap_m68040_fpu_save(AP_M68040_FPU_FRAME_SHORT, mode).calculate,
        "a second FSAVE row broke the short offset");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(
        base.calculate + 38u,
        ap_m68040_fpu_save(AP_M68040_FPU_FRAME_LONG, mode).calculate,
        "a second FSAVE row broke the long offset");
    constant++;
  }
  TEST_ASSERT_EQUAL_UINT(10u, constant);

  /* `FRESTORE` separately, because the two tables do not accept the same modes
   * -- `-(An)` is an `FSAVE` row and `(An)+` is a `FRESTORE` row, so one loop
   * over both would compare a priced cell against a dash. */
  unsigned restore_constant = 0;
  for (unsigned m = 0; m < AP_M68040_FPU_MODE_COUNT; m++) {
    const ap_m68040_fpu_mode_t mode = (ap_m68040_fpu_mode_t)m;
    const ap_m68040_fpu_cell_t r =
        ap_m68040_fpu_restore(AP_M68040_FPU_FRAME_IDLE_OR_NULL, mode);
    if (!r.valid || mode == odd) continue;
    TEST_ASSERT_EQUAL_UINT_MESSAGE(
        total(r) + 13u,
        total(ap_m68040_fpu_restore(AP_M68040_FPU_FRAME_SHORT, mode)),
        "a second FRESTORE row broke the short execute offset");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(
        total(r) + 27u,
        total(ap_m68040_fpu_restore(AP_M68040_FPU_FRAME_LONG, mode)),
        "a second FRESTORE row broke the long execute offset");
    restore_constant++;
  }
  TEST_ASSERT_EQUAL_UINT(10u, restore_constant);
}

static void test_saving_a_long_frame_is_the_dearest_thing_in_the_section(void) {
  /* `FSAVE (An)` with a long frame costs 50 and `1L + 49` -- a hundred clocks
   * of machine time for one instruction, and by a wide margin the most
   * expensive figure in §10.7.2. It is what an exception during a
   * floating-point operation actually costs, and the reason `FSAVE` reports the
   * frame size at all: a null or idle frame is a fifth the price, so the common
   * case of saving an FPU that was doing nothing does not pay for the rare case
   * of saving one mid-operation. */
  const ap_m68040_fpu_cell_t lng =
      ap_m68040_fpu_save(AP_M68040_FPU_FRAME_LONG, AP_M68040_FPU_INDIRECT);
  const ap_m68040_fpu_cell_t idle = ap_m68040_fpu_save(
      AP_M68040_FPU_FRAME_IDLE_OR_NULL, AP_M68040_FPU_INDIRECT);
  TEST_ASSERT_EQUAL_UINT(50u, lng.calculate);
  TEST_ASSERT_EQUAL_UINT(1u, lng.execute.lead);
  TEST_ASSERT_EQUAL_UINT(49u, lng.execute.base);
  TEST_ASSERT_EQUAL_UINT(12u, idle.calculate);
  TEST_ASSERT_TRUE(lng.calculate > idle.calculate * 4u);

  /* Which of the pair is dearer depends on the frame, and the crossover is the
   * interesting part. With real state to move, saving costs more -- 33 against
   * 26 short, 50 against 40 long -- because the state must be gathered out of
   * the FPU before any of it reaches the bus. With a null or idle frame the
   * order *reverses*: 12 to save against 13 to restore. There is almost nothing
   * to move either way, so what remains is the fixed cost, and a restore has to
   * read the frame's format word and decide what it is looking at where a save
   * already knows what it is writing. */
  TEST_ASSERT_EQUAL_UINT(
      13u, ap_m68040_fpu_restore(AP_M68040_FPU_FRAME_IDLE_OR_NULL,
                                 AP_M68040_FPU_INDIRECT)
               .calculate);
  TEST_ASSERT_TRUE_MESSAGE(
      ap_m68040_fpu_restore(AP_M68040_FPU_FRAME_IDLE_OR_NULL,
                            AP_M68040_FPU_INDIRECT)
              .calculate > idle.calculate,
      "an idle restore should cost more than an idle save");
  const ap_m68040_fpu_frame_t stateful[] = {AP_M68040_FPU_FRAME_SHORT,
                                            AP_M68040_FPU_FRAME_LONG};
  for (unsigned f = 0; f < 2; f++) {
    TEST_ASSERT_TRUE_MESSAGE(
        ap_m68040_fpu_save(stateful[f], AP_M68040_FPU_INDIRECT).calculate >
            ap_m68040_fpu_restore(stateful[f], AP_M68040_FPU_INDIRECT)
                .calculate,
        "saving real state should cost more than restoring it");
  }
}

static void test_every_table_refuses_an_out_of_range_frame_or_format(void) {
  TEST_ASSERT_FALSE(ap_m68040_fpu_store(AP_M68040_FPU_STORE_FORMAT_COUNT,
                                        AP_M68040_FPU_INDIRECT)
                        .valid);
  TEST_ASSERT_FALSE(
      ap_m68040_fpu_save(AP_M68040_FPU_FRAME_COUNT, AP_M68040_FPU_INDIRECT)
          .valid);
  TEST_ASSERT_FALSE(
      ap_m68040_fpu_restore(AP_M68040_FPU_FRAME_COUNT, AP_M68040_FPU_INDIRECT)
          .valid);
  TEST_ASSERT_FALSE(ap_m68040_fpu_control(AP_M68040_FPU_MODE_COUNT).valid);
  TEST_ASSERT_FALSE(ap_m68040_fpu_movem(AP_M68040_FPU_MODE_COUNT).valid);
  TEST_ASSERT_FALSE(ap_m68040_fpu_scc(AP_M68040_FPU_MODE_COUNT).valid);
  TEST_ASSERT_FALSE(ap_m68040_fpu_store(AP_M68040_FPU_STORE_EXTENDED,
                                        AP_M68040_FPU_MODE_COUNT)
                        .valid);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_floating_point_operand_comes_from_fpn_and_never_from_an);
  RUN_TEST(test_an_fp_register_is_priced_only_as_extended);
  RUN_TEST(test_a_data_register_cannot_hold_more_than_thirty_two_bits);
  RUN_TEST(test_the_load_table_prices_every_memory_mode);
  RUN_TEST(test_storing_to_an_integer_is_far_dearer_than_loading_one);
  RUN_TEST(test_the_formats_regroup_between_the_two_directions);
  RUN_TEST(test_a_store_has_no_source_register_row_and_no_program_space);
  RUN_TEST(test_a_control_register_move_is_the_only_one_taking_an);
  RUN_TEST(test_each_extra_floating_register_costs_three_clocks);
  RUN_TEST(test_an_empty_register_list_is_refused_not_discounted);
  RUN_TEST(test_a_register_list_cannot_go_to_a_register);
  RUN_TEST(test_fmovem_dashes_one_program_space_mode_and_prices_the_other);
  RUN_TEST(test_fscc_writes_a_byte_like_its_integer_namesake);
  RUN_TEST(test_a_save_pushes_and_a_restore_pops);
  RUN_TEST(test_the_frame_size_costs_the_same_whatever_the_addressing);
  RUN_TEST(test_the_deepest_row_is_unreliable_in_both_save_tables);
  RUN_TEST(test_saving_a_long_frame_is_the_dearest_thing_in_the_section);
  RUN_TEST(test_every_table_refuses_an_out_of_range_frame_or_format);
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

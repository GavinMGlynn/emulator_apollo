/* MC68040 §10.7.3: the timings inside the floating-point unit.
 *
 * Every figure is in half cycles. Tests that name a whole-cycle quantity say so
 * by doubling it in the assertion, so a reader comparing against the page never
 * has to remember the scaling. */

#include "cpu/m68040/ap_m68040_fp_pipeline.h"

#include <string.h>

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define CYCLES(x) ((unsigned)((x) * 2))

static const ap_m68040_fp_row_t *find(const char *name, unsigned opclass,
                                      ap_m68040_fp_size_t size,
                                      ap_m68040_fp_size_t precision,
                                      ap_m68040_fp_operands_t operands) {
  return ap_m68040_fp_find(name, opclass, size, precision, operands);
}

/* ---------------------------------------------------------------------------
 * The half-cycle unit.
 * ------------------------------------------------------------------------- */

static void test_a_divide_takes_thirty_seven_and_a_half_cycles(void) {
  /* The figure that forces the whole module's unit. `FDIV`'s execution stage is
   * printed 37.5 -- not a range, not an average, a half cycle. Held as 75 half
   * cycles so that two divides add to exactly 75 cycles rather than to whatever
   * two roundings of 37.5 happen to give. */
  const ap_m68040_fp_row_t *row =
      find("FDIV", 0u, AP_M68040_FP_SIZE_NONE, AP_M68040_FP_SIZE_X,
           AP_M68040_FP_OPERANDS_NORM_NORM);
  TEST_ASSERT_NOT_NULL(row);
  TEST_ASSERT_EQUAL_UINT(75u, row->execution.latency_halves);
  TEST_ASSERT_EQUAL_UINT(CYCLES(37.5), row->execution.latency_halves);
  /* Odd, so it genuinely is not a whole number of cycles. */
  TEST_ASSERT_EQUAL_UINT(1u, row->execution.latency_halves % 2u);
}

static void test_several_figures_are_not_whole_cycles(void) {
  /* `FDIV` is not alone, which is what rules out treating it as a special case:
   * `FMOVE` to an integer converts in 1.5 cycles and executes in 4.5, and one
   * of its busy times is printed 12.5. Any of these would break an
   * integer-cycle model on its own. */
  unsigned fractional = 0;
  for (size_t i = 0; i < ap_m68040_fp_row_count(); i++) {
    const ap_m68040_fp_row_t *row = ap_m68040_fp_row(i);
    const unsigned figures[] = {
        row->conversion.latency_halves,    row->conversion.busy_halves,
        row->execution.latency_halves,     row->execution.busy_halves,
        row->normalization.latency_halves, row->normalization.busy_halves};
    for (unsigned f = 0; f < 6; f++)
      if (figures[f] % 2u != 0u) fractional++;
  }
  TEST_ASSERT_TRUE_MESSAGE(fractional > 20u,
                           "half cycles should be common, not exceptional");

  const ap_m68040_fp_row_t *integer_move =
      find("FMOVE", 2u, AP_M68040_FP_SIZE_L, AP_M68040_FP_SIZE_S,
           AP_M68040_FP_OPERANDS_POSITIVE);
  TEST_ASSERT_NOT_NULL(integer_move);
  TEST_ASSERT_EQUAL_UINT(CYCLES(1.5), integer_move->conversion.latency_halves);
  TEST_ASSERT_EQUAL_UINT(CYCLES(12.5), integer_move->conversion.busy_halves);
  TEST_ASSERT_EQUAL_UINT(CYCLES(4.5), integer_move->execution.latency_halves);
}

static void test_a_stage_is_never_free_before_it_has_finished(void) {
  /* The parenthesised figure is occupancy and the bare one is latency, so busy
   * can exceed latency but never fall below it: a stage cannot release itself
   * before it has produced a result. Checked over all 91 rows, because it is
   * the invariant a transposed pair of figures would break. */
  for (size_t i = 0; i < ap_m68040_fp_row_count(); i++) {
    const ap_m68040_fp_row_t *row = ap_m68040_fp_row(i);
    TEST_ASSERT_TRUE_MESSAGE(
        row->conversion.busy_halves >= row->conversion.latency_halves,
        "a conversion stage freed itself early");
    TEST_ASSERT_TRUE_MESSAGE(
        row->execution.busy_halves >= row->execution.latency_halves,
        "an execution stage freed itself early");
    TEST_ASSERT_TRUE_MESSAGE(
        row->normalization.busy_halves >= row->normalization.latency_halves,
        "a normalization stage freed itself early");
  }
}

static void test_an_unparenthesised_figure_is_busy_for_exactly_its_latency(void) {
  /* Where the table prints no parenthesis the two figures are equal. That is a
   * reading, and the honest one: an unparenthesised figure is not a missing
   * busy time but a stage that frees itself the moment it is done. Recording it
   * as zero-or-unknown instead would make the pipeline model unable to say when
   * the next instruction may enter. */
  const ap_m68040_fp_row_t *add =
      find("FADD", 0u, AP_M68040_FP_SIZE_NONE, AP_M68040_FP_SIZE_X,
           AP_M68040_FP_OPERANDS_NORM_NORM);
  TEST_ASSERT_NOT_NULL(add);
  /* Conversion prints `2(3)`: they differ. */
  TEST_ASSERT_EQUAL_UINT(CYCLES(2), add->conversion.latency_halves);
  TEST_ASSERT_EQUAL_UINT(CYCLES(3), add->conversion.busy_halves);
  /* Execution prints a bare `3`: they are equal. */
  TEST_ASSERT_EQUAL_UINT(CYCLES(3), add->execution.latency_halves);
  TEST_ASSERT_EQUAL_UINT(CYCLES(3), add->execution.busy_halves);
}

/* ---------------------------------------------------------------------------
 * What the operand class costs.
 * ------------------------------------------------------------------------- */

static void test_a_special_operand_skips_both_later_stages(void) {
  /* The section's governing pattern, and the reason this table cannot be folded
   * into §10.7.2's. When an operand is a zero, an infinity or a NAN the result
   * is known without arithmetic, so execution and normalisation are priced at
   * zero and only the conversion is paid. A divide by zero costs 4 cycles
   * against a real divide's 37.5 plus its stages -- the short circuit is worth
   * an order of magnitude, and it depends on the operand's *value*, which no
   * addressing-mode table can know. */
  const ap_m68040_fp_row_t *real =
      find("FDIV", 0u, AP_M68040_FP_SIZE_NONE, AP_M68040_FP_SIZE_X,
           AP_M68040_FP_OPERANDS_NORM_NORM);
  const ap_m68040_fp_row_t *by_zero =
      find("FDIV", 0u, AP_M68040_FP_SIZE_NONE, AP_M68040_FP_SIZE_X,
           AP_M68040_FP_OPERANDS_ANY_ZERO);
  TEST_ASSERT_NOT_NULL(real);
  TEST_ASSERT_NOT_NULL(by_zero);
  TEST_ASSERT_EQUAL_UINT(0u, by_zero->execution.latency_halves);
  TEST_ASSERT_EQUAL_UINT(0u, by_zero->normalization.latency_halves);
  TEST_ASSERT_EQUAL_UINT(CYCLES(4), ap_m68040_fp_total_latency_halves(by_zero));
  TEST_ASSERT_TRUE(ap_m68040_fp_total_latency_halves(real) >
                   ap_m68040_fp_total_latency_halves(by_zero) * 9u);

  /* And it is a rule, not a `FDIV` quirk: every row whose execution is zero
   * also has a zero normalisation. A stage that did nothing cannot leave
   * anything to normalise. */
  unsigned short_circuited = 0;
  for (size_t i = 0; i < ap_m68040_fp_row_count(); i++) {
    const ap_m68040_fp_row_t *row = ap_m68040_fp_row(i);
    if (row->execution.latency_halves != 0u) continue;
    short_circuited++;
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, row->normalization.latency_halves,
                                   "a row normalised a result it never made");
  }
  TEST_ASSERT_TRUE(short_circuited > 40u);
}

static void test_a_square_root_is_the_most_expensive_operation(void) {
  /* `FSQRT` executes in 103 cycles against `FDIV`'s 37.5 and `FMUL`'s 5 --
   * nearly three times a divide, and twenty times a multiply. It is the most
   * expensive single figure in Section 10. The conversion and normalisation
   * stages are the same `2(3)` and `2(3)` an addition pays, so the whole
   * difference is the iteration itself. */
  const ap_m68040_fp_row_t *sqrt_row =
      find("FSQRT", 0u, AP_M68040_FP_SIZE_NONE, AP_M68040_FP_SIZE_X,
           AP_M68040_FP_OPERANDS_NORM);
  const ap_m68040_fp_row_t *mul =
      find("FMUL", 0u, AP_M68040_FP_SIZE_NONE, AP_M68040_FP_SIZE_X,
           AP_M68040_FP_OPERANDS_NORM_NORM);
  TEST_ASSERT_NOT_NULL(sqrt_row);
  TEST_ASSERT_NOT_NULL(mul);
  TEST_ASSERT_EQUAL_UINT(CYCLES(103), sqrt_row->execution.latency_halves);
  TEST_ASSERT_EQUAL_UINT(CYCLES(5), mul->execution.latency_halves);
  TEST_ASSERT_EQUAL_UINT(mul->conversion.latency_halves,
                         sqrt_row->conversion.latency_halves);
  TEST_ASSERT_EQUAL_UINT(mul->normalization.latency_halves,
                         sqrt_row->normalization.latency_halves);
}

static void test_a_compare_normalises_in_one_cycle_where_an_add_takes_two(void) {
  /* `FCMP` prints a normalisation of `1` where `FADD` prints `2(3)`, and its
   * conversion and execution figures are identical to `FADD`'s throughout. A
   * compare performs the subtraction but discards the difference, so there is a
   * condition code to settle and no result to renormalise. The one clock that
   * remains is the difference between having an answer and having to write one
   * down. */
  const ap_m68040_fp_row_t *cmp =
      find("FCMP", 0u, AP_M68040_FP_SIZE_NONE, AP_M68040_FP_SIZE_X,
           AP_M68040_FP_OPERANDS_NORM_NORM);
  const ap_m68040_fp_row_t *add =
      find("FADD", 0u, AP_M68040_FP_SIZE_NONE, AP_M68040_FP_SIZE_X,
           AP_M68040_FP_OPERANDS_NORM_NORM);
  TEST_ASSERT_NOT_NULL(cmp);
  TEST_ASSERT_NOT_NULL(add);
  TEST_ASSERT_EQUAL_UINT(add->conversion.latency_halves,
                         cmp->conversion.latency_halves);
  TEST_ASSERT_EQUAL_UINT(add->execution.latency_halves,
                         cmp->execution.latency_halves);
  TEST_ASSERT_EQUAL_UINT(CYCLES(1), cmp->normalization.latency_halves);
  TEST_ASSERT_EQUAL_UINT(CYCLES(1), cmp->normalization.busy_halves);
  TEST_ASSERT_EQUAL_UINT(CYCLES(2), add->normalization.latency_halves);
  TEST_ASSERT_EQUAL_UINT(CYCLES(3), add->normalization.busy_halves);
}

static void test_an_extended_source_costs_one_more_conversion_cycle(void) {
  /* Throughout §10.7.3 the extended-size block converts in `3(4)` where the
   * single/double block converts in `2(3)`, and the special-operand rows are
   * 5 against 4. One cycle, uniformly, for the wider operand -- and the
   * execution stage never changes with size, because by then the operand is
   * extended whatever it started as. */
  /* `FADD` is compared on `Norm,Zero` rather than `Norm,Norm`, because its
   * `Norm,Norm` row at opclass 2 is the one the misprinted opclass makes
   * unreachable -- see the suspect test at the end of this file. The
   * substitution is a consequence of that defect and not a weakening of this
   * one: `Norm,Zero` prints the same figures in every block. */
  const struct {
    const char *name;
    ap_m68040_fp_operands_t operands;
  } dyadic[] = {{"FADD", AP_M68040_FP_OPERANDS_NORM_ZERO},
                {"FMUL", AP_M68040_FP_OPERANDS_NORM_NORM},
                {"FDIV", AP_M68040_FP_OPERANDS_NORM_NORM},
                {"FCMP", AP_M68040_FP_OPERANDS_NORM_NORM}};
  for (unsigned i = 0; i < 4; i++) {
    const ap_m68040_fp_row_t *narrow =
        find(dyadic[i].name, 2u, AP_M68040_FP_SIZE_D, AP_M68040_FP_SIZE_X,
             dyadic[i].operands);
    const ap_m68040_fp_row_t *wide =
        find(dyadic[i].name, 2u, AP_M68040_FP_SIZE_X, AP_M68040_FP_SIZE_X,
             dyadic[i].operands);
    TEST_ASSERT_NOT_NULL(narrow);
    TEST_ASSERT_NOT_NULL(wide);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(narrow->conversion.latency_halves + CYCLES(1),
                                   wide->conversion.latency_halves,
                                   "extended should convert one cycle slower");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(narrow->conversion.busy_halves + CYCLES(1),
                                   wide->conversion.busy_halves,
                                   "extended should occupy one cycle longer");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(narrow->execution.latency_halves,
                                   wide->execution.latency_halves,
                                   "execution should not depend on the source "
                                   "size");
  }
}

static void test_a_negative_integer_conversion_costs_more_than_a_positive(void) {
  /* `FMOVE` between an integer and a floating-point register prices the *sign*:
   * a positive source occupies the conversion stage for 11 cycles and a
   * negative one for 11.5, and the opclass 3 direction prints `3(9)` against
   * `3(10)` with a normalisation of 3.5 against 4.5.
   *
   * A negation is a real step in the conversion, not a flag: the mantissa has
   * to be complemented before it can be normalised. This is the only place in
   * Section 10 where an operand's *sign* changes a figure. */
  const ap_m68040_fp_row_t *pos =
      find("FMOVE", 2u, AP_M68040_FP_SIZE_W, AP_M68040_FP_SIZE_X,
           AP_M68040_FP_OPERANDS_POSITIVE);
  const ap_m68040_fp_row_t *neg =
      find("FMOVE", 2u, AP_M68040_FP_SIZE_W, AP_M68040_FP_SIZE_X,
           AP_M68040_FP_OPERANDS_NEGATIVE);
  TEST_ASSERT_NOT_NULL(pos);
  TEST_ASSERT_NOT_NULL(neg);
  TEST_ASSERT_EQUAL_UINT(CYCLES(11), pos->conversion.busy_halves);
  TEST_ASSERT_EQUAL_UINT(CYCLES(11.5), neg->conversion.busy_halves);
  TEST_ASSERT_EQUAL_UINT(pos->conversion.latency_halves,
                         neg->conversion.latency_halves);

  const ap_m68040_fp_row_t *out_pos =
      find("FMOVE", 3u, AP_M68040_FP_SIZE_L, AP_M68040_FP_SIZE_X,
           AP_M68040_FP_OPERANDS_POSITIVE);
  const ap_m68040_fp_row_t *out_neg =
      find("FMOVE", 3u, AP_M68040_FP_SIZE_L, AP_M68040_FP_SIZE_X,
           AP_M68040_FP_OPERANDS_NEGATIVE);
  TEST_ASSERT_NOT_NULL(out_pos);
  TEST_ASSERT_NOT_NULL(out_neg);
  TEST_ASSERT_EQUAL_UINT(CYCLES(9), out_pos->conversion.busy_halves);
  TEST_ASSERT_EQUAL_UINT(CYCLES(10), out_neg->conversion.busy_halves);
  TEST_ASSERT_EQUAL_UINT(CYCLES(3.5), out_pos->normalization.latency_halves);
  TEST_ASSERT_EQUAL_UINT(CYCLES(4.5), out_neg->normalization.latency_halves);
}

/* ---------------------------------------------------------------------------
 * FMOVEM, and two tables that count registers differently.
 * ------------------------------------------------------------------------- */

static void test_the_two_fmovem_tables_count_registers_differently(void) {
  /* A trap worth stating plainly. §10.7.2's `FMOVEM` cell prices *one* register
   * and note b adds three clocks for "each additional" one, so its count starts
   * at one. §10.7.3 prints "2 + (2 per reg)" -- an explicit base plus a term,
   * so its count starts at *zero* and no register is already paid for.
   *
   * Reading either convention onto the other misprices every list, and by a
   * different amount at each end. Both are modelled as their own page states
   * them and neither accessor is shared. */
  const ap_m68040_fp_row_t *row =
      find("FMOVEM", 4u, AP_M68040_FP_SIZE_NONE, AP_M68040_FP_SIZE_NONE,
           AP_M68040_FP_OPERANDS_ANY);
  TEST_ASSERT_NOT_NULL(row);
  TEST_ASSERT_EQUAL_UINT(CYCLES(2), row->conversion.latency_halves);
  TEST_ASSERT_EQUAL_UINT(CYCLES(2), row->conversion_per_register_halves);

  /* Zero registers costs the bare base, which is the point of the convention. */
  TEST_ASSERT_EQUAL_UINT(CYCLES(2),
                         ap_m68040_fp_movem_conversion_halves(row, 0u));
  TEST_ASSERT_EQUAL_UINT(CYCLES(4),
                         ap_m68040_fp_movem_conversion_halves(row, 1u));
  TEST_ASSERT_EQUAL_UINT(CYCLES(18),
                         ap_m68040_fp_movem_conversion_halves(row, 8u));
}

static void test_the_data_register_opclasses_cost_three_per_register(void) {
  /* Opclasses 4 and 5 charge two cycles a register and 6 and 7 charge three.
   * The pairs are the control-register and data-register forms of `FMOVEM`, and
   * a floating-point register is ten bytes where a control register is four --
   * so the wider transfer costs the extra cycle. */
  const unsigned expected[8] = {0u, 0u, 0u, 0u, 2u, 2u, 3u, 3u};
  for (unsigned opclass = 4u; opclass <= 7u; opclass++) {
    const ap_m68040_fp_row_t *row =
        find("FMOVEM", opclass, AP_M68040_FP_SIZE_NONE,
             AP_M68040_FP_SIZE_NONE, AP_M68040_FP_OPERANDS_ANY);
    TEST_ASSERT_NOT_NULL(row);
    TEST_ASSERT_EQUAL_UINT(CYCLES(expected[opclass]),
                           row->conversion_per_register_halves);
    /* The base is the same 2 in all four. */
    TEST_ASSERT_EQUAL_UINT(CYCLES(2), row->conversion.latency_halves);
  }
}

static void test_only_fmovem_grows_with_a_register_list(void) {
  /* Nothing else in §10.7.3 has a per-register term, and asking for one on a
   * row that has none returns zero rather than the row's plain conversion --
   * which would silently answer a question about a register list for an
   * instruction that has no list. */
  unsigned with_term = 0;
  for (size_t i = 0; i < ap_m68040_fp_row_count(); i++) {
    const ap_m68040_fp_row_t *row = ap_m68040_fp_row(i);
    if (row->conversion_per_register_halves == 0u) continue;
    with_term++;
    TEST_ASSERT_EQUAL_STRING("FMOVEM", row->instruction);
  }
  TEST_ASSERT_EQUAL_UINT(4u, with_term);

  const ap_m68040_fp_row_t *add =
      find("FADD", 0u, AP_M68040_FP_SIZE_NONE, AP_M68040_FP_SIZE_X,
           AP_M68040_FP_OPERANDS_NORM_NORM);
  TEST_ASSERT_EQUAL_UINT(0u, ap_m68040_fp_movem_conversion_halves(add, 4u));
  TEST_ASSERT_EQUAL_UINT(0u, ap_m68040_fp_movem_conversion_halves(NULL, 4u));
}

/* ---------------------------------------------------------------------------
 * Lookup and the table's shape.
 * ------------------------------------------------------------------------- */

static void test_a_size_set_matches_any_of_its_members(void) {
  /* The table prints "S,D" and "B,W,L" as single rows, so a query for either
   * member must select the same row. Modelling the sets as printed rather than
   * expanding them keeps a row checkable against the page. */
  const ap_m68040_fp_row_t *s =
      find("FADD", 2u, AP_M68040_FP_SIZE_S, AP_M68040_FP_SIZE_X,
           AP_M68040_FP_OPERANDS_ZERO_ZERO);
  const ap_m68040_fp_row_t *d =
      find("FADD", 2u, AP_M68040_FP_SIZE_D, AP_M68040_FP_SIZE_X,
           AP_M68040_FP_OPERANDS_ZERO_ZERO);
  TEST_ASSERT_NOT_NULL(s);
  TEST_ASSERT_EQUAL_PTR(s, d);
  /* And extended is a different row with a different figure. */
  const ap_m68040_fp_row_t *x =
      find("FADD", 2u, AP_M68040_FP_SIZE_X, AP_M68040_FP_SIZE_X,
           AP_M68040_FP_OPERANDS_ZERO_ZERO);
  TEST_ASSERT_NOT_NULL(x);
  TEST_ASSERT_NOT_EQUAL(s, x);
  TEST_ASSERT_EQUAL_UINT(CYCLES(4), s->conversion.latency_halves);
  TEST_ASSERT_EQUAL_UINT(CYCLES(5), x->conversion.latency_halves);
}

static void test_a_dash_matches_only_a_dash(void) {
  /* Opclass 0 rows print a dash in the size column: the operand is already in a
   * register, so there is no source size. A query naming a size must not select
   * one, and a query naming no size must not select a sized row -- otherwise a
   * register-to-register operation would be priced as a memory one. */
  TEST_ASSERT_NOT_NULL(find("FADD", 0u, AP_M68040_FP_SIZE_NONE,
                            AP_M68040_FP_SIZE_X,
                            AP_M68040_FP_OPERANDS_ZERO_ZERO));
  TEST_ASSERT_NULL(find("FADD", 0u, AP_M68040_FP_SIZE_D, AP_M68040_FP_SIZE_X,
                        AP_M68040_FP_OPERANDS_ZERO_ZERO));
  TEST_ASSERT_NULL(find("FADD", 2u, AP_M68040_FP_SIZE_NONE,
                        AP_M68040_FP_SIZE_X,
                        AP_M68040_FP_OPERANDS_ZERO_ZERO));
}

static void test_an_unknown_query_returns_nothing_rather_than_a_guess(void) {
  TEST_ASSERT_NULL(find("FNONESUCH", 0u, AP_M68040_FP_SIZE_NONE,
                        AP_M68040_FP_SIZE_X, AP_M68040_FP_OPERANDS_NORM_NORM));
  TEST_ASSERT_NULL(find(NULL, 0u, AP_M68040_FP_SIZE_NONE, AP_M68040_FP_SIZE_X,
                        AP_M68040_FP_OPERANDS_NORM_NORM));
  /* `FSQRT` is monadic and has no `Norm,Norm` row. */
  TEST_ASSERT_NULL(find("FSQRT", 0u, AP_M68040_FP_SIZE_NONE,
                        AP_M68040_FP_SIZE_X,
                        AP_M68040_FP_OPERANDS_NORM_NORM));
  TEST_ASSERT_NULL(ap_m68040_fp_row(ap_m68040_fp_row_count()));
  TEST_ASSERT_EQUAL_UINT(0u, ap_m68040_fp_total_latency_halves(NULL));
}

static void test_section_10_7_3_is_fully_transcribed(void) {
  /* Pages 10-35 to 10-37, 91 rows across eight instruction groups. Pinned so a
   * regeneration that drops a block fails here rather than as a wrong figure
   * much later. */
  TEST_ASSERT_EQUAL_UINT(91u, (unsigned)ap_m68040_fp_row_count());
  const struct {
    const char *name;
    unsigned rows;
  } expected[] = {{"FADD", 15u},  {"FMUL", 12u},   {"FDIV", 12u},
                  {"FSQRT", 6u},  {"FMOVE", 27u},  {"FMOVEM", 4u},
                  {"FCMP", 15u}};
  unsigned counted = 0;
  for (unsigned e = 0; e < 7; e++) {
    unsigned n = 0;
    for (size_t i = 0; i < ap_m68040_fp_row_count(); i++)
      if (strcmp(ap_m68040_fp_row(i)->instruction, expected[e].name) == 0) n++;
    TEST_ASSERT_EQUAL_UINT_MESSAGE(expected[e].rows, n,
                                   "an instruction group changed size");
    counted += n;
  }
  TEST_ASSERT_EQUAL_UINT(91u, counted);
}

/* ---------------------------------------------------------------------------
 * The qualifier-column slips.
 * ------------------------------------------------------------------------- */

static void test_the_add_table_prints_an_opclass_its_own_block_contradicts(void) {
  /* Twelfth suspect entry, and the one with the clearest witness in Section 10.
   *
   * `FADD, FSUB` is fifteen rows: three blocks of five operand cases, one block
   * per (opclass, size) pair -- opclass 0 with no size, opclass 2 at single or
   * double, opclass 2 at extended. The sixth row is the `Norm,Norm` case of the
   * second block, and it prints opclass **0** with size `S,D`. Taken as printed
   * the first block has six rows and the second has four, with the second
   * missing the only case that actually computes.
   *
   * `FCMP` settles it. It is the same fifteen-row shape, three blocks of the
   * same five operand cases, printed two pages later -- and its sixth row reads
   * opclass **2**. `FMUL` and `FDIV` have the same three-block structure at four
   * operand cases each and are consistent throughout. So the replacement is
   * supplied by the section itself.
   *
   * Transcribed as printed all the same, under the standing rule and for
   * consistency with the eleven entries before it. This test states the
   * contradiction so that whoever fixes it has to delete a finding to do so. */
  const ap_m68040_fp_row_t *row = ap_m68040_fp_row(5u);
  TEST_ASSERT_EQUAL_STRING("FADD", row->instruction);
  TEST_ASSERT_EQUAL_UINT(AP_M68040_FP_OPERANDS_NORM_NORM, row->operands);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, row->opclass,
                                 "row 6 is transcribed as printed");
  TEST_ASSERT_EQUAL_UINT(AP_M68040_FP_SIZE_S | AP_M68040_FP_SIZE_D, row->size);

  /* The witness: `FCMP`'s matching row prints 2. */
  const ap_m68040_fp_row_t *witness =
      find("FCMP", 2u, AP_M68040_FP_SIZE_D, AP_M68040_FP_SIZE_X,
           AP_M68040_FP_OPERANDS_NORM_NORM);
  TEST_ASSERT_NOT_NULL(witness);
  TEST_ASSERT_EQUAL_UINT(2u, witness->opclass);

  /* The consequence, stated as a lookup: as printed, an `FADD` at opclass 2
   * with a double source and two normalised operands has no row at all -- the
   * one combination a program is most likely to execute. */
  TEST_ASSERT_NULL(find("FADD", 2u, AP_M68040_FP_SIZE_D, AP_M68040_FP_SIZE_X,
                        AP_M68040_FP_OPERANDS_NORM_NORM));
  /* Where the equivalent `FCMP` and `FMUL` queries both succeed. */
  TEST_ASSERT_NOT_NULL(find("FMUL", 2u, AP_M68040_FP_SIZE_D,
                            AP_M68040_FP_SIZE_X,
                            AP_M68040_FP_OPERANDS_NORM_NORM));
}

static void test_the_divide_table_drops_a_size_from_one_row(void) {
  /* Thirteenth, and the same class: `FDIV`'s opclass 2 extended block is five
   * rows, and its `-,Inf` row prints a dash in the size column where the four
   * around it print `X`. A dash means "no size field applies", which is true of
   * opclass 0 and false of opclass 2 -- an opclass 2 operation reads a sized
   * source by definition.
   *
   * As with the `FADD` slip the effect is a lookup that fails: an extended
   * `FDIV` by an infinity finds no row. Kept as printed. */
  TEST_ASSERT_NULL(find("FDIV", 2u, AP_M68040_FP_SIZE_X, AP_M68040_FP_SIZE_X,
                        AP_M68040_FP_OPERANDS_ANY_INF));
  /* Its neighbours in the same block are reachable. */
  TEST_ASSERT_NOT_NULL(find("FDIV", 2u, AP_M68040_FP_SIZE_X,
                            AP_M68040_FP_SIZE_X,
                            AP_M68040_FP_OPERANDS_ANY_ZERO));
  TEST_ASSERT_NOT_NULL(find("FDIV", 2u, AP_M68040_FP_SIZE_X,
                            AP_M68040_FP_SIZE_X,
                            AP_M68040_FP_OPERANDS_ANY_NAN));
  /* And the figure itself is the block's, so only the qualifier is at fault:
   * 5 cycles, matching the extended block's other special-operand rows. */
  const ap_m68040_fp_row_t *orphan =
      find("FDIV", 2u, AP_M68040_FP_SIZE_NONE, AP_M68040_FP_SIZE_X,
           AP_M68040_FP_OPERANDS_ANY_INF);
  TEST_ASSERT_NOT_NULL(orphan);
  TEST_ASSERT_EQUAL_UINT(CYCLES(5), orphan->conversion.latency_halves);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_divide_takes_thirty_seven_and_a_half_cycles);
  RUN_TEST(test_several_figures_are_not_whole_cycles);
  RUN_TEST(test_a_stage_is_never_free_before_it_has_finished);
  RUN_TEST(test_an_unparenthesised_figure_is_busy_for_exactly_its_latency);
  RUN_TEST(test_a_special_operand_skips_both_later_stages);
  RUN_TEST(test_a_square_root_is_the_most_expensive_operation);
  RUN_TEST(test_a_compare_normalises_in_one_cycle_where_an_add_takes_two);
  RUN_TEST(test_an_extended_source_costs_one_more_conversion_cycle);
  RUN_TEST(test_a_negative_integer_conversion_costs_more_than_a_positive);
  RUN_TEST(test_the_two_fmovem_tables_count_registers_differently);
  RUN_TEST(test_the_data_register_opclasses_cost_three_per_register);
  RUN_TEST(test_only_fmovem_grows_with_a_register_list);
  RUN_TEST(test_a_size_set_matches_any_of_its_members);
  RUN_TEST(test_a_dash_matches_only_a_dash);
  RUN_TEST(test_an_unknown_query_returns_nothing_rather_than_a_guess);
  RUN_TEST(test_section_10_7_3_is_fully_transcribed);
  RUN_TEST(test_the_add_table_prints_an_opclass_its_own_block_contradicts);
  RUN_TEST(test_the_divide_table_drops_a_size_from_one_row);
  return UNITY_END();
}

/* MC68882 §4.3.2: the transcendental classification, and the acceptance
 * criterion for a PROVISIONAL that is deliberately left open.
 *
 * These tests are unusual in this tree: several assert what the model does
 * *not* do. That is the point. An unimplemented operation is only an honest
 * gap for as long as something checks that it is still reported as one, and the
 * bounds a future implementation must meet are only a specification if they are
 * written down somewhere a test can read them. */

#include "cpu/m68882/ap_m68882_accuracy.h"

#include "cpu/m68882/ap_m68882.h"
#include "cpu/m68882/ap_m68882_decode.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Opclass 000 -- register to register -- source FP0, destination FP1. */
static uint16_t command_for(ap_m68882_operation_t operation) {
  return (uint16_t)((0u << 13) | (0u << 10) | (1u << 7) | (unsigned)operation);
}

static void test_the_four_families_are_exactly_what_4_3_2_names(void) {
  /* "the trigonometric, hyperbolic, logarithmic, and exponential
   * instructions" -- nineteen operations, enumerated rather than inferred,
   * because §4.3.2 defines the set by *exclusion* from the paragraphs before
   * it and a definition by exclusion silently absorbs anything later added to
   * the enum. */
  const ap_m68882_operation_t transcendental[] = {
      AP_M68882_OP_FSIN,   AP_M68882_OP_FCOS,     AP_M68882_OP_FTAN,
      AP_M68882_OP_FASIN,  AP_M68882_OP_FACOS,    AP_M68882_OP_FATAN,
      AP_M68882_OP_FSINCOS, AP_M68882_OP_FSINH,   AP_M68882_OP_FCOSH,
      AP_M68882_OP_FTANH,  AP_M68882_OP_FATANH,   AP_M68882_OP_FLOGN,
      AP_M68882_OP_FLOGNP1, AP_M68882_OP_FLOG10,  AP_M68882_OP_FLOG2,
      AP_M68882_OP_FETOX,  AP_M68882_OP_FETOXM1,  AP_M68882_OP_FTWOTOX,
      AP_M68882_OP_FTENTOX};
  const unsigned count =
      sizeof transcendental / sizeof transcendental[0];
  TEST_ASSERT_EQUAL_UINT(ap_m68882_transcendental_count(), count);
  for (unsigned i = 0; i < count; i++)
    TEST_ASSERT_TRUE_MESSAGE(ap_m68882_is_transcendental(transcendental[i]),
                             "a named transcendental was not classified");

  /* And the count is what a sweep of the whole encoding space finds, so an
   * operation added to the enum without being classified changes this. */
  unsigned found = 0;
  for (unsigned op = 0; op <= 0x3F; op++)
    if (ap_m68882_is_transcendental((ap_m68882_operation_t)op)) found++;
  TEST_ASSERT_EQUAL_UINT(19u, found);
}

static void test_a_square_root_is_not_a_transcendental(void) {
  /* §4.3.2's parenthesis is doing real work: "transcendental (**except square
   * root**) functions". A square root has one right answer to within half a
   * unit in the last place, which is why `FSQRT` is implemented and exact
   * while `FSIN` is neither. Misclassifying it would move a solved operation
   * into the unsolved pile. */
  TEST_ASSERT_FALSE(ap_m68882_is_transcendental(AP_M68882_OP_FSQRT));

  /* The other IEEE-specified monadics, likewise implemented and exact. */
  const ap_m68882_operation_t exact[] = {
      AP_M68882_OP_FGETEXP, AP_M68882_OP_FGETMAN, AP_M68882_OP_FINT,
      AP_M68882_OP_FINTRZ,  AP_M68882_OP_FSCALE,  AP_M68882_OP_FABS,
      AP_M68882_OP_FNEG};
  for (unsigned i = 0; i < sizeof exact / sizeof exact[0]; i++)
    TEST_ASSERT_FALSE_MESSAGE(ap_m68882_is_transcendental(exact[i]),
                              "an exact operation was called transcendental");

  /* And the arithmetic, which §4.3.1 holds to the IEEE bound. */
  const ap_m68882_operation_t arithmetic[] = {
      AP_M68882_OP_FADD, AP_M68882_OP_FSUB, AP_M68882_OP_FMUL,
      AP_M68882_OP_FDIV, AP_M68882_OP_FCMP, AP_M68882_OP_FTST,
      AP_M68882_OP_FREM, AP_M68882_OP_FMOD};
  for (unsigned i = 0; i < sizeof arithmetic / sizeof arithmetic[0]; i++)
    TEST_ASSERT_FALSE(ap_m68882_is_transcendental(arithmetic[i]));
}

static void test_the_manual_gives_two_worst_case_figures_that_disagree(void) {
  /* §4.3.2 prints the worst case both ways -- "one unit in the last place of
   * double precision (which is equal to 4096 units in the last place of
   * extended precision)" -- and the conversion does not hold. A double
   * significand is 53 bits and an extended one 64, so one ULP of double is
   * 2^11 = 2048 ULP of extended, not 4096.
   *
   * Both are transcribed as printed and neither is derived from the other. This
   * test states the discrepancy so that nobody quietly reconciles the two
   * constants: whoever does will have to delete an assertion that says the
   * manual disagrees with itself, which is the point at which they should go
   * and read page 4-7. */
  const unsigned derived = 1u << (64u - 53u);
  TEST_ASSERT_EQUAL_UINT(2048u, derived);
  TEST_ASSERT_EQUAL_UINT(4096u,
                         AP_M68882_TRANSCENDENTAL_WORST_CASE_ULP_EXTENDED);
  TEST_ASSERT_EQUAL_UINT(1u, AP_M68882_TRANSCENDENTAL_WORST_CASE_ULP_DOUBLE);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      derived * 2u, AP_M68882_TRANSCENDENTAL_WORST_CASE_ULP_EXTENDED,
      "the manual's figure is exactly twice the derived one");
}

static void test_the_typical_bound_is_far_tighter_than_the_worst_case(void) {
  /* "The typical error bound for these instructions is approximately 64 units
   * in the last place of extended precision" -- sixty-four times better than
   * the worst case, and the figure the manual's worked example computes: the
   * example's difference is 2^6 times the least-significant bit, and 2^6 is 64.
   *
   * Worth pinning because the exponent is exactly what an OCR pass destroys.
   * `pdftotext` renders "2^6" as "26", and so does the page image at ordinary
   * resolution; only the arithmetic recovers it. A model that took "26 times
   * the least-significant bit" at face value would record a typical bound of
   * 26 rather than 64. */
  TEST_ASSERT_EQUAL_UINT(64u, AP_M68882_TRANSCENDENTAL_TYPICAL_ULP_EXTENDED);
  TEST_ASSERT_EQUAL_UINT(1u << 6, AP_M68882_TRANSCENDENTAL_TYPICAL_ULP_EXTENDED);
  TEST_ASSERT_EQUAL_UINT(
      64u, AP_M68882_TRANSCENDENTAL_WORST_CASE_ULP_EXTENDED /
               AP_M68882_TRANSCENDENTAL_TYPICAL_ULP_EXTENDED);
  /* "an ALU with a finite precision of 67 bits": three guard bits over the
   * 64-bit extended significand, which is the hardware reason the recursion
   * cannot do better. */
  TEST_ASSERT_EQUAL_UINT(67u, AP_M68882_ALU_PRECISION_BITS);
  TEST_ASSERT_EQUAL_UINT(3u, AP_M68882_ALU_PRECISION_BITS - 64u);
}

static void test_every_transcendental_is_now_computed(void) {
  /* The `PROVISIONAL` is closed. All nineteen of §4.3.2's transcendentals are
   * computed to within its published bound -- `m68882_transcendental_suite`
   * measures that against expectations generated to 120 decimal digits -- and
   * none of them reports `UNIMPLEMENTED` any more.
   *
   * They remain *classified* as transcendentals, and that is not a leftover.
   * §4.3.2's bound still applies to them: these are approximations conforming
   * to a published interval, not exactly-rounded operations like `FSQRT`. The
   * classification is what marks the difference, and erasing it because the
   * functions now return answers would lose the only record that they are
   * approximate at all. */
  const ap_m68882_operation_t transcendental[] = {
      AP_M68882_OP_FSIN,   AP_M68882_OP_FCOS,     AP_M68882_OP_FTAN,
      AP_M68882_OP_FASIN,  AP_M68882_OP_FACOS,    AP_M68882_OP_FATAN,
      AP_M68882_OP_FSINCOS, AP_M68882_OP_FSINH,   AP_M68882_OP_FCOSH,
      AP_M68882_OP_FTANH,  AP_M68882_OP_FATANH,   AP_M68882_OP_FLOGN,
      AP_M68882_OP_FLOGNP1, AP_M68882_OP_FLOG10,  AP_M68882_OP_FLOG2,
      AP_M68882_OP_FETOX,  AP_M68882_OP_FETOXM1,  AP_M68882_OP_FTWOTOX,
      AP_M68882_OP_FTENTOX};
  TEST_ASSERT_EQUAL_UINT(19u, ap_m68882_transcendental_count());
  for (unsigned i = 0; i < 19u; i++) {
    TEST_ASSERT_TRUE_MESSAGE(ap_m68882_is_transcendental(transcendental[i]),
                             "a computed transcendental lost its marking");
    ap_m68882_t fpu;
    ap_m68882_reset(&fpu);
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        AP_M68882_EXECUTED,
        ap_m68882_execute(&fpu, 0xF200u, command_for(transcendental[i])),
        "a transcendental did not execute");
  }
}

static void test_the_remaining_gaps_are_not_transcendentals(void) {
  /* What is still unimplemented is the rounding and remainder forms, and none
   * of them is a §4.3.2 transcendental -- so the accuracy bound in this header
   * has nothing to say about them, and closing them is a different piece of
   * work with a different acceptance criterion. Pinned so the two kinds of gap
   * do not get conflated. */
  const ap_m68882_operation_t pending[] = {
      AP_M68882_OP_FMOD, AP_M68882_OP_FREM, AP_M68882_OP_FSGLDIV,
      AP_M68882_OP_FSGLMUL};
  for (unsigned i = 0; i < 4u; i++) {
    TEST_ASSERT_FALSE(ap_m68882_is_transcendental(pending[i]));
    ap_m68882_t fpu;
    ap_m68882_reset(&fpu);
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        AP_M68882_UNIMPLEMENTED,
        ap_m68882_execute(&fpu, 0xF200u, command_for(pending[i])),
        "a remaining gap should report unimplemented, not F-line");
  }
}

static void test_an_implemented_operation_is_not_reported_as_a_gap(void) {
  /* The control for the test above: the same harness on an operation that *is*
   * implemented must not report `UNIMPLEMENTED`. Without this, a change that
   * broke `ap_m68882_execute` into always reporting a gap would make the
   * previous test pass for the wrong reason. */
  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  TEST_ASSERT_EQUAL_INT(AP_M68882_EXECUTED,
                        ap_m68882_execute(&fpu, 0xF200u,
                                          command_for(AP_M68882_OP_FSQRT)));
}

static void test_a_conditional_reaches_the_status_register(void) {
  /* The evaluator is one thing; a part that *uses* it is another. Two
   * iterations ago a write-back was built and left uncalled, and the unit test
   * of the helper could not see it -- so this drives `ap_m68882_condition`
   * against a real part and reads the FPSR back, rather than testing the
   * predicate table again.
   *
   * `FTST` of a NAN leaves the NAN condition code set; a non-aware predicate
   * then raises `BSUN`, and §6.1.10 folds it into `AEXC(IOP)`. Checking the
   * accrued bit is the part that a direct assignment to the exception byte
   * would fail: the accrued byte is the history, and it is what a handler reads
   * after the fact. */
  ap_m68882_t fpu;
  ap_m68882_reset(&fpu);
  /* A NAN in FP0, then FTST to set the condition codes from it. */
  fpu.regs.fp[0] = (ap_m68882_extended_t){false, 0x7FFFu,
                                          0xC000000000000000ULL};
  TEST_ASSERT_EQUAL_INT(AP_M68882_EXECUTED,
                        ap_m68882_execute(&fpu, 0xF200u,
                                          command_for(AP_M68882_OP_FTST)));
  TEST_ASSERT_NOT_EQUAL_UINT_MESSAGE(
      0u, fpu.regs.fpsr & (1u << AP_M68882_FPCC_NAN),
      "FTST of a NAN should leave the unordered condition");

  /* `GT` at $12: IEEE non-aware, so an unordered comparison raises BSUN. */
  TEST_ASSERT_FALSE_MESSAGE(ap_m68882_condition(&fpu, 0x12u),
                            "GT is false when unordered");
  TEST_ASSERT_NOT_EQUAL_UINT_MESSAGE(
      0u, fpu.regs.fpsr & (1u << AP_M68882_EXC_BSUN),
      "a non-aware predicate must raise BSUN into the FPSR");
  TEST_ASSERT_NOT_EQUAL_UINT_MESSAGE(
      0u, fpu.regs.fpsr & (1u << AP_M68882_AEXC_IOP),
      "BSUN accrues into AEXC(IOP), which is the history a handler reads");

  /* `OGT` at $02 asks the same question of the same operands and raises
   * nothing: the difference between the two halves of Table 4-8 is visible in
   * the status register and nowhere else. */
  ap_m68882_t aware;
  ap_m68882_reset(&aware);
  aware.regs.fp[0] = fpu.regs.fp[0];
  (void)ap_m68882_execute(&aware, 0xF200u, command_for(AP_M68882_OP_FTST));
  TEST_ASSERT_FALSE(ap_m68882_condition(&aware, 0x02u));
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      0u, aware.regs.fpsr & (1u << AP_M68882_EXC_BSUN),
      "an IEEE-aware predicate never raises BSUN");

  /* And an ordered comparison raises nothing even from the non-aware half. */
  ap_m68882_t ordered;
  ap_m68882_reset(&ordered);
  ordered.regs.fp[0] = (ap_m68882_extended_t){false, 0x3FFF,
                                              0x8000000000000000ULL};
  (void)ap_m68882_execute(&ordered, 0xF200u, command_for(AP_M68882_OP_FTST));
  TEST_ASSERT_TRUE_MESSAGE(ap_m68882_condition(&ordered, 0x12u),
                           "GT is true for an ordered positive result");
  TEST_ASSERT_EQUAL_UINT(0u,
                         ordered.regs.fpsr & (1u << AP_M68882_EXC_BSUN));
}

/* Put `value` in FP0 and run `FTST`, which sets the condition codes from it. */
static void set_condition_from(ap_m68882_t *fpu, ap_m68882_extended_t value) {
  ap_m68882_reset(fpu);
  fpu->regs.fp[0] = value;
  (void)ap_m68882_execute(fpu, 0xF200u, command_for(AP_M68882_OP_FTST));
}

static void test_table_2_1_generates_only_eight_combinations(void) {
  /* "The operation result data type determines how the four condition code bits
   * are set ... Because of the mutually exclusive nature of the data types
   * described by the condition code bits, the FPCP generates only eight of the
   * 16 possible combinations."
   *
   * `N` is the sign of the mantissa and is set *independently of the type*, so
   * a negative zero is `N` **and** `Z`, and a negative NAN is `N` **and**
   * `NAN`. An implementation that treated `N` as meaningful only for a
   * normalized result would clear it in exactly the two places a program is
   * most likely to be checking a sign. */
  const struct {
    ap_m68882_extended_t value;
    bool n, z, i, nan;
    const char *what;
  } rows[] = {
      {{false, 0x3FFF, 0x8000000000000000ULL}, false, false, false, false,
       "+normalized"},
      {{true, 0x3FFF, 0x8000000000000000ULL}, true, false, false, false,
       "-normalized"},
      {{false, 0u, 0x4000000000000000ULL}, false, false, false, false,
       "+denormalized"},
      {{true, 0u, 0x4000000000000000ULL}, true, false, false, false,
       "-denormalized"},
      {{false, 0u, 0u}, false, true, false, false, "+0"},
      {{true, 0u, 0u}, true, true, false, false, "-0"},
      {{false, 0x7FFFu, 0u}, false, false, true, false, "+infinity"},
      {{true, 0x7FFFu, 0u}, true, false, true, false, "-infinity"},
      {{false, 0x7FFFu, 0xC000000000000000ULL}, false, false, false, true,
       "+NAN"},
      {{true, 0x7FFFu, 0xC000000000000000ULL}, true, false, false, true,
       "-NAN"},
  };
  for (unsigned r = 0; r < sizeof rows / sizeof rows[0]; r++) {
    ap_m68882_t fpu;
    set_condition_from(&fpu, rows[r].value);
    const uint32_t s = fpu.regs.fpsr;
    TEST_ASSERT_EQUAL_MESSAGE(rows[r].n, ((s >> AP_M68882_FPCC_N) & 1u) != 0u,
                              rows[r].what);
    TEST_ASSERT_EQUAL_MESSAGE(rows[r].z, ((s >> AP_M68882_FPCC_Z) & 1u) != 0u,
                              rows[r].what);
    TEST_ASSERT_EQUAL_MESSAGE(rows[r].i, ((s >> AP_M68882_FPCC_I) & 1u) != 0u,
                              rows[r].what);
    TEST_ASSERT_EQUAL_MESSAGE(rows[r].nan,
                              ((s >> AP_M68882_FPCC_NAN) & 1u) != 0u,
                              rows[r].what);
    /* Never more than one of Z, I and NAN: the data types are mutually
     * exclusive, which is why only eight combinations occur. */
    const unsigned exclusive = (unsigned)rows[r].z + (unsigned)rows[r].i +
                               (unsigned)rows[r].nan;
    TEST_ASSERT_TRUE_MESSAGE(exclusive <= 1u,
                             "Z, I and NAN are mutually exclusive");
  }
}

static void test_a_comparison_reaches_a_branch_decision(void) {
  /* The whole chain, which nothing exercised end to end: `FCMP` produces a
   * result, the result's data type sets the condition codes, a predicate reads
   * them, and the answer is what a branch would act on. Each half was tested
   * against the manual separately -- and two halves that are individually right
   * can still disagree about what they mean by `N`.
   *
   * §2.3.1 states the four IEEE conditions the chain must produce:
   * `EQ = Z`, `GT = ~(N v NAN v Z)`, `LT = N ^ ~(NAN v Z)`, `UN = NAN`. Those
   * are the aware predicates `$01`, `$02`, `$04` and `$08`. */
  const ap_m68882_extended_t one = {false, AP_M68882_BIAS_EXTENDED,
                                    0x8000000000000000ULL};
  const ap_m68882_extended_t two = {false, AP_M68882_BIAS_EXTENDED + 1,
                                    0x8000000000000000ULL};
  const ap_m68882_extended_t nan = {false, 0x7FFFu, 0xC000000000000000ULL};

  const struct {
    ap_m68882_extended_t destination, source;
    bool eq, gt, lt, un;
    const char *what;
  } cases[] = {
      {two, one, false, true, false, false, "2 compared with 1 is greater"},
      {one, two, false, false, true, false, "1 compared with 2 is less"},
      {one, one, true, false, false, false, "1 compared with 1 is equal"},
      {one, nan, false, false, false, true, "anything against a NAN is "
                                            "unordered"},
      {nan, one, false, false, false, true, "and either way round"},
  };

  for (unsigned c = 0; c < sizeof cases / sizeof cases[0]; c++) {
    ap_m68882_t fpu;
    ap_m68882_reset(&fpu);
    fpu.regs.fp[0] = cases[c].source;
    fpu.regs.fp[1] = cases[c].destination;
    /* Source FP0, destination FP1. */
    TEST_ASSERT_EQUAL_INT(AP_M68882_EXECUTED,
                          ap_m68882_execute(&fpu, 0xF200u,
                                            command_for(AP_M68882_OP_FCMP)));

    TEST_ASSERT_EQUAL_MESSAGE(cases[c].eq,
                              ap_m68882_condition(&fpu, 0x01u),
                              cases[c].what);
    TEST_ASSERT_EQUAL_MESSAGE(cases[c].gt,
                              ap_m68882_condition(&fpu, 0x02u),
                              cases[c].what);
    TEST_ASSERT_EQUAL_MESSAGE(cases[c].lt,
                              ap_m68882_condition(&fpu, 0x04u),
                              cases[c].what);
    TEST_ASSERT_EQUAL_MESSAGE(cases[c].un,
                              ap_m68882_condition(&fpu, 0x08u),
                              cases[c].what);

    /* Exactly one of the four holds, always: that is what makes them the IEEE
     * conditions rather than four independent tests. */
    const unsigned held = (unsigned)cases[c].eq + (unsigned)cases[c].gt +
                          (unsigned)cases[c].lt + (unsigned)cases[c].un;
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1u, held,
                                   "exactly one IEEE condition holds");
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_four_families_are_exactly_what_4_3_2_names);
  RUN_TEST(test_a_square_root_is_not_a_transcendental);
  RUN_TEST(test_the_manual_gives_two_worst_case_figures_that_disagree);
  RUN_TEST(test_the_typical_bound_is_far_tighter_than_the_worst_case);
  RUN_TEST(test_table_2_1_generates_only_eight_combinations);
  RUN_TEST(test_a_comparison_reaches_a_branch_decision);
  RUN_TEST(test_a_conditional_reaches_the_status_register);
  RUN_TEST(test_every_transcendental_is_now_computed);
  RUN_TEST(test_the_remaining_gaps_are_not_transcendentals);
  RUN_TEST(test_an_implemented_operation_is_not_reported_as_a_gap);
  return UNITY_END();
}

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

static void test_the_exponential_family_is_computed_and_the_rest_is_not(void) {
  /* The PROVISIONAL, tracked as it closes. §4.3.2's nineteen transcendentals
   * are being implemented a family at a time; each is computed to within the
   * published bound (`m68882_transcendental_suite` measures that) and the rest
   * still report `UNIMPLEMENTED`.
   *
   * The distinction matters more than the count. An unimplemented operation
   * must report `UNIMPLEMENTED` and never `TAKE_LINE_F`: the encoding is
   * perfectly valid and the gap is ours, and dressing it up as the machine's
   * behaviour would make it invisible. This test fails whenever a family lands,
   * which is intended -- the failure points at the acceptance criterion. */
  const ap_m68882_operation_t computed[] = {
      AP_M68882_OP_FETOX,  AP_M68882_OP_FETOXM1, AP_M68882_OP_FTWOTOX,
      AP_M68882_OP_FTENTOX, AP_M68882_OP_FLOGN,  AP_M68882_OP_FLOGNP1,
      AP_M68882_OP_FLOG10, AP_M68882_OP_FLOG2,  AP_M68882_OP_FSIN,
      AP_M68882_OP_FCOS,   AP_M68882_OP_FTAN,   AP_M68882_OP_FSINCOS};
  const ap_m68882_operation_t pending[] = {
      AP_M68882_OP_FASIN,  AP_M68882_OP_FACOS,  AP_M68882_OP_FATAN,
      AP_M68882_OP_FSINH,  AP_M68882_OP_FCOSH,  AP_M68882_OP_FTANH,
      AP_M68882_OP_FATANH};

  /* Every one of the nineteen is still classified as transcendental: computing
   * one does not make it stop being an approximation under a published bound. */
  for (unsigned i = 0; i < 12u; i++)
    TEST_ASSERT_TRUE(ap_m68882_is_transcendental(computed[i]));
  for (unsigned i = 0; i < 7u; i++)
    TEST_ASSERT_TRUE(ap_m68882_is_transcendental(pending[i]));
  TEST_ASSERT_EQUAL_UINT(19u, 12u + 7u);
  TEST_ASSERT_EQUAL_UINT(19u, ap_m68882_transcendental_count());

  for (unsigned i = 0; i < 12u; i++) {
    ap_m68882_t fpu;
    ap_m68882_reset(&fpu);
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        AP_M68882_EXECUTED,
        ap_m68882_execute(&fpu, 0xF200u, command_for(computed[i])),
        "an implemented transcendental did not execute");
  }
  for (unsigned i = 0; i < 7u; i++) {
    ap_m68882_t fpu;
    ap_m68882_reset(&fpu);
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        AP_M68882_UNIMPLEMENTED,
        ap_m68882_execute(&fpu, 0xF200u, command_for(pending[i])),
        "a pending transcendental should report unimplemented, not F-line");
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

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_four_families_are_exactly_what_4_3_2_names);
  RUN_TEST(test_a_square_root_is_not_a_transcendental);
  RUN_TEST(test_the_manual_gives_two_worst_case_figures_that_disagree);
  RUN_TEST(test_the_typical_bound_is_far_tighter_than_the_worst_case);
  RUN_TEST(test_the_exponential_family_is_computed_and_the_rest_is_not);
  RUN_TEST(test_an_implemented_operation_is_not_reported_as_a_gap);
  return UNITY_END();
}

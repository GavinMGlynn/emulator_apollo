/* The MC68040's floating-point programming model is the MC68882's.
 *
 * `MC68040 User's Manual (1993)` §9.1 and §9.2, against the 68882 modules this
 * core already carries.
 *
 * ## Why this suite exists instead of a second register file
 *
 * §9.1 states the relationship outright: "The MC68040 FPU is compatible with
 * the MC68881/MC68882." Duplicating `FPCR`, `FPSR` and their encodings for the
 * 68040 would create two descriptions of one thing, and the copy no booting
 * machine exercised would drift -- the same argument that made `ap_cpu_decode`
 * a wrapper rather than a second decoder.
 *
 * So the model is shared, and this suite is the evidence for that sharing: it
 * checks the 68040 manual's own statements against the 68882 implementation. If
 * the two parts ever turn out to differ, one of these tests fails and the
 * sharing has to be revisited rather than silently being wrong.
 *
 * What the 68040 does *not* share is which operations execute -- that is
 * Table 9-10 and lives in `ap_m68040_fpu.c`. The programming model is common;
 * the instruction set is not.
 */

#include "cpu/m68040/ap_m68040_fpu.h"
#include "cpu/m68882/ap_m68882_regs.h"
#include "cpu/m68882/ap_m68882_round.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_the_four_rounding_modes_match_table_9_1(void) {
  /* Table 9-1: "To Nearest (RN) 0 0; Toward Zero (RZ) 0 1; Toward Minus
   * Infinity (RM) 1 0", and toward plus infinity at 1 1. The same encodings the
   * 68882 uses, in the same order. */
  TEST_ASSERT_EQUAL_INT(0, AP_M68882_ROUND_NEAREST);
  TEST_ASSERT_EQUAL_INT(1, AP_M68882_ROUND_ZERO);
  TEST_ASSERT_EQUAL_INT(2, AP_M68882_ROUND_MINUS_INFINITY);
  TEST_ASSERT_EQUAL_INT(3, AP_M68882_ROUND_PLUS_INFINITY);
}

static void test_the_rounding_precisions_match_table_9_1(void) {
  /* Table 9-1's precision column: "Extend (X); Single (S); Double (D)", and a
   * fourth encoding the table leaves undefined. */
  TEST_ASSERT_EQUAL_INT(0, AP_M68882_PRECISION_EXTENDED);
  TEST_ASSERT_EQUAL_INT(1, AP_M68882_PRECISION_SINGLE);
  TEST_ASSERT_EQUAL_INT(2, AP_M68882_PRECISION_DOUBLE);
  TEST_ASSERT_EQUAL_INT(3, AP_M68882_PRECISION_RESERVED);
}

static void test_the_rounding_boundaries(void) {
  /* §9.2.2.2: "Single-precision results are rounded to a 24-bit boundary;
   * double-precision results are rounded to a 53-bit boundary; and
   * extended-precision results are rounded to a 64-bit boundary." The 68040
   * manual's three widths, checked against the 68882 rounding stage this core
   * already has -- which is the whole claim this suite is making. */
  TEST_ASSERT_EQUAL_UINT(
      24u, ap_m68882_precision_bits(AP_M68882_PRECISION_SINGLE));
  TEST_ASSERT_EQUAL_UINT(
      53u, ap_m68882_precision_bits(AP_M68882_PRECISION_DOUBLE));
  TEST_ASSERT_EQUAL_UINT(
      64u, ap_m68882_precision_bits(AP_M68882_PRECISION_EXTENDED));
}

static void test_the_exception_bits_sit_where_the_68040_draws_them(void) {
  /* §9.2.3.3's Figure 9-5 spans bits 15-8, and its labels match the 68882's
   * byte exactly: branch/set on unordered at the top, signalling NAN below it,
   * and the two inexact bits at the bottom -- "inexact decimal input" at 8 and
   * "inexact operation" at 9. */
  TEST_ASSERT_EQUAL_UINT(15u, AP_M68882_EXC_BSUN);
  TEST_ASSERT_EQUAL_UINT(14u, AP_M68882_EXC_SNAN);
  TEST_ASSERT_EQUAL_UINT(13u, AP_M68882_EXC_OPERR);
  TEST_ASSERT_EQUAL_UINT(12u, AP_M68882_EXC_OVFL);
  TEST_ASSERT_EQUAL_UINT(11u, AP_M68882_EXC_UNFL);
  TEST_ASSERT_EQUAL_UINT(10u, AP_M68882_EXC_DZ);
  TEST_ASSERT_EQUAL_UINT(9u, AP_M68882_EXC_INEX2);
  TEST_ASSERT_EQUAL_UINT(8u, AP_M68882_EXC_INEX1);
}

static void test_reset_gives_the_ieee_defaults(void) {
  /* §9.2.2: "The reset function or a restore operation of the null state clears
   * the FPCR. When cleared, this register provides the IEEE 754 standard
   * defaults." A cleared mode byte is round-to-nearest at extended precision,
   * which is what the standard asks for -- so the 68882's reset satisfies the
   * 68040's statement without a second implementation. */
  ap_m68882_regs_t regs;
  ap_m68882_regs_reset(&regs);
  TEST_ASSERT_EQUAL_INT(AP_M68882_ROUND_NEAREST,
                        ap_m68882_rounding_mode(&regs));
  TEST_ASSERT_EQUAL_INT(AP_M68882_PRECISION_EXTENDED,
                        ap_m68882_rounding_precision(&regs));
}

static void test_reset_disables_every_trap(void) {
  /* The same sentence clears the ENABLE byte, so nothing traps after reset --
   * which is what lets a machine run floating-point code before it has
   * installed a handler. */
  ap_m68882_regs_t regs;
  ap_m68882_regs_reset(&regs);
  const unsigned classes[] = {AP_M68882_EXC_BSUN,  AP_M68882_EXC_SNAN,
                              AP_M68882_EXC_OPERR, AP_M68882_EXC_OVFL,
                              AP_M68882_EXC_UNFL,  AP_M68882_EXC_DZ,
                              AP_M68882_EXC_INEX2, AP_M68882_EXC_INEX1};
  for (unsigned i = 0; i < sizeof classes / sizeof classes[0]; i++) {
    TEST_ASSERT_FALSE(ap_m68882_exception_enabled(&regs, classes[i]));
  }
}

static void test_the_shared_model_does_not_imply_a_shared_instruction_set(void) {
  /* The distinction this suite exists to keep straight. `FSIN` uses the same
   * registers, the same rounding mode and the same exception bits on both
   * parts -- and executes on one and traps on the other. */
  TEST_ASSERT_TRUE(ap_m68040_fpu_is_unimplemented(AP_M68882_OP_FSIN));
  TEST_ASSERT_FALSE(ap_m68040_fpu_is_unimplemented(AP_M68882_OP_FADD));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_four_rounding_modes_match_table_9_1);
  RUN_TEST(test_the_rounding_precisions_match_table_9_1);
  RUN_TEST(test_the_rounding_boundaries);
  RUN_TEST(test_the_exception_bits_sit_where_the_68040_draws_them);
  RUN_TEST(test_reset_gives_the_ieee_defaults);
  RUN_TEST(test_reset_disables_every_trap);
  RUN_TEST(test_the_shared_model_does_not_imply_a_shared_instruction_set);
  return UNITY_END();
}

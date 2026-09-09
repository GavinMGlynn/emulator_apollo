/* MC68040 floating-point exceptions and data types, `[040]` §9.6-§9.8.
 *
 * §9.2's programming model is not tested here: it "is identical to the
 * programming model for the MC68881/MC68882", and `m68882_regs_suite` already
 * covers every bit of it. These are the parts of §9 that are *not* the 68882,
 * and each of them is somewhere a model inherited from the 68882 goes wrong.
 */

#include "cpu/m68040/ap_m68040_fp_exception.h"
#include "cpu/m68882/ap_m68882_regs.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* ---------------------------------------------------------------------------
 * Data formats and types, Table 9-2.
 * ------------------------------------------------------------------------- */

static void test_no_denormal_is_handled_in_hardware(void) {
  /* Every denormalized cell of Table 9-2 is marked software, in single, double
   * and extended alike. The 68882 this core models handles denormals in
   * hardware, so a 68040 FPU that inherited that behaviour would compute where
   * the part traps. */
  TEST_ASSERT_EQUAL_INT(
      AP_M68040_FP_SOFTWARE,
      ap_m68040_fp_support(AP_M68040_FP_SINGLE, AP_M68040_FP_DENORMALIZED));
  TEST_ASSERT_EQUAL_INT(
      AP_M68040_FP_SOFTWARE,
      ap_m68040_fp_support(AP_M68040_FP_DOUBLE, AP_M68040_FP_DENORMALIZED));
  TEST_ASSERT_EQUAL_INT(
      AP_M68040_FP_SOFTWARE,
      ap_m68040_fp_support(AP_M68040_FP_EXTENDED, AP_M68040_FP_DENORMALIZED));
}

static void test_packed_decimal_is_entirely_software(void) {
  /* The whole packed column is marked software -- there is no number type the
   * hardware will take in that format. */
  for (unsigned t = 0; t < (unsigned)AP_M68040_FP_TYPE_COUNT; t++) {
    TEST_ASSERT_EQUAL_INT(
        AP_M68040_FP_SOFTWARE,
        ap_m68040_fp_support(AP_M68040_FP_PACKED, (ap_m68040_fp_type_t)t));
  }
}

static void test_the_hardware_handles_normalized_zero_infinity_and_nan(void) {
  /* The three binary real formats take all four of those in hardware. */
  const ap_m68040_fp_format_t reals[] = {
      AP_M68040_FP_SINGLE, AP_M68040_FP_DOUBLE, AP_M68040_FP_EXTENDED};
  const ap_m68040_fp_type_t types[] = {AP_M68040_FP_NORMALIZED,
                                       AP_M68040_FP_ZERO,
                                       AP_M68040_FP_INFINITY, AP_M68040_FP_NAN};
  for (unsigned f = 0; f < 3u; f++) {
    for (unsigned t = 0; t < 4u; t++) {
      TEST_ASSERT_EQUAL_INT(AP_M68040_FP_HARDWARE,
                            ap_m68040_fp_support(reals[f], types[t]));
    }
  }
}

static void test_an_integer_has_no_infinity_or_nan(void) {
  /* Blank cells in Table 9-2 -- the combination does not exist, which is a
   * different answer from "the FPSP handles it". */
  const ap_m68040_fp_format_t integers[] = {AP_M68040_FP_BYTE_INTEGER,
                                            AP_M68040_FP_WORD_INTEGER,
                                            AP_M68040_FP_LONG_INTEGER};
  for (unsigned f = 0; f < 3u; f++) {
    TEST_ASSERT_EQUAL_INT(AP_M68040_FP_HARDWARE,
                          ap_m68040_fp_support(integers[f],
                                               AP_M68040_FP_NORMALIZED));
    TEST_ASSERT_EQUAL_INT(
        AP_M68040_FP_UNSUPPORTED,
        ap_m68040_fp_support(integers[f], AP_M68040_FP_INFINITY));
    TEST_ASSERT_EQUAL_INT(AP_M68040_FP_UNSUPPORTED,
                          ap_m68040_fp_support(integers[f], AP_M68040_FP_NAN));
    /* And no integer format is ever an unsupported *operand*: a blank cell
     * cannot arise, so it is not an exception. */
    TEST_ASSERT_FALSE(ap_m68040_fp_operand_unsupported(
        integers[f], AP_M68040_FP_DENORMALIZED));
  }
}

static void test_an_unnormalized_number_exists_in_two_formats(void) {
  /* Only extended and packed have room for one; the other five cells are
   * blank. */
  TEST_ASSERT_EQUAL_INT(
      AP_M68040_FP_SOFTWARE,
      ap_m68040_fp_support(AP_M68040_FP_EXTENDED, AP_M68040_FP_UNNORMALIZED));
  TEST_ASSERT_EQUAL_INT(
      AP_M68040_FP_SOFTWARE,
      ap_m68040_fp_support(AP_M68040_FP_PACKED, AP_M68040_FP_UNNORMALIZED));
  TEST_ASSERT_EQUAL_INT(
      AP_M68040_FP_UNSUPPORTED,
      ap_m68040_fp_support(AP_M68040_FP_SINGLE, AP_M68040_FP_UNNORMALIZED));
  TEST_ASSERT_EQUAL_INT(
      AP_M68040_FP_UNSUPPORTED,
      ap_m68040_fp_support(AP_M68040_FP_DOUBLE, AP_M68040_FP_UNNORMALIZED));
}

static void test_the_unsupported_operand_rule_matches_section_nine_six_two(void) {
  /* "Denormalized (for single-, double-, and extended-precision operands),
   * unnormalized (for extended-precision operands), or either the source or
   * destination data format is packed decimal real." Counting the cells the
   * predicate accepts should give exactly those. */
  unsigned unsupported = 0;
  for (unsigned f = 0; f < (unsigned)AP_M68040_FP_FORMAT_COUNT; f++) {
    for (unsigned t = 0; t < (unsigned)AP_M68040_FP_TYPE_COUNT; t++) {
      if (ap_m68040_fp_operand_unsupported((ap_m68040_fp_format_t)f,
                                           (ap_m68040_fp_type_t)t)) {
        unsupported++;
      }
    }
  }
  /* Three denormals, one unnormalized extended, and all six packed types. */
  TEST_ASSERT_EQUAL_UINT(3u + 1u + 6u, unsupported);
}

/* ---------------------------------------------------------------------------
 * Arithmetic exceptions, §9.7 and Table 9-9.
 * ------------------------------------------------------------------------- */

static void test_four_exceptions_are_nonmaskable(void) {
  /* "The processor encounters a nonmaskable SNAN, OPERR, OVFL, and UNFL
   * condition ... This allows a supervisor exception handler to correct a
   * defaulting result generated by the MC68040 that is different from the
   * result generated by an MC68881/MC68882 executing the same code." The 68882
   * has no such concept, so a model inheriting its masking drops four trap
   * classes silently. */
  unsigned nonmaskable = 0;
  for (unsigned e = 0; e < (unsigned)AP_M68040_FPEXC_COUNT; e++) {
    if (ap_m68040_fp_has_nonmaskable((ap_m68040_fp_exception_t)e)) {
      nonmaskable++;
    }
  }
  TEST_ASSERT_EQUAL_UINT(4u, nonmaskable);
  TEST_ASSERT_TRUE(ap_m68040_fp_has_nonmaskable(AP_M68040_FPEXC_SNAN));
  TEST_ASSERT_TRUE(ap_m68040_fp_has_nonmaskable(AP_M68040_FPEXC_OPERR));
  TEST_ASSERT_TRUE(ap_m68040_fp_has_nonmaskable(AP_M68040_FPEXC_OVFL));
  TEST_ASSERT_TRUE(ap_m68040_fp_has_nonmaskable(AP_M68040_FPEXC_UNFL));
}

static void test_overflow_and_underflow_are_nonmaskable_only(void) {
  /* §9.7.4.1 and §9.7.5.1 both read "MASKABLE EXCEPTION CONDITIONS. There are
   * no conditions." Clearing their FPCR enable bits does not stop them. */
  TEST_ASSERT_FALSE(ap_m68040_fp_has_maskable(AP_M68040_FPEXC_OVFL));
  TEST_ASSERT_FALSE(ap_m68040_fp_has_maskable(AP_M68040_FPEXC_UNFL));
  TEST_ASSERT_TRUE(ap_m68040_fp_has_nonmaskable(AP_M68040_FPEXC_OVFL));
  TEST_ASSERT_TRUE(ap_m68040_fp_has_nonmaskable(AP_M68040_FPEXC_UNFL));
}

static void test_branch_on_unordered_is_the_mirror_image(void) {
  /* §9.7.1.2: "NONMASKABLE EXCEPTION CONDITIONS. There are no conditions." */
  TEST_ASSERT_TRUE(ap_m68040_fp_has_maskable(AP_M68040_FPEXC_BSUN));
  TEST_ASSERT_FALSE(ap_m68040_fp_has_nonmaskable(AP_M68040_FPEXC_BSUN));
}

static void test_the_eighth_exception_is_never_raised_by_hardware(void) {
  /* "The MC68040 generates the first seven exceptions in hardware and the
   * eighth only in software", because "the processor never sets INEX1 bit in
   * the FPSR EXC byte, but provides it as a latch". */
  for (unsigned e = 0; e < (unsigned)AP_M68040_FPEXC_INEX1; e++) {
    TEST_ASSERT_TRUE(
        ap_m68040_fp_generated_in_hardware((ap_m68040_fp_exception_t)e));
  }
  TEST_ASSERT_FALSE(ap_m68040_fp_generated_in_hardware(AP_M68040_FPEXC_INEX1));
}

static void test_priority_order_is_the_enable_byte_read_downwards(void) {
  /* "The bits of the ENABLE byte are organized in decreasing priority, with bit
   * 15 being the highest and bit 8 the lowest", and the priority list of §9.7
   * is BSUN, SNAN, OPERR, OVFL, UNFL, DZ, INEX2, INEX1. So the enable bits
   * count down from 15 -- and they are the 68882's bit positions exactly, which
   * is the one thing about the exceptions that did *not* change. */
  TEST_ASSERT_EQUAL_UINT(15u, ap_m68040_fp_enable_bit(AP_M68040_FPEXC_BSUN));
  TEST_ASSERT_EQUAL_UINT(8u, ap_m68040_fp_enable_bit(AP_M68040_FPEXC_INEX1));
  TEST_ASSERT_EQUAL_UINT((unsigned)AP_M68882_EXC_BSUN,
                         ap_m68040_fp_enable_bit(AP_M68040_FPEXC_BSUN));
  TEST_ASSERT_EQUAL_UINT((unsigned)AP_M68882_EXC_SNAN,
                         ap_m68040_fp_enable_bit(AP_M68040_FPEXC_SNAN));
  TEST_ASSERT_EQUAL_UINT((unsigned)AP_M68882_EXC_OPERR,
                         ap_m68040_fp_enable_bit(AP_M68040_FPEXC_OPERR));
  TEST_ASSERT_EQUAL_UINT((unsigned)AP_M68882_EXC_INEX1,
                         ap_m68040_fp_enable_bit(AP_M68040_FPEXC_INEX1));
}

static void test_the_two_inexact_classes_share_one_vector(void) {
  /* §9.7.7: "only one inexact exception vector number is generated by the
   * processor", so a handler on vector 49 must distinguish them itself. */
  TEST_ASSERT_EQUAL_INT(AP_M68040_FPVEC_INEX,
                        ap_m68040_fp_exception_vector(AP_M68040_FPEXC_INEX2));
  TEST_ASSERT_EQUAL_INT(AP_M68040_FPVEC_INEX,
                        ap_m68040_fp_exception_vector(AP_M68040_FPEXC_INEX1));
  TEST_ASSERT_EQUAL_UINT(49u, (unsigned)AP_M68040_FPVEC_INEX);
}

static void test_the_exception_vectors_of_table_nine_nine(void) {
  TEST_ASSERT_EQUAL_UINT(48u, (unsigned)AP_M68040_FPVEC_BSUN);
  TEST_ASSERT_EQUAL_UINT(50u, (unsigned)AP_M68040_FPVEC_DZ);
  TEST_ASSERT_EQUAL_UINT(51u, (unsigned)AP_M68040_FPVEC_UNFL);
  TEST_ASSERT_EQUAL_UINT(52u, (unsigned)AP_M68040_FPVEC_OPERR);
  TEST_ASSERT_EQUAL_UINT(53u, (unsigned)AP_M68040_FPVEC_OVFL);
  TEST_ASSERT_EQUAL_UINT(54u, (unsigned)AP_M68040_FPVEC_SNAN);
  TEST_ASSERT_EQUAL_UINT(55u,
                         (unsigned)AP_M68040_FPVEC_UNIMPLEMENTED_DATA_TYPE);
  /* And the unimplemented *instruction* shares vector 11 with the F-line
   * illegal instruction, which is why §9.6.1 says the handler "uses the stack
   * frame format ($0 or $2) to distinguish between the two". */
  TEST_ASSERT_EQUAL_UINT(
      11u, (unsigned)AP_M68040_FPVEC_UNIMPLEMENTED_INSTRUCTION);
}

/* ---------------------------------------------------------------------------
 * State frames, §9.8.
 * ------------------------------------------------------------------------- */

static void test_a_conditional_instruction_leaves_a_null_state_frame(void) {
  /* §9.8's stated divergence: conditionals "do not set an internal flag, which
   * changes the state frame from null to idle ... Note that this function is
   * different from that of the MC68881 and MC68882." Reset, execute an FBcc,
   * FSAVE -- null here, idle there. */
  TEST_ASSERT_EQUAL_INT(AP_M68040_FPFRAME_NULL,
                        ap_m68040_fp_save_frame(false, false, false));
  TEST_ASSERT_EQUAL_INT(AP_M68040_FPFRAME_IDLE,
                        ap_m68040_fp_save_frame(true, false, false));
}

static void test_the_five_conditional_instructions(void) {
  /* "Conditionals include FNOP, FBcc, FDBcc, FScc, and FTRAPcc." FNOP being
   * among them is the surprise: it looks like an operation and is not one. */
  TEST_ASSERT_TRUE(ap_m68040_fp_is_conditional("FNOP"));
  TEST_ASSERT_TRUE(ap_m68040_fp_is_conditional("FBcc"));
  TEST_ASSERT_TRUE(ap_m68040_fp_is_conditional("FDBcc"));
  TEST_ASSERT_TRUE(ap_m68040_fp_is_conditional("FScc"));
  TEST_ASSERT_TRUE(ap_m68040_fp_is_conditional("FTRAPcc"));
  TEST_ASSERT_FALSE(ap_m68040_fp_is_conditional("FADD"));
  TEST_ASSERT_FALSE(ap_m68040_fp_is_conditional("FMOVE"));
  TEST_ASSERT_FALSE(ap_m68040_fp_is_conditional(NULL));
}

static void test_the_two_exception_frames_and_their_sizes(void) {
  /* "When an unimplemented floating-point exception occurs, the FSAVE generates
   * a 26-word unimplemented instruction state frame. When an unsupported data
   * type exception occurs, the FSAVE generates a 50-word busy state frame." */
  TEST_ASSERT_EQUAL_INT(AP_M68040_FPFRAME_UNIMPLEMENTED,
                        ap_m68040_fp_save_frame(true, true, false));
  TEST_ASSERT_EQUAL_INT(AP_M68040_FPFRAME_BUSY,
                        ap_m68040_fp_save_frame(true, false, true));
  TEST_ASSERT_EQUAL_UINT(26u, AP_M68040_FPFRAME_UNIMPLEMENTED_WORDS);
  TEST_ASSERT_EQUAL_UINT(50u, AP_M68040_FPFRAME_BUSY_WORDS);
  /* An unimplemented instruction outranks an unsupported data type, since the
   * instruction never got as far as looking at its operand. */
  TEST_ASSERT_EQUAL_INT(AP_M68040_FPFRAME_UNIMPLEMENTED,
                        ap_m68040_fp_save_frame(true, true, true));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_no_denormal_is_handled_in_hardware);
  RUN_TEST(test_packed_decimal_is_entirely_software);
  RUN_TEST(test_the_hardware_handles_normalized_zero_infinity_and_nan);
  RUN_TEST(test_an_integer_has_no_infinity_or_nan);
  RUN_TEST(test_an_unnormalized_number_exists_in_two_formats);
  RUN_TEST(test_the_unsupported_operand_rule_matches_section_nine_six_two);
  RUN_TEST(test_four_exceptions_are_nonmaskable);
  RUN_TEST(test_overflow_and_underflow_are_nonmaskable_only);
  RUN_TEST(test_branch_on_unordered_is_the_mirror_image);
  RUN_TEST(test_the_eighth_exception_is_never_raised_by_hardware);
  RUN_TEST(test_priority_order_is_the_enable_byte_read_downwards);
  RUN_TEST(test_the_two_inexact_classes_share_one_vector);
  RUN_TEST(test_the_exception_vectors_of_table_nine_nine);
  RUN_TEST(test_a_conditional_instruction_leaves_a_null_state_frame);
  RUN_TEST(test_the_five_conditional_instructions);
  RUN_TEST(test_the_two_exception_frames_and_their_sizes);
  return UNITY_END();
}

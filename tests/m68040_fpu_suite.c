/* MC68040 floating-point unit: the subset it implements, `[68040]` §9.6.1,
 * Table 9-10 and Appendix E's Table E-2.
 *
 * The interesting property of this FPU is what it refuses, so most of these
 * tests are about instructions that do *not* execute.
 */

#include "cpu/m68040/ap_m68040_fpu.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Build a general-type command word for a register-to-register operation with
 * the given extension, which is how an operation is named. */
static uint16_t command_for(ap_m68882_operation_t op) {
  return (uint16_t)((unsigned)op & 0x7Fu);
}

/* ---------------------------------------------------------------------------
 * Table 9-10.
 * ------------------------------------------------------------------------- */

static void test_every_transcendental_is_unimplemented(void) {
  /* The whole of Table 9-10's transcendental content. None of these executes
   * in silicon; each traps to the M68040FPSP. */
  const ap_m68882_operation_t transcendentals[] = {
      AP_M68882_OP_FACOS,   AP_M68882_OP_FASIN,   AP_M68882_OP_FATAN,
      AP_M68882_OP_FATANH,  AP_M68882_OP_FCOS,    AP_M68882_OP_FCOSH,
      AP_M68882_OP_FETOX,   AP_M68882_OP_FETOXM1, AP_M68882_OP_FLOG10,
      AP_M68882_OP_FLOG2,   AP_M68882_OP_FLOGN,   AP_M68882_OP_FLOGNP1,
      AP_M68882_OP_FSIN,    AP_M68882_OP_FSINCOS, AP_M68882_OP_FSINH,
      AP_M68882_OP_FTAN,    AP_M68882_OP_FTANH,   AP_M68882_OP_FTENTOX,
      AP_M68882_OP_FTWOTOX};
  for (unsigned i = 0; i < sizeof transcendentals / sizeof transcendentals[0];
       i++) {
    TEST_ASSERT_TRUE(ap_m68040_fpu_is_unimplemented(transcendentals[i]));
  }
}

static void test_flog2_is_unimplemented_despite_table_9_10(void) {
  /* Table 9-10 lists FLOG10, FLOGN and FLOGNP1 and omits FLOG2 -- confirmed in
   * the page image, so not an extraction artefact. Appendix E's Table E-2
   * lists FLOG2 among the instructions the M68040FPSP *provides*, and without
   * the asterisk that marks instructions the hardware implements except for
   * special data types. So the omission is a defect in Table 9-10.
   *
   * The engineering argument agrees: log base 10 and natural log are log base
   * 2 times a constant, so hardware holding log2 would get them nearly free.
   * A part that trapped those two while computing log2 would be a strange
   * machine indeed. */
  TEST_ASSERT_TRUE(ap_m68040_fpu_is_unimplemented(AP_M68882_OP_FLOG2));
  TEST_ASSERT_TRUE(ap_m68040_fpu_is_unimplemented(AP_M68882_OP_FLOG10));
  TEST_ASSERT_TRUE(ap_m68040_fpu_is_unimplemented(AP_M68882_OP_FLOGN));
  TEST_ASSERT_TRUE(ap_m68040_fpu_is_unimplemented(AP_M68882_OP_FLOGNP1));
}

static void test_the_exactly_specified_operations_are_also_unimplemented(void) {
  /* The surprise in Table 9-10: these are not transcendentals. Each has one
   * right answer, and this core computes each bit-exactly for the 68882 --
   * Motorola still moved them to software on the 68040. */
  TEST_ASSERT_TRUE(ap_m68040_fpu_is_unimplemented(AP_M68882_OP_FINT));
  TEST_ASSERT_TRUE(ap_m68040_fpu_is_unimplemented(AP_M68882_OP_FINTRZ));
  TEST_ASSERT_TRUE(ap_m68040_fpu_is_unimplemented(AP_M68882_OP_FGETEXP));
  TEST_ASSERT_TRUE(ap_m68040_fpu_is_unimplemented(AP_M68882_OP_FGETMAN));
  TEST_ASSERT_TRUE(ap_m68040_fpu_is_unimplemented(AP_M68882_OP_FSCALE));
  TEST_ASSERT_TRUE(ap_m68040_fpu_is_unimplemented(AP_M68882_OP_FMOD));
  TEST_ASSERT_TRUE(ap_m68040_fpu_is_unimplemented(AP_M68882_OP_FREM));
}

static void test_the_four_arithmetic_operations_stay_in_hardware(void) {
  TEST_ASSERT_FALSE(ap_m68040_fpu_is_unimplemented(AP_M68882_OP_FADD));
  TEST_ASSERT_FALSE(ap_m68040_fpu_is_unimplemented(AP_M68882_OP_FSUB));
  TEST_ASSERT_FALSE(ap_m68040_fpu_is_unimplemented(AP_M68882_OP_FMUL));
  TEST_ASSERT_FALSE(ap_m68040_fpu_is_unimplemented(AP_M68882_OP_FDIV));
  TEST_ASSERT_FALSE(ap_m68040_fpu_is_unimplemented(AP_M68882_OP_FCMP));
  TEST_ASSERT_FALSE(ap_m68040_fpu_is_unimplemented(AP_M68882_OP_FTST));
}

static void test_square_root_survives_where_the_extractions_did_not(void) {
  /* `FSQRT` is the notable survivor: IEEE specifies it exactly, like `FGETEXP`
   * and `FINT`, and unlike them it stayed in silicon. So "exactly specified"
   * does not predict which side of the line an operation falls -- only the
   * table does, which is why the table is transcribed rather than reasoned
   * about. */
  TEST_ASSERT_FALSE(ap_m68040_fpu_is_unimplemented(AP_M68882_OP_FSQRT));
  TEST_ASSERT_TRUE(ap_m68040_fpu_is_unimplemented(AP_M68882_OP_FGETEXP));
}

static void test_the_moves_and_sign_operations_stay_in_hardware(void) {
  TEST_ASSERT_FALSE(ap_m68040_fpu_is_unimplemented(AP_M68882_OP_FMOVE_TO_FPN));
  TEST_ASSERT_FALSE(ap_m68040_fpu_is_unimplemented(AP_M68882_OP_FABS));
  TEST_ASSERT_FALSE(ap_m68040_fpu_is_unimplemented(AP_M68882_OP_FNEG));
  TEST_ASSERT_FALSE(ap_m68040_fpu_is_unimplemented(AP_M68882_OP_FSGLMUL));
  TEST_ASSERT_FALSE(ap_m68040_fpu_is_unimplemented(AP_M68882_OP_FSGLDIV));
}

/* ---------------------------------------------------------------------------
 * Classification, and the shared vector.
 * ------------------------------------------------------------------------- */

static void test_a_defined_hardware_operation_executes(void) {
  TEST_ASSERT_EQUAL_INT(AP_M68040_FPU_IMPLEMENTED,
                        ap_m68040_fpu_classify(command_for(AP_M68882_OP_FADD)));
}

static void test_a_defined_software_operation_takes_the_unimplemented_path(void) {
  TEST_ASSERT_EQUAL_INT(
      AP_M68040_FPU_UNIMPLEMENTED_INSTRUCTION,
      ap_m68040_fpu_classify(command_for(AP_M68882_OP_FSIN)));
}

static void test_an_unrecognised_pattern_is_f_line_illegal(void) {
  /* "If the processor encounters an F-line instruction and the instruction
   * patterns do not match either of the above two cases, the processor takes an
   * F-line illegal exception." Extensions `$40-$7F` are that case. */
  TEST_ASSERT_EQUAL_INT(AP_M68040_FPU_F_LINE_ILLEGAL,
                        ap_m68040_fpu_classify(0x0040u));
  TEST_ASSERT_EQUAL_INT(AP_M68040_FPU_F_LINE_ILLEGAL,
                        ap_m68040_fpu_classify(0x007Fu));
}

static void test_the_two_exceptions_share_a_vector_and_differ_by_frame(void) {
  /* "Since the unimplemented floating-point exception and the F-line illegal
   * instruction share the same vector, the exception handler uses the stack
   * frame format ($0 or $2) to distinguish between the two."
   *
   * So the frame format is the *only* discriminator. A model that pushed the
   * wrong one would send a legal `FSIN` to the illegal-instruction handler,
   * and the operating system would kill a process that should have had its
   * sine computed in software. */
  TEST_ASSERT_EQUAL_UINT(11u, AP_M68040_FPU_VECTOR);
  TEST_ASSERT_EQUAL_UINT(
      0u, ap_m68040_fpu_frame_format(AP_M68040_FPU_F_LINE_ILLEGAL));
  TEST_ASSERT_EQUAL_UINT(
      2u, ap_m68040_fpu_frame_format(AP_M68040_FPU_UNIMPLEMENTED_INSTRUCTION));
  TEST_ASSERT_NOT_EQUAL_UINT(
      ap_m68040_fpu_frame_format(AP_M68040_FPU_F_LINE_ILLEGAL),
      ap_m68040_fpu_frame_format(AP_M68040_FPU_UNIMPLEMENTED_INSTRUCTION));
}

static void test_an_executed_instruction_pushes_no_frame(void) {
  TEST_ASSERT_EQUAL_UINT(0u,
                         ap_m68040_fpu_frame_format(AP_M68040_FPU_IMPLEMENTED));
}

static void test_every_defined_operation_classifies_one_way_or_the_other(void) {
  /* Sweeping the whole extension space: every encoding lands in exactly one of
   * the three outcomes, and the defined ones never fall to F-line illegal --
   * which is the property that would break if a case were dropped from the
   * unimplemented switch. */
  unsigned implemented = 0, unimplemented = 0, illegal = 0;
  for (unsigned ext = 0; ext < 128u; ext++) {
    switch (ap_m68040_fpu_classify((uint16_t)ext)) {
    case AP_M68040_FPU_IMPLEMENTED:
      implemented++;
      break;
    case AP_M68040_FPU_UNIMPLEMENTED_INSTRUCTION:
      unimplemented++;
      break;
    case AP_M68040_FPU_F_LINE_ILLEGAL:
      illegal++;
      break;
    }
  }
  TEST_ASSERT_EQUAL_UINT(128u, implemented + unimplemented + illegal);
  /* `$40-$7F` is exactly sixty-four encodings, and all of them are illegal. */
  TEST_ASSERT_EQUAL_UINT(64u, illegal);
  /* And the software-emulated set is large enough to matter. */
  TEST_ASSERT_TRUE(unimplemented >= 25u);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_every_transcendental_is_unimplemented);
  RUN_TEST(test_flog2_is_unimplemented_despite_table_9_10);
  RUN_TEST(test_the_exactly_specified_operations_are_also_unimplemented);
  RUN_TEST(test_the_four_arithmetic_operations_stay_in_hardware);
  RUN_TEST(test_square_root_survives_where_the_extractions_did_not);
  RUN_TEST(test_the_moves_and_sign_operations_stay_in_hardware);
  RUN_TEST(test_a_defined_hardware_operation_executes);
  RUN_TEST(test_a_defined_software_operation_takes_the_unimplemented_path);
  RUN_TEST(test_an_unrecognised_pattern_is_f_line_illegal);
  RUN_TEST(test_the_two_exceptions_share_a_vector_and_differ_by_frame);
  RUN_TEST(test_an_executed_instruction_pushes_no_frame);
  RUN_TEST(test_every_defined_operation_classifies_one_way_or_the_other);
  return UNITY_END();
}

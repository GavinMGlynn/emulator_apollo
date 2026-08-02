/* MC68851 coprocessor interface, `[68851]` Table 9-2, Table 9-3 and Table 9-6.
 *
 * The 68851 and the 68882 share this interface at cpID 0 and 1, so several
 * tests here are really about the *difference* between the two parts' subsets.
 */

#include "cpu/m68851/ap_m68851_cir.h"
#include "cpu/m68882/ap_m68882_cir.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* ---------------------------------------------------------------------------
 * The register map, Table 9-2.
 * ------------------------------------------------------------------------- */

static void test_the_register_offsets(void) {
  /* Selects are A4-A0, so the offset is the select: `$00` is select 0, `$1C`
   * is select 28. */
  const struct { unsigned offset; ap_m68851_cir_t cir; } map[] = {
      {0x00u, AP_M68851_CIR_RESPONSE},
      {0x02u, AP_M68851_CIR_CONTROL},
      {0x04u, AP_M68851_CIR_SAVE},
      {0x06u, AP_M68851_CIR_RESTORE},
      {0x08u, AP_M68851_CIR_OPERATION_WORD},
      {0x0Au, AP_M68851_CIR_COMMAND},
      {0x0Cu, AP_M68851_CIR_RESERVED},
      {0x0Eu, AP_M68851_CIR_CONDITION},
      {0x10u, AP_M68851_CIR_OPERAND},
      {0x14u, AP_M68851_CIR_REGISTER_SELECT},
      {0x16u, AP_M68851_CIR_RESERVED},
      {0x18u, AP_M68851_CIR_INSTRUCTION_ADDRESS},
      {0x1Cu, AP_M68851_CIR_OPERAND_ADDRESS},
  };
  for (unsigned i = 0; i < sizeof map / sizeof map[0]; i++) {
    TEST_ASSERT_EQUAL_INT(map[i].cir, ap_m68851_cir_decode(map[i].offset));
  }
}

static void test_the_select_field_has_dont_care_bits(void) {
  /* Table 9-2 writes `0000x` for the response CIR and `100xx` for the operand
   * CIR: a 16-bit register spans two byte addresses and a 32-bit one spans
   * four, so every address within a register selects it. */
  TEST_ASSERT_EQUAL_INT(AP_M68851_CIR_RESPONSE, ap_m68851_cir_decode(0x00u));
  TEST_ASSERT_EQUAL_INT(AP_M68851_CIR_RESPONSE, ap_m68851_cir_decode(0x01u));

  for (unsigned i = 0; i < 4u; i++) {
    TEST_ASSERT_EQUAL_INT(AP_M68851_CIR_OPERAND,
                          ap_m68851_cir_decode(0x10u + i));
    TEST_ASSERT_EQUAL_INT(AP_M68851_CIR_OPERAND_ADDRESS,
                          ap_m68851_cir_decode(0x1Cu + i));
    TEST_ASSERT_EQUAL_INT(AP_M68851_CIR_INSTRUCTION_ADDRESS,
                          ap_m68851_cir_decode(0x18u + i));
  }
}

static void test_every_select_value_decodes(void) {
  /* All 32 selects land somewhere; none is unhandled. */
  for (unsigned select = 0; select < 32u; select++) {
    const ap_m68851_cir_t cir = ap_m68851_cir_decode(select);
    TEST_ASSERT_TRUE(cir >= AP_M68851_CIR_RESPONSE &&
                     cir <= AP_M68851_CIR_RESERVED);
  }
}

static void test_the_32_bit_registers(void) {
  /* Table 9-2's width column: three registers are 32 bits and the rest 16. */
  TEST_ASSERT_EQUAL_UINT(32u, ap_m68851_cir_width(AP_M68851_CIR_OPERAND));
  TEST_ASSERT_EQUAL_UINT(
      32u, ap_m68851_cir_width(AP_M68851_CIR_INSTRUCTION_ADDRESS));
  TEST_ASSERT_EQUAL_UINT(32u,
                         ap_m68851_cir_width(AP_M68851_CIR_OPERAND_ADDRESS));
  TEST_ASSERT_EQUAL_UINT(16u, ap_m68851_cir_width(AP_M68851_CIR_RESPONSE));
  TEST_ASSERT_EQUAL_UINT(16u, ap_m68851_cir_width(AP_M68851_CIR_COMMAND));
}

static void test_the_read_and_write_attributes(void) {
  /* Table 9-2's type column. The response CIR is read-only and the control CIR
   * write-only, which is what makes the dialog a pair of one-way channels. */
  TEST_ASSERT_TRUE(ap_m68851_cir_readable(AP_M68851_CIR_RESPONSE));
  TEST_ASSERT_FALSE(ap_m68851_cir_writable(AP_M68851_CIR_RESPONSE));
  TEST_ASSERT_FALSE(ap_m68851_cir_readable(AP_M68851_CIR_CONTROL));
  TEST_ASSERT_TRUE(ap_m68851_cir_writable(AP_M68851_CIR_CONTROL));
  /* The restore and operand CIRs go both ways. */
  TEST_ASSERT_TRUE(ap_m68851_cir_readable(AP_M68851_CIR_RESTORE));
  TEST_ASSERT_TRUE(ap_m68851_cir_writable(AP_M68851_CIR_RESTORE));
  TEST_ASSERT_TRUE(ap_m68851_cir_readable(AP_M68851_CIR_OPERAND));
  TEST_ASSERT_TRUE(ap_m68851_cir_writable(AP_M68851_CIR_OPERAND));
}

static void test_the_operand_address_cir_is_write_only(void) {
  /* §9.1.2.11 calls it a "read/write register" in its first sentence and then
   * says "reads from this register are ignored and always return all ones".
   * Table 9-2's type column says `Write`, and the behaviour is what counts. */
  TEST_ASSERT_TRUE(ap_m68851_cir_writable(AP_M68851_CIR_OPERAND_ADDRESS));
  TEST_ASSERT_FALSE(ap_m68851_cir_readable(AP_M68851_CIR_OPERAND_ADDRESS));
}

/* ---------------------------------------------------------------------------
 * What each part implements: the reason this is not one shared table.
 * ------------------------------------------------------------------------- */

static void test_the_two_unimplemented_registers(void) {
  /* Table 9-2's asterisks. */
  TEST_ASSERT_FALSE(ap_m68851_cir_implemented(AP_M68851_CIR_OPERATION_WORD));
  TEST_ASSERT_FALSE(
      ap_m68851_cir_implemented(AP_M68851_CIR_INSTRUCTION_ADDRESS));
  TEST_ASSERT_TRUE(ap_m68851_cir_implemented(AP_M68851_CIR_OPERAND_ADDRESS));
  TEST_ASSERT_TRUE(ap_m68851_cir_implemented(AP_M68851_CIR_RESPONSE));
}

static void test_the_two_coprocessors_implement_complementary_registers(void) {
  /* The finding this module exists to record: the MMU and the FPU leave
   * *different* registers unimplemented, so one shared CIR table would be
   * wrong in both directions.
   *
   *   $18 instruction address: the FPU has it, the MMU does not -- "used to
   *   support concurrent processor/coprocessor instruction execution and is not
   *   implemented by the MC68851".
   *   $1C operand address: the MMU has it, the FPU does not -- PFLUSH, PLOAD,
   *   PTEST and PVALID evaluate an effective address, and no floating-point
   *   instruction does. */
  TEST_ASSERT_FALSE(
      ap_m68851_cir_implemented(AP_M68851_CIR_INSTRUCTION_ADDRESS));
  TEST_ASSERT_TRUE(
      ap_m68882_cir_implemented(AP_M68882_CIR_INSTRUCTION_ADDRESS));

  TEST_ASSERT_TRUE(ap_m68851_cir_implemented(AP_M68851_CIR_OPERAND_ADDRESS));
  TEST_ASSERT_FALSE(ap_m68882_cir_implemented(AP_M68882_CIR_OPERAND_ADDRESS));

  /* The one they agree on. */
  TEST_ASSERT_FALSE(ap_m68851_cir_implemented(AP_M68851_CIR_OPERATION_WORD));
  TEST_ASSERT_FALSE(ap_m68882_cir_implemented(AP_M68882_CIR_OPERATION_WORD));
}

static void test_only_an_implemented_register_can_violate_the_protocol(void) {
  /* The counter-intuitive part: the two *unimplemented* registers are
   * explicitly exempt -- "accessing this register will not cause a protocol
   * violation" -- while the implemented operand address CIR is the one that
   * faults on an out-of-protocol write. */
  TEST_ASSERT_FALSE(
      ap_m68851_cir_write_can_violate(AP_M68851_CIR_OPERATION_WORD));
  TEST_ASSERT_FALSE(
      ap_m68851_cir_write_can_violate(AP_M68851_CIR_INSTRUCTION_ADDRESS));
  TEST_ASSERT_TRUE(
      ap_m68851_cir_write_can_violate(AP_M68851_CIR_OPERAND_ADDRESS));
}

/* ---------------------------------------------------------------------------
 * Null primitives, Table 9-3.
 * ------------------------------------------------------------------------- */

static void test_the_idle_null_primitive(void) {
  /* CA=0 PC=0 IA=0 PF=1 TF=0: "the MC68851 is in the idle state or as the final
   * primitive of an instruction dialog. The PF bit indicates that no
   * instruction is being executed." */
  const ap_m68851_null_primitive_t idle = {.processing_finished = true};
  TEST_ASSERT_EQUAL_INT(AP_M68851_NULL_IDLE, ap_m68851_null_classify(idle));
}

static void test_the_come_again_null_primitive(void) {
  /* CA=1 PC=0 IA=0 PF=0 TF=0: "requires further service ... the expected
   * response is for the main processor to re-read the response CIR." */
  const ap_m68851_null_primitive_t again = {.come_again = true};
  TEST_ASSERT_EQUAL_INT(AP_M68851_NULL_COME_AGAIN,
                        ap_m68851_null_classify(again));
}

static void test_a_true_condition_sets_tf(void) {
  /* "TF = 1 if the condition is true, TF = 0 if the condition is false." */
  const ap_m68851_null_primitive_t true_result = {.processing_finished = true,
                                                  .true_false = true};
  TEST_ASSERT_EQUAL_INT(AP_M68851_NULL_CONDITION_RESULT,
                        ap_m68851_null_classify(true_result));
}

static void test_a_false_condition_shares_the_idle_encoding(void) {
  /* Table 9-3's first and third rows are the same bits when TF is clear. The
   * manual distinguishes them by *when* they are read -- after a write to the
   * condition CIR, or otherwise -- not by the encoding, so a classifier working
   * from bits alone cannot tell them apart and must not pretend to. */
  const ap_m68851_null_primitive_t false_result = {.processing_finished = true,
                                                   .true_false = false};
  TEST_ASSERT_EQUAL_INT(AP_M68851_NULL_IDLE,
                        ap_m68851_null_classify(false_result));
}

static void test_this_part_never_sets_the_pc_bit(void) {
  /* "Primitives returned by the MC68851 do not have the PC bit set" -- which is
   * the same fact as its not implementing the instruction address CIR. */
  const ap_m68851_null_primitive_t with_pc = {.processing_finished = true,
                                              .pass_pc = true};
  TEST_ASSERT_EQUAL_INT(AP_M68851_NULL_NOT_USED_BY_THIS_PART,
                        ap_m68851_null_classify(with_pc));
}

static void test_most_null_encodings_are_unused(void) {
  /* "There are 32 possible null primitive encodings of which the MC68851 uses
   * only three." Sweeping all 32 five-bit combinations, at most three distinct
   * usages appear and the rest are unused. */
  unsigned used = 0;
  for (unsigned bits = 0; bits < 32u; bits++) {
    const ap_m68851_null_primitive_t p = {
        .come_again = (bits & 0x10u) != 0u,
        .pass_pc = (bits & 0x08u) != 0u,
        .interrupts_allowed = (bits & 0x04u) != 0u,
        .processing_finished = (bits & 0x02u) != 0u,
        .true_false = (bits & 0x01u) != 0u,
    };
    if (ap_m68851_null_classify(p) != AP_M68851_NULL_NOT_USED_BY_THIS_PART) {
      used++;
    }
  }
  /* Three encodings, since the idle and false-condition rows coincide. */
  TEST_ASSERT_EQUAL_UINT(3u, used);
}

/* ---------------------------------------------------------------------------
 * Vector numbers, Table 9-6.
 * ------------------------------------------------------------------------- */

static void test_the_five_vector_numbers_and_their_timing(void) {
  /* The pre/post split decides which stack frame the CPU builds and where
   * execution resumes, so it is not a labelling detail. */
  const struct {
    unsigned vector;
    ap_m68851_exception_timing_t timing;
  } vectors[] = {
      {AP_M68851_VECTOR_F_LINE, AP_M68851_EXCEPTION_PRE_INSTRUCTION},
      {AP_M68851_VECTOR_PROTOCOL_VIOLATION,
       AP_M68851_EXCEPTION_PRE_INSTRUCTION},
      {AP_M68851_VECTOR_CONFIGURATION_ERROR,
       AP_M68851_EXCEPTION_POST_INSTRUCTION},
      {AP_M68851_VECTOR_ILLEGAL_OPERATION,
       AP_M68851_EXCEPTION_POST_INSTRUCTION},
      {AP_M68851_VECTOR_ACCESS_VIOLATION,
       AP_M68851_EXCEPTION_POST_INSTRUCTION},
  };
  for (unsigned i = 0; i < 5u; i++) {
    ap_m68851_exception_timing_t timing;
    TEST_ASSERT_TRUE(ap_m68851_vector_timing(vectors[i].vector, &timing));
    TEST_ASSERT_EQUAL_INT(vectors[i].timing, timing);
  }
}

static void test_the_transcribed_offsets_agree_with_the_vector_numbers(void) {
  /* Table 9-6 gives both columns, so they can check each other: a decimal
   * number and a hexadecimal offset that disagreed would mean one was misread.
   * $02C, $034, $0E0, $0E4, $0E8. */
  TEST_ASSERT_EQUAL_HEX32(0x02Cu,
                          ap_m68851_vector_offset(AP_M68851_VECTOR_F_LINE));
  TEST_ASSERT_EQUAL_HEX32(
      0x034u, ap_m68851_vector_offset(AP_M68851_VECTOR_PROTOCOL_VIOLATION));
  TEST_ASSERT_EQUAL_HEX32(
      0x0E0u, ap_m68851_vector_offset(AP_M68851_VECTOR_CONFIGURATION_ERROR));
  TEST_ASSERT_EQUAL_HEX32(
      0x0E4u, ap_m68851_vector_offset(AP_M68851_VECTOR_ILLEGAL_OPERATION));
  TEST_ASSERT_EQUAL_HEX32(
      0x0E8u, ap_m68851_vector_offset(AP_M68851_VECTOR_ACCESS_VIOLATION));
}

static void test_other_vectors_are_not_this_parts(void) {
  ap_m68851_exception_timing_t timing;
  TEST_ASSERT_FALSE(ap_m68851_vector_timing(0u, &timing));
  TEST_ASSERT_FALSE(ap_m68851_vector_timing(12u, &timing));
  TEST_ASSERT_FALSE(ap_m68851_vector_timing(55u, &timing));
  TEST_ASSERT_FALSE(ap_m68851_vector_timing(59u, &timing));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_register_offsets);
  RUN_TEST(test_the_select_field_has_dont_care_bits);
  RUN_TEST(test_every_select_value_decodes);
  RUN_TEST(test_the_32_bit_registers);
  RUN_TEST(test_the_read_and_write_attributes);
  RUN_TEST(test_the_operand_address_cir_is_write_only);
  RUN_TEST(test_the_two_unimplemented_registers);
  RUN_TEST(test_the_two_coprocessors_implement_complementary_registers);
  RUN_TEST(test_only_an_implemented_register_can_violate_the_protocol);
  RUN_TEST(test_the_idle_null_primitive);
  RUN_TEST(test_the_come_again_null_primitive);
  RUN_TEST(test_a_true_condition_sets_tf);
  RUN_TEST(test_a_false_condition_shares_the_idle_encoding);
  RUN_TEST(test_this_part_never_sets_the_pc_bit);
  RUN_TEST(test_most_null_encodings_are_unused);
  RUN_TEST(test_the_five_vector_numbers_and_their_timing);
  RUN_TEST(test_the_transcribed_offsets_agree_with_the_vector_numbers);
  RUN_TEST(test_other_vectors_are_not_this_parts);
  return UNITY_END();
}

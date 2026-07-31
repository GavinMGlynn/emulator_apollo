/* MC68030 family 1111: the coprocessor interface and the MMU instructions.
 *
 * Cited to the M68000 Family Programmer's Reference Manual 1992 §8.2 and to
 * MC68030 User's Manual 3ed §9.7.6 p. 9-64.
 *
 * The fact under test is that an unsupported coprocessor-zero instruction takes
 * a *different exception vector* depending on the privilege state it was
 * attempted from. Almost everywhere else the exception a word takes is a
 * property of the word alone.
 */

#include "cpu/m68030/ap_m68030_coproc.h"
#include "cpu/m68030/ap_m68030_exception.h"
#include "cpu/m68030/ap_m68030_opcode.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_the_family_is_the_coprocessor_family(void) {
  TEST_ASSERT_EQUAL_INT(AP_M68030_OP_LINE_F, ap_m68030_opcode_family(0xF000u));
  TEST_ASSERT_TRUE(ap_m68030_coproc_decode(0xF000u).valid);
  TEST_ASSERT_FALSE(ap_m68030_coproc_decode(0xE000u).valid);
}

/* Bits 11-9 are the coprocessor ID, and zero is the 68030's own MMU: "The MMU
 * instructions use the same opcodes and coprocessor identification (CpID) as
 * the corresponding instructions of the MC68851." */
static void test_coprocessor_zero_is_the_mmu(void) {
  TEST_ASSERT_TRUE(ap_m68030_coproc_decode(0xF000u).is_mmu);
  TEST_ASSERT_EQUAL_UINT(0, ap_m68030_coproc_decode(0xF000u).cpid);

  /* Coprocessor 1, where a 68882 sits, is not. */
  const ap_m68030_coproc_t fpu = ap_m68030_coproc_decode(0xF200u);
  TEST_ASSERT_EQUAL_UINT(1, fpu.cpid);
  TEST_ASSERT_FALSE(fpu.is_mmu);
}

/* Bits 8-6 select the coprocessor operation type. */
static void test_the_operation_types_decode_from_bits_eight_six(void) {
  const ap_m68030_coproc_type_t expected[6] = {
      AP_M68030_CP_GENERAL,     AP_M68030_CP_CONDITIONAL,
      AP_M68030_CP_BRANCH_WORD, AP_M68030_CP_BRANCH_LONG,
      AP_M68030_CP_SAVE,        AP_M68030_CP_RESTORE};
  for (unsigned type = 0; type < 6; type++) {
    const uint16_t word = (uint16_t)(0xF200u | (type << 6));
    TEST_ASSERT_EQUAL_INT(expected[type],
                          ap_m68030_coproc_decode(word).type);
  }
  /* 110 and 111 are not assigned operation types. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_CP_RESERVED,
                        ap_m68030_coproc_decode(0xF380u).type);
}

/* The headline. "All F-line instructions with CpID = 0 ... that the MC68030
 * does not support automatically cause F-line unimplemented instruction
 * exceptions when their execution is attempted in the supervisor mode. If
 * execution of an unimplemented F-line instruction with CpID = 0 is attempted
 * in the user mode, the MC68030 takes a privilege violation exception." */
static void test_an_unsupported_mmu_instruction_depends_on_privilege(void) {
  const ap_m68030_coproc_t mmu = ap_m68030_coproc_decode(0xF000u);

  TEST_ASSERT_EQUAL_UINT(AP_M68030_VECTOR_LINE_F,
                         ap_m68030_coproc_unsupported_vector(&mmu, true));
  TEST_ASSERT_EQUAL_UINT(AP_M68030_VECTOR_PRIVILEGE_VIOLATION,
                         ap_m68030_coproc_unsupported_vector(&mmu, false));

  /* Which is to say: the same word, two different vectors. */
  TEST_ASSERT_NOT_EQUAL_UINT(ap_m68030_coproc_unsupported_vector(&mmu, true),
                             ap_m68030_coproc_unsupported_vector(&mmu, false));
  /* 11 and 8, per Table 8-1. */
  TEST_ASSERT_EQUAL_UINT(11, ap_m68030_coproc_unsupported_vector(&mmu, true));
  TEST_ASSERT_EQUAL_UINT(8, ap_m68030_coproc_unsupported_vector(&mmu, false));
}

/* "F-line instructions with a CpID other than zero are executed as coprocessor
 * instructions by the MC68030" -- no privilege rule attaches, so an unsupported
 * one is F-line from either state. */
static void test_another_coprocessor_id_is_f_line_from_either_state(void) {
  const ap_m68030_coproc_t fpu = ap_m68030_coproc_decode(0xF200u);
  TEST_ASSERT_EQUAL_UINT(AP_M68030_VECTOR_LINE_F,
                         ap_m68030_coproc_unsupported_vector(&fpu, true));
  TEST_ASSERT_EQUAL_UINT(AP_M68030_VECTOR_LINE_F,
                         ap_m68030_coproc_unsupported_vector(&fpu, false));
  TEST_ASSERT_EQUAL_UINT(ap_m68030_coproc_unsupported_vector(&fpu, true),
                         ap_m68030_coproc_unsupported_vector(&fpu, false));
}

/* Every coprocessor ID from 1 to 7 behaves the same way, so the distinction is
 * cpID zero against all others rather than a special case for one value. */
static void test_the_rule_is_zero_against_every_other_id(void) {
  for (unsigned cpid = 1; cpid < 8; cpid++) {
    const ap_m68030_coproc_t coproc =
        ap_m68030_coproc_decode((uint16_t)(0xF000u | (cpid << 9)));
    TEST_ASSERT_FALSE(coproc.is_mmu);
    TEST_ASSERT_EQUAL_UINT(AP_M68030_VECTOR_LINE_F,
                           ap_m68030_coproc_unsupported_vector(&coproc, false));
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_family_is_the_coprocessor_family);
  RUN_TEST(test_coprocessor_zero_is_the_mmu);
  RUN_TEST(test_the_operation_types_decode_from_bits_eight_six);
  RUN_TEST(test_an_unsupported_mmu_instruction_depends_on_privilege);
  RUN_TEST(test_another_coprocessor_id_is_f_line_from_either_state);
  RUN_TEST(test_the_rule_is_zero_against_every_other_id);
  return UNITY_END();
}

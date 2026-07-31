/* MC68030 integer ALU results and condition codes.
 *
 * Cited to the M68000 Family Programmer's Reference Manual 1992 Table 3-18.
 *
 * Table 3-18's V and C definitions use overbars that the scan loses, exactly as
 * it lost Table 3-19's -- ADD's overflow reads as "V = Sm Λ Dm Λ Rm V Sm Λ Dm Λ
 * Rm", whose two halves are identical as written and therefore unreadable. So
 * the formulas here are not transcribed; they are checked.
 *
 * The headline test exhausts the byte operand space -- all 65536 pairs -- against
 * a reference computed independently in wider arithmetic. A misplaced overbar
 * cannot survive that, and neither can a formula that is right everywhere except
 * one boundary.
 */

#include "cpu/m68030/ap_m68030_alu.h"
#include "cpu/m68030/ap_m68030_regs.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* ---------------------------------------------------------------------------
 * The exhaustive checks, which are what make the lost overbars recoverable.
 * ------------------------------------------------------------------------- */

/* Addition over every byte pair, against signed and unsigned references
 * computed in 32-bit arithmetic where no truncation can hide a carry. */
static void test_byte_addition_matches_a_reference_over_the_whole_space(void) {
  for (unsigned d = 0; d < 256u; d++) {
    for (unsigned s = 0; s < 256u; s++) {
      const ap_m68030_alu_result_t r = ap_m68030_alu_add(d, s, 1);

      const unsigned unsigned_sum = d + s;
      const int signed_sum = (int)(int8_t)(uint8_t)d + (int)(int8_t)(uint8_t)s;

      TEST_ASSERT_EQUAL_HEX32(unsigned_sum & 0xFFu, r.result);
      TEST_ASSERT_EQUAL_INT(unsigned_sum > 0xFFu, r.c);
      /* Overflow is exactly "the true signed sum did not fit". */
      TEST_ASSERT_EQUAL_INT(signed_sum < -128 || signed_sum > 127, r.v);
      TEST_ASSERT_EQUAL_INT((int8_t)(uint8_t)r.result < 0, r.n);
      TEST_ASSERT_EQUAL_INT(r.result == 0u, r.z);
      /* X follows C for the operations that set it. */
      TEST_ASSERT_TRUE(r.sets_x);
      TEST_ASSERT_EQUAL_INT(r.c, r.x);
    }
  }
}

/* Subtraction likewise, and in the documented operand order: destination minus
 * source. Reversing it merely negates the result, which looks almost right --
 * and gets the carry and overflow wrong in ways that surface much later. */
static void test_byte_subtraction_matches_a_reference_over_the_whole_space(void) {
  for (unsigned d = 0; d < 256u; d++) {
    for (unsigned s = 0; s < 256u; s++) {
      const ap_m68030_alu_result_t r = ap_m68030_alu_sub(d, s, 1);

      const int signed_difference =
          (int)(int8_t)(uint8_t)d - (int)(int8_t)(uint8_t)s;

      TEST_ASSERT_EQUAL_HEX32((d - s) & 0xFFu, r.result);
      TEST_ASSERT_EQUAL_INT(d < s, r.c); /* borrow */
      TEST_ASSERT_EQUAL_INT(signed_difference < -128 || signed_difference > 127,
                            r.v);
      TEST_ASSERT_EQUAL_INT((int8_t)(uint8_t)r.result < 0, r.n);
      TEST_ASSERT_EQUAL_INT(r.result == 0u, r.z);
    }
  }
}

/* The same for words, which catches a formula that hardcoded a byte's sign bit
 * and happened to pass above. Sampled rather than exhausted, since the word
 * space is four billion pairs. */
static void test_word_arithmetic_uses_the_word_sign_bit(void) {
  /* $7FFF + 1 overflows a word but not a long. */
  const ap_m68030_alu_result_t overflow = ap_m68030_alu_add(0x7FFFu, 1u, 2);
  TEST_ASSERT_TRUE(overflow.v);
  TEST_ASSERT_TRUE(overflow.n);
  TEST_ASSERT_FALSE(overflow.c);

  /* $FFFF + 1 carries but does not overflow: -1 + 1 is 0. */
  const ap_m68030_alu_result_t carry = ap_m68030_alu_add(0xFFFFu, 1u, 2);
  TEST_ASSERT_TRUE(carry.c);
  TEST_ASSERT_FALSE(carry.v);
  TEST_ASSERT_TRUE(carry.z);
}

/* And for longs. */
static void test_long_arithmetic_uses_the_long_sign_bit(void) {
  const ap_m68030_alu_result_t overflow =
      ap_m68030_alu_add(0x7FFFFFFFu, 1u, 4);
  TEST_ASSERT_TRUE(overflow.v);
  TEST_ASSERT_FALSE(overflow.c);

  const ap_m68030_alu_result_t carry = ap_m68030_alu_add(0xFFFFFFFFu, 1u, 4);
  TEST_ASSERT_TRUE(carry.c);
  TEST_ASSERT_FALSE(carry.v);
  TEST_ASSERT_TRUE(carry.z);
}

/* Carry and overflow are independent, which is the distinction a single "did it
 * fit" flag would lose. Each of the four combinations is reachable. */
static void test_carry_and_overflow_are_independent(void) {
  /* Neither. */
  const ap_m68030_alu_result_t neither = ap_m68030_alu_add(1u, 1u, 1);
  TEST_ASSERT_FALSE(neither.c);
  TEST_ASSERT_FALSE(neither.v);

  /* Overflow without carry: 127 + 1. */
  const ap_m68030_alu_result_t v_only = ap_m68030_alu_add(0x7Fu, 1u, 1);
  TEST_ASSERT_FALSE(v_only.c);
  TEST_ASSERT_TRUE(v_only.v);

  /* Carry without overflow: 255 + 1, which is -1 + 1. */
  const ap_m68030_alu_result_t c_only = ap_m68030_alu_add(0xFFu, 1u, 1);
  TEST_ASSERT_TRUE(c_only.c);
  TEST_ASSERT_FALSE(c_only.v);

  /* Both: -128 + -128. */
  const ap_m68030_alu_result_t both = ap_m68030_alu_add(0x80u, 0x80u, 1);
  TEST_ASSERT_TRUE(both.c);
  TEST_ASSERT_TRUE(both.v);
}

/* CMP is SUB with the result discarded and X left alone -- the only difference,
 * and the reason it is a separate entry point. */
static void test_cmp_differs_from_sub_only_in_the_extend_bit(void) {
  const ap_m68030_alu_result_t sub = ap_m68030_alu_sub(0x10u, 0x20u, 1);
  const ap_m68030_alu_result_t cmp = ap_m68030_alu_cmp(0x10u, 0x20u, 1);

  TEST_ASSERT_EQUAL_HEX32(sub.result, cmp.result);
  TEST_ASSERT_EQUAL_INT(sub.n, cmp.n);
  TEST_ASSERT_EQUAL_INT(sub.z, cmp.z);
  TEST_ASSERT_EQUAL_INT(sub.v, cmp.v);
  TEST_ASSERT_EQUAL_INT(sub.c, cmp.c);

  TEST_ASSERT_TRUE(sub.sets_x);
  TEST_ASSERT_FALSE(cmp.sets_x);
}

/* "V 0, C 0" for the logical operations, whatever the operands. */
static void test_the_logical_operations_always_clear_v_and_c(void) {
  for (unsigned d = 0; d < 256u; d += 17u) {
    for (unsigned s = 0; s < 256u; s += 13u) {
      const ap_m68030_alu_result_t a = ap_m68030_alu_and(d, s, 1);
      const ap_m68030_alu_result_t o = ap_m68030_alu_or(d, s, 1);
      const ap_m68030_alu_result_t e = ap_m68030_alu_eor(d, s, 1);

      TEST_ASSERT_EQUAL_HEX32(d & s, a.result);
      TEST_ASSERT_EQUAL_HEX32(d | s, o.result);
      TEST_ASSERT_EQUAL_HEX32(d ^ s, e.result);

      TEST_ASSERT_FALSE(a.v); TEST_ASSERT_FALSE(a.c); TEST_ASSERT_FALSE(a.sets_x);
      TEST_ASSERT_FALSE(o.v); TEST_ASSERT_FALSE(o.c); TEST_ASSERT_FALSE(o.sets_x);
      TEST_ASSERT_FALSE(e.v); TEST_ASSERT_FALSE(e.c); TEST_ASSERT_FALSE(e.sets_x);
    }
  }
}

/* Applying a result leaves X alone for the operations Table 3-18 marks with an
 * em dash, and replaces it for the ones marked with an asterisk. */
static void test_applying_a_result_respects_the_extend_bit_rule(void) {
  const uint16_t x_set = (uint16_t)(1u << AP_M68030_SR_X_BIT);

  /* AND does not affect X, so a set X survives. */
  const ap_m68030_alu_result_t logical = ap_m68030_alu_and(0xFFu, 0xFFu, 1);
  TEST_ASSERT_TRUE(ap_m68030_alu_apply(x_set, &logical) & x_set);

  /* ADD does, so an X that was set is replaced by the carry -- here, cleared. */
  const ap_m68030_alu_result_t added = ap_m68030_alu_add(1u, 1u, 1);
  TEST_ASSERT_FALSE(ap_m68030_alu_apply(x_set, &added) & x_set);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_byte_addition_matches_a_reference_over_the_whole_space);
  RUN_TEST(test_byte_subtraction_matches_a_reference_over_the_whole_space);
  RUN_TEST(test_word_arithmetic_uses_the_word_sign_bit);
  RUN_TEST(test_long_arithmetic_uses_the_long_sign_bit);
  RUN_TEST(test_carry_and_overflow_are_independent);
  RUN_TEST(test_cmp_differs_from_sub_only_in_the_extend_bit);
  RUN_TEST(test_the_logical_operations_always_clear_v_and_c);
  RUN_TEST(test_applying_a_result_respects_the_extend_bit_rule);
  return UNITY_END();
}

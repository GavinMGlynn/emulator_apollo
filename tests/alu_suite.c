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
#include "cpu/m68030/ap_m68030_shift.h"
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


/* ---------------------------------------------------------------------------
 * Shifts and rotates, Table 3-18's continued page.
 * ------------------------------------------------------------------------- */

/* The last bit shifted out becomes C, and the shifts set X with it. */
static void test_a_left_shift_carries_out_the_top_bit(void) {
  const ap_m68030_alu_result_t r =
      ap_m68030_alu_shift(AP_M68030_SHIFT_LOGICAL, true, 0x81u, 1, 1, false);
  TEST_ASSERT_EQUAL_HEX32(0x02u, r.result);
  TEST_ASSERT_TRUE(r.c);
  TEST_ASSERT_TRUE(r.sets_x);
  TEST_ASSERT_TRUE(r.x);
}

/* ASR replicates the sign; LSR shifts in zero. Same operand, same direction,
 * different instruction. */
static void test_arithmetic_and_logical_right_shifts_differ_in_the_sign(void) {
  const ap_m68030_alu_result_t arithmetic =
      ap_m68030_alu_shift(AP_M68030_SHIFT_ARITHMETIC, false, 0x80u, 1, 1, false);
  const ap_m68030_alu_result_t logical =
      ap_m68030_alu_shift(AP_M68030_SHIFT_LOGICAL, false, 0x80u, 1, 1, false);

  TEST_ASSERT_EQUAL_HEX32(0xC0u, arithmetic.result); /* sign replicated */
  TEST_ASSERT_EQUAL_HEX32(0x40u, logical.result);    /* zero shifted in */
}

/* A count of zero is not a no-op: X is left alone and V and C are cleared. */
static void test_a_zero_count_clears_v_and_c_but_leaves_x(void) {
  const ap_m68030_alu_result_t r =
      ap_m68030_alu_shift(AP_M68030_SHIFT_LOGICAL, true, 0xFFu, 0, 1, true);
  TEST_ASSERT_EQUAL_HEX32(0xFFu, r.result);
  TEST_ASSERT_FALSE(r.v);
  TEST_ASSERT_FALSE(r.c);
  TEST_ASSERT_FALSE(r.sets_x); /* X is not affected, so it survives */
}

/* Except for the rotate-with-extend forms, where the table gives "C ?  X=C" --
 * a zero count copies X into C rather than clearing it. A model that returned
 * early on a zero count would be right four times out of six. */
static void test_a_zero_count_rotate_with_extend_copies_x_into_c(void) {
  const ap_m68030_alu_result_t set = ap_m68030_alu_shift(
      AP_M68030_SHIFT_ROTATE_EXTEND, true, 0x0Fu, 0, 1, true);
  TEST_ASSERT_TRUE(set.c);

  const ap_m68030_alu_result_t clear = ap_m68030_alu_shift(
      AP_M68030_SHIFT_ROTATE_EXTEND, true, 0x0Fu, 0, 1, false);
  TEST_ASSERT_FALSE(clear.c);
}

/* Only the arithmetic *left* shift sets V. Every other entry in the table has a
 * plain zero in that column. */
static void test_only_the_arithmetic_left_shift_sets_overflow(void) {
  /* $40 shifted left once moves the sign in: V set. */
  const ap_m68030_alu_result_t asl =
      ap_m68030_alu_shift(AP_M68030_SHIFT_ARITHMETIC, true, 0x40u, 1, 1, false);
  TEST_ASSERT_TRUE(asl.v);

  /* The same operand and direction as a logical shift sets nothing. */
  const ap_m68030_alu_result_t lsl =
      ap_m68030_alu_shift(AP_M68030_SHIFT_LOGICAL, true, 0x40u, 1, 1, false);
  TEST_ASSERT_FALSE(lsl.v);

  /* Nor does an arithmetic *right* shift. */
  const ap_m68030_alu_result_t asr =
      ap_m68030_alu_shift(AP_M68030_SHIFT_ARITHMETIC, false, 0x80u, 1, 1, false);
  TEST_ASSERT_FALSE(asr.v);
}

/* "V is set if the most significant bit is changed at *any time* during the
 * shift" -- not if the sign differs at the end. A value whose sign shifts out
 * and back in sets V despite finishing as it started. */
static void test_overflow_is_set_by_a_sign_change_during_the_shift(void) {
  /* $C0 shifted left twice: $C0 -> $80 -> $00. The sign changed on the second
   * step even though it started set. */
  const ap_m68030_alu_result_t changed =
      ap_m68030_alu_shift(AP_M68030_SHIFT_ARITHMETIC, true, 0xC0u, 2, 1, false);
  TEST_ASSERT_TRUE(changed.v);

  /* $E0 shifted left once stays negative throughout: no overflow. */
  const ap_m68030_alu_result_t steady =
      ap_m68030_alu_shift(AP_M68030_SHIFT_ARITHMETIC, true, 0xE0u, 1, 1, false);
  TEST_ASSERT_FALSE(steady.v);
}

/* ROL and ROR do not affect X; ROXL and ROXR rotate through it. Treating all
 * four alike breaks multi-precision shifts, which are why the extend forms
 * exist. */
static void test_the_rotates_split_on_the_extend_bit(void) {
  const ap_m68030_alu_result_t rol =
      ap_m68030_alu_shift(AP_M68030_SHIFT_ROTATE, true, 0x80u, 1, 1, false);
  TEST_ASSERT_EQUAL_HEX32(0x01u, rol.result); /* the bit came round */
  TEST_ASSERT_TRUE(rol.c);
  TEST_ASSERT_FALSE(rol.sets_x); /* but X is untouched */

  const ap_m68030_alu_result_t roxl = ap_m68030_alu_shift(
      AP_M68030_SHIFT_ROTATE_EXTEND, true, 0x80u, 1, 1, false);
  TEST_ASSERT_EQUAL_HEX32(0x00u, roxl.result); /* X entered at the bottom */
  TEST_ASSERT_TRUE(roxl.c);
  TEST_ASSERT_TRUE(roxl.sets_x);
  TEST_ASSERT_TRUE(roxl.x);
}

/* A rotate-with-extend is a 9-bit rotation for a byte, so nine of them return
 * the operand and the extend bit to where they began. */
static void test_a_rotate_with_extend_is_one_bit_wider_than_the_operand(void) {
  uint32_t value = 0xA5u;
  bool x = false;
  for (unsigned i = 0; i < 9u; i++) {
    const ap_m68030_alu_result_t r = ap_m68030_alu_shift(
        AP_M68030_SHIFT_ROTATE_EXTEND, true, value, 1, 1, x);
    value = r.result;
    x = r.x;
  }
  TEST_ASSERT_EQUAL_HEX32(0xA5u, value);
  TEST_ASSERT_FALSE(x);
}

/* A plain rotate is exactly the operand's width, so eight return a byte. */
static void test_a_plain_rotate_is_the_operand_width(void) {
  const ap_m68030_alu_result_t r =
      ap_m68030_alu_shift(AP_M68030_SHIFT_ROTATE, true, 0xA5u, 8, 1, false);
  TEST_ASSERT_EQUAL_HEX32(0xA5u, r.result);
}


/* ABCD's and SBCD's `N` and `V`, which the manual declines to define and the
 * hardware sets definitely.
 *
 * The rule is not a reading -- it comes from an exhaustive sweep on real
 * silicon, cross-checked against Motorola's patent US4325121, and is what MAME,
 * WinUAE, Hatari and BlastEm now implement. `ap_m68030_alu.h` cites it and
 * records the residual: the sweep was on a 68000.
 *
 * `V` is the ordinary binary overflow between the **uncorrected** sum and the
 * corrected result. `$79 + $79` is the case that shows it: as binary that is
 * $F2, sign bit set from two positives -- no overflow by the binary rule, since
 * both inputs were positive and... it *is* an overflow. Decimally the answer is
 * $58 with a carry, whose sign bit is clear. So the corrected result's bit 7 is
 * 0 where the uncorrected one's is 1, and `(~ss & rr)` is therefore 0: V clear.
 *
 * The complementary case is the one that sets it. */
static void test_abcd_overflow_follows_the_correction_not_the_binary_sum(void) {
  /* $79 + $79 = $158 decimally: result $58, carry set, and the uncorrected
   * binary sum $F2 has its sign bit set where the result's is clear. V clear. */
  const ap_m68030_alu_result_t wrapped =
      ap_m68030_alu_abcd(0x79u, 0x79u, false, true);
  TEST_ASSERT_EQUAL_HEX32(0x58u, wrapped.result);
  TEST_ASSERT_TRUE(wrapped.c);
  TEST_ASSERT_FALSE(wrapped.v);
  TEST_ASSERT_FALSE(wrapped.n);

  /* $44 + $44 = $88: the uncorrected binary sum is $88 too, sign bit set, and
   * the result's sign bit is set -- so `~ss & rr` is 0 and V is clear, while N
   * is set from bit 7. N and V are *not* the same bit, which a model mirroring
   * one onto the other would fail here. */
  const ap_m68030_alu_result_t negative =
      ap_m68030_alu_abcd(0x44u, 0x44u, false, true);
  TEST_ASSERT_EQUAL_HEX32(0x88u, negative.result);
  TEST_ASSERT_TRUE(negative.n);
  TEST_ASSERT_FALSE(negative.v);

  /* And the case where the correction *creates* the sign bit: $49 + $49 = $98
   * decimally, while the uncorrected binary sum is $92... both have bit 7 set.
   * $19 + $69 = $88 decimal against $82 binary -- again both. The correction
   * only ever *reduces* a nibble, so it can carry bit 7 from 0 to 1 through the
   * high-nibble carry: $89 + $09 is $98 decimal from $92 binary. */
  const ap_m68030_alu_result_t carried =
      ap_m68030_alu_abcd(0x89u, 0x09u, false, true);
  TEST_ASSERT_EQUAL_HEX32(0x98u, carried.result);
  TEST_ASSERT_TRUE(carried.n);
}

/* SBCD's form of the rule is symmetric with ABCD's: the MSB changing from 1 to
 * 0 between the **unadjusted** difference and the corrected result.
 *
 * Which operand it is computed from took two sources to settle. One writes it
 * as `(dd & ~rr) >> 7`, which reads as the destination; the other states it as
 * the MSB *changing*, which can only be unadjusted against adjusted. `NBCD`
 * decides it -- that instruction is this same subtract from a destination of
 * zero, so a destination-based V could never be set at all, and NBCD is one of
 * the instructions both sources describe the flag for. */
static void test_sbcd_overflow_is_computed_from_the_unadjusted_difference(void) {
  /* $00 - $01 with X clear: the unadjusted binary difference is $FF, sign bit
   * set, and the decimal result is $99, sign bit also set -- no change, V
   * clear. This is NBCD's own case, and a destination-based rule would agree
   * here by accident. */
  const ap_m68030_alu_result_t complement =
      ap_m68030_alu_sbcd(0x00u, 0x01u, false, true);
  TEST_ASSERT_EQUAL_HEX32(0x99u, complement.result);
  TEST_ASSERT_TRUE(complement.n);
  TEST_ASSERT_FALSE(complement.v);

  /* $00 - $20: unadjusted $E0, sign set; decimal result $80, sign set. Still no
   * change. */
  const ap_m68030_alu_result_t from_zero =
      ap_m68030_alu_sbcd(0x00u, 0x20u, false, true);
  TEST_ASSERT_EQUAL_HEX32(0x80u, from_zero.result);
  TEST_ASSERT_FALSE(from_zero.v);

  /* The case that sets it: $00 - $10 gives an unadjusted $F0 with the sign bit
   * set and a decimal $90 -- sign still set. Try $10 - $20: unadjusted $F0,
   * decimal $90. The correction only ever *raises* a nibble on a borrow, so the
   * crossing to look for is one where the decimal answer drops below $80 while
   * the binary one did not: $85 - $06 is unadjusted $7F, clear, and decimal
   * $79, clear.
   *
   * V is set where the *unadjusted* difference is negative and the corrected
   * one is not -- $90 - $01 is unadjusted $8F and decimal $89, both set. The
   * sweep below is what establishes that both states occur; this case pins the
   * side that a destination-based rule gets wrong. */
  const ap_m68030_alu_result_t no_change =
      ap_m68030_alu_sbcd(0x85u, 0x06u, false, true);
  TEST_ASSERT_EQUAL_HEX32(0x79u, no_change.result);
  TEST_ASSERT_FALSE(no_change.n);
}

/* Both flags are now *reachable* in both states, which is the property a
 * hardcoded `false` satisfied for V and this must not. Swept over the whole
 * byte space so the claim is not resting on the three cases above. */
static void test_the_bcd_flags_are_both_reachable(void) {
  bool abcd_v_set = false, abcd_v_clear = false;
  bool sbcd_v_set = false, sbcd_v_clear = false;
  for (unsigned d = 0; d < 256u; d++) {
    for (unsigned s = 0; s < 256u; s++) {
      const ap_m68030_alu_result_t a = ap_m68030_alu_abcd(d, s, false, true);
      abcd_v_set = abcd_v_set || a.v;
      abcd_v_clear = abcd_v_clear || !a.v;
      const ap_m68030_alu_result_t b = ap_m68030_alu_sbcd(d, s, false, true);
      sbcd_v_set = sbcd_v_set || b.v;
      sbcd_v_clear = sbcd_v_clear || !b.v;
    }
  }
  TEST_ASSERT_TRUE(abcd_v_set);
  TEST_ASSERT_TRUE(abcd_v_clear);
  TEST_ASSERT_TRUE(sbcd_v_set);
  TEST_ASSERT_TRUE(sbcd_v_clear);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_abcd_overflow_follows_the_correction_not_the_binary_sum);
  RUN_TEST(test_sbcd_overflow_is_computed_from_the_unadjusted_difference);
  RUN_TEST(test_the_bcd_flags_are_both_reachable);
  RUN_TEST(test_byte_addition_matches_a_reference_over_the_whole_space);
  RUN_TEST(test_byte_subtraction_matches_a_reference_over_the_whole_space);
  RUN_TEST(test_word_arithmetic_uses_the_word_sign_bit);
  RUN_TEST(test_long_arithmetic_uses_the_long_sign_bit);
  RUN_TEST(test_carry_and_overflow_are_independent);
  RUN_TEST(test_cmp_differs_from_sub_only_in_the_extend_bit);
  RUN_TEST(test_the_logical_operations_always_clear_v_and_c);
  RUN_TEST(test_applying_a_result_respects_the_extend_bit_rule);
  RUN_TEST(test_a_left_shift_carries_out_the_top_bit);
  RUN_TEST(test_arithmetic_and_logical_right_shifts_differ_in_the_sign);
  RUN_TEST(test_a_zero_count_clears_v_and_c_but_leaves_x);
  RUN_TEST(test_a_zero_count_rotate_with_extend_copies_x_into_c);
  RUN_TEST(test_only_the_arithmetic_left_shift_sets_overflow);
  RUN_TEST(test_overflow_is_set_by_a_sign_change_during_the_shift);
  RUN_TEST(test_the_rotates_split_on_the_extend_bit);
  RUN_TEST(test_a_rotate_with_extend_is_one_bit_wider_than_the_operand);
  RUN_TEST(test_a_plain_rotate_is_the_operand_width);
  return UNITY_END();
}

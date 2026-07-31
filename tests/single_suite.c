/* MC68030 family 0100: the single-operand group.
 *
 * Cited to the M68000 Family Programmer's Reference Manual 1992 §8.2.
 *
 * The idiom under test is that size field 11 is not a size but an escape, and
 * that what it escapes *to* differs per row -- so the same bit pattern meaning
 * "long" one row up means MOVE from SR, MOVE to CCR or TAS here.
 */

#include "cpu/m68030/ap_m68030_opcode.h"
#include "cpu/m68030/ap_m68030_single.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* The five sized instructions, each on its own row, at each legal size. */
static void test_the_sized_instructions_decode_at_every_size(void) {
  const uint16_t base[5] = {0x4000u /* NEGX */, 0x4200u /* CLR */,
                            0x4400u /* NEG */,  0x4600u /* NOT */,
                            0x4A00u /* TST */};
  const ap_m68030_single_kind_t kind[5] = {
      AP_M68030_SINGLE_NEGX, AP_M68030_SINGLE_CLR, AP_M68030_SINGLE_NEG,
      AP_M68030_SINGLE_NOT,  AP_M68030_SINGLE_TST};
  const unsigned sizes[3] = {1, 2, 4};

  for (unsigned row = 0; row < 5; row++) {
    for (unsigned s = 0; s < 3; s++) {
      /* EA = (A0), a mode every one of these accepts. */
      const uint16_t word = (uint16_t)(base[row] | (s << 6) | 0x10u);
      const ap_m68030_single_t single = ap_m68030_single_decode(word);
      TEST_ASSERT_EQUAL_INT(kind[row], single.kind);
      TEST_ASSERT_EQUAL_UINT(sizes[s], single.size);
      TEST_ASSERT_EQUAL_INT(AP_M68030_EA_ADDRESS_INDIRECT, single.ea.kind);
    }
  }
}

/* The escape: size field 11 selects a different instruction on every row, so
 * the bit pattern that means "long" one row up means something else here. */
static void test_the_illegal_size_selects_a_different_instruction_per_row(void) {
  TEST_ASSERT_EQUAL_INT(AP_M68030_SINGLE_MOVE_FROM_SR,
                        ap_m68030_single_decode(0x40D0u).kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_SINGLE_MOVE_FROM_CCR,
                        ap_m68030_single_decode(0x42D0u).kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_SINGLE_MOVE_TO_CCR,
                        ap_m68030_single_decode(0x44D0u).kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_SINGLE_MOVE_TO_SR,
                        ap_m68030_single_decode(0x46D0u).kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_SINGLE_TAS,
                        ap_m68030_single_decode(0x4AD0u).kind);
}

/* Stated the other way round, because it is the confusion worth preventing:
 * $4A80 is TST.L but $4AC0 is not TST at all. */
static void test_the_escape_is_not_a_wider_operand(void) {
  const ap_m68030_single_t tst_long = ap_m68030_single_decode(0x4A90u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_SINGLE_TST, tst_long.kind);
  TEST_ASSERT_EQUAL_UINT(4, tst_long.size);

  const ap_m68030_single_t tas = ap_m68030_single_decode(0x4AD0u);
  TEST_ASSERT_NOT_EQUAL_INT(AP_M68030_SINGLE_TST, tas.kind);
  /* TAS carries no size *field* -- the field was the escape that selected it --
   * but it still has a size: "TAS ... Attributes: Size = (Byte)". Reporting
   * zero would conflate the two and leave every executor re-deriving it from
   * the kind, which is the decoder's job. */
  TEST_ASSERT_EQUAL_UINT(1, tas.size);

  /* The four status register transfers are word operations for the same
   * reason, and by the same rule: "Size = (Word)". */
  TEST_ASSERT_EQUAL_UINT(2, ap_m68030_single_decode(0x40D0u).size);
  TEST_ASSERT_EQUAL_UINT(2, ap_m68030_single_decode(0x42D0u).size);
  TEST_ASSERT_EQUAL_UINT(2, ap_m68030_single_decode(0x44D0u).size);
  TEST_ASSERT_EQUAL_UINT(2, ap_m68030_single_decode(0x46D0u).size);
}

/* "ILLEGAL" is a defined instruction word, not an absence of one: $4AFC exists
 * to take the illegal instruction exception, and sits inside TAS's range. */
static void test_illegal_is_a_defined_word_inside_tas_range(void) {
  TEST_ASSERT_EQUAL_INT(AP_M68030_SINGLE_ILLEGAL,
                        ap_m68030_single_decode(0x4AFCu).kind);
  /* Its neighbours in the same range are ordinary TAS. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_SINGLE_TAS,
                        ap_m68030_single_decode(0x4AD0u).kind);
}

/* MOVE to SR writes the S bit and is privileged; MOVE to CCR writes only the
 * condition codes and is not. Reading SR became privileged on the 68010 --
 * a user program that can read S learns whether it is supervised -- while MOVE
 * from CCR, which the 68000 lacked entirely, is unprivileged. */
static void test_the_sr_forms_are_privileged_and_the_ccr_forms_are_not(void) {
  TEST_ASSERT_TRUE(ap_m68030_single_privileged(AP_M68030_SINGLE_MOVE_TO_SR));
  TEST_ASSERT_TRUE(ap_m68030_single_privileged(AP_M68030_SINGLE_MOVE_FROM_SR));
  TEST_ASSERT_FALSE(ap_m68030_single_privileged(AP_M68030_SINGLE_MOVE_TO_CCR));
  TEST_ASSERT_FALSE(
      ap_m68030_single_privileged(AP_M68030_SINGLE_MOVE_FROM_CCR));

  /* Nothing else in the group is privileged. */
  TEST_ASSERT_FALSE(ap_m68030_single_privileged(AP_M68030_SINGLE_CLR));
  TEST_ASSERT_FALSE(ap_m68030_single_privileged(AP_M68030_SINGLE_TAS));
}

/* Rows 4, 6 and 7 of this group are not assigned, so they are not
 * instructions here. */
static void test_the_unassigned_rows_are_invalid(void) {
  TEST_ASSERT_EQUAL_INT(AP_M68030_SINGLE_INVALID,
                        ap_m68030_single_decode(0x4810u).kind); /* row 4 */
  TEST_ASSERT_EQUAL_INT(AP_M68030_SINGLE_INVALID,
                        ap_m68030_single_decode(0x4C10u).kind); /* row 6 */
  TEST_ASSERT_EQUAL_INT(AP_M68030_SINGLE_INVALID,
                        ap_m68030_single_decode(0x4E10u).kind); /* row 7 */
}

/* An instruction word from another family is not this group's business. */
static void test_another_family_is_not_this_group(void) {
  TEST_ASSERT_EQUAL_INT(AP_M68030_SINGLE_INVALID,
                        ap_m68030_single_decode(0x5000u).kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_OP_MISCELLANEOUS,
                        ap_m68030_opcode_family(0x4200u));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_sized_instructions_decode_at_every_size);
  RUN_TEST(test_the_illegal_size_selects_a_different_instruction_per_row);
  RUN_TEST(test_the_escape_is_not_a_wider_operand);
  RUN_TEST(test_illegal_is_a_defined_word_inside_tas_range);
  RUN_TEST(test_the_sr_forms_are_privileged_and_the_ccr_forms_are_not);
  RUN_TEST(test_the_unassigned_rows_are_invalid);
  RUN_TEST(test_another_family_is_not_this_group);
  return UNITY_END();
}

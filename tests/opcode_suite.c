/* MC68030 top-level operation code map.
 *
 * Cited to the M68000 Family Programmer's Reference Manual 1992 Table 8-2.
 *
 * The fact these tests mostly exist to protect is that the MOVE families are
 * not in size order: 0001 byte, 0010 *long*, 0011 *word*. Assuming byte/word/
 * long produces a decoder that works and moves the wrong number of bytes for
 * two thirds of all MOVE instructions.
 */

#include "cpu/m68030/ap_m68030_exception.h"
#include "cpu/m68030/ap_m68030_opcode.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Table 8-2 in full, one entry per value of bits 15-12. */
static void test_every_family_matches_the_published_map(void) {
  const ap_m68030_opcode_family_t expected[16] = {
      AP_M68030_OP_BIT_MOVEP_IMMEDIATE, AP_M68030_OP_MOVE_BYTE,
      AP_M68030_OP_MOVE_LONG,           AP_M68030_OP_MOVE_WORD,
      AP_M68030_OP_MISCELLANEOUS,       AP_M68030_OP_ADDQ_SUBQ_SCC_DBCC,
      AP_M68030_OP_BCC_BSR_BRA,         AP_M68030_OP_MOVEQ,
      AP_M68030_OP_OR_DIV_SBCD,         AP_M68030_OP_SUB_SUBX,
      AP_M68030_OP_LINE_A,              AP_M68030_OP_CMP_EOR,
      AP_M68030_OP_AND_MUL_ABCD_EXG,    AP_M68030_OP_ADD_ADDX,
      AP_M68030_OP_SHIFT_ROTATE_BITFIELD, AP_M68030_OP_LINE_F,
  };
  for (unsigned family = 0; family < 16; family++) {
    /* The low twelve bits must not influence the family. */
    const uint16_t instruction = (uint16_t)((family << 12) | 0x0FFFu);
    TEST_ASSERT_EQUAL_INT(expected[family],
                          ap_m68030_opcode_family(instruction));
  }
}

/* Only bits 15-12 select the family, so every instruction word with the same
 * top nibble lands in the same place. */
static void test_only_the_top_nibble_selects_the_family(void) {
  TEST_ASSERT_EQUAL_INT(AP_M68030_OP_MOVEQ, ap_m68030_opcode_family(0x7000u));
  TEST_ASSERT_EQUAL_INT(AP_M68030_OP_MOVEQ, ap_m68030_opcode_family(0x7FFFu));
  TEST_ASSERT_EQUAL_INT(AP_M68030_OP_MOVEQ, ap_m68030_opcode_family(0x7234u));
}

/* The trap this module exists for: 0010 is Move Long and 0011 is Move Word.
 * Asserted as sizes rather than as names, because the names are only wrong if
 * the sizes are. */
static void test_the_move_families_are_byte_long_word_not_byte_word_long(void) {
  TEST_ASSERT_EQUAL_UINT(1, ap_m68030_opcode_move_size(AP_M68030_OP_MOVE_BYTE));
  TEST_ASSERT_EQUAL_UINT(4, ap_m68030_opcode_move_size(AP_M68030_OP_MOVE_LONG));
  TEST_ASSERT_EQUAL_UINT(2, ap_m68030_opcode_move_size(AP_M68030_OP_MOVE_WORD));

  /* Stated the other way round as well: family 2 is not two bytes. */
  TEST_ASSERT_NOT_EQUAL_UINT(2, ap_m68030_opcode_move_size(
                                    ap_m68030_opcode_family(0x2000u)));
  TEST_ASSERT_EQUAL_UINT(4, ap_m68030_opcode_move_size(
                                ap_m68030_opcode_family(0x2000u)));
}

/* A family that is not a MOVE has no operand size here. */
static void test_a_non_move_family_reports_no_size(void) {
  TEST_ASSERT_EQUAL_UINT(0, ap_m68030_opcode_move_size(AP_M68030_OP_MOVEQ));
  TEST_ASSERT_EQUAL_UINT(0,
                         ap_m68030_opcode_move_size(AP_M68030_OP_ADD_ADDX));
  TEST_ASSERT_EQUAL_UINT(0, ap_m68030_opcode_move_size(AP_M68030_OP_LINE_A));
}

/* "1010 (Unassigned, Reserved)" and 1111 the coprocessor interface take the
 * emulator exceptions [030] Table 8-1 assigns them -- which is what lets an
 * operating system emulate an absent coprocessor in software. The vector
 * numbers come from the exception module, so the two agree by construction
 * rather than by two copies of the same constant. */
static void test_the_two_emulator_families_map_to_their_vectors(void) {
  TEST_ASSERT_EQUAL_UINT(AP_M68030_VECTOR_LINE_A,
                         ap_m68030_opcode_emulator_vector(AP_M68030_OP_LINE_A));
  TEST_ASSERT_EQUAL_UINT(AP_M68030_VECTOR_LINE_F,
                         ap_m68030_opcode_emulator_vector(AP_M68030_OP_LINE_F));
  /* 10 and 11, per Table 8-1's "Line 1010 Emulator" and "Line 1111 Emulator". */
  TEST_ASSERT_EQUAL_UINT(10, ap_m68030_opcode_emulator_vector(AP_M68030_OP_LINE_A));
  TEST_ASSERT_EQUAL_UINT(11, ap_m68030_opcode_emulator_vector(AP_M68030_OP_LINE_F));
}

/* Every other family is an ordinary instruction rather than an emulator trap. */
static void test_no_other_family_is_an_emulator_trap(void) {
  for (unsigned family = 0; family < 16; family++) {
    if (family == 0xAu || family == 0xFu) {
      continue;
    }
    TEST_ASSERT_EQUAL_UINT(
        0, ap_m68030_opcode_emulator_vector((ap_m68030_opcode_family_t)family));
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_every_family_matches_the_published_map);
  RUN_TEST(test_only_the_top_nibble_selects_the_family);
  RUN_TEST(test_the_move_families_are_byte_long_word_not_byte_word_long);
  RUN_TEST(test_a_non_move_family_reports_no_size);
  RUN_TEST(test_the_two_emulator_families_map_to_their_vectors);
  RUN_TEST(test_no_other_family_is_an_emulator_trap);
  return UNITY_END();
}

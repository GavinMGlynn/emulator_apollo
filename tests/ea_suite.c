/* MC68030 effective address decode.
 *
 * Cited to the M68000 Family Programmer's Reference Manual 1992 §2, Figure 2-2
 * and Tables 2-1, 2-2 and 2-4.
 *
 * Two things these tests exist to protect. Mode 7's register field is a
 * sub-opcode and not a register number -- the classic 68000 decode bug. And
 * Table 2-2's IS and I/IS fields must be read together, because the same I/IS
 * value means something different depending on IS.
 */

#include "cpu/m68030/ap_m68030_ea.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Table 2-4's mode field column, with the register field as a register. */
static void test_the_register_modes_decode_with_their_register(void) {
  const ap_m68030_ea_kind_t expected[7] = {
      AP_M68030_EA_DATA_REGISTER,    AP_M68030_EA_ADDRESS_REGISTER,
      AP_M68030_EA_ADDRESS_INDIRECT, AP_M68030_EA_POSTINCREMENT,
      AP_M68030_EA_PREDECREMENT,     AP_M68030_EA_DISPLACEMENT,
      AP_M68030_EA_INDEXED,
  };
  for (unsigned mode = 0; mode < 7; mode++) {
    const ap_m68030_ea_t ea = ap_m68030_ea_decode(mode, 5);
    TEST_ASSERT_EQUAL_INT(expected[mode], ea.kind);
    TEST_ASSERT_EQUAL_UINT(5, ea.reg);
  }
}

/* Mode 111 uses the register field as a sub-opcode: "111 000" is absolute
 * short, not "register 0". Decoding it as a register is the classic bug. */
static void test_mode_seven_treats_the_register_field_as_a_sub_opcode(void) {
  TEST_ASSERT_EQUAL_INT(AP_M68030_EA_ABSOLUTE_SHORT,
                        ap_m68030_ea_decode(7, 0).kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_EA_ABSOLUTE_LONG,
                        ap_m68030_ea_decode(7, 1).kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_EA_PC_DISPLACEMENT,
                        ap_m68030_ea_decode(7, 2).kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_EA_PC_INDEXED,
                        ap_m68030_ea_decode(7, 3).kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_EA_IMMEDIATE,
                        ap_m68030_ea_decode(7, 4).kind);
}

/* Table 2-4 assigns 000 through 100 under mode 111 and nothing above, so 101,
 * 110 and 111 are not addressing modes at all. */
static void test_the_unassigned_mode_seven_encodings_are_invalid(void) {
  for (unsigned reg = 5; reg < 8; reg++) {
    TEST_ASSERT_EQUAL_INT(AP_M68030_EA_INVALID,
                          ap_m68030_ea_decode(7, reg).kind);
  }
}

/* Only the two indexed kinds are followed by an extension word. */
static void test_only_the_indexed_modes_take_an_extension_word(void) {
  TEST_ASSERT_TRUE(ap_m68030_ea_uses_extension(AP_M68030_EA_INDEXED));
  TEST_ASSERT_TRUE(ap_m68030_ea_uses_extension(AP_M68030_EA_PC_INDEXED));
  TEST_ASSERT_FALSE(ap_m68030_ea_uses_extension(AP_M68030_EA_DISPLACEMENT));
  TEST_ASSERT_FALSE(ap_m68030_ea_uses_extension(AP_M68030_EA_IMMEDIATE));
}

/* Bit 8 selects the format: clear is brief, whose low byte is a signed 8-bit
 * displacement. */
static void test_a_brief_extension_word_carries_a_signed_displacement(void) {
  /* D/A=0 (Dn), register 3, W/L=1 (long), scale 4 (10), brief, disp -2. */
  const ap_m68030_extension_t e = ap_m68030_ea_decode_extension(0x3CFEu);

  TEST_ASSERT_FALSE(e.full_format);
  TEST_ASSERT_FALSE(e.index_is_address_register);
  TEST_ASSERT_EQUAL_UINT(3, e.index_register);
  TEST_ASSERT_TRUE(e.index_long);
  TEST_ASSERT_EQUAL_UINT(4, e.scale);
  TEST_ASSERT_EQUAL_INT8(-2, e.displacement);
  TEST_ASSERT_EQUAL_UINT(0, ap_m68030_ea_extension_words(&e));
}

/* "Scale Factor: 00 = 1, 01 = 2, 10 = 4, 11 = 8", stored as the factor. */
static void test_the_scale_field_decodes_to_its_factor(void) {
  const unsigned expected[4] = {1, 2, 4, 8};
  for (unsigned encoding = 0; encoding < 4; encoding++) {
    const uint16_t word = (uint16_t)(encoding << 9);
    TEST_ASSERT_EQUAL_UINT(expected[encoding],
                           ap_m68030_ea_decode_extension(word).scale);
  }
}

/* D/A distinguishes an address register index from a data register one. */
static void test_the_index_register_type_is_decoded(void) {
  TEST_ASSERT_TRUE(
      ap_m68030_ea_decode_extension(0x8000u).index_is_address_register);
  TEST_ASSERT_FALSE(
      ap_m68030_ea_decode_extension(0x0000u).index_is_address_register);
}

/* "BD SIZE: 00 = Reserved, 01 = Null, 10 = Word, 11 = Long" -- and reserved is
 * not null. Collapsing them would accept an illegal instruction word. */
static void test_a_reserved_base_displacement_size_is_flagged(void) {
  /* Full format, BD SIZE = 00. */
  const ap_m68030_extension_t reserved =
      ap_m68030_ea_decode_extension(0x0100u);
  TEST_ASSERT_TRUE(reserved.reserved);

  /* Full format, BD SIZE = 01 (null) is perfectly legal. */
  const ap_m68030_extension_t null_bd =
      ap_m68030_ea_decode_extension(0x0110u);
  TEST_ASSERT_FALSE(null_bd.reserved);
  TEST_ASSERT_EQUAL_INT(AP_M68030_BD_NULL, null_bd.base_displacement_size);
}

/* Table 2-2 in full. The same I/IS value means different things depending on
 * IS, which is why the pair is decoded together. */
static void test_the_memory_indirect_table_decodes_in_full(void) {
  /* IS = 0: 000 none, 001-011 preindexed null/word/long, 100 reserved,
   * 101-111 postindexed null/word/long. */
  const ap_m68030_indirect_t is0[8] = {
      AP_M68030_INDIRECT_NONE,        AP_M68030_INDIRECT_PREINDEXED,
      AP_M68030_INDIRECT_PREINDEXED,  AP_M68030_INDIRECT_PREINDEXED,
      AP_M68030_INDIRECT_RESERVED,    AP_M68030_INDIRECT_POSTINDEXED,
      AP_M68030_INDIRECT_POSTINDEXED, AP_M68030_INDIRECT_POSTINDEXED,
  };
  const ap_m68030_od_size_t is0_od[8] = {
      AP_M68030_OD_NONE, AP_M68030_OD_NULL, AP_M68030_OD_WORD,
      AP_M68030_OD_LONG, AP_M68030_OD_NONE, AP_M68030_OD_NULL,
      AP_M68030_OD_WORD, AP_M68030_OD_LONG,
  };
  for (unsigned i_is = 0; i_is < 8; i_is++) {
    /* Full format, BD SIZE = 01 so only I/IS is under test. */
    const uint16_t word = (uint16_t)(0x0110u | i_is);
    const ap_m68030_extension_t e = ap_m68030_ea_decode_extension(word);
    TEST_ASSERT_EQUAL_INT(is0[i_is], e.indirect);
    if (is0[i_is] != AP_M68030_INDIRECT_RESERVED) {
      TEST_ASSERT_EQUAL_INT(is0_od[i_is], e.outer_displacement_size);
    }
  }

  /* IS = 1: 000 none, 001-011 memory indirect null/word/long, 100-111
   * reserved. Note 001 means something different here than it did above. */
  const ap_m68030_indirect_t is1[8] = {
      AP_M68030_INDIRECT_NONE,     AP_M68030_INDIRECT_MEMORY,
      AP_M68030_INDIRECT_MEMORY,   AP_M68030_INDIRECT_MEMORY,
      AP_M68030_INDIRECT_RESERVED, AP_M68030_INDIRECT_RESERVED,
      AP_M68030_INDIRECT_RESERVED, AP_M68030_INDIRECT_RESERVED,
  };
  for (unsigned i_is = 0; i_is < 8; i_is++) {
    const uint16_t word = (uint16_t)(0x0150u | i_is); /* IS set */
    const ap_m68030_extension_t e = ap_m68030_ea_decode_extension(word);
    TEST_ASSERT_EQUAL_INT(is1[i_is], e.indirect);
  }
}

/* The same I/IS encoding under the two IS values, side by side -- the single
 * comparison that a decoder ignoring IS would fail. */
static void test_the_same_i_is_means_different_things_under_is(void) {
  const ap_m68030_extension_t preindexed =
      ap_m68030_ea_decode_extension(0x0111u); /* IS = 0, I/IS = 001 */
  const ap_m68030_extension_t memory =
      ap_m68030_ea_decode_extension(0x0151u); /* IS = 1, I/IS = 001 */

  TEST_ASSERT_EQUAL_INT(AP_M68030_INDIRECT_PREINDEXED, preindexed.indirect);
  TEST_ASSERT_EQUAL_INT(AP_M68030_INDIRECT_MEMORY, memory.indirect);
}

/* "BASE DISPLACEMENT (0, 1, OR 2 WORDS)" and "OUTER DISPLACEMENT (0, 1, OR 2
 * WORDS)", so an instruction's length is known from the extension word alone. */
static void test_the_following_word_count_covers_both_displacements(void) {
  /* Full, BD = null (01), no memory indirect: no following words. */
  TEST_ASSERT_EQUAL_UINT(0, ap_m68030_ea_extension_words(
                                &(ap_m68030_extension_t){
                                    .full_format = true,
                                    .base_displacement_size = AP_M68030_BD_NULL,
                                    .outer_displacement_size = AP_M68030_OD_NONE}));

  /* BD long (2) plus outer long (2) is the maximum, four words. */
  TEST_ASSERT_EQUAL_UINT(4, ap_m68030_ea_extension_words(
                                &(ap_m68030_extension_t){
                                    .full_format = true,
                                    .base_displacement_size = AP_M68030_BD_LONG,
                                    .outer_displacement_size = AP_M68030_OD_LONG}));

  /* BD word plus outer word is two. */
  TEST_ASSERT_EQUAL_UINT(2, ap_m68030_ea_extension_words(
                                &(ap_m68030_extension_t){
                                    .full_format = true,
                                    .base_displacement_size = AP_M68030_BD_WORD,
                                    .outer_displacement_size = AP_M68030_OD_WORD}));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_register_modes_decode_with_their_register);
  RUN_TEST(test_mode_seven_treats_the_register_field_as_a_sub_opcode);
  RUN_TEST(test_the_unassigned_mode_seven_encodings_are_invalid);
  RUN_TEST(test_only_the_indexed_modes_take_an_extension_word);
  RUN_TEST(test_a_brief_extension_word_carries_a_signed_displacement);
  RUN_TEST(test_the_scale_field_decodes_to_its_factor);
  RUN_TEST(test_the_index_register_type_is_decoded);
  RUN_TEST(test_a_reserved_base_displacement_size_is_flagged);
  RUN_TEST(test_the_memory_indirect_table_decodes_in_full);
  RUN_TEST(test_the_same_i_is_means_different_things_under_is);
  RUN_TEST(test_the_following_word_count_covers_both_displacements);
  return UNITY_END();
}

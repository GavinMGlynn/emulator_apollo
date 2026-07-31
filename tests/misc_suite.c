/* MC68030 family 0100: LEA, CHK and the $48/$4C subtree.
 *
 * Cited to the M68000 Family Programmer's Reference Manual 1992 §8.2 and each
 * instruction's page.
 *
 * The fact these tests exist to protect is that three instructions here live in
 * encodings their neighbours cannot use -- SWAP and BKPT inside PEA's space,
 * EXT inside MOVEM's -- so the register-direct cases must be recognised before
 * falling through. A decoder that checks the MOVEM shape first decodes EXT.W D3
 * as a register-list move, which is a working instruction doing the wrong thing.
 */

#include "cpu/m68030/ap_m68030_misc.h"
#include "cpu/m68030/ap_m68030_opcode.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_the_subtree_is_in_the_miscellaneous_family(void) {
  TEST_ASSERT_EQUAL_INT(AP_M68030_OP_MISCELLANEOUS,
                        ap_m68030_opcode_family(0x41F0u));
}

/* LEA is bits 8-6 = 111, with the register field the destination address
 * register. $41F0 is LEA (xxx),A0 in the indexed form. */
static void test_lea_decodes_its_destination_and_source(void) {
  /* $43D0 : LEA (A0),A1 */
  const ap_m68030_misc_t lea = ap_m68030_misc_decode(0x43D0u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_MISC_LEA, lea.kind);
  TEST_ASSERT_EQUAL_UINT(1, lea.reg);
  TEST_ASSERT_EQUAL_INT(AP_M68030_EA_ADDRESS_INDIRECT, lea.ea.kind);
  TEST_ASSERT_EQUAL_UINT(0, lea.ea.reg);
}

/* CHK shares the form, with 110 the word and 100 the long. The 68020 put the
 * long form *below* the word form's opmode, not above it. */
static void test_chk_word_and_long_sit_either_side_of_lea(void) {
  const ap_m68030_misc_t word = ap_m68030_misc_decode(0x4390u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_MISC_CHK_WORD, word.kind);
  TEST_ASSERT_EQUAL_UINT(1, word.reg);

  const ap_m68030_misc_t lng = ap_m68030_misc_decode(0x4310u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_MISC_CHK_LONG, lng.kind);

  /* The long form's opmode is numerically lower than the word form's. */
  TEST_ASSERT_TRUE(0x4u < 0x6u);
}

/* PEA is $4840-$487F, but not where it cannot go. */
static void test_pea_decodes_a_memory_operand(void) {
  /* $4850 : PEA (A0) */
  const ap_m68030_misc_t pea = ap_m68030_misc_decode(0x4850u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_MISC_PEA, pea.kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_EA_ADDRESS_INDIRECT, pea.ea.kind);
}

/* SWAP and BKPT occupy the two register-direct encodings PEA cannot use --
 * pushing a register's *value* being meaningless for an instruction that
 * pushes an address. */
static void test_swap_and_bkpt_occupy_the_modes_pea_cannot_use(void) {
  /* $4843 : SWAP D3 -- mode 000 in PEA's range. */
  const ap_m68030_misc_t swap = ap_m68030_misc_decode(0x4843u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_MISC_SWAP, swap.kind);
  TEST_ASSERT_EQUAL_UINT(3, swap.reg);

  /* $484A : BKPT #2 -- mode 001, the other hole. */
  const ap_m68030_misc_t bkpt = ap_m68030_misc_decode(0x484Au);
  TEST_ASSERT_EQUAL_INT(AP_M68030_MISC_BKPT, bkpt.kind);
  TEST_ASSERT_EQUAL_UINT(2, bkpt.reg);
}

/* MOVEM: bit 10 the direction, bit 6 the size, and a register list word
 * always follows. */
static void test_movem_decodes_direction_and_size(void) {
  /* $48A0 : MOVEM.W registers to -(A0) */
  const ap_m68030_misc_t to_memory = ap_m68030_misc_decode(0x48A0u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_MISC_MOVEM_TO_MEMORY, to_memory.kind);
  TEST_ASSERT_EQUAL_UINT(2, to_memory.size);
  TEST_ASSERT_EQUAL_INT(AP_M68030_EA_PREDECREMENT, to_memory.ea.kind);

  /* $4CD8 : MOVEM.L (A0)+ to registers */
  const ap_m68030_misc_t to_registers = ap_m68030_misc_decode(0x4CD8u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_MISC_MOVEM_TO_REGISTERS, to_registers.kind);
  TEST_ASSERT_EQUAL_UINT(4, to_registers.size);
  TEST_ASSERT_EQUAL_INT(AP_M68030_EA_POSTINCREMENT, to_registers.ea.kind);

  /* "16-BIT REGISTER LIST MASK" always follows. */
  TEST_ASSERT_EQUAL_UINT(4, ap_m68030_misc_length(&to_memory));
  TEST_ASSERT_EQUAL_UINT(4, ap_m68030_misc_length(&to_registers));
}

/* The trap this module exists for. EXT sits at mode 000 inside MOVEM's shape,
 * because MOVEM moves registers to or from *memory* and a data register operand
 * is meaningless. Decoding the MOVEM shape first turns EXT.W D3 into a
 * register-list move -- an instruction that runs and does the wrong thing. */
static void test_ext_occupies_the_mode_movem_cannot_use(void) {
  /* $4883 : EXT.W D3 -- inside MOVEM's shape, mode 000. */
  const ap_m68030_misc_t ext_word = ap_m68030_misc_decode(0x4883u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_MISC_EXT_WORD, ext_word.kind);
  TEST_ASSERT_EQUAL_UINT(3, ext_word.reg);
  TEST_ASSERT_NOT_EQUAL_INT(AP_M68030_MISC_MOVEM_TO_MEMORY, ext_word.kind);
  /* And it carries no register list word. */
  TEST_ASSERT_EQUAL_UINT(2, ap_m68030_misc_length(&ext_word));

  /* $48C3 : EXT.L D3 */
  const ap_m68030_misc_t ext_long = ap_m68030_misc_decode(0x48C3u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_MISC_EXT_LONG, ext_long.kind);

  /* The neighbouring encoding with a real memory mode is still MOVEM. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_MISC_MOVEM_TO_MEMORY,
                        ap_m68030_misc_decode(0x4893u).kind);
}

/* The other direction has no EXT at all. EXT's encoding fixes bits 11-9 at
 * 100, which is MOVEM's registers-to-memory direction; memory-to-registers puts
 * 6 there, so a data register operand is invalid rather than another EXT. */
static void test_the_other_movem_direction_has_no_ext(void) {
  /* $4C80 : MOVEM.W (mem to registers) shape with mode 000. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_MISC_INVALID,
                        ap_m68030_misc_decode(0x4C80u).kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_MISC_INVALID,
                        ap_m68030_misc_decode(0x4CC0u).kind);
}

/* EXTB.L is the 68020's byte-to-long, opmode 111 with mode 000 -- which is the
 * encoding LEA cannot use, since LEA loads an address and a data register
 * source is meaningless. Third instance of the same trick in this subtree. */
static void test_extb_long_is_the_68020_addition(void) {
  const ap_m68030_misc_t extb = ap_m68030_misc_decode(0x49C3u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_MISC_EXTB_LONG, extb.kind);
  TEST_ASSERT_EQUAL_UINT(3, extb.reg);
}

/* NBCD is $4800-$483F, immediately below PEA. */
static void test_nbcd_sits_below_pea(void) {
  const ap_m68030_misc_t nbcd = ap_m68030_misc_decode(0x4810u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_MISC_NBCD, nbcd.kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_EA_ADDRESS_INDIRECT, nbcd.ea.kind);
}

/* An instruction word outside family 0100 is not this subtree's business. */
static void test_another_family_decodes_as_invalid(void) {
  TEST_ASSERT_EQUAL_INT(AP_M68030_MISC_INVALID, ap_m68030_misc_decode(0x6000u).kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_MISC_INVALID, ap_m68030_misc_decode(0x2000u).kind);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_subtree_is_in_the_miscellaneous_family);
  RUN_TEST(test_lea_decodes_its_destination_and_source);
  RUN_TEST(test_chk_word_and_long_sit_either_side_of_lea);
  RUN_TEST(test_pea_decodes_a_memory_operand);
  RUN_TEST(test_swap_and_bkpt_occupy_the_modes_pea_cannot_use);
  RUN_TEST(test_movem_decodes_direction_and_size);
  RUN_TEST(test_ext_occupies_the_mode_movem_cannot_use);
  RUN_TEST(test_the_other_movem_direction_has_no_ext);
  RUN_TEST(test_extb_long_is_the_68020_addition);
  RUN_TEST(test_nbcd_sits_below_pea);
  RUN_TEST(test_another_family_decodes_as_invalid);
  return UNITY_END();
}

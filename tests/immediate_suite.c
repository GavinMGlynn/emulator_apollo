/* MC68030 family 0000: immediate, bit manipulation and MOVEP.
 *
 * Cited to the M68000 Family Programmer's Reference Manual 1992 §8.2.
 *
 * The strongest instance yet of this encoding's recurring idiom: MOVEP and the
 * dynamic bit operations use *the same four opmodes*, and are told apart only
 * by the effective address mode. The overlap is total rather than partial.
 */

#include "cpu/m68030/ap_m68030_immediate.h"
#include "cpu/m68030/ap_m68030_opcode.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_the_family_is_the_immediate_family(void) {
  TEST_ASSERT_EQUAL_INT(AP_M68030_OP_BIT_MOVEP_IMMEDIATE,
                        ap_m68030_opcode_family(0x0000u));
}

/* Bits 11-9 select the immediate row, at each legal size. EA (A0). */
static void test_the_immediate_rows_decode_at_every_size(void) {
  const unsigned rows[6] = {0x0u, 0x1u, 0x2u, 0x3u, 0x5u, 0x6u};
  const ap_m68030_immediate_kind_t kind[6] = {
      AP_M68030_IMM_ORI,  AP_M68030_IMM_ANDI, AP_M68030_IMM_SUBI,
      AP_M68030_IMM_ADDI, AP_M68030_IMM_EORI, AP_M68030_IMM_CMPI};
  const unsigned sizes[3] = {1, 2, 4};

  for (unsigned r = 0; r < 6; r++) {
    for (unsigned s = 0; s < 3; s++) {
      const uint16_t word = (uint16_t)((rows[r] << 9) | (s << 6) | 0x10u);
      const ap_m68030_immediate_t imm = ap_m68030_immediate_decode(word);
      TEST_ASSERT_EQUAL_INT(kind[r], imm.kind);
      TEST_ASSERT_EQUAL_UINT(sizes[s], imm.size);
    }
  }
}

/* The CCR and SR forms are not separate opcodes: they are the immediate-
 * destination encoding, which is otherwise meaningless, with the size field
 * choosing between byte (CCR) and word (SR). */
static void test_the_ccr_and_sr_forms_reuse_the_immediate_destination(void) {
  TEST_ASSERT_EQUAL_INT(AP_M68030_IMM_ORI_TO_CCR,
                        ap_m68030_immediate_decode(0x003Cu).kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_IMM_ORI_TO_SR,
                        ap_m68030_immediate_decode(0x007Cu).kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_IMM_ANDI_TO_CCR,
                        ap_m68030_immediate_decode(0x023Cu).kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_IMM_ANDI_TO_SR,
                        ap_m68030_immediate_decode(0x027Cu).kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_IMM_EORI_TO_CCR,
                        ap_m68030_immediate_decode(0x0A3Cu).kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_IMM_EORI_TO_SR,
                        ap_m68030_immediate_decode(0x0A7Cu).kind);
}

/* SUBI, ADDI and CMPI have no CCR or SR form, so for them that encoding stays
 * unassigned rather than aliasing onto one. */
static void test_only_three_rows_have_ccr_and_sr_forms(void) {
  TEST_ASSERT_EQUAL_INT(AP_M68030_IMM_INVALID,
                        ap_m68030_immediate_decode(0x043Cu).kind); /* SUBI */
  TEST_ASSERT_EQUAL_INT(AP_M68030_IMM_INVALID,
                        ap_m68030_immediate_decode(0x063Cu).kind); /* ADDI */
  TEST_ASSERT_EQUAL_INT(AP_M68030_IMM_INVALID,
                        ap_m68030_immediate_decode(0x0C3Cu).kind); /* CMPI */
}

/* Static bit operations are row 100, where the size field becomes the operation
 * selector: 00 BTST, 01 BCHG, 10 BCLR, 11 BSET. */
static void test_the_static_bit_operations_use_the_size_field_as_a_selector(void) {
  const ap_m68030_immediate_kind_t kind[4] = {
      AP_M68030_IMM_BTST, AP_M68030_IMM_BCHG, AP_M68030_IMM_BCLR,
      AP_M68030_IMM_BSET};
  for (unsigned s = 0; s < 4; s++) {
    const uint16_t word = (uint16_t)(0x0800u | (s << 6) | 0x10u);
    const ap_m68030_immediate_t imm = ap_m68030_immediate_decode(word);
    TEST_ASSERT_EQUAL_INT(kind[s], imm.kind);
    TEST_ASSERT_FALSE(imm.dynamic);
  }
}

/* Bit 8 set makes the bit number dynamic, taken from the register in bits
 * 11-9, and the opmode becomes the selector instead. */
static void test_bit_eight_makes_the_bit_number_dynamic(void) {
  /* $0710 : BSET D3,(A0) -- register 011, opmode 111. */
  const ap_m68030_immediate_t bset = ap_m68030_immediate_decode(0x07D0u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_IMM_BSET, bset.kind);
  TEST_ASSERT_TRUE(bset.dynamic);
  TEST_ASSERT_EQUAL_UINT(3, bset.reg);

  /* $0110 : BTST D0,(A0) */
  const ap_m68030_immediate_t btst = ap_m68030_immediate_decode(0x0110u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_IMM_BTST, btst.kind);
  TEST_ASSERT_TRUE(btst.dynamic);
}

/* The point of the module. MOVEP uses the same four opmodes as the dynamic bit
 * operations and is separated only by the address register mode, which a bit
 * operation cannot use. The overlap is total: there is no opmode that is MOVEP
 * and not also a bit operation. */
static void test_movep_shares_every_opmode_with_the_bit_operations(void) {
  for (unsigned opmode = 4; opmode < 8; opmode++) {
    /* Mode 001 -- an address register -- is MOVEP. */
    const uint16_t movep_word =
        (uint16_t)(0x0100u | (0x3u << 9) | (opmode << 6) | 0x08u | 0x2u);
    const ap_m68030_immediate_t movep = ap_m68030_immediate_decode(movep_word);
    TEST_ASSERT_EQUAL_INT(AP_M68030_IMM_MOVEP, movep.kind);
    TEST_ASSERT_EQUAL_UINT(3, movep.reg);
    TEST_ASSERT_EQUAL_UINT(2, movep.address_register);

    /* The same opmode with any other mode is a bit operation. */
    const uint16_t bit_word =
        (uint16_t)(0x0100u | (0x3u << 9) | (opmode << 6) | 0x10u);
    TEST_ASSERT_NOT_EQUAL_INT(AP_M68030_IMM_MOVEP,
                              ap_m68030_immediate_decode(bit_word).kind);
  }
}

/* MOVEP's opmode carries both direction and size: 100/101 memory to register,
 * 110/111 register to memory, with the low bit selecting long. */
static void test_movep_opmode_carries_direction_and_size(void) {
  const ap_m68030_immediate_t from_memory_word =
      ap_m68030_immediate_decode(0x0108u); /* opmode 100 */
  TEST_ASSERT_FALSE(from_memory_word.movep_to_memory);
  TEST_ASSERT_EQUAL_UINT(2, from_memory_word.size);

  const ap_m68030_immediate_t to_memory_long =
      ap_m68030_immediate_decode(0x01C8u); /* opmode 111 */
  TEST_ASSERT_TRUE(to_memory_long.movep_to_memory);
  TEST_ASSERT_EQUAL_UINT(4, to_memory_long.size);
}

/* The SR forms and MOVES are privileged; the CCR forms are not. */
static void test_the_sr_forms_and_moves_are_privileged(void) {
  TEST_ASSERT_TRUE(ap_m68030_immediate_privileged(AP_M68030_IMM_ORI_TO_SR));
  TEST_ASSERT_TRUE(ap_m68030_immediate_privileged(AP_M68030_IMM_ANDI_TO_SR));
  TEST_ASSERT_TRUE(ap_m68030_immediate_privileged(AP_M68030_IMM_EORI_TO_SR));
  TEST_ASSERT_TRUE(ap_m68030_immediate_privileged(AP_M68030_IMM_MOVES));

  TEST_ASSERT_FALSE(ap_m68030_immediate_privileged(AP_M68030_IMM_ORI_TO_CCR));
  TEST_ASSERT_FALSE(ap_m68030_immediate_privileged(AP_M68030_IMM_ORI));
  TEST_ASSERT_FALSE(ap_m68030_immediate_privileged(AP_M68030_IMM_BSET));
}

/* Size field 11 in the immediate rows is CMP2/CHK2, CAS and CAS2, which this
 * decoder does not yet cover. Reporting invalid is honest; decoding them as a
 * wider ORI would not be. */
static void test_the_undecoded_size_field_reports_invalid(void) {
  TEST_ASSERT_EQUAL_INT(AP_M68030_IMM_INVALID,
                        ap_m68030_immediate_decode(0x00D0u).kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_IMM_INVALID,
                        ap_m68030_immediate_decode(0x0CD0u).kind);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_family_is_the_immediate_family);
  RUN_TEST(test_the_immediate_rows_decode_at_every_size);
  RUN_TEST(test_the_ccr_and_sr_forms_reuse_the_immediate_destination);
  RUN_TEST(test_only_three_rows_have_ccr_and_sr_forms);
  RUN_TEST(test_the_static_bit_operations_use_the_size_field_as_a_selector);
  RUN_TEST(test_bit_eight_makes_the_bit_number_dynamic);
  RUN_TEST(test_movep_shares_every_opmode_with_the_bit_operations);
  RUN_TEST(test_movep_opmode_carries_direction_and_size);
  RUN_TEST(test_the_sr_forms_and_moves_are_privileged);
  RUN_TEST(test_the_undecoded_size_field_reports_invalid);
  return UNITY_END();
}

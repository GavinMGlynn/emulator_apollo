/* MC68030 family 1110: shifts, rotates and bit field instructions.
 *
 * Cited to the M68000 Family Programmer's Reference Manual 1992 §8.2.
 *
 * The trap under test is that the register and memory shift forms both carry a
 * two-bit type field in the same order, in *different bits* -- 4-3 for the
 * register form, 11-9 for the memory form. Reading one position for both gives
 * a working shift of the wrong kind.
 */

#include "cpu/m68030/ap_m68030_opcode.h"
#include "cpu/m68030/ap_m68030_shift.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_the_family_is_the_shift_family(void) {
  TEST_ASSERT_EQUAL_INT(AP_M68030_OP_SHIFT_ROTATE_BITFIELD,
                        ap_m68030_opcode_family(0xE000u));
}

/* Bits 7-6 other than 11 is a register shift, at byte, word or long. */
static void test_a_register_shift_decodes_its_size_and_direction(void) {
  /* $E308 : LSL.B #1,D0 -- count 1, dr 1 (left), size 00, i/r 0, type 01. */
  const ap_m68030_shift_t lsl = ap_m68030_shift_decode(0xE308u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_SHIFT_REGISTER, lsl.form);
  TEST_ASSERT_EQUAL_INT(AP_M68030_SHIFT_LOGICAL, lsl.type);
  TEST_ASSERT_TRUE(lsl.left);
  TEST_ASSERT_EQUAL_UINT(1, lsl.size);
  TEST_ASSERT_EQUAL_UINT(1, lsl.count);
  TEST_ASSERT_EQUAL_UINT(0, lsl.reg);

  /* $E280 : ASR.L #1,D0 -- dr 0 (right), size 10, type 00. */
  const ap_m68030_shift_t asr = ap_m68030_shift_decode(0xE280u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_SHIFT_ARITHMETIC, asr.type);
  TEST_ASSERT_FALSE(asr.left);
  TEST_ASSERT_EQUAL_UINT(4, asr.size);
}

/* The four types, in the register form, at bits 4-3. */
static void test_the_register_form_reads_its_type_from_bits_four_three(void) {
  const ap_m68030_shift_type_t expected[4] = {
      AP_M68030_SHIFT_ARITHMETIC, AP_M68030_SHIFT_LOGICAL,
      AP_M68030_SHIFT_ROTATE_EXTEND, AP_M68030_SHIFT_ROTATE};
  for (unsigned type = 0; type < 4; type++) {
    const uint16_t word = (uint16_t)(0xE100u | (type << 3));
    TEST_ASSERT_EQUAL_INT(expected[type], ap_m68030_shift_decode(word).type);
  }
}

/* Size field 11 is not a size: with bit 11 clear it selects the memory form,
 * which shifts one word in memory by exactly one. */
static void test_the_illegal_size_selects_the_memory_form(void) {
  /* $E0D0 : ASR (A0) -- type 000 at bits 11-9, dr 0. */
  const ap_m68030_shift_t memory = ap_m68030_shift_decode(0xE0D0u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_SHIFT_MEMORY, memory.form);
  TEST_ASSERT_EQUAL_INT(AP_M68030_SHIFT_ARITHMETIC, memory.type);
  TEST_ASSERT_EQUAL_UINT(2, memory.size);
  TEST_ASSERT_EQUAL_UINT(1, memory.count);
  TEST_ASSERT_EQUAL_INT(AP_M68030_EA_ADDRESS_INDIRECT, memory.ea.kind);
}

/* The trap. The memory form reads its type from bits 11-9 -- where the register
 * form keeps its shift count -- so the same two-bit value sits in a different
 * place in each. */
static void test_the_memory_form_reads_its_type_from_bits_eleven_nine(void) {
  const ap_m68030_shift_type_t expected[4] = {
      AP_M68030_SHIFT_ARITHMETIC, AP_M68030_SHIFT_LOGICAL,
      AP_M68030_SHIFT_ROTATE_EXTEND, AP_M68030_SHIFT_ROTATE};
  for (unsigned type = 0; type < 4; type++) {
    const uint16_t word = (uint16_t)(0xE0C0u | (type << 9) | 0x10u);
    const ap_m68030_shift_t shift = ap_m68030_shift_decode(word);
    TEST_ASSERT_EQUAL_INT(AP_M68030_SHIFT_MEMORY, shift.form);
    TEST_ASSERT_EQUAL_INT(expected[type], shift.type);
  }

  /* And the same bits in a register shift are the count, not the type: $E700
   * has 011 at bits 11-9 and is still an arithmetic shift. */
  const ap_m68030_shift_t register_shift = ap_m68030_shift_decode(0xE700u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_SHIFT_REGISTER, register_shift.form);
  TEST_ASSERT_EQUAL_INT(AP_M68030_SHIFT_ARITHMETIC, register_shift.type);
  TEST_ASSERT_EQUAL_UINT(3, register_shift.count);
}

/* "1-7 represent counts of 1-7; 0 represents a count of 8" -- the same quirk as
 * ADDQ's quick data, and the same consequence if passed through: a shift by 8
 * silently becomes a shift by 0. */
static void test_the_immediate_count_zero_means_eight(void) {
  /* Count field 000, i/r 0. */
  const ap_m68030_shift_t eight = ap_m68030_shift_decode(0xE108u);
  TEST_ASSERT_FALSE(eight.count_in_register);
  TEST_ASSERT_EQUAL_UINT(8, eight.count);
  TEST_ASSERT_NOT_EQUAL_UINT(0, eight.count);

  for (unsigned n = 1; n < 8; n++) {
    const uint16_t word = (uint16_t)(0xE108u | (n << 9));
    TEST_ASSERT_EQUAL_UINT(n, ap_m68030_shift_decode(word).count);
  }
}

/* With i/r set the field is a register number, where zero means register 0 --
 * so the substitution must not happen. */
static void test_a_register_count_of_zero_is_register_zero(void) {
  const ap_m68030_shift_t from_register = ap_m68030_shift_decode(0xE128u);
  TEST_ASSERT_TRUE(from_register.count_in_register);
  TEST_ASSERT_EQUAL_UINT(0, from_register.count);
}

/* Size field 11 with bit 11 *set* is the bit field group, whose eight members
 * are named by bits 11-8. */
static void test_the_bit_field_instructions_decode_from_bits_eleven_eight(void) {
  const ap_m68030_bitfield_t expected[8] = {
      AP_M68030_BF_TST, AP_M68030_BF_EXTU, AP_M68030_BF_CHG, AP_M68030_BF_EXTS,
      AP_M68030_BF_CLR, AP_M68030_BF_FFO,  AP_M68030_BF_SET, AP_M68030_BF_INS};
  for (unsigned i = 0; i < 8; i++) {
    const uint16_t word = (uint16_t)(0xE8C0u | (i << 8) | 0x10u);
    const ap_m68030_shift_t bf = ap_m68030_shift_decode(word);
    TEST_ASSERT_EQUAL_INT(AP_M68030_SHIFT_BITFIELD, bf.form);
    TEST_ASSERT_EQUAL_INT(expected[i], bf.bitfield);
    /* Each carries an offset-and-width extension word. */
    TEST_ASSERT_EQUAL_UINT(4, ap_m68030_shift_length(&bf));
  }
}

/* A memory shift carries no extension word, which is what distinguishes its
 * length from a bit field instruction's at the same size field. */
static void test_only_the_bit_field_forms_carry_an_extension_word(void) {
  const ap_m68030_shift_t memory = ap_m68030_shift_decode(0xE0D0u);
  const ap_m68030_shift_t register_shift = ap_m68030_shift_decode(0xE108u);
  TEST_ASSERT_EQUAL_UINT(2, ap_m68030_shift_length(&memory));
  TEST_ASSERT_EQUAL_UINT(2, ap_m68030_shift_length(&register_shift));
}

/* The memory and bit field shapes partition the size-field-11 space exactly,
 * with bit 11 the divider -- so there is no unassigned memory shift type to
 * reject. Every word with bits 7-6 set is one shape or the other, which is
 * asserted by walking the whole of bits 11-8. */
static void test_the_two_wide_shapes_partition_the_space(void) {
  for (unsigned high = 0; high < 16; high++) {
    const uint16_t word = (uint16_t)(0xE0C0u | (high << 8) | 0x10u);
    const ap_m68030_shift_t shift = ap_m68030_shift_decode(word);
    const ap_m68030_shift_form_t expected = (high & 0x8u)
                                                ? AP_M68030_SHIFT_BITFIELD
                                                : AP_M68030_SHIFT_MEMORY;
    TEST_ASSERT_EQUAL_INT(expected, shift.form);
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_family_is_the_shift_family);
  RUN_TEST(test_a_register_shift_decodes_its_size_and_direction);
  RUN_TEST(test_the_register_form_reads_its_type_from_bits_four_three);
  RUN_TEST(test_the_illegal_size_selects_the_memory_form);
  RUN_TEST(test_the_memory_form_reads_its_type_from_bits_eleven_nine);
  RUN_TEST(test_the_immediate_count_zero_means_eight);
  RUN_TEST(test_a_register_count_of_zero_is_register_zero);
  RUN_TEST(test_the_bit_field_instructions_decode_from_bits_eleven_eight);
  RUN_TEST(test_only_the_bit_field_forms_carry_an_extension_word);
  RUN_TEST(test_the_two_wide_shapes_partition_the_space);
  return UNITY_END();
}

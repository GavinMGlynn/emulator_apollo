/* MC68030 arithmetic and logic families 1000, 1001, 1011, 1100 and 1101.
 *
 * Cited to the M68000 Family Programmer's Reference Manual 1992 §8.2.
 *
 * Two facts under test. Opmode 011 and 111 occupy the same *position* in all
 * five families but mean different instructions -- a word DIVU in one, a word
 * SUBA in another. And the memory-destination opmodes leave register-direct
 * holes that each family fills differently.
 */

#include "cpu/m68030/ap_m68030_arith.h"
#include "cpu/m68030/ap_m68030_opcode.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Build an instruction: family, register, opmode, and EA (A0) unless given. */
static uint16_t word_of(unsigned family, unsigned reg, unsigned opmode,
                        unsigned mode, unsigned ea_reg) {
  return (uint16_t)((family << 12) | (reg << 9) | (opmode << 6) | (mode << 3) |
                    ea_reg);
}

/* The register direction, opmodes 000-010, at each size in every family. */
static void test_the_register_direction_decodes_in_every_family(void) {
  const unsigned families[5] = {0x8u, 0x9u, 0xBu, 0xCu, 0xDu};
  const ap_m68030_arith_kind_t kind[5] = {
      AP_M68030_ARITH_OR, AP_M68030_ARITH_SUB, AP_M68030_ARITH_CMP,
      AP_M68030_ARITH_AND, AP_M68030_ARITH_ADD};
  const unsigned sizes[3] = {1, 2, 4};

  for (unsigned f = 0; f < 5; f++) {
    for (unsigned opmode = 0; opmode < 3; opmode++) {
      const ap_m68030_arith_t a =
          ap_m68030_arith_decode(word_of(families[f], 1, opmode, 2, 0));
      TEST_ASSERT_EQUAL_INT(kind[f], a.kind);
      TEST_ASSERT_EQUAL_UINT(sizes[opmode], a.size);
      TEST_ASSERT_FALSE(a.to_effective_address);
    }
  }
}

/* The same opmode position, five different meanings. Opmode 011 is a word-wide
 * form in every family, but which instruction depends entirely on the family. */
static void test_the_wide_opmodes_mean_different_things_per_family(void) {
  TEST_ASSERT_EQUAL_INT(AP_M68030_ARITH_DIVU,
                        ap_m68030_arith_decode(word_of(0x8u, 1, 3, 2, 0)).kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_ARITH_MULU,
                        ap_m68030_arith_decode(word_of(0xCu, 1, 3, 2, 0)).kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_ARITH_SUBA,
                        ap_m68030_arith_decode(word_of(0x9u, 1, 3, 2, 0)).kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_ARITH_CMPA,
                        ap_m68030_arith_decode(word_of(0xBu, 1, 3, 2, 0)).kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_ARITH_ADDA,
                        ap_m68030_arith_decode(word_of(0xDu, 1, 3, 2, 0)).kind);

  /* Opmode 111 is the long counterpart of each. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_ARITH_DIVS,
                        ap_m68030_arith_decode(word_of(0x8u, 1, 7, 2, 0)).kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_ARITH_MULS,
                        ap_m68030_arith_decode(word_of(0xCu, 1, 7, 2, 0)).kind);
}

/* The A-forms take their size from the opmode; the divides and multiplies
 * always take a word operand whichever opmode they use. */
static void test_the_a_forms_are_sized_and_the_divides_are_not(void) {
  TEST_ASSERT_EQUAL_UINT(
      2, ap_m68030_arith_decode(word_of(0xDu, 1, 3, 2, 0)).size); /* ADDA.W */
  TEST_ASSERT_EQUAL_UINT(
      4, ap_m68030_arith_decode(word_of(0xDu, 1, 7, 2, 0)).size); /* ADDA.L */

  /* DIVU and DIVS both divide a long by a *word*. */
  TEST_ASSERT_EQUAL_UINT(
      2, ap_m68030_arith_decode(word_of(0x8u, 1, 3, 2, 0)).size);
  TEST_ASSERT_EQUAL_UINT(
      2, ap_m68030_arith_decode(word_of(0x8u, 1, 7, 2, 0)).size);
}

/* CMP and EOR share family 1011 without overlapping: CMP has the register
 * direction, EOR the memory one. There is no <ea> EOR Dn -> Dn form. */
static void test_cmp_and_eor_split_family_1011_by_direction(void) {
  TEST_ASSERT_EQUAL_INT(AP_M68030_ARITH_CMP,
                        ap_m68030_arith_decode(word_of(0xBu, 1, 1, 2, 0)).kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_ARITH_EOR,
                        ap_m68030_arith_decode(word_of(0xBu, 1, 5, 2, 0)).kind);

  /* Stated as the absence it is: no opmode below 100 in this family is EOR. */
  for (unsigned opmode = 0; opmode < 3; opmode++) {
    TEST_ASSERT_NOT_EQUAL_INT(
        AP_M68030_ARITH_EOR,
        ap_m68030_arith_decode(word_of(0xBu, 1, opmode, 2, 0)).kind);
  }
}

/* The memory-destination opmodes leave register-direct holes, and each family
 * fills them differently. This is the same idiom as SWAP inside PEA. */
static void test_each_family_fills_the_register_hole_differently(void) {
  /* 1001 opmode 101, mode 000 : SUBX.W Dy,Dx */
  TEST_ASSERT_EQUAL_INT(AP_M68030_ARITH_SUBX,
                        ap_m68030_arith_decode(word_of(0x9u, 1, 5, 0, 2)).kind);
  /* 1101 opmode 101, mode 000 : ADDX.W */
  TEST_ASSERT_EQUAL_INT(AP_M68030_ARITH_ADDX,
                        ap_m68030_arith_decode(word_of(0xDu, 1, 5, 0, 2)).kind);
  /* 1100 opmode 100, mode 000 : ABCD */
  TEST_ASSERT_EQUAL_INT(AP_M68030_ARITH_ABCD,
                        ap_m68030_arith_decode(word_of(0xCu, 1, 4, 0, 2)).kind);
  /* 1000 opmode 100, mode 000 : SBCD */
  TEST_ASSERT_EQUAL_INT(AP_M68030_ARITH_SBCD,
                        ap_m68030_arith_decode(word_of(0x8u, 1, 4, 0, 2)).kind);
  /* 1011 opmode 101, mode 001 : CMPM.W (Ay)+,(Ax)+ */
  TEST_ASSERT_EQUAL_INT(AP_M68030_ARITH_CMPM,
                        ap_m68030_arith_decode(word_of(0xBu, 1, 5, 1, 2)).kind);
  /* 1100 opmode 101, mode 000 : EXG Dx,Dy */
  TEST_ASSERT_EQUAL_INT(AP_M68030_ARITH_EXG,
                        ap_m68030_arith_decode(word_of(0xCu, 1, 5, 0, 2)).kind);
}

/* The same opmode with a real memory mode is the ordinary instruction, which is
 * what makes the holes holes rather than special cases. */
static void test_a_memory_mode_gives_the_ordinary_instruction(void) {
  /* 1001 opmode 101 with (A0) is SUB, not SUBX. */
  const ap_m68030_arith_t sub =
      ap_m68030_arith_decode(word_of(0x9u, 1, 5, 2, 0));
  TEST_ASSERT_EQUAL_INT(AP_M68030_ARITH_SUB, sub.kind);
  TEST_ASSERT_TRUE(sub.to_effective_address);

  /* 1100 opmode 100 with (A0) is AND, not ABCD. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_ARITH_AND,
                        ap_m68030_arith_decode(word_of(0xCu, 1, 4, 2, 0)).kind);
}

/* CMPM is postincrement only -- and what mode 000 falls back to is **EOR**,
 * not nothing.
 *
 * This test asserted `INVALID` on the reasoning that "CMP has no
 * memory-destination form to fall back on". The fallback is not `CMP`, it is
 * `EOR`: four of the five families in this direction say their destination must
 * be *memory* alterable, which is what leaves the register-destination hole for
 * `SBCD`, `SUBX`, `ADDX` and `ABCD` -- but `EOR`'s page says **data** alterable,
 * so `EOR Dn,Dn` is an ordinary instruction and a common one. Treating all five
 * alike made it illegal here. */
static void test_cmpm_is_postincrement_only_and_mode_zero_is_eor(void) {
  TEST_ASSERT_EQUAL_INT(AP_M68030_ARITH_CMPM,
                        ap_m68030_arith_decode(word_of(0xBu, 1, 5, 1, 2)).kind);
  const ap_m68030_arith_t eor = ap_m68030_arith_decode(word_of(0xBu, 1, 5, 0, 2));
  TEST_ASSERT_EQUAL_INT(AP_M68030_ARITH_EOR, eor.kind);
  TEST_ASSERT_TRUE(eor.to_effective_address);
}

/* The R/M bit distinguishes the register-register and memory-memory forms of
 * the extended arithmetic. */
static void test_the_rm_bit_selects_memory_operands(void) {
  const ap_m68030_arith_t registers =
      ap_m68030_arith_decode(word_of(0x9u, 1, 5, 0, 2));
  const ap_m68030_arith_t memory =
      ap_m68030_arith_decode(word_of(0x9u, 1, 5, 1, 2));

  TEST_ASSERT_EQUAL_INT(AP_M68030_ARITH_SUBX, registers.kind);
  TEST_ASSERT_FALSE(registers.memory_operands);
  TEST_ASSERT_EQUAL_INT(AP_M68030_ARITH_SUBX, memory.kind);
  TEST_ASSERT_TRUE(memory.memory_operands);
}

/* Family 1010 is the Line A trap and is not an arithmetic family at all. */
static void test_family_1010_is_not_arithmetic(void) {
  TEST_ASSERT_EQUAL_INT(AP_M68030_ARITH_INVALID,
                        ap_m68030_arith_decode(0xA000u).kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_OP_LINE_A, ap_m68030_opcode_family(0xA000u));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_register_direction_decodes_in_every_family);
  RUN_TEST(test_the_wide_opmodes_mean_different_things_per_family);
  RUN_TEST(test_the_a_forms_are_sized_and_the_divides_are_not);
  RUN_TEST(test_cmp_and_eor_split_family_1011_by_direction);
  RUN_TEST(test_each_family_fills_the_register_hole_differently);
  RUN_TEST(test_a_memory_mode_gives_the_ordinary_instruction);
  RUN_TEST(test_cmpm_is_postincrement_only_and_mode_zero_is_eor);
  RUN_TEST(test_the_rm_bit_selects_memory_operands);
  RUN_TEST(test_family_1010_is_not_arithmetic);
  return UNITY_END();
}

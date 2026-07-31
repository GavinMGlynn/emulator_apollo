/* MC68030 branch family: Bcc, BSR and BRA.
 *
 * Cited to the M68000 Family Programmer's Reference Manual 1992 §8.2 and the
 * Bcc/BRA/BSR instruction pages.
 *
 * The fact these tests exist to protect is that the branch base and the BSR
 * return address are *different addresses* for the 16- and 32-bit forms. The
 * base is the instruction address plus two whatever the size; the return
 * address is the whole instruction length away.
 */

#include "cpu/m68030/ap_m68030_branch.h"
#include "cpu/m68030/ap_m68030_opcode.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define AT 0x00001000u

/* Every branch is family 0110 of the operation code map. */
static void test_the_family_is_the_branch_family(void) {
  TEST_ASSERT_EQUAL_INT(AP_M68030_OP_BCC_BSR_BRA,
                        ap_m68030_opcode_family(0x6000u));
  TEST_ASSERT_EQUAL_INT(AP_M68030_OP_BCC_BSR_BRA,
                        ap_m68030_opcode_family(0x6FFFu));
}

/* Condition 0 is BRA and condition 1 is BSR -- the encodings Table 3-19 marks
 * "*Not available for the Bcc instruction". */
static void test_conditions_zero_and_one_are_bra_and_bsr(void) {
  const ap_m68030_branch_t bra = ap_m68030_branch_decode(0x6010u);
  TEST_ASSERT_TRUE(bra.is_bra);
  TEST_ASSERT_FALSE(bra.is_bsr);

  const ap_m68030_branch_t bsr = ap_m68030_branch_decode(0x6110u);
  TEST_ASSERT_TRUE(bsr.is_bsr);
  TEST_ASSERT_FALSE(bsr.is_bra);

  /* Everything else is a real condition and neither. */
  const ap_m68030_branch_t beq = ap_m68030_branch_decode(0x6710u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_COND_EQ, beq.condition);
  TEST_ASSERT_FALSE(beq.is_bra);
  TEST_ASSERT_FALSE(beq.is_bsr);
}

/* "16-BIT DISPLACEMENT IF 8-BIT DISPLACEMENT = $00" and "32-BIT ... IF ... =
 * $FF", so those two byte values are escapes rather than displacements. */
static void test_the_two_escape_values_select_the_wider_displacements(void) {
  TEST_ASSERT_EQUAL_INT(AP_M68030_BRANCH_16BIT,
                        ap_m68030_branch_decode(0x6700u).size);
  TEST_ASSERT_EQUAL_INT(AP_M68030_BRANCH_32BIT,
                        ap_m68030_branch_decode(0x67FFu).size);
  /* Anything else is the 8-bit form and carries its own signed value. */
  const ap_m68030_branch_t eight = ap_m68030_branch_decode(0x67FEu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_BRANCH_8BIT, eight.size);
  TEST_ASSERT_EQUAL_INT8(-2, eight.displacement8);
}

/* Instruction length follows the displacement size. */
static void test_the_instruction_length_follows_the_displacement_size(void) {
  const ap_m68030_branch_t eight = ap_m68030_branch_decode(0x6710u);
  const ap_m68030_branch_t sixteen = ap_m68030_branch_decode(0x6700u);
  const ap_m68030_branch_t thirtytwo = ap_m68030_branch_decode(0x67FFu);

  TEST_ASSERT_EQUAL_UINT(2, ap_m68030_branch_length(&eight));
  TEST_ASSERT_EQUAL_UINT(4, ap_m68030_branch_length(&sixteen));
  TEST_ASSERT_EQUAL_UINT(6, ap_m68030_branch_length(&thirtytwo));
}

/* "PC + dn -> PC" with PC the instruction address plus two -- the same base for
 * every displacement size, which is what makes it independent of the length. */
static void test_the_branch_base_is_the_instruction_address_plus_two(void) {
  TEST_ASSERT_EQUAL_HEX32(AT + 2u, ap_m68030_branch_target(AT, 0));
  TEST_ASSERT_EQUAL_HEX32(AT + 2u + 0x10u, ap_m68030_branch_target(AT, 0x10));
  TEST_ASSERT_EQUAL_HEX32(AT + 2u - 0x10u, ap_m68030_branch_target(AT, -0x10));
}

/* The point of the module. For the 8-bit form the branch base and the return
 * address coincide; for the wider forms they do not, and computing the return
 * address as the base gives a BSR that returns into its own displacement. */
static void test_the_return_address_is_not_the_branch_base(void) {
  const ap_m68030_branch_t eight = ap_m68030_branch_decode(0x6110u);
  const ap_m68030_branch_t sixteen = ap_m68030_branch_decode(0x6100u);
  const ap_m68030_branch_t thirtytwo = ap_m68030_branch_decode(0x61FFu);

  /* 8-bit: they agree, which is why the mistake survives casual testing. */
  TEST_ASSERT_EQUAL_HEX32(ap_m68030_branch_target(AT, 0),
                          ap_m68030_branch_return_address(AT, &eight));

  /* 16-bit: the return address is two bytes past the base. */
  TEST_ASSERT_EQUAL_HEX32(AT + 4u,
                          ap_m68030_branch_return_address(AT, &sixteen));
  TEST_ASSERT_NOT_EQUAL_UINT32(ap_m68030_branch_target(AT, 0),
                               ap_m68030_branch_return_address(AT, &sixteen));

  /* 32-bit: four bytes past. */
  TEST_ASSERT_EQUAL_HEX32(AT + 6u,
                          ap_m68030_branch_return_address(AT, &thirtytwo));
}

/* The manual's own NOTE: "A branch to the immediately following instruction
 * automatically uses the 16-bit displacement format because the 8-bit
 * displacement field contains $00 (zero offset)." A displacement of zero from
 * the base is the next instruction only for the two-byte form -- which is
 * exactly the case $00 cannot encode. */
static void test_a_branch_to_the_next_instruction_needs_the_wider_form(void) {
  const ap_m68030_branch_t sixteen = ap_m68030_branch_decode(0x6700u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_BRANCH_16BIT, sixteen.size);

  /* With the 16-bit form the instruction is four bytes, so reaching the next
   * instruction takes a displacement of 2, not 0. */
  TEST_ASSERT_EQUAL_HEX32(AT + 4u, ap_m68030_branch_target(AT, 2));
  TEST_ASSERT_EQUAL_HEX32(AT + 4u,
                          ap_m68030_branch_return_address(AT, &sixteen));
}

/* The PC is 32 bits and a branch past either end wraps on the real part, so the
 * model must wrap rather than saturate or trap. */
static void test_a_branch_past_the_end_of_the_address_space_wraps(void) {
  TEST_ASSERT_EQUAL_HEX32(0x00000000u,
                          ap_m68030_branch_target(0xFFFFFFFEu, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFEu, ap_m68030_branch_target(0u, -4));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_family_is_the_branch_family);
  RUN_TEST(test_conditions_zero_and_one_are_bra_and_bsr);
  RUN_TEST(test_the_two_escape_values_select_the_wider_displacements);
  RUN_TEST(test_the_instruction_length_follows_the_displacement_size);
  RUN_TEST(test_the_branch_base_is_the_instruction_address_plus_two);
  RUN_TEST(test_the_return_address_is_not_the_branch_base);
  RUN_TEST(test_a_branch_to_the_next_instruction_needs_the_wider_form);
  RUN_TEST(test_a_branch_past_the_end_of_the_address_space_wraps);
  return UNITY_END();
}

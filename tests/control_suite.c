/* MC68030 family 0100, the $4E control group.
 *
 * Cited to the M68000 Family Programmer's Reference Manual 1992 §8.2 and each
 * instruction's page.
 *
 * The facts these tests protect: the $4E7x singles decode to exactly the
 * instructions the manual assigns and nothing else, TRAP's four-bit field is an
 * index into vectors 32-47 rather than a vector number, and four of these are
 * privileged -- a class whose failure mode is silent, since getting it wrong
 * lets a user program halt the processor.
 */

#include "cpu/m68030/ap_m68030_control.h"
#include "cpu/m68030/ap_m68030_exception.h"
#include "cpu/m68030/ap_m68030_opcode.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* The group lives in family 0100 of the operation code map. */
static void test_the_group_is_in_the_miscellaneous_family(void) {
  TEST_ASSERT_EQUAL_INT(AP_M68030_OP_MISCELLANEOUS,
                        ap_m68030_opcode_family(0x4E71u));
  TEST_ASSERT_TRUE(ap_m68030_control_matches(0x4E71u));
  /* A neighbouring family 0100 instruction is not in this subtree. */
  TEST_ASSERT_FALSE(ap_m68030_control_matches(0x4A00u));
}

/* $4E70-$4E77, each a distinct instruction, checked as the whole run so a
 * transposition inside it cannot pass. */
static void test_the_fully_decoded_singles_map_one_for_one(void) {
  const ap_m68030_control_kind_t expected[8] = {
      AP_M68030_CTL_RESET, AP_M68030_CTL_NOP,   AP_M68030_CTL_STOP,
      AP_M68030_CTL_RTE,   AP_M68030_CTL_RTD,   AP_M68030_CTL_RTS,
      AP_M68030_CTL_TRAPV, AP_M68030_CTL_RTR,
  };
  for (unsigned i = 0; i < 8; i++) {
    const uint16_t word = (uint16_t)(0x4E70u + i);
    TEST_ASSERT_EQUAL_INT(expected[i], ap_m68030_control_decode(word).kind);
  }
}

/* TRAP occupies $4E40-$4E4F, its low four bits the trap number. */
static void test_trap_takes_its_number_from_the_low_four_bits(void) {
  for (unsigned n = 0; n < 16; n++) {
    const ap_m68030_control_t trap =
        ap_m68030_control_decode((uint16_t)(0x4E40u + n));
    TEST_ASSERT_EQUAL_INT(AP_M68030_CTL_TRAP, trap.kind);
    TEST_ASSERT_EQUAL_UINT(n, trap.vector);
  }
}

/* Table 8-1 puts TRAP #0-15 at vectors 32-47, so the field is an *index* into
 * that range. Returning the field itself would send TRAP #0 to the reset
 * vector. The vector comes from the exception module, so the two agree by
 * construction. */
static void test_the_trap_field_is_an_index_not_a_vector_number(void) {
  const ap_m68030_control_t trap0 = ap_m68030_control_decode(0x4E40u);
  const ap_m68030_control_t trap15 = ap_m68030_control_decode(0x4E4Fu);

  TEST_ASSERT_EQUAL_UINT(32, ap_m68030_control_trap_vector(&trap0));
  TEST_ASSERT_EQUAL_UINT(47, ap_m68030_control_trap_vector(&trap15));
  TEST_ASSERT_EQUAL_UINT(ap_m68030_trap_vector(0),
                         ap_m68030_control_trap_vector(&trap0));
  /* And emphatically not the raw field. */
  TEST_ASSERT_NOT_EQUAL_UINT(trap0.vector,
                             ap_m68030_control_trap_vector(&trap0));
}

/* LINK is $4E50-$4E57 and UNLK $4E58-$4E5F, split by bit 3. */
static void test_link_and_unlk_split_on_bit_three(void) {
  const ap_m68030_control_t link = ap_m68030_control_decode(0x4E52u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_CTL_LINK, link.kind);
  TEST_ASSERT_EQUAL_UINT(2, link.reg);

  const ap_m68030_control_t unlk = ap_m68030_control_decode(0x4E5Au);
  TEST_ASSERT_EQUAL_INT(AP_M68030_CTL_UNLK, unlk.kind);
  TEST_ASSERT_EQUAL_UINT(2, unlk.reg);
}

/* MOVE USP is $4E60-$4E6F with bit 3 the direction. */
static void test_move_usp_decodes_its_direction(void) {
  const ap_m68030_control_t to_usp = ap_m68030_control_decode(0x4E61u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_CTL_MOVE_TO_USP, to_usp.kind);
  TEST_ASSERT_EQUAL_UINT(1, to_usp.reg);

  const ap_m68030_control_t from_usp = ap_m68030_control_decode(0x4E69u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_CTL_MOVE_FROM_USP, from_usp.kind);
  TEST_ASSERT_EQUAL_UINT(1, from_usp.reg);
}

/* Bits 7-6 select JSR (10) and JMP (11), each with a six-bit effective
 * address -- so these two cover half the subtree between them. */
static void test_jsr_and_jmp_carry_an_effective_address(void) {
  /* $4E90 : JSR (A0) */
  const ap_m68030_control_t jsr = ap_m68030_control_decode(0x4E90u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_CTL_JSR, jsr.kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_EA_ADDRESS_INDIRECT, jsr.ea.kind);
  TEST_ASSERT_EQUAL_UINT(0, jsr.ea.reg);

  /* $4ED2 : JMP (A2) */
  const ap_m68030_control_t jmp = ap_m68030_control_decode(0x4ED2u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_CTL_JMP, jmp.kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_EA_ADDRESS_INDIRECT, jmp.ea.kind);
  TEST_ASSERT_EQUAL_UINT(2, jmp.ea.reg);

  /* $4EF9 : JMP (xxx).L */
  TEST_ASSERT_EQUAL_INT(AP_M68030_EA_ABSOLUTE_LONG,
                        ap_m68030_control_decode(0x4EF9u).ea.kind);
}

/* LINK.W, RTD and STOP each carry one following word; nothing else here does. */
static void test_only_three_of_these_carry_a_following_word(void) {
  const uint16_t four_byte[3] = {0x4E52u /* LINK */, 0x4E74u /* RTD */,
                                 0x4E72u /* STOP */};
  for (unsigned i = 0; i < 3; i++) {
    const ap_m68030_control_t control = ap_m68030_control_decode(four_byte[i]);
    TEST_ASSERT_EQUAL_UINT(4, ap_m68030_control_length(&control));
  }

  const uint16_t two_byte[4] = {0x4E71u /* NOP */, 0x4E75u /* RTS */,
                                0x4E73u /* RTE */, 0x4E40u /* TRAP */};
  for (unsigned i = 0; i < 4; i++) {
    const ap_m68030_control_t control = ap_m68030_control_decode(two_byte[i]);
    TEST_ASSERT_EQUAL_UINT(2, ap_m68030_control_length(&control));
  }
}

/* RESET, STOP, RTE and both directions of MOVE USP are supervisor-only. The
 * failure mode is silent rather than loud: a user program that could execute
 * these could halt the processor or forge a return from exception. */
static void test_the_privileged_instructions_are_exactly_these(void) {
  TEST_ASSERT_TRUE(ap_m68030_control_privileged(AP_M68030_CTL_RESET));
  TEST_ASSERT_TRUE(ap_m68030_control_privileged(AP_M68030_CTL_STOP));
  TEST_ASSERT_TRUE(ap_m68030_control_privileged(AP_M68030_CTL_RTE));
  TEST_ASSERT_TRUE(ap_m68030_control_privileged(AP_M68030_CTL_MOVE_TO_USP));
  TEST_ASSERT_TRUE(ap_m68030_control_privileged(AP_M68030_CTL_MOVE_FROM_USP));

  /* The returns a user program legitimately uses are not privileged, which is
   * the distinction worth stating: RTS and RTR are ordinary, RTE is not. */
  TEST_ASSERT_FALSE(ap_m68030_control_privileged(AP_M68030_CTL_RTS));
  TEST_ASSERT_FALSE(ap_m68030_control_privileged(AP_M68030_CTL_RTR));
  TEST_ASSERT_FALSE(ap_m68030_control_privileged(AP_M68030_CTL_RTD));
  TEST_ASSERT_FALSE(ap_m68030_control_privileged(AP_M68030_CTL_NOP));
  TEST_ASSERT_FALSE(ap_m68030_control_privileged(AP_M68030_CTL_TRAP));
  TEST_ASSERT_FALSE(ap_m68030_control_privileged(AP_M68030_CTL_JSR));
  TEST_ASSERT_FALSE(ap_m68030_control_privileged(AP_M68030_CTL_JMP));
}

/* $4E7A and $4E7B are MOVEC, one per direction. An earlier version of this
 * decoder treated the whole $4E78-$4E7F run as unassigned, which would have
 * made every MOVEC illegal -- and with it VBR, CACR and the MMU root pointers
 * unreachable, since MOVEC is the only way to load them. */
static void test_movec_occupies_two_of_the_high_singles(void) {
  TEST_ASSERT_EQUAL_INT(AP_M68030_CTL_MOVEC_FROM_CONTROL,
                        ap_m68030_control_decode(0x4E7Au).kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_CTL_MOVEC_TO_CONTROL,
                        ap_m68030_control_decode(0x4E7Bu).kind);

  /* It carries an extension word naming the general and control registers. */
  const ap_m68030_control_t movec = ap_m68030_control_decode(0x4E7Bu);
  TEST_ASSERT_EQUAL_UINT(4, ap_m68030_control_length(&movec));

  /* And it is privileged, which is the point: a user program that could reach
   * CACR or the root pointers could disable the MMU. */
  TEST_ASSERT_TRUE(
      ap_m68030_control_privileged(AP_M68030_CTL_MOVEC_TO_CONTROL));
}

/* The rest of $4E78-$4E7F really is unassigned. */
static void test_the_remaining_high_singles_are_invalid(void) {
  const unsigned unassigned[6] = {0x78u, 0x79u, 0x7Cu, 0x7Du, 0x7Eu, 0x7Fu};
  for (unsigned i = 0; i < 6; i++) {
    TEST_ASSERT_EQUAL_INT(
        AP_M68030_CTL_INVALID,
        ap_m68030_control_decode((uint16_t)(0x4E00u + unassigned[i])).kind);
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_group_is_in_the_miscellaneous_family);
  RUN_TEST(test_the_fully_decoded_singles_map_one_for_one);
  RUN_TEST(test_trap_takes_its_number_from_the_low_four_bits);
  RUN_TEST(test_the_trap_field_is_an_index_not_a_vector_number);
  RUN_TEST(test_link_and_unlk_split_on_bit_three);
  RUN_TEST(test_move_usp_decodes_its_direction);
  RUN_TEST(test_jsr_and_jmp_carry_an_effective_address);
  RUN_TEST(test_only_three_of_these_carry_a_following_word);
  RUN_TEST(test_the_privileged_instructions_are_exactly_these);
  RUN_TEST(test_movec_occupies_two_of_the_high_singles);
  RUN_TEST(test_the_remaining_high_singles_are_invalid);
  return UNITY_END();
}

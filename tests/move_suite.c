/* MC68030 MOVE and MOVEA, families 0001, 0010 and 0011.
 *
 * Cited to the M68000 Family Programmer's Reference Manual 1992 §8.2 and the
 * MOVE and MOVEA pages.
 *
 * The trap under test is that the destination field is REGISTER then MODE,
 * reversed relative to the source and to every other instruction in the set.
 * Reading it the same way round as the source yields a plausible wrong
 * instruction rather than a fault.
 */

#include "cpu/m68030/ap_m68030_move.h"
#include "cpu/m68030/ap_m68030_opcode.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* The three MOVE families carry the operand size, in the operation code map's
 * out-of-order assignment: 0001 byte, 0010 long, 0011 word. */
static void test_the_three_families_give_the_operand_size(void) {
  /* $1000 : MOVE.B D0,D0 */
  TEST_ASSERT_EQUAL_UINT(1, ap_m68030_move_decode(0x1000u).size);
  /* $2000 : MOVE.L D0,D0 -- family 0010 is long, not word */
  TEST_ASSERT_EQUAL_UINT(4, ap_m68030_move_decode(0x2000u).size);
  /* $3000 : MOVE.W D0,D0 */
  TEST_ASSERT_EQUAL_UINT(2, ap_m68030_move_decode(0x3000u).size);

  /* Family 0000 is not a MOVE at all. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_MOVE_INVALID,
                        ap_m68030_move_decode(0x0000u).kind);
}

/* The trap. $2280 is MOVE.L D0,(A1): source mode 000 register 000, destination
 * register 001 mode 010. Read the destination the same way round as the source
 * and it becomes something else entirely. */
static void test_the_destination_is_register_then_mode(void) {
  const ap_m68030_move_t move = ap_m68030_move_decode(0x2280u);

  TEST_ASSERT_EQUAL_INT(AP_M68030_MOVE_ORDINARY, move.kind);
  TEST_ASSERT_EQUAL_UINT(4, move.size);

  /* Source: D0. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_EA_DATA_REGISTER, move.source.kind);
  TEST_ASSERT_EQUAL_UINT(0, move.source.reg);

  /* Destination: (A1) -- mode 010 from bits 8-6, register 001 from bits 11-9.
   * Swapping them would give mode 001 register 010, i.e. A2 direct. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_EA_ADDRESS_INDIRECT, move.destination.kind);
  TEST_ASSERT_EQUAL_UINT(1, move.destination.reg);
  TEST_ASSERT_NOT_EQUAL_INT(AP_M68030_EA_ADDRESS_REGISTER,
                            move.destination.kind);
}

/* The same point from the other side: an instruction whose destination mode and
 * register differ must not decode symmetrically with its source. */
static void test_source_and_destination_are_not_symmetric(void) {
  /* $3410 : MOVE.W (A0),D2 -- source (A0), destination D2. */
  const ap_m68030_move_t move = ap_m68030_move_decode(0x3410u);

  TEST_ASSERT_EQUAL_INT(AP_M68030_EA_ADDRESS_INDIRECT, move.source.kind);
  TEST_ASSERT_EQUAL_UINT(0, move.source.reg);
  TEST_ASSERT_EQUAL_INT(AP_M68030_EA_DATA_REGISTER, move.destination.kind);
  TEST_ASSERT_EQUAL_UINT(2, move.destination.reg);
}

/* MOVEA is simply MOVE with destination mode 001 -- not a separate encoding. */
static void test_movea_is_the_address_register_destination(void) {
  /* $3040 : MOVEA.W D0,A0 */
  const ap_m68030_move_t movea = ap_m68030_move_decode(0x3040u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_MOVE_TO_ADDRESS_REGISTER, movea.kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_EA_ADDRESS_REGISTER, movea.destination.kind);
  TEST_ASSERT_EQUAL_UINT(0, movea.destination.reg);

  /* $2240 : MOVEA.L D0,A1 */
  const ap_m68030_move_t movea_long = ap_m68030_move_decode(0x2240u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_MOVE_TO_ADDRESS_REGISTER, movea_long.kind);
  TEST_ASSERT_EQUAL_UINT(1, movea_long.destination.reg);
}

/* There is no byte MOVEA, so a byte-sized address register destination is not
 * an instruction rather than a byte move into an address register. */
static void test_there_is_no_byte_movea(void) {
  /* $1040 would be MOVEA.B D0,A0 if it existed. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_MOVE_INVALID,
                        ap_m68030_move_decode(0x1040u).kind);
  /* The same destination at word and long sizes is fine. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_MOVE_TO_ADDRESS_REGISTER,
                        ap_m68030_move_decode(0x3040u).kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_MOVE_TO_ADDRESS_REGISTER,
                        ap_m68030_move_decode(0x2040u).kind);
}

/* "MOVEA ... does not affect the condition codes", where MOVE sets N and Z and
 * clears V and C. That difference is why MOVEA is worth distinguishing at all. */
static void test_movea_does_not_affect_the_condition_codes(void) {
  const ap_m68030_move_t move = ap_m68030_move_decode(0x2280u);
  const ap_m68030_move_t movea = ap_m68030_move_decode(0x2240u);

  TEST_ASSERT_TRUE(ap_m68030_move_affects_condition_codes(&move));
  TEST_ASSERT_FALSE(ap_m68030_move_affects_condition_codes(&movea));
}

/* An immediate source is legal, and the reversed destination field is what
 * makes the immediate *destination* encoding reachable at all: it needs
 * register 100 in bits 11-9 and mode 111 in bits 8-6, which is $29C0 and not
 * the $21C0 a same-way-round reading would suggest.
 *
 * Whether such a destination is a legal *instruction* is a question of
 * addressing mode categories -- Table 2-4's "Alterable" column -- which this
 * decoder does not yet answer, so the test asserts only what is checked: the
 * field decodes to the immediate mode. Categories are a named plan item. */
static void test_the_immediate_destination_encoding_decodes(void) {
  /* $203C : MOVE.L #imm,D0 -- immediate source, entirely ordinary. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_MOVE_ORDINARY,
                        ap_m68030_move_decode(0x203Cu).kind);

  const ap_m68030_move_t immediate_destination =
      ap_m68030_move_decode(0x29C0u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_EA_IMMEDIATE,
                        immediate_destination.destination.kind);

  /* The same bits read the source's way round are absolute short, which is
   * what $21C0 actually decodes to -- the reversal in one comparison. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_EA_ABSOLUTE_SHORT,
                        ap_m68030_move_decode(0x21C0u).destination.kind);
}

/* The family lookup and the move decoder must agree about which words are
 * moves, since the decoder takes its size from the map. */
static void test_the_decoder_agrees_with_the_operation_code_map(void) {
  for (unsigned family = 0; family < 16; family++) {
    const uint16_t word = (uint16_t)(family << 12);
    const bool map_says_move =
        ap_m68030_opcode_move_size(
            ap_m68030_opcode_family(word)) != 0u;
    const bool decoded =
        ap_m68030_move_decode(word).kind != AP_M68030_MOVE_INVALID;
    /* Family 0000 aside, a word the map calls a move must decode as one. */
    if (map_says_move) {
      TEST_ASSERT_TRUE(decoded);
    }
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_three_families_give_the_operand_size);
  RUN_TEST(test_the_destination_is_register_then_mode);
  RUN_TEST(test_source_and_destination_are_not_symmetric);
  RUN_TEST(test_movea_is_the_address_register_destination);
  RUN_TEST(test_there_is_no_byte_movea);
  RUN_TEST(test_movea_does_not_affect_the_condition_codes);
  RUN_TEST(test_the_immediate_destination_encoding_decodes);
  RUN_TEST(test_the_decoder_agrees_with_the_operation_code_map);
  return UNITY_END();
}

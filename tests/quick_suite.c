/* MC68030 family 0101: ADDQ, SUBQ, Scc, DBcc and TRAPcc.
 *
 * Cited to the M68000 Family Programmer's Reference Manual 1992 §8.2 and the
 * instruction pages for each.
 *
 * Two facts these tests exist to protect: the quick data field's zero means
 * eight, and DBcc terminates on -1 *after* decrementing, so a starting count of
 * zero runs once rather than 65536 times.
 */

#include "cpu/m68030/ap_m68030_opcode.h"
#include "cpu/m68030/ap_m68030_quick.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* All five live in family 0101 of the operation code map. */
static void test_the_family_is_the_quick_and_conditional_family(void) {
  TEST_ASSERT_EQUAL_INT(AP_M68030_OP_ADDQ_SUBQ_SCC_DBCC,
                        ap_m68030_opcode_family(0x5000u));
  TEST_ASSERT_EQUAL_INT(AP_M68030_OP_ADDQ_SUBQ_SCC_DBCC,
                        ap_m68030_opcode_family(0x5FFFu));
}

/* Bit 8 is the direction: clear is ADDQ, set is SUBQ. */
static void test_bit_eight_separates_addq_from_subq(void) {
  /* 0101 010 0 01 000 011 : ADDQ.W #2,D3 */
  const ap_m68030_quick_t addq = ap_m68030_quick_decode(0x5443u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_QUICK_ADDQ, addq.kind);
  TEST_ASSERT_EQUAL_UINT(2, addq.data);
  TEST_ASSERT_EQUAL_UINT(2, addq.size);
  TEST_ASSERT_EQUAL_INT(AP_M68030_EA_DATA_REGISTER, addq.ea.kind);
  TEST_ASSERT_EQUAL_UINT(3, addq.ea.reg);

  /* The same word with bit 8 set. */
  const ap_m68030_quick_t subq = ap_m68030_quick_decode(0x5543u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_QUICK_SUBQ, subq.kind);
  TEST_ASSERT_EQUAL_UINT(2, subq.data);
}

/* "1 - 7 represent immediate values of 1 - 7, and zero represents eight." A
 * decoder passing the field through turns every add-8 into an add-0: an
 * instruction that runs, sets condition codes, and does nothing. */
static void test_the_quick_data_zero_means_eight(void) {
  /* Data field 000. */
  const ap_m68030_quick_t eight = ap_m68030_quick_decode(0x5043u);
  TEST_ASSERT_EQUAL_UINT(8, eight.data);
  TEST_ASSERT_NOT_EQUAL_UINT(0, eight.data);

  /* 1 through 7 pass through unchanged. */
  for (unsigned data = 1; data < 8; data++) {
    const uint16_t word = (uint16_t)(0x5043u | (data << 9));
    TEST_ASSERT_EQUAL_UINT(data, ap_m68030_quick_decode(word).data);
  }
}

/* "00 - Byte, 01 - Word, 10 - Long", so the size field is a power of two. */
static void test_the_size_field_gives_the_operand_size(void) {
  TEST_ASSERT_EQUAL_UINT(1, ap_m68030_quick_decode(0x5003u).size);
  TEST_ASSERT_EQUAL_UINT(2, ap_m68030_quick_decode(0x5043u).size);
  TEST_ASSERT_EQUAL_UINT(4, ap_m68030_quick_decode(0x5083u).size);
}

/* Size field 11 is not a legal ADDQ/SUBQ size; that spare encoding is what
 * selects the conditional group, and bit 8 becomes part of the condition. */
static void test_size_field_three_selects_the_conditional_group(void) {
  /* 0101 0111 11 000 011 : SEQ D3 -- condition 0111 spans bit 8. */
  const ap_m68030_quick_t scc = ap_m68030_quick_decode(0x57C3u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_QUICK_SCC, scc.kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_COND_EQ, scc.condition);
  TEST_ASSERT_EQUAL_INT(AP_M68030_EA_DATA_REGISTER, scc.ea.kind);
}

/* DBcc occupies address-register-direct, an encoding Scc could never use since
 * it writes a byte. The reuse is a hole in Scc's address space, not a special
 * case bolted on. */
static void test_dbcc_occupies_the_mode_scc_cannot_use(void) {
  /* 0101 0111 11 001 100 : DBEQ D4 */
  const ap_m68030_quick_t dbcc = ap_m68030_quick_decode(0x57CCu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_QUICK_DBCC, dbcc.kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_COND_EQ, dbcc.condition);
  TEST_ASSERT_EQUAL_UINT(4, dbcc.reg);
  /* "16-BIT DISPLACEMENT" always follows, so the instruction is four bytes. */
  TEST_ASSERT_EQUAL_UINT(4, ap_m68030_quick_length(&dbcc));
}

/* TRAPcc is mode 111 with the low bits read as an opmode: "010 - one operand
 * word, 011 - two operand words, 100 - no following operand words". */
static void test_trapcc_opmodes_select_the_operand_form(void) {
  const ap_m68030_quick_t word = ap_m68030_quick_decode(0x57FAu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_QUICK_TRAPCC, word.kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_TRAPCC_WORD, word.form);
  TEST_ASSERT_EQUAL_UINT(4, ap_m68030_quick_length(&word));

  const ap_m68030_quick_t lng = ap_m68030_quick_decode(0x57FBu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_TRAPCC_LONG, lng.form);
  TEST_ASSERT_EQUAL_UINT(6, ap_m68030_quick_length(&lng));

  const ap_m68030_quick_t none = ap_m68030_quick_decode(0x57FCu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_TRAPCC_NONE, none.form);
  TEST_ASSERT_EQUAL_UINT(2, ap_m68030_quick_length(&none));
}

/* Mode 111 with an opmode the manual does not assign is not a TRAPcc. Register
 * 000 and 001 there are absolute short and long, which are legal Scc
 * destinations, so they must decode as Scc rather than as a broken TRAPcc. */
static void test_mode_seven_falls_back_to_scc_where_it_is_not_a_trapcc(void) {
  const ap_m68030_quick_t absolute_short = ap_m68030_quick_decode(0x57F8u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_QUICK_SCC, absolute_short.kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_EA_ABSOLUTE_SHORT, absolute_short.ea.kind);

  /* And an unassigned mode 111 register is not an instruction at all. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_QUICK_INVALID,
                        ap_m68030_quick_decode(0x57FFu).kind);
}

/* "If Condition False Then (Dn - 1 -> Dn; If Dn != -1 Then PC + dn -> PC)".
 * The condition being true exits without touching the counter. */
static void test_a_true_condition_exits_the_dbcc_loop(void) {
  TEST_ASSERT_FALSE(ap_m68030_dbcc_taken(true, 0x1234u));
  TEST_ASSERT_FALSE(ap_m68030_dbcc_taken(true, 0xFFFFu));
}

/* The loop rule proper: the branch is taken unless the decrement reached -1.
 * A starting count of zero therefore decrements to $FFFF and terminates,
 * running the body once -- not wrapping to 65535 and looping. */
static void test_dbcc_terminates_at_minus_one_after_decrementing(void) {
  /* Counter was 5, now 4: keep looping. */
  TEST_ASSERT_TRUE(ap_m68030_dbcc_taken(false, 4));
  /* Counter was 1, now 0: still loops -- zero is not the terminator. */
  TEST_ASSERT_TRUE(ap_m68030_dbcc_taken(false, 0));
  /* Counter was 0, now $FFFF: this is where it stops. */
  TEST_ASSERT_FALSE(ap_m68030_dbcc_taken(false, 0xFFFFu));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_family_is_the_quick_and_conditional_family);
  RUN_TEST(test_bit_eight_separates_addq_from_subq);
  RUN_TEST(test_the_quick_data_zero_means_eight);
  RUN_TEST(test_the_size_field_gives_the_operand_size);
  RUN_TEST(test_size_field_three_selects_the_conditional_group);
  RUN_TEST(test_dbcc_occupies_the_mode_scc_cannot_use);
  RUN_TEST(test_trapcc_opmodes_select_the_operand_form);
  RUN_TEST(test_mode_seven_falls_back_to_scc_where_it_is_not_a_trapcc);
  RUN_TEST(test_a_true_condition_exits_the_dbcc_loop);
  RUN_TEST(test_dbcc_terminates_at_minus_one_after_decrementing);
  return UNITY_END();
}

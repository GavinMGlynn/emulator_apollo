/* MC68030 instruction decode: the dispatcher over the family decoders.
 *
 * Cited to the M68000 Family Programmer's Reference Manual 1992 Table 8-2.
 *
 * The family suites each check their own encoding in detail. What this one
 * checks is the property none of them can: that the families *compose* -- every
 * word reaches exactly one of them, and a word no family claims is reported
 * illegal rather than absorbed by a fallback.
 */

#include "cpu/m68030/ap_m68030_decode.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* One representative instruction per family, decoded through the dispatcher
 * rather than through its own decoder. */
static void test_each_family_reaches_its_own_decoder(void) {
  TEST_ASSERT_EQUAL_INT(AP_M68030_DECODED_IMMEDIATE,
                        ap_m68030_decode(0x0010u).kind); /* ORI.B #,(A0) */
  TEST_ASSERT_EQUAL_INT(AP_M68030_DECODED_MOVE,
                        ap_m68030_decode(0x2280u).kind); /* MOVE.L D0,(A1) */
  TEST_ASSERT_EQUAL_INT(AP_M68030_DECODED_CONTROL,
                        ap_m68030_decode(0x4E71u).kind); /* NOP */
  TEST_ASSERT_EQUAL_INT(AP_M68030_DECODED_MISC,
                        ap_m68030_decode(0x43D0u).kind); /* LEA (A0),A1 */
  TEST_ASSERT_EQUAL_INT(AP_M68030_DECODED_SINGLE,
                        ap_m68030_decode(0x4210u).kind); /* CLR.B (A0) */
  TEST_ASSERT_EQUAL_INT(AP_M68030_DECODED_QUICK,
                        ap_m68030_decode(0x5443u).kind); /* ADDQ.W #2,D3 */
  TEST_ASSERT_EQUAL_INT(AP_M68030_DECODED_BRANCH,
                        ap_m68030_decode(0x6710u).kind); /* BEQ */
  TEST_ASSERT_EQUAL_INT(AP_M68030_DECODED_MOVEQ,
                        ap_m68030_decode(0x7042u).kind); /* MOVEQ #$42,D0 */
  TEST_ASSERT_EQUAL_INT(AP_M68030_DECODED_ARITH,
                        ap_m68030_decode(0xD290u).kind); /* ADD.L (A0),D1 */
  TEST_ASSERT_EQUAL_INT(AP_M68030_DECODED_LINE_A,
                        ap_m68030_decode(0xA000u).kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_DECODED_SHIFT,
                        ap_m68030_decode(0xE308u).kind); /* LSL.B #1,D0 */
  TEST_ASSERT_EQUAL_INT(AP_M68030_DECODED_COPROC,
                        ap_m68030_decode(0xF200u).kind);
}

/* Family 0100's three subtrees must each be reachable through the dispatcher,
 * which is the ordering claim the header makes. */
static void test_all_three_subtrees_of_family_0100_are_reachable(void) {
  /* $4E control group. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_DECODED_CONTROL,
                        ap_m68030_decode(0x4E90u).kind); /* JSR (A0) */
  /* The $48/$4C subtree. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_DECODED_MISC,
                        ap_m68030_decode(0x48A0u).kind); /* MOVEM */
  TEST_ASSERT_EQUAL_INT(AP_M68030_DECODED_MISC,
                        ap_m68030_decode(0x4843u).kind); /* SWAP D3 */
  /* The single-operand group. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_DECODED_SINGLE,
                        ap_m68030_decode(0x4AD0u).kind); /* TAS (A0) */
  TEST_ASSERT_EQUAL_INT(AP_M68030_DECODED_SINGLE,
                        ap_m68030_decode(0x46D0u).kind); /* MOVE to SR */
}

/* MOVEQ requires bit 8 clear; with it set the encoding is not MOVEQ and is not
 * assigned to anything else either. */
static void test_moveq_requires_bit_eight_clear(void) {
  const ap_m68030_decoded_t moveq = ap_m68030_decode(0x7042u);
  TEST_ASSERT_EQUAL_INT(AP_M68030_DECODED_MOVEQ, moveq.kind);
  TEST_ASSERT_EQUAL_UINT(0, moveq.as.moveq.reg);
  TEST_ASSERT_EQUAL_INT8(0x42, moveq.as.moveq.data);

  TEST_ASSERT_EQUAL_INT(AP_M68030_DECODED_ILLEGAL,
                        ap_m68030_decode(0x7142u).kind);
}

/* MOVEQ's data is signed and sign-extends to a long, so $FF is -1 and not 255. */
static void test_moveq_data_is_signed(void) {
  const ap_m68030_decoded_t negative = ap_m68030_decode(0x70FFu);
  TEST_ASSERT_EQUAL_INT8(-1, negative.as.moveq.data);
}

/* A word no family claims is illegal rather than absorbed. $4AFC is the
 * *defined* ILLEGAL instruction and must still decode, which is the distinction
 * worth keeping: "illegal" the encoding and ILLEGAL the instruction differ. */
static void test_an_unclaimed_word_is_illegal_but_the_illegal_instruction_is_not(void) {
  /* $4E7F is unassigned in the control group. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_DECODED_ILLEGAL,
                        ap_m68030_decode(0x4E7Fu).kind);

  /* $4AFC is the ILLEGAL instruction, which decodes perfectly well. */
  const ap_m68030_decoded_t illegal_instruction = ap_m68030_decode(0x4AFCu);
  TEST_ASSERT_EQUAL_INT(AP_M68030_DECODED_SINGLE, illegal_instruction.kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_SINGLE_ILLEGAL,
                        illegal_instruction.as.single.kind);
}

/* The property no family suite can check: sweeping the whole 16-bit space must
 * not crash, must classify every word, and must leave a sane proportion
 * illegal -- a dispatcher that claimed everything would be as wrong as one that
 * claimed nothing. */
static void test_the_whole_instruction_space_classifies(void) {
  unsigned illegal = 0;
  unsigned claimed = 0;
  for (uint32_t word = 0; word <= 0xFFFFu; word++) {
    const ap_m68030_decoded_t decoded = ap_m68030_decode((uint16_t)word);
    if (decoded.kind == AP_M68030_DECODED_ILLEGAL) {
      illegal++;
    } else {
      claimed++;
    }
  }
  TEST_ASSERT_EQUAL_UINT(0x10000u, illegal + claimed);

  /* Most of the space is assigned -- the 68000 encoding is dense -- but a real
   * fraction is not, so neither count may be zero. */
  TEST_ASSERT_TRUE(claimed > 0x8000u);
  TEST_ASSERT_TRUE(illegal > 0u);
}

/* Every word in a family the map assigns wholesale must be claimed, since those
 * families have no unassigned encodings at all. */
static void test_the_wholesale_families_claim_every_word(void) {
  for (uint32_t low = 0; low <= 0xFFFu; low++) {
    /* 1010 is entirely the Line A trap. */
    TEST_ASSERT_EQUAL_INT(AP_M68030_DECODED_LINE_A,
                          ap_m68030_decode((uint16_t)(0xA000u | low)).kind);
    /* 1111 is entirely the coprocessor interface. */
    TEST_ASSERT_EQUAL_INT(AP_M68030_DECODED_COPROC,
                          ap_m68030_decode((uint16_t)(0xF000u | low)).kind);
    /* 0110 is entirely Bcc/BSR/BRA. */
    TEST_ASSERT_EQUAL_INT(AP_M68030_DECODED_BRANCH,
                          ap_m68030_decode((uint16_t)(0x6000u | low)).kind);
  }
}


/* ---------------------------------------------------------------------------
 * Total instruction length, which is what advances the PC. A wrong length here
 * desynchronises the instruction stream rather than faulting.
 * ------------------------------------------------------------------------- */

static unsigned length_of(uint16_t word) {
  const ap_m68030_decoded_t decoded = ap_m68030_decode(word);
  return ap_m68030_instruction_length(&decoded, 0, 0);
}

/* The instructions that are exactly their own word. */
static void test_a_bare_instruction_is_two_bytes(void) {
  TEST_ASSERT_EQUAL_UINT(2, length_of(0x4E71u)); /* NOP */
  TEST_ASSERT_EQUAL_UINT(2, length_of(0x4E75u)); /* RTS */
  TEST_ASSERT_EQUAL_UINT(2, length_of(0x7042u)); /* MOVEQ */
  TEST_ASSERT_EQUAL_UINT(2, length_of(0x4843u)); /* SWAP D3 */
  TEST_ASSERT_EQUAL_UINT(2, length_of(0xE308u)); /* LSL.B #1,D0 */
}

/* Those carrying one word of their own. */
static void test_the_instructions_with_their_own_extension_word(void) {
  TEST_ASSERT_EQUAL_UINT(4, length_of(0x4E72u)); /* STOP #imm */
  TEST_ASSERT_EQUAL_UINT(4, length_of(0x4E74u)); /* RTD #d16 */
  TEST_ASSERT_EQUAL_UINT(4, length_of(0x4E52u)); /* LINK.W A2 */
  TEST_ASSERT_EQUAL_UINT(4, length_of(0x4E7Bu)); /* MOVEC */
  TEST_ASSERT_EQUAL_UINT(4, length_of(0x57CCu)); /* DBEQ D4 */
  TEST_ASSERT_EQUAL_UINT(4, length_of(0x48A0u)); /* MOVEM -(A0) */
}

/* Table 2-3 through the whole stack: a byte immediate still costs a word, so
 * ORI.B and ORI.W are the same length and only ORI.L is longer. */
static void test_a_byte_immediate_costs_the_same_as_a_word(void) {
  TEST_ASSERT_EQUAL_UINT(4, length_of(0x0010u)); /* ORI.B #x,(A0) */
  TEST_ASSERT_EQUAL_UINT(4, length_of(0x0050u)); /* ORI.W #x,(A0) */
  TEST_ASSERT_EQUAL_UINT(6, length_of(0x0090u)); /* ORI.L #x,(A0) */
}

/* An effective address adds its own words on top. */
static void test_an_effective_address_adds_its_words(void) {
  /* CLR.B (A0) : no extension. */
  TEST_ASSERT_EQUAL_UINT(2, length_of(0x4210u));
  /* CLR.B (d16,A0) : one displacement word. */
  TEST_ASSERT_EQUAL_UINT(4, length_of(0x4228u));
  /* CLR.B (xxx).W : one word. */
  TEST_ASSERT_EQUAL_UINT(4, length_of(0x4238u));
  /* CLR.B (xxx).L : two words. */
  TEST_ASSERT_EQUAL_UINT(6, length_of(0x4239u));
}

/* A static bit operation carries its bit number in a word; a dynamic one takes
 * it from a register and carries nothing, so the two differ by exactly one
 * word at the same effective address. */
static void test_static_and_dynamic_bit_operations_differ_by_one_word(void) {
  /* BSET #n,(A0) -- static, row 100, so a bit number word follows. */
  TEST_ASSERT_EQUAL_UINT(4, length_of(0x08D0u));
  /* BSET D3,(A0) -- dynamic, no bit number word. */
  TEST_ASSERT_EQUAL_UINT(2, length_of(0x07D0u));
}

/* MOVE is the only instruction with two effective addresses, and both
 * contribute. Source (A0) and destination (A1) cost nothing; widening either
 * adds words. */
static void test_move_counts_both_effective_addresses(void) {
  /* MOVE.W (A0),(A1) : neither side has an extension. */
  TEST_ASSERT_EQUAL_UINT(2, length_of(0x3290u));
  /* MOVE.W (d16,A0),(A1) : the source adds one. */
  TEST_ASSERT_EQUAL_UINT(4, length_of(0x32A8u));
  /* MOVE.W (A0),(d16,A1) : the destination adds one. */
  TEST_ASSERT_EQUAL_UINT(4, length_of(0x3350u));
  /* MOVE.W (d16,A0),(d16,A1) : both. */
  TEST_ASSERT_EQUAL_UINT(6, length_of(0x3368u));
}

/* An immediate source contributes by operand size, on top of any destination. */
static void test_an_immediate_move_source_is_sized_by_the_operand(void) {
  /* MOVE.W #x,D0 : one immediate word. */
  TEST_ASSERT_EQUAL_UINT(4, length_of(0x303Cu));
  /* MOVE.L #x,D0 : two. */
  TEST_ASSERT_EQUAL_UINT(6, length_of(0x203Cu));
  /* MOVE.B #x,D0 : still one, per Table 2-3. */
  TEST_ASSERT_EQUAL_UINT(4, length_of(0x103Cu));
}

/* An indexed mode reads the extension word to size itself, so the same
 * instruction word is a different length depending on the word that follows --
 * which is exactly why the length function takes it. */
static void test_an_indexed_mode_is_sized_from_its_extension_word(void) {
  const ap_m68030_decoded_t clr_indexed = ap_m68030_decode(0x4230u);

  /* Brief format: the extension word alone. */
  TEST_ASSERT_EQUAL_UINT(
      4, ap_m68030_instruction_length(&clr_indexed, 0x0010u, 0));

  /* Full format with long base and long outer displacement: four more words. */
  TEST_ASSERT_EQUAL_UINT(
      12, ap_m68030_instruction_length(&clr_indexed, 0x0133u, 0));
}

/* Zero means "cannot be sized" and is never a real length, so it cannot be
 * mistaken for one. Coprocessor formats vary by coprocessor and are declined
 * rather than guessed. */
static void test_unsizeable_instructions_report_zero(void) {
  TEST_ASSERT_EQUAL_UINT(0, length_of(0x4E7Fu)); /* illegal encoding */
  TEST_ASSERT_EQUAL_UINT(0, length_of(0xF200u)); /* coprocessor */
}

/* Every instruction the dispatcher claims, other than the two kinds documented
 * as unsizeable, must have an even non-zero length -- instructions are whole
 * words. This sweeps the entire space, which is the check no single case can
 * make. */
static void test_every_sizeable_instruction_has_an_even_length(void) {
  unsigned sized = 0;
  for (uint32_t word = 0; word <= 0xFFFFu; word++) {
    const ap_m68030_decoded_t decoded = ap_m68030_decode((uint16_t)word);
    if (decoded.kind == AP_M68030_DECODED_ILLEGAL ||
        decoded.kind == AP_M68030_DECODED_COPROC) {
      continue;
    }
    const unsigned length = ap_m68030_instruction_length(&decoded, 0, 0);
    TEST_ASSERT_TRUE(length >= 2u);
    TEST_ASSERT_EQUAL_UINT(0, length % 2u);
    sized++;
  }
  TEST_ASSERT_TRUE(sized > 0x8000u);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_each_family_reaches_its_own_decoder);
  RUN_TEST(test_all_three_subtrees_of_family_0100_are_reachable);
  RUN_TEST(test_moveq_requires_bit_eight_clear);
  RUN_TEST(test_moveq_data_is_signed);
  RUN_TEST(test_an_unclaimed_word_is_illegal_but_the_illegal_instruction_is_not);
  RUN_TEST(test_the_whole_instruction_space_classifies);
  RUN_TEST(test_the_wholesale_families_claim_every_word);
  RUN_TEST(test_a_bare_instruction_is_two_bytes);
  RUN_TEST(test_the_instructions_with_their_own_extension_word);
  RUN_TEST(test_a_byte_immediate_costs_the_same_as_a_word);
  RUN_TEST(test_an_effective_address_adds_its_words);
  RUN_TEST(test_static_and_dynamic_bit_operations_differ_by_one_word);
  RUN_TEST(test_move_counts_both_effective_addresses);
  RUN_TEST(test_an_immediate_move_source_is_sized_by_the_operand);
  RUN_TEST(test_an_indexed_mode_is_sized_from_its_extension_word);
  RUN_TEST(test_unsizeable_instructions_report_zero);
  RUN_TEST(test_every_sizeable_instruction_has_an_even_length);
  return UNITY_END();
}

/* MC68030 family 0000's size-11 escape: CMP2, CHK2, CAS and CAS2.
 *
 * The four share one six-bit hole in the opcode map and are separated by bits
 * scattered across the instruction word, the extension word, and the effective
 * address field being used as something other than an address. Every test here
 * names the bit doing the separating, because each of them is a place a decoder
 * can produce a working instruction that is the wrong one.
 */

#include "cpu/m68030/ap_m68030_bounds.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Build an instruction: family 0000, bit 11 chooses the half, size in 10-9,
 * bits 8-6 fixed at 011, effective address in 5-0. */
static uint16_t word(bool compare_and_swap, unsigned size_field, unsigned mode,
                     unsigned reg) {
  return (uint16_t)((compare_and_swap ? 0x0800u : 0x0000u) |
                    (size_field << 9) | 0x00C0u | (mode << 3) | reg);
}

/* The escape only exists where the immediate instructions' size field reads
 * 11. Anything else in family 0000 is an ordinary ORI, bit operation or MOVEP
 * and must not be claimed here. */
static void test_the_escape_is_only_the_unassigned_size_field(void) {
  TEST_ASSERT_TRUE(ap_m68030_bounds_matches(word(false, 0x1u, 0x2u, 0x0u)));

  /* ORI.W #x,(A0) is $0050: bits 8-6 read 001, an ordinary size. */
  TEST_ASSERT_FALSE(ap_m68030_bounds_matches(0x0050u));
  /* And nothing outside family 0000 is in the escape at all. */
  TEST_ASSERT_FALSE(ap_m68030_bounds_matches(0x10C0u));
}

/* The trap in this escape: the two halves count their sizes differently. CMP2
 * and CHK2 use "00 Byte, 01 Word, 10 Long" and CAS uses "01 Byte, 10 Word,
 * 11 Long" -- one higher throughout. The same three bits in the same position
 * therefore mean a byte in one half and a word in the other, and a decoder that
 * read the size once for the whole escape would give every CAS the wrong
 * operand width, silently. */
static void test_the_two_halves_count_their_sizes_differently(void) {
  /* Size field 01 is a *word* CMP2 ... */
  const ap_m68030_bounds_t compare =
      ap_m68030_bounds_decode(word(false, 0x1u, 0x2u, 0x0u));
  TEST_ASSERT_EQUAL_INT(AP_M68030_BOUNDS_CMP2, compare.kind);
  TEST_ASSERT_EQUAL_UINT(2u, compare.size);

  /* ... and a *byte* CAS. */
  const ap_m68030_bounds_t swap =
      ap_m68030_bounds_decode(word(true, 0x1u, 0x2u, 0x0u));
  TEST_ASSERT_EQUAL_INT(AP_M68030_BOUNDS_CAS, swap.kind);
  TEST_ASSERT_EQUAL_UINT(1u, swap.size);
}

/* And the unassigned value differs with it: 11 has no meaning for CMP2/CHK2,
 * 00 has none for CAS. Each half's hole is where the other half's byte or long
 * sits, so accepting the wrong one produces an instruction from thin air. */
static void test_each_half_leaves_a_different_size_unassigned(void) {
  TEST_ASSERT_EQUAL_INT(AP_M68030_BOUNDS_INVALID,
                        ap_m68030_bounds_decode(word(false, 0x3u, 0x2u, 0x0u))
                            .kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_BOUNDS_INVALID,
                        ap_m68030_bounds_decode(word(true, 0x0u, 0x2u, 0x0u))
                            .kind);

  /* The same two values are perfectly good in the other half. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_BOUNDS_CAS,
                        ap_m68030_bounds_decode(word(true, 0x3u, 0x2u, 0x0u))
                            .kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_BOUNDS_CMP2,
                        ap_m68030_bounds_decode(word(false, 0x0u, 0x2u, 0x0u))
                            .kind);
}

/* "This instruction is identical to CHK2 except that it sets condition codes
 * rather than taking an exception when the value in Rn is out of bounds", and
 * the bit that separates them is in the *extension* word. A decoder reading
 * only the instruction word cannot tell them apart at all -- so it must not
 * pretend to, which is why the decode reports the pair and a second call
 * resolves it. */
static void test_only_the_extension_word_separates_cmp2_from_chk2(void) {
  const ap_m68030_bounds_t decoded =
      ap_m68030_bounds_decode(word(false, 0x1u, 0x2u, 0x0u));

  /* Extension bit 11 clear: CMP2. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_BOUNDS_CMP2,
                        ap_m68030_bounds_kind(&decoded, 0x0000u));
  /* Set: CHK2, from the identical instruction word. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_BOUNDS_CHK2,
                        ap_m68030_bounds_kind(&decoded, 0x0800u));
}

/* The extension word also names the register to check, and it may be an address
 * register: "D/A field -- 0 Data register, 1 Address register". Reading only
 * the number would check D3 where A3 was meant, and both hold plausible
 * values. */
static void test_the_checked_register_may_be_an_address_register(void) {
  /* D/A set, register 011. */
  TEST_ASSERT_TRUE(ap_m68030_bounds_register_is_address(0x8000u | (3u << 12)));
  TEST_ASSERT_EQUAL_UINT(3u, ap_m68030_bounds_register(0x8000u | (3u << 12)));

  TEST_ASSERT_FALSE(ap_m68030_bounds_register_is_address(3u << 12));
  TEST_ASSERT_EQUAL_UINT(3u, ap_m68030_bounds_register(3u << 12));
}

/* CAS2's effective address field is not an address: `111100` is the immediate
 * mode's encoding, which CAS cannot use, so the pattern is free and CAS2 takes
 * it as an escape. A decoder treating it as an address would decode an
 * immediate operand for an instruction whose operands are both in memory. */
static void test_cas2_hides_behind_the_immediate_encoding(void) {
  const ap_m68030_bounds_t cas2 =
      ap_m68030_bounds_decode(word(true, 0x2u, 0x7u, 0x4u));
  TEST_ASSERT_EQUAL_INT(AP_M68030_BOUNDS_CAS2, cas2.kind);
  TEST_ASSERT_EQUAL_UINT(2u, cas2.size);
  /* Two extension words, one per operand -- which is what lets CAS2 swap both
   * ends of a linked list in one indivisible operation. */
  TEST_ASSERT_EQUAL_UINT(2u, ap_m68030_bounds_length(&cas2));

  /* CAS itself carries one. */
  const ap_m68030_bounds_t cas =
      ap_m68030_bounds_decode(word(true, 0x2u, 0x2u, 0x0u));
  TEST_ASSERT_EQUAL_INT(AP_M68030_BOUNDS_CAS, cas.kind);
  TEST_ASSERT_EQUAL_UINT(1u, ap_m68030_bounds_length(&cas));

  /* "10 -- Word operation, 11 -- Long operation": CAS2 has no byte form, so
   * the size CAS reads as a byte is not a CAS2 at all. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_BOUNDS_INVALID,
                        ap_m68030_bounds_decode(word(true, 0x1u, 0x7u, 0x4u))
                            .kind);
}

/* The three take different addressing mode categories, and each exclusion is
 * the instruction's own shape: CMP2/CHK2 only *read* their bounds pair, so
 * control; CAS reads and writes its operand indivisibly, so memory alterable --
 * which is why a register operand is meaningless for it. */
static void test_each_half_takes_the_category_its_operand_needs(void) {
  /* (A0)+ is not a control mode, so CMP2 refuses it ... */
  TEST_ASSERT_EQUAL_INT(AP_M68030_BOUNDS_INVALID,
                        ap_m68030_bounds_decode(word(false, 0x1u, 0x3u, 0x0u))
                            .kind);
  /* ... and CAS accepts it, being memory alterable. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_BOUNDS_CAS,
                        ap_m68030_bounds_decode(word(true, 0x2u, 0x3u, 0x0u))
                            .kind);

  /* (d16,PC) is control but not alterable: CMP2 takes it, CAS does not. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_BOUNDS_CMP2,
                        ap_m68030_bounds_decode(word(false, 0x1u, 0x7u, 0x2u))
                            .kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_BOUNDS_INVALID,
                        ap_m68030_bounds_decode(word(true, 0x2u, 0x7u, 0x2u))
                            .kind);

  /* Neither takes a data register. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_BOUNDS_INVALID,
                        ap_m68030_bounds_decode(word(false, 0x1u, 0x0u, 0x0u))
                            .kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_BOUNDS_INVALID,
                        ap_m68030_bounds_decode(word(true, 0x2u, 0x0u, 0x0u))
                            .kind);
}

/* CAS's extension word names two data registers in two separate fields, and
 * they are not interchangeable: "Du ... contains the update value to be written
 * to the memory operand location if the comparison is successful" and "Dc ...
 * contains the value to be compared". Swapping them writes the value that was
 * supposed to be the test. */
static void test_cas_names_its_compare_and_update_registers_apart(void) {
  /* Du = 5 in bits 8-6, Dc = 2 in bits 2-0. */
  const uint16_t extension = (uint16_t)((5u << 6) | 2u);
  TEST_ASSERT_EQUAL_UINT(5u, ap_m68030_cas_update_register(extension));
  TEST_ASSERT_EQUAL_UINT(2u, ap_m68030_cas_compare_register(extension));
}

/* The one place the escape and the rest of family 0000 genuinely interleave,
 * and the reason CAS leaves size 00 unassigned: that pattern *is* a static bit
 * operation. `BSET #n,(A0)` is $08D0, which has family 0000, bit 8 clear and
 * bits 7-6 reading 11 -- the escape's shape exactly -- while being an ordinary
 * bit operation.
 *
 * So the escape must decline it, and a decoder must fall through rather than
 * stop: claiming the pattern turns every static BTST/BCHG/BCLR/BSET into an
 * illegal instruction, which is how this was first noticed. */
static void test_the_static_bit_operations_live_in_cas_size_zero(void) {
  /* BSET #n,(A0): the escape's shape, and not one of these four. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_BOUNDS_INVALID,
                        ap_m68030_bounds_decode(0x08D0u).kind);
  /* BTST, BCHG and BCLR sit beside it and are refused for the same reason. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_BOUNDS_INVALID,
                        ap_m68030_bounds_decode(0x0810u).kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_BOUNDS_INVALID,
                        ap_m68030_bounds_decode(0x0850u).kind);
  TEST_ASSERT_EQUAL_INT(AP_M68030_BOUNDS_INVALID,
                        ap_m68030_bounds_decode(0x0890u).kind);

  /* One size field higher is a byte CAS, and that one *is* claimed -- the two
   * subtrees are adjacent, not overlapping. */
  TEST_ASSERT_EQUAL_INT(AP_M68030_BOUNDS_CAS,
                        ap_m68030_bounds_decode(0x0AD0u).kind);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_escape_is_only_the_unassigned_size_field);
  RUN_TEST(test_the_two_halves_count_their_sizes_differently);
  RUN_TEST(test_each_half_leaves_a_different_size_unassigned);
  RUN_TEST(test_the_static_bit_operations_live_in_cas_size_zero);
  RUN_TEST(test_only_the_extension_word_separates_cmp2_from_chk2);
  RUN_TEST(test_the_checked_register_may_be_an_address_register);
  RUN_TEST(test_cas2_hides_behind_the_immediate_encoding);
  RUN_TEST(test_each_half_takes_the_category_its_operand_needs);
  RUN_TEST(test_cas_names_its_compare_and_update_registers_apart);
  return UNITY_END();
}

/* MC68030 effective address calculation.
 *
 * Cited to the M68000 Family Programmer's Reference Manual 1992 §2.2.
 *
 * The rule these tests exist for is that A7 is not an ordinary address
 * register: a byte access through the increment modes moves it by *two*, to
 * keep the stack word aligned. A model that misses it keeps running, and the
 * stack simply drifts odd.
 */

#include "cpu/m68030/ap_m68030_addr.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static ap_m68030_regs_t regs_with_a(unsigned reg, uint32_t value) {
  ap_m68030_regs_t regs = {0};
  ap_m68030_write_sr(&regs, 1u << AP_M68030_SR_S_BIT); /* supervisor: A7 is ISP */
  ap_m68030_write_address_register(&regs, reg, value);
  return regs;
}

static ap_m68030_address_input_t input_of(unsigned size) {
  return (ap_m68030_address_input_t){.operand_size = size};
}

/* Register direct: the operand is the register, not an address. */
static void test_the_register_modes_report_a_register(void) {
  ap_m68030_regs_t regs = regs_with_a(0, 0x1000u);
  const ap_m68030_address_input_t in = input_of(4);

  const ap_m68030_address_t d = ap_m68030_address_calculate(
      &regs, (ap_m68030_ea_t){AP_M68030_EA_DATA_REGISTER, 3}, &in);
  TEST_ASSERT_TRUE(d.in_register);
  TEST_ASSERT_FALSE(d.address_register);
  TEST_ASSERT_EQUAL_UINT(3, d.reg);

  const ap_m68030_address_t a = ap_m68030_address_calculate(
      &regs, (ap_m68030_ea_t){AP_M68030_EA_ADDRESS_REGISTER, 5}, &in);
  TEST_ASSERT_TRUE(a.in_register);
  TEST_ASSERT_TRUE(a.address_register);
}

/* "EA = (An); An + SIZE -> An" -- the address is taken before the register
 * moves, which is the whole difference from predecrement. */
static void test_postincrement_takes_the_address_before_moving(void) {
  ap_m68030_regs_t regs = regs_with_a(2, 0x1000u);
  const ap_m68030_address_input_t in = input_of(4);

  const ap_m68030_address_t r = ap_m68030_address_calculate(
      &regs, (ap_m68030_ea_t){AP_M68030_EA_POSTINCREMENT, 2}, &in);

  TEST_ASSERT_EQUAL_HEX32(0x1000u, r.address);
  TEST_ASSERT_EQUAL_HEX32(0x1004u, ap_m68030_read_address_register(&regs, 2));
}

/* "An - SIZE -> An; EA = (An)" -- the register moves first. */
static void test_predecrement_moves_the_register_before_taking_it(void) {
  ap_m68030_regs_t regs = regs_with_a(2, 0x1000u);
  const ap_m68030_address_input_t in = input_of(4);

  const ap_m68030_address_t r = ap_m68030_address_calculate(
      &regs, (ap_m68030_ea_t){AP_M68030_EA_PREDECREMENT, 2}, &in);

  TEST_ASSERT_EQUAL_HEX32(0x0FFCu, r.address);
  TEST_ASSERT_EQUAL_HEX32(0x0FFCu, ap_m68030_read_address_register(&regs, 2));
}

/* The rule. "If the address register is the stack pointer and the operand size
 * is byte, the address is incremented by two to keep the stack pointer aligned
 * to a word boundary." */
static void test_a_byte_access_moves_the_stack_pointer_by_two(void) {
  TEST_ASSERT_EQUAL_UINT(2, ap_m68030_address_step(7, 1));
  /* Any other register moves by one for a byte. */
  TEST_ASSERT_EQUAL_UINT(1, ap_m68030_address_step(3, 1));
  /* And the rule is byte-only: word and long are unaffected. */
  TEST_ASSERT_EQUAL_UINT(2, ap_m68030_address_step(7, 2));
  TEST_ASSERT_EQUAL_UINT(4, ap_m68030_address_step(7, 4));
}

/* The same rule through the calculation, in both directions, so the stack stays
 * word aligned rather than drifting odd. */
static void test_the_stack_stays_word_aligned_through_byte_accesses(void) {
  ap_m68030_regs_t regs = regs_with_a(7, 0x2000u);
  const ap_m68030_address_input_t byte = input_of(1);

  (void)ap_m68030_address_calculate(
      &regs, (ap_m68030_ea_t){AP_M68030_EA_POSTINCREMENT, 7}, &byte);
  TEST_ASSERT_EQUAL_HEX32(0x2002u, ap_m68030_read_address_register(&regs, 7));

  (void)ap_m68030_address_calculate(
      &regs, (ap_m68030_ea_t){AP_M68030_EA_PREDECREMENT, 7}, &byte);
  TEST_ASSERT_EQUAL_HEX32(0x2000u, ap_m68030_read_address_register(&regs, 7));

  /* A3 doing the same thing moves by one each way, and ends up odd -- which is
   * exactly what A7 must not do. */
  ap_m68030_regs_t other = regs_with_a(3, 0x2000u);
  (void)ap_m68030_address_calculate(
      &other, (ap_m68030_ea_t){AP_M68030_EA_POSTINCREMENT, 3}, &byte);
  TEST_ASSERT_EQUAL_HEX32(0x2001u, ap_m68030_read_address_register(&other, 3));
}

/* A7 names a different register per privilege state, and the byte rule follows
 * whichever it currently is. */
static void test_the_byte_rule_follows_whichever_stack_a7_names(void) {
  ap_m68030_regs_t regs = {0};
  regs.usp = 0x3000u;
  regs.isp = 0x4000u;
  const ap_m68030_address_input_t byte = input_of(1);

  /* User state: A7 is the USP. */
  ap_m68030_write_sr(&regs, 0);
  (void)ap_m68030_address_calculate(
      &regs, (ap_m68030_ea_t){AP_M68030_EA_POSTINCREMENT, 7}, &byte);
  TEST_ASSERT_EQUAL_HEX32(0x3002u, regs.usp);
  TEST_ASSERT_EQUAL_HEX32(0x4000u, regs.isp);

  /* Supervisor state: the ISP, moved by two as well. */
  ap_m68030_write_sr(&regs, 1u << AP_M68030_SR_S_BIT);
  (void)ap_m68030_address_calculate(
      &regs, (ap_m68030_ea_t){AP_M68030_EA_POSTINCREMENT, 7}, &byte);
  TEST_ASSERT_EQUAL_HEX32(0x4002u, regs.isp);
}

/* "EA = (An) + d16", with the displacement signed. */
static void test_a_displacement_is_signed(void) {
  ap_m68030_regs_t regs = regs_with_a(1, 0x1000u);
  ap_m68030_address_input_t in = input_of(2);
  in.displacement = -8;

  const ap_m68030_address_t r = ap_m68030_address_calculate(
      &regs, (ap_m68030_ea_t){AP_M68030_EA_DISPLACEMENT, 1}, &in);
  TEST_ASSERT_EQUAL_HEX32(0x0FF8u, r.address);
}

/* The PC forms are relative to the *extension word*, not to the instruction
 * word and not to the next instruction. */
static void test_pc_relative_is_relative_to_the_extension_word(void) {
  ap_m68030_regs_t regs = {0};
  ap_m68030_address_input_t in = input_of(2);
  in.extension_address = 0x1002u;
  in.displacement = 0x10;

  const ap_m68030_address_t r = ap_m68030_address_calculate(
      &regs, (ap_m68030_ea_t){AP_M68030_EA_PC_DISPLACEMENT, 0}, &in);
  TEST_ASSERT_EQUAL_HEX32(0x1012u, r.address);
}

/* The brief indexed form: base plus a signed 8-bit displacement plus the scaled
 * index register. */
static void test_a_brief_indexed_address_sums_base_index_and_displacement(void) {
  ap_m68030_regs_t regs = regs_with_a(1, 0x1000u);
  regs.d[2] = 4;
  ap_m68030_address_input_t in = input_of(2);
  /* D2, word index, scale 4, displacement +8. */
  in.extension_word = (uint16_t)((2u << 12) | (2u << 9) | 0x08u);

  const ap_m68030_address_t r = ap_m68030_address_calculate(
      &regs, (ap_m68030_ea_t){AP_M68030_EA_INDEXED, 1}, &in);

  /* 0x1000 + (4 * 4) + 8 */
  TEST_ASSERT_EQUAL_HEX32(0x1018u, r.address);
}

/* A word index is sign extended, so a negative index reaches below the base. */
static void test_a_word_index_is_sign_extended(void) {
  ap_m68030_regs_t regs = regs_with_a(1, 0x1000u);
  regs.d[2] = 0xFFFFFFF8u; /* -8 as a long, and 0xFFF8 as a word */
  ap_m68030_address_input_t in = input_of(2);
  in.extension_word = (uint16_t)(2u << 12); /* D2, word, scale 1 */

  const ap_m68030_address_t r = ap_m68030_address_calculate(
      &regs, (ap_m68030_ea_t){AP_M68030_EA_INDEXED, 1}, &in);
  TEST_ASSERT_EQUAL_HEX32(0x0FF8u, r.address);
}

/* The full format can suppress the base register, the index, or both. */
static void test_the_full_format_can_suppress_base_and_index(void) {
  ap_m68030_regs_t regs = regs_with_a(1, 0x1000u);
  regs.d[2] = 0x40;
  ap_m68030_address_input_t in = input_of(2);
  in.base_displacement = 0x200;

  /* Full format, BS set (base suppressed), BD word, no indirect. */
  in.extension_word = (uint16_t)((2u << 12) | 0x0100u | 0x0080u | 0x0020u | 1u);
  const ap_m68030_address_t no_base = ap_m68030_address_calculate(
      &regs, (ap_m68030_ea_t){AP_M68030_EA_INDEXED, 1}, &in);
  TEST_ASSERT_EQUAL_HEX32(0x240u, no_base.address); /* index + displacement */

  /* IS set as well: displacement alone. */
  in.extension_word |= 0x0040u;
  const ap_m68030_address_t neither = ap_m68030_address_calculate(
      &regs, (ap_m68030_ea_t){AP_M68030_EA_INDEXED, 1}, &in);
  TEST_ASSERT_EQUAL_HEX32(0x200u, neither.address);
}

/* A memory indirect action needs a bus read this module cannot perform, so it
 * says so rather than returning a half-computed address as though it were
 * final. */
static void test_a_memory_indirect_mode_reports_the_indirection(void) {
  ap_m68030_regs_t regs = regs_with_a(1, 0x1000u);
  ap_m68030_address_input_t in = input_of(2);
  /* Full format, BD null, IS clear, I/IS 001: indirect preindexed. */
  in.extension_word = 0x0111u;

  const ap_m68030_address_t r = ap_m68030_address_calculate(
      &regs, (ap_m68030_ea_t){AP_M68030_EA_INDEXED, 1}, &in);
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_TRUE(r.indirection_pending);
}

/* An extension word using a reserved encoding is not a valid address. */
static void test_a_reserved_extension_word_is_not_valid(void) {
  ap_m68030_regs_t regs = regs_with_a(1, 0x1000u);
  ap_m68030_address_input_t in = input_of(2);
  in.extension_word = 0x0100u; /* full format, BD SIZE 00 = Reserved */

  const ap_m68030_address_t r = ap_m68030_address_calculate(
      &regs, (ap_m68030_ea_t){AP_M68030_EA_INDEXED, 1}, &in);
  TEST_ASSERT_FALSE(r.valid);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_register_modes_report_a_register);
  RUN_TEST(test_postincrement_takes_the_address_before_moving);
  RUN_TEST(test_predecrement_moves_the_register_before_taking_it);
  RUN_TEST(test_a_byte_access_moves_the_stack_pointer_by_two);
  RUN_TEST(test_the_stack_stays_word_aligned_through_byte_accesses);
  RUN_TEST(test_the_byte_rule_follows_whichever_stack_a7_names);
  RUN_TEST(test_a_displacement_is_signed);
  RUN_TEST(test_pc_relative_is_relative_to_the_extension_word);
  RUN_TEST(test_a_brief_indexed_address_sums_base_index_and_displacement);
  RUN_TEST(test_a_word_index_is_sign_extended);
  RUN_TEST(test_the_full_format_can_suppress_base_and_index);
  RUN_TEST(test_a_memory_indirect_mode_reports_the_indirection);
  RUN_TEST(test_a_reserved_extension_word_is_not_valid);
  return UNITY_END();
}

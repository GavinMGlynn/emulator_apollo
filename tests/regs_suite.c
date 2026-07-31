/* MC68030 programming model.
 *
 * Cited to MC68030 User's Manual 3ed §1.3 and to the M68000 Family
 * Programmer's Reference Manual §1.3.2, whose Figure 1-8 survives intact where
 * the 68030 manual's does not.
 *
 * The fact these tests mostly exist to protect is that A7 names one of *three*
 * registers, chosen by S and M, and that in user state M is ignored rather than
 * required to be zero.
 */

#include "cpu/m68030/ap_m68030_regs.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static ap_m68030_regs_t regs_with_sr(uint16_t sr) {
  ap_m68030_regs_t regs = {0};
  regs.usp = 0x11110000u;
  regs.isp = 0x22220000u;
  regs.msp = 0x33330000u;
  ap_m68030_write_sr(&regs, sr);
  return regs;
}

/* The PRM's own S/M table:  0 x -> USP,  1 0 -> ISP,  1 1 -> MSP. */
static void test_the_active_stack_follows_the_documented_s_and_m_table(void) {
  ap_m68030_regs_t user = regs_with_sr(0);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STACK_USP, ap_m68030_active_stack(&user));

  ap_m68030_regs_t interrupt = regs_with_sr(1u << AP_M68030_SR_S_BIT);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STACK_ISP,
                        ap_m68030_active_stack(&interrupt));

  ap_m68030_regs_t master = regs_with_sr((1u << AP_M68030_SR_S_BIT) |
                                         (1u << AP_M68030_SR_M_BIT));
  TEST_ASSERT_EQUAL_INT(AP_M68030_STACK_MSP, ap_m68030_active_stack(&master));
}

/* The "x" in the table is load-bearing: in user state M is ignored, so S=0 M=1
 * is still the USP and not a fourth stack. */
static void test_the_master_bit_is_ignored_in_user_state(void) {
  ap_m68030_regs_t user_with_m = regs_with_sr(1u << AP_M68030_SR_M_BIT);
  TEST_ASSERT_EQUAL_INT(AP_M68030_STACK_USP,
                        ap_m68030_active_stack(&user_with_m));
  TEST_ASSERT_EQUAL_HEX32(0x11110000u, ap_m68030_read_a7(&user_with_m));
}

/* A7 reads and writes through whichever stack is active, and must not disturb
 * the other two. */
static void test_a7_resolves_to_the_active_stack_and_leaves_the_others(void) {
  ap_m68030_regs_t regs = regs_with_sr((1u << AP_M68030_SR_S_BIT) |
                                       (1u << AP_M68030_SR_M_BIT));
  TEST_ASSERT_EQUAL_HEX32(0x33330000u, ap_m68030_read_a7(&regs));

  ap_m68030_write_a7(&regs, 0xAAAA0000u);
  TEST_ASSERT_EQUAL_HEX32(0xAAAA0000u, regs.msp);
  TEST_ASSERT_EQUAL_HEX32(0x11110000u, regs.usp);
  TEST_ASSERT_EQUAL_HEX32(0x22220000u, regs.isp);
}

/* Address register 7 goes through the stack selection; 0-6 do not. */
static void test_address_register_seven_is_the_stack_pointer(void) {
  ap_m68030_regs_t regs = regs_with_sr(1u << AP_M68030_SR_S_BIT);
  regs.a[3] = 0xDEADBEEFu;

  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu,
                          ap_m68030_read_address_register(&regs, 3));
  TEST_ASSERT_EQUAL_HEX32(0x22220000u,
                          ap_m68030_read_address_register(&regs, 7));

  ap_m68030_write_address_register(&regs, 7, 0xFEEDFACEu);
  TEST_ASSERT_EQUAL_HEX32(0xFEEDFACEu, regs.isp);
}

/* Switching S and M switches which register A7 names, without moving any
 * value between them -- which is the whole point of having three. */
static void test_changing_privilege_reaches_a_different_stack(void) {
  ap_m68030_regs_t regs = regs_with_sr(0);
  TEST_ASSERT_EQUAL_HEX32(0x11110000u, ap_m68030_read_a7(&regs));

  ap_m68030_write_sr(&regs, 1u << AP_M68030_SR_S_BIT);
  TEST_ASSERT_EQUAL_HEX32(0x22220000u, ap_m68030_read_a7(&regs));

  ap_m68030_write_sr(&regs,
                     (1u << AP_M68030_SR_S_BIT) | (1u << AP_M68030_SR_M_BIT));
  TEST_ASSERT_EQUAL_HEX32(0x33330000u, ap_m68030_read_a7(&regs));

  /* Nothing was copied: each still holds what it started with. */
  TEST_ASSERT_EQUAL_HEX32(0x11110000u, regs.usp);
  TEST_ASSERT_EQUAL_HEX32(0x22220000u, regs.isp);
  TEST_ASSERT_EQUAL_HEX32(0x33330000u, regs.msp);
}

/* The PRM's trace table, including that T1 T0 = 11 is "UNDEFINED" rather than a
 * fourth mode -- reported as such rather than folded into one of the others. */
static void test_the_trace_modes_match_the_published_table(void) {
  ap_m68030_regs_t none = regs_with_sr(0);
  TEST_ASSERT_EQUAL_INT(AP_M68030_TRACE_NONE, ap_m68030_trace_mode(&none));

  ap_m68030_regs_t any = regs_with_sr(1u << AP_M68030_SR_T1_BIT);
  TEST_ASSERT_EQUAL_INT(AP_M68030_TRACE_ANY_INSTRUCTION,
                        ap_m68030_trace_mode(&any));

  ap_m68030_regs_t flow = regs_with_sr(1u << AP_M68030_SR_T0_BIT);
  TEST_ASSERT_EQUAL_INT(AP_M68030_TRACE_ON_CHANGE_OF_FLOW,
                        ap_m68030_trace_mode(&flow));

  ap_m68030_regs_t undefined = regs_with_sr((1u << AP_M68030_SR_T1_BIT) |
                                            (1u << AP_M68030_SR_T0_BIT));
  TEST_ASSERT_EQUAL_INT(AP_M68030_TRACE_UNDEFINED,
                        ap_m68030_trace_mode(&undefined));
}

/* "I2, I1, and I0 define the interrupt mask level", bits 10-8. */
static void test_the_interrupt_mask_occupies_bits_ten_to_eight(void) {
  ap_m68030_regs_t regs = regs_with_sr(0x0700u);
  TEST_ASSERT_EQUAL_UINT(7, ap_m68030_interrupt_mask(&regs));

  ap_m68030_regs_t five = regs_with_sr(0x0500u);
  TEST_ASSERT_EQUAL_UINT(5, ap_m68030_interrupt_mask(&five));
}

/* Bits 11, 7, 6 and 5 are shown as zero and have no field, so writing them must
 * not make them readable. */
static void test_the_reserved_bits_never_read_back(void) {
  ap_m68030_regs_t regs = regs_with_sr(0xFFFFu);
  TEST_ASSERT_EQUAL_HEX16(0, regs.sr & (uint16_t)~AP_M68030_SR_DEFINED);
  TEST_ASSERT_EQUAL_HEX16(0xF71Fu, regs.sr);
}

/* "the CCR, the status register's lower byte, is the only portion of the status
 * register (SR) available in the user mode" -- so a CCR write must not be able
 * to reach S and escalate privilege. */
static void test_a_ccr_write_cannot_touch_the_system_byte(void) {
  ap_m68030_regs_t regs = regs_with_sr(0x0700u); /* user, mask 7 */
  TEST_ASSERT_FALSE(ap_m68030_supervisor(&regs));

  ap_m68030_write_ccr(&regs, 0xFFFFu);

  TEST_ASSERT_FALSE(ap_m68030_supervisor(&regs));
  TEST_ASSERT_EQUAL_UINT(7, ap_m68030_interrupt_mask(&regs));
  TEST_ASSERT_EQUAL_HEX16(AP_M68030_CCR_MASK, ap_m68030_read_ccr(&regs));
}

/* The five condition codes are X N Z V C at bits 4-0, and nothing above them. */
static void test_the_condition_codes_are_the_low_five_bits(void) {
  ap_m68030_regs_t regs = regs_with_sr(0);
  ap_m68030_write_ccr(&regs, (1u << AP_M68030_SR_X_BIT) |
                                 (1u << AP_M68030_SR_C_BIT));
  TEST_ASSERT_EQUAL_HEX16(0x0011u, ap_m68030_read_ccr(&regs));
  TEST_ASSERT_EQUAL_HEX16(0x0011u, regs.sr);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_active_stack_follows_the_documented_s_and_m_table);
  RUN_TEST(test_the_master_bit_is_ignored_in_user_state);
  RUN_TEST(test_a7_resolves_to_the_active_stack_and_leaves_the_others);
  RUN_TEST(test_address_register_seven_is_the_stack_pointer);
  RUN_TEST(test_changing_privilege_reaches_a_different_stack);
  RUN_TEST(test_the_trace_modes_match_the_published_table);
  RUN_TEST(test_the_interrupt_mask_occupies_bits_ten_to_eight);
  RUN_TEST(test_the_reserved_bits_never_read_back);
  RUN_TEST(test_a_ccr_write_cannot_touch_the_system_byte);
  RUN_TEST(test_the_condition_codes_are_the_low_five_bits);
  return UNITY_END();
}

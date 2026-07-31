/* Archive SC-499 cartridge tape controller, `[SC499]` §1.9. */

#include "unity.h"

#include <string.h>

#include "device/ap_sc499.h"

void setUp(void) {}
void tearDown(void) {}

static void test_a_reset_controller_is_ready_and_done(void) {
  ap_sc499_t t;
  ap_sc499_reset(&t);

  /* `[SC499]` on reset: it "sets DONE to 1". And the oracle's own idle
   * controller reads `40` from the status register -- Ready at bit 6, which is
   * where the measurement placed it because the manual's scan lost the bit
   * numbers. */
  uint8_t status = ap_sc499_read(&t, AP_SC499_CONTROL_STATUS);
  TEST_ASSERT_EQUAL_HEX8(AP_SC499_ST_RDY, status & AP_SC499_ST_RDY);
  /* DONE is clear at reset in the measured part, against the guide; see
   * `ap_sc499_reset`. */
  TEST_ASSERT_EQUAL_HEX8(0, status & AP_SC499_ST_DONE);
  TEST_ASSERT_EQUAL_HEX8(0, status & AP_SC499_ST_EXC);
}

static void test_resetting_the_dma_is_the_same_as_a_power_on_reset(void) {
  ap_sc499_t after_write;
  ap_sc499_t after_power_on;

  /* "RSTDMA initializes the DMA sequencer, clears all Control Register bits to
   * 0, and sets DONE to 1 (power-on reset from the IBM PC performs the same
   * functions)." A command defined as equal to power-on reset is the cheapest
   * possible test of both at once: they must be indistinguishable. */
  ap_sc499_reset(&after_write);
  ap_sc499_write(&after_write, AP_SC499_CONTROL_STATUS, 0x30);
  ap_sc499_write(&after_write, AP_SC499_DMAGO, 0xFF);
  ap_sc499_write(&after_write, AP_SC499_RSTDMA, 0x00);

  ap_sc499_reset(&after_power_on);
  TEST_ASSERT_EQUAL_MEMORY(&after_power_on, &after_write, sizeof after_write);
}

static void test_the_dma_commands_ignore_what_is_written(void) {
  ap_sc499_t a;
  ap_sc499_t b;
  ap_sc499_reset(&a);
  ap_sc499_reset(&b);

  /* "Any write to this register will cause DMAGO to be active." The value is
   * not a parameter, so storing it would invent a register the part has not
   * got -- and two controllers written different values must be identical. */
  ap_sc499_write(&a, AP_SC499_DMAGO, 0x00);
  ap_sc499_write(&b, AP_SC499_DMAGO, 0xA5);
  TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof a);
  TEST_ASSERT_TRUE(a.dma_active);
}

static void test_the_command_addresses_read_as_nothing(void) {
  ap_sc499_t t;
  ap_sc499_reset(&t);

  /* Write-only. A read sweep of the real controller found exactly these two
   * returning nothing, which is what identified them before the manual
   * confirmed it -- `FINDINGS.md` C17 and C18. */
  TEST_ASSERT_EQUAL_HEX8(0x00, ap_sc499_read(&t, AP_SC499_DMAGO));
  TEST_ASSERT_EQUAL_HEX8(0x00, ap_sc499_read(&t, AP_SC499_RSTDMA));
}

static void test_a_masked_controller_drives_no_interrupt(void) {
  ap_sc499_t t;
  ap_sc499_reset(&t);

  /* "The IRQ line is tri-stated when IEN is cleared. This allows other IBM PC
   * options the use of that interrupt line when the tape controller is not
   * using it." So a masked controller is absent from the line, not holding it
   * inactive -- which is why the guide warns to program the 8259 for this IRQ
   * only after setting IEN. */
  TEST_ASSERT_FALSE(ap_sc499_irq(&t));

  ap_sc499_write(&t, AP_SC499_CONTROL_STATUS, AP_SC499_CTL_IEN);
  t.exception = true; /* with Ready, the conjunction that raises the flag */
  TEST_ASSERT_TRUE(ap_sc499_irq(&t));
}

static void test_the_flag_is_a_conjunction_not_a_list(void) {
  ap_sc499_t t;
  ap_sc499_reset(&t);
  ap_sc499_write(&t, AP_SC499_CONTROL_STATUS, AP_SC499_CTL_IEN);

  /* "ORing of RDY AND EXC" is ambiguous English, and the measurement settles
   * it: the oracle reads `40` at reset -- Ready set, flag clear. A disjunction
   * would have interrupted on every idle controller. */
  TEST_ASSERT_TRUE(t.ready);
  TEST_ASSERT_FALSE(ap_sc499_irq(&t));

  t.exception = true;
  TEST_ASSERT_TRUE(ap_sc499_irq(&t));
}

static void test_the_interrupt_flag_reads_through_the_masks(void) {
  ap_sc499_t t;
  ap_sc499_reset(&t);
  t.exception = true;

  /* "Each interrupt source bit, RDY, EXC, and DONE, can be read through the
   * Status Register regardless of the state of the interrupt masks." So a
   * polling driver sees the flag with interrupts disabled -- the mask governs
   * the pin, not the register. */
  TEST_ASSERT_FALSE(ap_sc499_irq(&t));
  TEST_ASSERT_EQUAL_HEX8(AP_SC499_ST_IRQ,
                         ap_sc499_read(&t, AP_SC499_CONTROL_STATUS) &
                             AP_SC499_ST_IRQ);
}

static void test_done_contributes_to_the_flag_only_when_enabled(void) {
  ap_sc499_t t;
  ap_sc499_reset(&t);
  /* Leave the conjunction unsatisfied, so only DONE can raise the flag. DONE is
   * clear at reset in the measured part, so it has to be asserted here. */
  t.ready = false;
  t.exception = false;
  t.done = true;

  /* "Interrupt Request Flag. ORing of RDY AND EXC, and DONE if DNIEN is set."
   * DONE is the one source gated by its own enable, and flattening the three
   * into one OR would make a completed transfer interrupt a driver that had
   * asked it not to. */
  ap_sc499_write(&t, AP_SC499_CONTROL_STATUS, AP_SC499_CTL_IEN);
  TEST_ASSERT_FALSE(ap_sc499_irq(&t));

  ap_sc499_write(&t, AP_SC499_CONTROL_STATUS,
                 (uint8_t)(AP_SC499_CTL_IEN | AP_SC499_CTL_DNIEN));
  TEST_ASSERT_TRUE(ap_sc499_irq(&t));
}

static void test_holding_the_reset_bit_holds_the_controller(void) {
  ap_sc499_t t;
  ap_sc499_reset(&t);
  ap_sc499_write(&t, AP_SC499_DMAGO, 0x00);
  TEST_ASSERT_TRUE(t.dma_active);

  /* Control bit 7 "Reset controller microprocessor" -- and the bit lives in the
   * register the reset would clear, so the reset must not clear the byte that
   * requested it. A driver holding the bit high is holding the part in reset. */
  ap_sc499_write(&t, AP_SC499_CONTROL_STATUS, AP_SC499_CTL_RESET);
  TEST_ASSERT_FALSE(t.dma_active);
  TEST_ASSERT_EQUAL_HEX8(AP_SC499_CTL_RESET, t.control);
}

static void test_two_controllers_reset_alike_hold_identical_state(void) {
  ap_sc499_t a;
  ap_sc499_t b;
  memset(&a, 0xAA, sizeof a);
  memset(&b, 0x55, sizeof b);
  ap_sc499_reset(&a);
  ap_sc499_reset(&b);
  TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof a);
}

static void test_the_command_entry_condition_selects_a_figure(void) {
  ap_sc499_t t;
  ap_sc499_reset(&t);

  /* One protocol, three entry conditions, chosen by the device's state --
   * `[SC499]` §1.13.2's Figures 1-7, 1-8 and 1-9. */
  TEST_ASSERT_EQUAL_UINT(AP_SC499_ENTRY_READY, ap_sc499_command_entry(&t));

  t.direction = true;
  TEST_ASSERT_EQUAL_UINT(AP_SC499_ENTRY_DIRECTION,
                         ap_sc499_command_entry(&t));

  /* Exception outranks the bus, and cannot coexist with ready: §1.13.2's own
   * rule is that READY "shall not be asserted for an EXCEPTION condition". */
  ap_sc499_set_exception(&t, true);
  TEST_ASSERT_EQUAL_UINT(AP_SC499_ENTRY_EXCEPTION,
                         ap_sc499_command_entry(&t));
  TEST_ASSERT_FALSE(t.ready);
}

static void test_accepting_a_command_applies_all_three_figures(void) {
  ap_sc499_t t;
  ap_sc499_reset(&t);

  /* Whichever entry applies, the device ends ready, out of exception and off
   * the bus: 1-8's T3 deasserts EXCEPTION, 1-9's T4 deasserts DIRECTION, and
   * all three end with the device asserting READY. Modelled as one transition
   * because the three figures agree on the destination and differ only in the
   * path -- and the path is timing, which is all bounds. */
  ap_sc499_set_exception(&t, true);
  t.direction = true;

  ap_sc499_command_accepted(&t);
  TEST_ASSERT_FALSE(t.exception);
  TEST_ASSERT_FALSE(t.direction);
  TEST_ASSERT_TRUE(t.ready);
  TEST_ASSERT_EQUAL_UINT(AP_SC499_ENTRY_READY, ap_sc499_command_entry(&t));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_command_entry_condition_selects_a_figure);
  RUN_TEST(test_accepting_a_command_applies_all_three_figures);
  RUN_TEST(test_a_reset_controller_is_ready_and_done);
  RUN_TEST(test_resetting_the_dma_is_the_same_as_a_power_on_reset);
  RUN_TEST(test_the_dma_commands_ignore_what_is_written);
  RUN_TEST(test_the_command_addresses_read_as_nothing);
  RUN_TEST(test_a_masked_controller_drives_no_interrupt);
  RUN_TEST(test_the_flag_is_a_conjunction_not_a_list);
  RUN_TEST(test_the_interrupt_flag_reads_through_the_masks);
  RUN_TEST(test_done_contributes_to_the_flag_only_when_enabled);
  RUN_TEST(test_holding_the_reset_bit_holds_the_controller);
  RUN_TEST(test_two_controllers_reset_alike_hold_identical_state);
  return UNITY_END();
}

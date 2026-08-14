/* Archive SC-499 cartridge tape controller, `[SC499]` §1.9. */

#include "unity.h"

#include <string.h>

#include "device/ap_sc499.h"

void setUp(void) {}
void tearDown(void) {}

/* Two of the five status bits are asserted **low**, which `[SC499]`'s page
 * image gives in a polarity column its text layer drops entirely. Linux's
 * `tpqic02.h` and the oracle's `sc499.cpp` both agree. This is the fact the
 * rest of the suite's status assertions rest on, so it is asserted directly
 * rather than left implicit in a hex constant. */
static void test_ready_and_exception_are_asserted_low(void) {
  ap_sc499_t t;
  ap_sc499_reset(&t);

  /* Not ready, not in exception: both bits read as ones. */
  TEST_ASSERT_EQUAL_HEX8(AP_SC499_ST_RDY | AP_SC499_ST_EXC,
                         ap_sc499_read(&t, AP_SC499_CONTROL_STATUS) &
                             (AP_SC499_ST_RDY | AP_SC499_ST_EXC));

  /* Asserting them *clears* the bits. */
  t.ready = true;
  ap_sc499_set_exception(&t, true);
  TEST_ASSERT_EQUAL_HEX8(0, ap_sc499_read(&t, AP_SC499_CONTROL_STATUS) &
                                AP_SC499_ST_EXC);
}

static void test_a_reset_controller_is_not_ready_and_is_done(void) {
  ap_sc499_t t;
  ap_sc499_reset(&t);
  uint8_t status = ap_sc499_read(&t, AP_SC499_CONTROL_STATUS);

  /* The oracle's idle controller reads `40`. That was read here as "Ready is
   * asserted", which required RDY to be active high; it is active low, so `40`
   * means the opposite -- a controller that has just been reset is **not
   * ready**. The byte is unchanged and its meaning is inverted. */
  TEST_ASSERT_EQUAL_HEX8(AP_SC499_ST_RDY, status & AP_SC499_ST_RDY);
  TEST_ASSERT_FALSE(t.ready);

  /* And DONE is set, which the guide states twice: RSTDMA "clears all Control
   * Register bits to 0, and sets DONE to 1", and power-on reset "performs the
   * same functions". It was disbelieved while the bit numbers were thought
   * unknown. They are known -- bit 4, active high, from three agreeing sources
   * -- so the sentence means what it says. The oracle sets only RDY at reset
   * and so reads `40` where this reads `50`; the divergence is deliberate and
   * recorded in `PROJECT_STATUS.md`. */
  TEST_ASSERT_EQUAL_HEX8(AP_SC499_ST_DONE, status & AP_SC499_ST_DONE);
  TEST_ASSERT_EQUAL_HEX8(AP_SC499_ST_EXC, status & AP_SC499_ST_EXC);
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
  ap_sc499_set_exception(&t, true); /* either source alone raises the flag */
  TEST_ASSERT_TRUE(ap_sc499_irq(&t));
}

/* "ORing of RDY AND EXC" is ambiguous English, and this suite used to assert
 * the conjunction reading on the strength of a measurement it was misreading:
 * the oracle reads `40` at reset, taken to mean Ready asserted with the flag
 * clear, so a disjunction "would have interrupted on every idle controller".
 * RDY is active low, so `40` means Ready is *not* asserted -- a reset
 * controller asserts neither source, and the disjunction is clear at reset
 * exactly as measured. It is a list. */
static void test_the_flag_is_a_list_and_either_source_alone_raises_it(void) {
  ap_sc499_t t;
  ap_sc499_reset(&t);
  ap_sc499_write(&t, AP_SC499_CONTROL_STATUS, AP_SC499_CTL_IEN);

  /* Neither source asserted: no interrupt, which is what the measurement
   * actually shows. */
  TEST_ASSERT_FALSE(t.ready);
  TEST_ASSERT_FALSE(t.exception);
  TEST_ASSERT_FALSE(ap_sc499_irq(&t));

  /* Ready alone is enough -- and this is the case the conjunction got wrong.
   * READY asserted with no exception is a *completed command*, precisely when
   * a driver expects an interrupt, and a conjunction stays silent for it. */
  t.ready = true;
  TEST_ASSERT_TRUE(ap_sc499_irq(&t));

  /* And exception alone, with ready cleared. */
  t.ready = false;
  ap_sc499_set_exception(&t, true);
  TEST_ASSERT_TRUE(ap_sc499_irq(&t));
}

/* **The two status bytes Domain/OS's tape reset waits for, `F7` then `57`.**
 *
 * This pair was recorded as evidence that the interrupt flag must be a *latch*
 * -- "`F7` carries IRQF set while RDY and EXC are unasserted", which no live
 * ORing could produce. It rests on reading bit 7 as active high, and p. 12
 * prints it as `BIT 7  0 = IRQF` in the same column that makes RDY and EXC
 * active low. The same misreading of the same column is already recorded here
 * for RDY.
 *
 * Read with the manual's polarity, both bytes are ordinary states of a derived
 * flag, and this core produces them:
 *
 *     F7  IRQF *not* asserted, not ready, no exception, DONE set, DNIEN clear
 *     57  IRQF asserted because EXCEPTION is, DONE still set
 *
 * §1.10 is the mechanism, and it says level rather than latch: "Each interrupt
 * source bit, RDY, EXC, and DONE ... can be read through the Status Register
 * regardless of the state of the interrupt masks." Nothing in the guide clears
 * the flag on a status read, which is what a latch would need. */
static void test_the_reset_handshake_bytes_are_reachable_without_a_latch(void) {
  ap_sc499_t t;
  ap_sc499_reset(&t);

  /* DONE with DNIEN clear: the flag stays down because DONE only contributes
   * when enabled, so bit 7 reads 1 -- *not asserted*. */
  t.ready = false;
  ap_sc499_set_exception(&t, false);
  t.done = true;
  TEST_ASSERT_EQUAL_HEX8(0xF7u, ap_sc499_read(&t, AP_SC499_CONTROL_STATUS));

  /* Then the exception the reset produces, which pulls IRQF down with it. */
  ap_sc499_set_exception(&t, true);
  TEST_ASSERT_EQUAL_HEX8(0x57u, ap_sc499_read(&t, AP_SC499_CONTROL_STATUS));

  /* And a second read returns the same byte: reading status does not clear the
   * flag, which is the property that separates a level from a latch. */
  TEST_ASSERT_EQUAL_HEX8(0x57u, ap_sc499_read(&t, AP_SC499_CONTROL_STATUS));

  /* It follows the source down, too. */
  ap_sc499_set_exception(&t, false);
  TEST_ASSERT_EQUAL_HEX8(0xF7u, ap_sc499_read(&t, AP_SC499_CONTROL_STATUS));
}

static void test_the_interrupt_flag_reads_through_the_masks(void) {
  ap_sc499_t t;
  ap_sc499_reset(&t);
  ap_sc499_set_exception(&t, true);

  /* "Each interrupt source bit, RDY, EXC, and DONE, can be read through the
   * Status Register regardless of the state of the interrupt masks." So a
   * polling driver sees the flag with interrupts disabled -- the mask governs
   * the pin, not the register.
   *
   * The flag is **active low**, as `[SC499]`'s page image prints it, so an
   * asserted interrupt reads bit 7 as **zero**. That is the polarity this core
   * had backwards, and the whole reason Domain/OS's tape reset never got past
   * its first comparison. */
  TEST_ASSERT_FALSE(ap_sc499_irq(&t));
  TEST_ASSERT_EQUAL_HEX8(0u, ap_sc499_read(&t, AP_SC499_CONTROL_STATUS) &
                                 AP_SC499_ST_IRQ);

  /* And with nothing asserted the bit stands at one, which is what the driver
   * waits for: an idle controller reads `F7`. */
  ap_sc499_reset(&t);
  TEST_ASSERT_EQUAL_HEX8(0xF7u, ap_sc499_read(&t, AP_SC499_CONTROL_STATUS));
}

static void test_done_contributes_to_the_flag_only_when_enabled(void) {
  ap_sc499_t t;
  ap_sc499_reset(&t);
  /* Leave both of the ungated sources clear, so only DONE can raise the flag.
   * DONE is already set by the reset, which is itself the corrected fact. */
  t.ready = false;
  t.exception = false;
  TEST_ASSERT_TRUE(t.done);

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
   * all three end with the device asserting READY. The three figures agree on
   * the destination and differ in the path -- and the path is **time**, which
   * this used to skip: the destination was reached in one transition, so a
   * command completed the instant it was issued. */
  ap_sc499_set_exception(&t, true);
  t.direction = true;

  ap_sc499_command_accepted(&t);

  /* Exception outranks the bus, so this is Figure 1-8's entry. */
  TEST_ASSERT_EQUAL_UINT(AP_SC499_ENTRY_EXCEPTION, t.entry);

  /* READY goes down at once -- the one edge not taken at its bound, because a
   * device that still looked ready would look *finished*. Everything else is
   * still as it was: the command has been accepted, not executed. */
  TEST_ASSERT_FALSE(t.ready);
  TEST_ASSERT_TRUE(t.exception);
  TEST_ASSERT_TRUE(t.direction);
  TEST_ASSERT_TRUE(ap_sc499_executing(&t));

  /* One unit short of the deadline changes nothing. */
  ap_sc499_advance(&t, AP_SC499_T_EXCEPTION_TO_READY - 1u);
  TEST_ASSERT_TRUE(ap_sc499_executing(&t));
  TEST_ASSERT_TRUE(t.exception);

  /* And at the deadline the whole destination arrives at once. */
  ap_sc499_advance(&t, AP_SC499_T_EXCEPTION_TO_READY);
  TEST_ASSERT_FALSE(ap_sc499_executing(&t));
  TEST_ASSERT_FALSE(t.exception);
  TEST_ASSERT_FALSE(t.direction);
  TEST_ASSERT_TRUE(t.ready);
  TEST_ASSERT_EQUAL_UINT(AP_SC499_ENTRY_READY, ap_sc499_command_entry(&t));
}

/* Each figure's total, checked against `[SC499]` §1.13.2's own bounds. Figure
 * 1-9's is a *sum*: the device releases the bus and then asserts READY, two
 * intervals in sequence rather than one bound covering both. */
static void test_each_figure_takes_the_interval_its_bounds_give_it(void) {
  TEST_ASSERT_EQUAL_UINT64(AP_SC499_T_COMMAND_EXECUTION,
                           ap_sc499_handshake_duration(AP_SC499_ENTRY_READY));
  TEST_ASSERT_EQUAL_UINT64(
      AP_SC499_T_EXCEPTION_TO_READY,
      ap_sc499_handshake_duration(AP_SC499_ENTRY_EXCEPTION));
  TEST_ASSERT_EQUAL_UINT64(
      AP_SC499_T_DIRECTION_RELEASE + AP_SC499_T_DIRECTION_TO_READY,
      ap_sc499_handshake_duration(AP_SC499_ENTRY_DIRECTION));

  /* Figure 1-7's is by far the longest -- half a second against microseconds --
   * because "< 500 ms" is the drive executing a command rather than a bus edge
   * settling. Taking the bound makes every ordinary command cost the slowest
   * one the standard permits, which is the stated cost of a `PROVISIONAL`
   * figure and the reason closing it needs a measurement. */
  TEST_ASSERT_TRUE(ap_sc499_handshake_duration(AP_SC499_ENTRY_READY) >
                   ap_sc499_handshake_duration(AP_SC499_ENTRY_DIRECTION));
}

/* Advancing must not run the handshake backwards, and must be idempotent: the
 * board advances every device to the same instant on every tick, so this is
 * called far more often than it does anything. */
static void test_advancing_is_idempotent_and_refuses_to_go_backwards(void) {
  ap_sc499_t t;
  ap_sc499_reset(&t);
  ap_sc499_advance(&t, 1000u);
  ap_sc499_command_accepted(&t);
  TEST_ASSERT_TRUE(ap_sc499_executing(&t));

  /* Backwards: ignored, and the deadline still stands ahead of it. */
  ap_sc499_advance(&t, 0u);
  TEST_ASSERT_TRUE(ap_sc499_executing(&t));

  const ap_time_t due = 1000u + AP_SC499_T_COMMAND_EXECUTION;
  ap_sc499_advance(&t, due);
  TEST_ASSERT_FALSE(ap_sc499_executing(&t));
  TEST_ASSERT_TRUE(t.ready);

  /* Again, well past: nothing to undo and nothing to redo. */
  ap_sc499_advance(&t, due * 2u);
  TEST_ASSERT_FALSE(ap_sc499_executing(&t));
  TEST_ASSERT_TRUE(t.ready);
}

/* `[SC499]` p. 12: "(BITS 0-2 Not Used)". Nothing on the controller drives
 * them, so they read as one -- and the driver depends on it. Domain/OS's tape
 * reset waits for the status register to read exactly `F7` and then exactly
 * `57`, and both constants carry these three bits, so a controller that read
 * them as zero could never satisfy either comparison. Named as its own test
 * because the dump in `tape_suite` asserts the byte and this asserts the
 * reason. */
static void test_the_unused_low_bits_read_as_one(void) {
  ap_sc499_t tape;
  ap_sc499_reset(&tape);

  const uint8_t status = ap_sc499_read(&tape, AP_SC499_CONTROL_STATUS);
  TEST_ASSERT_EQUAL_HEX8(AP_SC499_ST_UNUSED,
                         (uint8_t)(status & AP_SC499_ST_UNUSED));

  /* And they stay set through a state change, since nothing drives them at
   * any point -- a model that set them once at reset would pass the line
   * above and still fail the driver. */
  tape.exception = true;
  const uint8_t later = ap_sc499_read(&tape, AP_SC499_CONTROL_STATUS);
  TEST_ASSERT_EQUAL_HEX8(AP_SC499_ST_UNUSED,
                         (uint8_t)(later & AP_SC499_ST_UNUSED));
}

/* `[SC499]` §1.12: RSTSAC "must be set, held for more than 25 usec, then
 * cleared". The *release* is what starts the controller's own reset, and it
 * comes out of that reporting a power-on-reset condition -- which QIC-02
 * reports as an exception (`tpqic02.h`'s `TP_POR`, obtained with READ STATUS,
 * which a host issues in response to one).
 *
 * The ordering is the part that matters and the part a driver depends on: the
 * exception must arrive *after* an interval, not with the release, or a driver
 * polling for the idle status first would never see it. Domain/OS does exactly
 * that -- `F7` then `57`. */
static void test_releasing_the_reset_raises_an_exception_only_after_the_interval(
    void) {
  ap_sc499_t t;
  ap_sc499_reset(&t);

  /* Held for more than the minimum, as the guide requires of the host -- a pulse
   * narrower than that is tested below and is not a reset. */
  const ap_time_t release = 100u + AP_SC499_T_RESET_MIN_HOLD + 1u;
  ap_sc499_write(&t, AP_SC499_CONTROL_STATUS, AP_SC499_CTL_RESET);
  ap_sc499_advance(&t, 100u);
  ap_sc499_advance(&t, release);
  ap_sc499_write(&t, AP_SC499_CONTROL_STATUS, 0u);

  /* Immediately after the release the controller is idle, not excepting: the
   * status reads `F7`, which is the first thing the driver waits for. */
  ap_sc499_advance(&t, release + 1000u);
  TEST_ASSERT_FALSE(t.exception);
  TEST_ASSERT_EQUAL_HEX8(0xF7u, ap_sc499_read(&t, AP_SC499_CONTROL_STATUS));

  /* Short of the interval, still idle. */
  ap_sc499_advance(&t, release + 1000u + AP_SC499_T_RESET_TO_EXCEPTION - 1u);
  TEST_ASSERT_FALSE(t.exception);

  /* At it, the exception is asserted and the status reads `57` -- the second
   * thing the driver waits for. */
  ap_sc499_advance(&t, release + 1000u + AP_SC499_T_RESET_TO_EXCEPTION);
  TEST_ASSERT_TRUE(t.exception);
  TEST_ASSERT_EQUAL_HEX8(0x57u, ap_sc499_read(&t, AP_SC499_CONTROL_STATUS));
}

/* `[SC499]` §1.12 states the hold as a requirement -- "must be set, held for
 * **more than** 25 usec, then cleared" -- and a requirement that nothing
 * enforces is a comment. A runt pulse resets nothing, so no confidence test
 * runs and no exception ever arrives, however long the host then waits.
 *
 * "More than" is strict, so the boundary belongs to the runt: exactly the
 * minimum is not more than it. */
static void test_a_reset_pulse_shorter_than_the_minimum_does_not_reset(void) {
  for (unsigned held = 0; held <= 1u; held++) {
    ap_sc499_t t;
    ap_sc499_reset(&t);

    /* held == 0: the two edges inside one tick, the narrowest pulse there is.
     * held == 1: exactly the minimum, which the word "more" excludes. */
    const ap_time_t release =
        100u + (held ? AP_SC499_T_RESET_MIN_HOLD : (ap_time_t)0);
    ap_sc499_write(&t, AP_SC499_CONTROL_STATUS, AP_SC499_CTL_RESET);
    ap_sc499_advance(&t, 100u);
    ap_sc499_advance(&t, release);
    ap_sc499_write(&t, AP_SC499_CONTROL_STATUS, 0u);

    /* Well past the interval a wide pulse would have used. */
    ap_sc499_advance(&t, release + 4u * AP_SC499_T_RESET_TO_EXCEPTION);
    TEST_ASSERT_FALSE(t.exception);
    TEST_ASSERT_EQUAL_HEX8(0xF7u, ap_sc499_read(&t, AP_SC499_CONTROL_STATUS));
  }
}

/* §1.12 gives RSTSAC two release paths: "cleared by either writing a 0 to
 * Control Register Bit 7 **or by a RSTDMA**". Both end the microprocessor reset,
 * so both start the confidence test -- modelling only the first would leave a
 * documented way to reset the controller that produced no exception at all. */
static void test_a_dma_reset_releases_the_hold_and_runs_the_confidence_test(
    void) {
  ap_sc499_t t;
  ap_sc499_reset(&t);

  const ap_time_t release = 100u + AP_SC499_T_RESET_MIN_HOLD + 1u;
  ap_sc499_write(&t, AP_SC499_CONTROL_STATUS, AP_SC499_CTL_RESET);
  ap_sc499_advance(&t, 100u);
  ap_sc499_advance(&t, release);
  ap_sc499_write(&t, AP_SC499_RSTDMA, 0u);

  ap_sc499_advance(&t, release + AP_SC499_T_RESET_TO_EXCEPTION);
  TEST_ASSERT_FALSE(t.exception);
  ap_sc499_advance(&t, release + 2u * AP_SC499_T_RESET_TO_EXCEPTION);
  TEST_ASSERT_TRUE(t.exception);

  /* And a RSTDMA with nothing held is a DMA reset and nothing more. */
  ap_sc499_t idle;
  ap_sc499_reset(&idle);
  ap_sc499_write(&idle, AP_SC499_RSTDMA, 0u);
  ap_sc499_advance(&idle, 4u * AP_SC499_T_RESET_TO_EXCEPTION);
  TEST_ASSERT_FALSE(idle.exception);
}

/* A driver that rewrites the control byte with the bit still up is holding the
 * part down, not re-pulsing it. The rewrite runs the reset again -- which clears
 * every field -- so the elapsed hold has to be carried across it, or the release
 * that follows looks like a runt and silently does nothing. */
static void test_rewriting_the_control_byte_does_not_restart_the_hold(void) {
  ap_sc499_t t;
  ap_sc499_reset(&t);

  ap_sc499_write(&t, AP_SC499_CONTROL_STATUS, AP_SC499_CTL_RESET);
  ap_sc499_advance(&t, 100u);

  /* Most of the minimum elapses, then the byte is written again with the bit
   * still up, then the rest of it. Neither half alone is a legal hold. */
  const ap_time_t half = AP_SC499_T_RESET_MIN_HOLD / 2u;
  ap_sc499_advance(&t, 100u + half);
  ap_sc499_write(&t, AP_SC499_CONTROL_STATUS,
                 (uint8_t)(AP_SC499_CTL_RESET | AP_SC499_CTL_IEN));
  const ap_time_t release = 100u + AP_SC499_T_RESET_MIN_HOLD + 1u;
  ap_sc499_advance(&t, release);
  ap_sc499_write(&t, AP_SC499_CONTROL_STATUS, 0u);

  /* Two advances: the first dates the arming, the second reaches the deadline
   * it set. The release itself cannot date it -- see `ap_sc499_advance`. */
  ap_sc499_advance(&t, release + AP_SC499_T_RESET_TO_EXCEPTION);
  TEST_ASSERT_FALSE(t.exception);
  ap_sc499_advance(&t, release + 2u * AP_SC499_T_RESET_TO_EXCEPTION);
  TEST_ASSERT_TRUE(t.exception);
}

static void test_the_handshake_times_are_exact_in_base_units(void) {
  /* `[SC499]` §1.13.2's figures are all bounds, and this core models them at the
   * bound -- `PROVISIONAL`, and recorded as such. What is *not* provisional is
   * that each converts exactly: no figure is rounded on top of being a bound,
   * so closing them later is a change of value and not of representability. */
  TEST_ASSERT_EQUAL_UINT64(AP_TIME_BASE_HZ / 1000000u * 1u,
                           AP_SC499_T_REQUEST_TO_NOT_READY);
  TEST_ASSERT_EQUAL_UINT64(AP_TIME_BASE_HZ / 1000000u * 150u,
                           AP_SC499_T_DIRECTION_RELEASE);
  TEST_ASSERT_EQUAL_UINT64(AP_TIME_BASE_HZ / 1000000u * 500u,
                           AP_SC499_T_DIRECTION_TO_READY);
  TEST_ASSERT_EQUAL_UINT64(AP_TIME_BASE_HZ / 2u, AP_SC499_T_COMMAND_EXECUTION);
  TEST_ASSERT_EQUAL_UINT64(AP_TIME_BASE_HZ / 1000000u * 20u,
                           AP_SC499_T_CLOSE_MIN);
  TEST_ASSERT_EQUAL_UINT64(AP_TIME_BASE_HZ / 1000000u * 100u,
                           AP_SC499_T_CLOSE_MAX);

  /* And the window the specification leaves open is real: the close is bounded
   * on both sides, so a model choosing one figure is choosing within 80
   * microseconds of slack rather than reading a number off the page. */
  TEST_ASSERT_TRUE(AP_SC499_T_CLOSE_MIN < AP_SC499_T_CLOSE_MAX);
}

/* Every modelled duration sits inside Apollo's own documented time-out.
 *
 * `08845 Apollo Specification for QIC-36 Tape Controller` §12.3 gives the
 * "maximum QIC-02 Command Set Timings before time-out conditions are
 * generated". Those are the *host's* patience, not the drive's speed, so they
 * bound this module rather than supplying its figures -- but a figure at or
 * beyond one of them would be a command the driver had already abandoned, which
 * is a mistake no amount of internal consistency would catch.
 *
 * It is also the second independent source for the reset's five seconds:
 * `[SC499]` §1.8.1 says EXC- is asserted "within five seconds", and this is
 * Apollo saying the same about the machine that carries the controller. */
static void test_the_modelled_times_sit_inside_apollos_documented_maxima(void) {
  TEST_ASSERT_TRUE(AP_SC499_T_RESET_TO_EXCEPTION < AP_SC499_MAX_RESET);
  TEST_ASSERT_TRUE(AP_SC499_T_COMMAND_EXECUTION < AP_SC499_MAX_BOT);
  TEST_ASSERT_TRUE(AP_SC499_T_COMMAND_EXECUTION < AP_SC499_MAX_RETENSION);
  TEST_ASSERT_TRUE(AP_SC499_T_COMMAND_EXECUTION < AP_SC499_MAX_ERASE);

  /* And the two documents agree on the reset ceiling, to the second. */
  TEST_ASSERT_EQUAL_UINT64(AP_SC499_US(5000000), AP_SC499_MAX_RESET);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_modelled_times_sit_inside_apollos_documented_maxima);
  RUN_TEST(test_the_handshake_times_are_exact_in_base_units);
  RUN_TEST(test_the_command_entry_condition_selects_a_figure);
  RUN_TEST(test_accepting_a_command_applies_all_three_figures);
  RUN_TEST(test_each_figure_takes_the_interval_its_bounds_give_it);
  RUN_TEST(test_advancing_is_idempotent_and_refuses_to_go_backwards);
  RUN_TEST(test_ready_and_exception_are_asserted_low);
  RUN_TEST(test_a_reset_controller_is_not_ready_and_is_done);
  RUN_TEST(test_resetting_the_dma_is_the_same_as_a_power_on_reset);
  RUN_TEST(test_the_dma_commands_ignore_what_is_written);
  RUN_TEST(test_the_command_addresses_read_as_nothing);
  RUN_TEST(test_a_masked_controller_drives_no_interrupt);
  RUN_TEST(test_the_reset_handshake_bytes_are_reachable_without_a_latch);
  RUN_TEST(test_the_flag_is_a_list_and_either_source_alone_raises_it);
  RUN_TEST(test_the_interrupt_flag_reads_through_the_masks);
  RUN_TEST(test_done_contributes_to_the_flag_only_when_enabled);
  RUN_TEST(test_holding_the_reset_bit_holds_the_controller);
  RUN_TEST(test_the_unused_low_bits_read_as_one);
  RUN_TEST(test_releasing_the_reset_raises_an_exception_only_after_the_interval);
  RUN_TEST(test_a_reset_pulse_shorter_than_the_minimum_does_not_reset);
  RUN_TEST(test_a_dma_reset_releases_the_hold_and_runs_the_confidence_test);
  RUN_TEST(test_rewriting_the_control_byte_does_not_restart_the_hold);
  RUN_TEST(test_two_controllers_reset_alike_hold_identical_state);
  return UNITY_END();
}

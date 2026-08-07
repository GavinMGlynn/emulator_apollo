/* Motorola MC6840, `[6840]` MC6840UM, read from the page images -- the scan has
 * no text layer. Apollo context from `008778-03` §3.8. */

#include "unity.h"

#include <string.h>

#include "device/ap_mc6840.h"
#include "time/ap_time.h"

void setUp(void) {}
void tearDown(void) {}

/* §4.1's recommended order: latches, then CR3, CR2, CR1. `mode` is the mode and
 * flag bits for every timer; bit 0 of each register is supplied here. */
static void program_continuous(ap_mc6840_t *ptm, uint16_t period_latch,
                               uint8_t mode) {
  ap_mc6840_reset(ptm);

  /* Hold everything while programming: CR1 bit 0 = 1 is "ALL TIMERS PRESET".
   * CR2 bit 0 must be 1 first for CR1 to be reachable at RS 0. */
  ap_mc6840_write(ptm, AP_MC6840_RS_CONTROL_2_OR_STATUS, 0x01);
  ap_mc6840_write(ptm, AP_MC6840_RS_CONTROL_1_OR_3, 0x01);

  for (unsigned i = 0; i < AP_MC6840_TIMERS; i++) {
    ap_mc6840_rs_t msb = (ap_mc6840_rs_t)(AP_MC6840_RS_TIMER1_MSB + i * 2u);
    ap_mc6840_rs_t lsb = (ap_mc6840_rs_t)(AP_MC6840_RS_TIMER1_LSB + i * 2u);
    ap_mc6840_write(ptm, msb, (uint8_t)(period_latch >> 8));
    ap_mc6840_write(ptm, lsb, (uint8_t)(period_latch & 0xFFu));
  }

  /* CR3 first, while CR2 bit 0 is still 0 -- so clear it, write CR3, set it
   * again, then write CR1 and release. */
  ap_mc6840_write(ptm, AP_MC6840_RS_CONTROL_2_OR_STATUS, 0x00);
  ap_mc6840_write(ptm, AP_MC6840_RS_CONTROL_1_OR_3, mode); /* CR3 */
  ap_mc6840_write(ptm, AP_MC6840_RS_CONTROL_2_OR_STATUS,
                  (uint8_t)(mode | 0x01u)); /* CR2, keeping CR1 reachable */
  /* Bit 0 must be cleared for CR1 and *only* for CR1. It is the prescaler in
   * CR3, the register select in CR2, and "ALL TIMERS PRESET" here -- so passing
   * the same byte to all three holds the whole part stopped, which is how this
   * helper was first written and why the prescaler test found nothing. */
  ap_mc6840_write(ptm, AP_MC6840_RS_CONTROL_1_OR_3, (uint8_t)(mode & 0xFEu));
}

/* 16-bit continuous, output enabled, interrupt enabled: bits 5,4,3 = 0,1,0. */
#define CONTINUOUS (0x10u | AP_MC6840_CR_IRQ_ENABLE | AP_MC6840_CR_OUTPUT_ENABLE)

static unsigned clocks_to_interrupt(ap_mc6840_t *ptm, unsigned index,
                                    unsigned limit) {
  for (unsigned i = 1; i <= limit; i++) {
    ap_mc6840_clock(ptm, index);
    if (ap_mc6840_irq(ptm)) {
      return i;
    }
  }
  return limit + 1u;
}


/* Control-register bit 1 selects the internal `E` clock against the external
 * `Cx` input, and this core read it nowhere -- so every timer counted the
 * internal clock whatever a driver selected, which is the failure mode that
 * looks like the timer working. */
static void test_the_clock_source_selection_is_reported(void) {
  ap_mc6840_t ptm;
  ap_mc6840_reset(&ptm);

  /* Reset leaves the control registers clear, which is the external source. */
  TEST_ASSERT_FALSE(ap_mc6840_uses_internal_clock(&ptm, 0u));

  /* Selecting the internal clock on timer 1 says so, and does not move the
   * others -- the bit is per timer, in each timer's own control register.
   * `CR2` bit 0 chooses whether address 0 is `CR1` or `CR3`, so it is set
   * first. */
  ap_mc6840_write(&ptm, AP_MC6840_RS_CONTROL_2_OR_STATUS, 0x01u);
  ap_mc6840_write(&ptm, AP_MC6840_RS_CONTROL_1_OR_3,
                  AP_MC6840_CR_INTERNAL_CLOCK);
  TEST_ASSERT_TRUE(ap_mc6840_uses_internal_clock(&ptm, 0u));
  TEST_ASSERT_FALSE(ap_mc6840_uses_internal_clock(&ptm, 1u));
}

static void test_the_latches_come_up_all_ones(void) {
  ap_mc6840_t ptm;
  ap_mc6840_reset(&ptm);

  /* §4.1: "If the latches are not written, they default to $FFFF." A zeroed
   * latch would instead give the shortest possible period rather than the
   * longest -- the opposite end of the range, and an interrupt storm on any
   * machine that enabled a timer before programming it. */
  for (unsigned i = 0; i < AP_MC6840_TIMERS; i++) {
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, ptm.timer[i].latch);
  }
}

static void test_control_register_three_is_selected_after_reset(void) {
  ap_mc6840_t ptm;
  ap_mc6840_reset(&ptm);

  /* §4.1: "bit 0 of control register 2 defaults to zero (control register #3 is
   * selected address zero)". So the first write to RS 0 lands in CR3. */
  ap_mc6840_write(&ptm, AP_MC6840_RS_CONTROL_1_OR_3, 0xAA);
  TEST_ASSERT_EQUAL_HEX8(0xAA, ptm.timer[2].control);
  TEST_ASSERT_EQUAL_HEX8(0x00, ptm.timer[0].control);
}

static void test_one_address_reaches_two_control_registers(void) {
  ap_mc6840_t ptm;
  ap_mc6840_reset(&ptm);

  /* Figure 2-6: "CR20 = 0  Write Control Register # 3 / CR20 = 1  Write Control
   * Register #1". Which register an address means is held in a *third*
   * register, so a write to RS 0 without setting up CR2 first silently
   * reprograms the prescaler instead of the timers. */
  ap_mc6840_write(&ptm, AP_MC6840_RS_CONTROL_2_OR_STATUS, 0x01);
  ap_mc6840_write(&ptm, AP_MC6840_RS_CONTROL_1_OR_3, 0x55);
  TEST_ASSERT_EQUAL_HEX8(0x55, ptm.timer[0].control);
  TEST_ASSERT_EQUAL_HEX8(0x00, ptm.timer[2].control);
}

static void test_a_sixteen_bit_write_takes_the_buffered_high_byte(void) {
  ap_mc6840_t ptm;
  ap_mc6840_reset(&ptm);

  /* §3.5.1: "When writing to the LS byte of the timer latch ... the contents of
   * the MSB buffer is internally written to the MS byte of that latch." */
  ap_mc6840_write(&ptm, AP_MC6840_RS_TIMER2_MSB, 0x12);
  ap_mc6840_write(&ptm, AP_MC6840_RS_TIMER2_LSB, 0x34);
  TEST_ASSERT_EQUAL_HEX16(0x1234, ptm.timer[1].latch);
  /* And the counter is reloaded, so a new period takes effect at once. */
  TEST_ASSERT_EQUAL_HEX16(0x1234, ptm.timer[1].counter);
}

static void test_a_sixteen_bit_read_latches_the_low_byte(void) {
  ap_mc6840_t ptm;
  program_continuous(&ptm, 0x0123, CONTINUOUS);

  /* §3.5.2: the MS byte is read from the counter and "the LS byte is internally
   * written to the LSB buffer register which may be read at the next memory
   * location". Without that, a counter that ticked between the two reads would
   * hand back a torn value. */
  uint8_t high = ap_mc6840_read(&ptm, AP_MC6840_RS_TIMER1_MSB);
  ap_mc6840_clock(&ptm, 0); /* the counter moves between the two reads */
  ap_mc6840_clock(&ptm, 0);
  uint8_t low = ap_mc6840_read(&ptm, AP_MC6840_RS_TIMER1_LSB);

  TEST_ASSERT_EQUAL_HEX16(0x0123, (uint16_t)((high << 8) | low));
}

static void test_a_period_is_the_latch_plus_one_clocks(void) {
  ap_mc6840_t ptm;
  program_continuous(&ptm, 9, CONTINUOUS);

  /* §3.7.1: "A = the total 16-bit count in the latch +1, times the period of
   * the clock", and §3.7's "Time Out -- occurance one count after the contents
   * of a timer equals $0000."
   *
   * The plus one is the whole point: reloading when the counter *reaches* zero
   * gives every period one clock too few, which is a rounding error at a large
   * latch and a total failure at a small one. */
  TEST_ASSERT_EQUAL_UINT(10u, clocks_to_interrupt(&ptm, 0, 64));
}

static void test_a_latch_of_zero_still_takes_one_clock(void) {
  ap_mc6840_t ptm;
  program_continuous(&ptm, 0, CONTINUOUS);

  /* The degenerate end of the same rule, and the one that separates "latch + 1"
   * from "latch": a zero latch is a one-clock period, not a zero-clock one.
   * A model that reloaded on reaching zero would never advance here at all. */
  TEST_ASSERT_EQUAL_UINT(1u, clocks_to_interrupt(&ptm, 0, 8));
}

static void test_a_continuous_timer_reloads_and_interrupts_again(void) {
  ap_mc6840_t ptm;
  program_continuous(&ptm, 4, CONTINUOUS);

  /* "Continuous": the period repeats without reprogramming. */
  TEST_ASSERT_EQUAL_UINT(5u, clocks_to_interrupt(&ptm, 0, 32));

  ap_mc6840_write(&ptm, AP_MC6840_RS_CONTROL_2_OR_STATUS, 0x01); /* clear via */
  (void)ap_mc6840_read(&ptm, AP_MC6840_RS_CONTROL_2_OR_STATUS);  /* the two- */
  (void)ap_mc6840_read(&ptm, AP_MC6840_RS_TIMER1_MSB);           /* step read */
  TEST_ASSERT_FALSE(ap_mc6840_irq(&ptm));

  TEST_ASSERT_EQUAL_UINT(5u, clocks_to_interrupt(&ptm, 0, 32));
}

static void test_the_output_toggles_at_each_time_out(void) {
  ap_mc6840_t ptm;
  program_continuous(&ptm, 2, CONTINUOUS);

  /* §3.7.1's diagram: the output is a square wave whose half-period is the
   * count, so it inverts on each time out rather than pulsing. */
  bool first = ap_mc6840_output(&ptm, 0);
  for (unsigned i = 0; i < 3; i++) {
    ap_mc6840_clock(&ptm, 0);
  }
  TEST_ASSERT_NOT_EQUAL(first, ap_mc6840_output(&ptm, 0));
  for (unsigned i = 0; i < 3; i++) {
    ap_mc6840_clock(&ptm, 0);
  }
  TEST_ASSERT_EQUAL(first, ap_mc6840_output(&ptm, 0));
}

static void test_a_masked_output_reads_low_however_the_timer_runs(void) {
  ap_mc6840_t ptm;
  program_continuous(&ptm, 2, 0x10u | AP_MC6840_CR_IRQ_ENABLE);

  /* §3.6.1 bit 7: "If the output is masked it will always be electrically
   * low." */
  for (unsigned i = 0; i < 12; i++) {
    ap_mc6840_clock(&ptm, 0);
    TEST_ASSERT_FALSE(ap_mc6840_output(&ptm, 0));
  }
}

static void test_an_individual_interrupt_flag_cannot_be_masked(void) {
  ap_mc6840_t ptm;
  program_continuous(&ptm, 3, 0x10u); /* no CR bit 6 */

  /* §3.11: "Individual timer interrupts cannot be masked." So the status
   * register shows the flag even with the composite interrupt disabled -- which
   * is exactly the state a polling driver reads, and a model that masked the
   * per-timer bits would hide it. */
  for (unsigned i = 0; i < 4; i++) {
    ap_mc6840_clock(&ptm, 0);
  }
  TEST_ASSERT_FALSE(ap_mc6840_irq(&ptm));

  uint8_t status = ap_mc6840_read(&ptm, AP_MC6840_RS_CONTROL_2_OR_STATUS);
  TEST_ASSERT_EQUAL_HEX8(AP_MC6840_STATUS_TIMER1,
                         status & AP_MC6840_STATUS_TIMER1);
  TEST_ASSERT_EQUAL_HEX8(0, status & AP_MC6840_STATUS_COMPOSITE);
}

static void test_the_composite_flag_needs_the_interrupt_enabled(void) {
  ap_mc6840_t ptm;
  program_continuous(&ptm, 3, CONTINUOUS);

  /* §3.11: "A composite interrupt is caused by a timer interrupt *and* that
   * timer's interrupt flag enabled (CRX6 = 1)." */
  for (unsigned i = 0; i < 4; i++) {
    ap_mc6840_clock(&ptm, 0);
  }
  uint8_t status = ap_mc6840_read(&ptm, AP_MC6840_RS_CONTROL_2_OR_STATUS);
  TEST_ASSERT_EQUAL_HEX8(AP_MC6840_STATUS_COMPOSITE,
                         status & AP_MC6840_STATUS_COMPOSITE);
  TEST_ASSERT_TRUE(ap_mc6840_irq(&ptm));
}

static void test_reading_status_then_the_timer_clears_the_interrupt(void) {
  ap_mc6840_t ptm;
  program_continuous(&ptm, 3, CONTINUOUS);
  for (unsigned i = 0; i < 4; i++) {
    ap_mc6840_clock(&ptm, 0);
  }
  TEST_ASSERT_TRUE(ap_mc6840_irq(&ptm));

  /* §3.11: "Read the status register (RS), then read the timer (RT) causing the
   * interrupt." Both steps, in that order. */
  (void)ap_mc6840_read(&ptm, AP_MC6840_RS_TIMER1_MSB); /* timer alone: no */
  TEST_ASSERT_TRUE(ap_mc6840_irq(&ptm));

  (void)ap_mc6840_read(&ptm, AP_MC6840_RS_CONTROL_2_OR_STATUS);
  (void)ap_mc6840_read(&ptm, AP_MC6840_RS_TIMER1_MSB);
  TEST_ASSERT_FALSE(ap_mc6840_irq(&ptm));
}

static void test_an_interrupt_raised_between_the_two_reads_survives(void) {
  ap_mc6840_t ptm;
  program_continuous(&ptm, 3, CONTINUOUS);

  /* §3.11's parenthesis, which is the whole reason the part snapshots rather
   * than simply clearing: "(An interrupt that occurs between RS and RT will
   * *not* be cleared.)" Losing this drops interrupts under exactly the load
   * that makes them matter. */
  (void)ap_mc6840_read(&ptm, AP_MC6840_RS_CONTROL_2_OR_STATUS); /* nothing set */
  for (unsigned i = 0; i < 4; i++) {
    ap_mc6840_clock(&ptm, 0); /* the interrupt arrives now */
  }
  (void)ap_mc6840_read(&ptm, AP_MC6840_RS_TIMER1_MSB);

  TEST_ASSERT_TRUE(ap_mc6840_irq(&ptm));
}

static void test_a_software_reset_clears_every_interrupt(void) {
  ap_mc6840_t ptm;
  program_continuous(&ptm, 3, CONTINUOUS);
  for (unsigned i = 0; i < 4; i++) {
    ap_mc6840_clock(&ptm, 0);
  }
  TEST_ASSERT_TRUE(ap_mc6840_irq(&ptm));

  /* §3.11: "Software reset (CR10 = 1)". Unconditional, unlike the latch-write
   * and gate clears which are qualified by other control bits. */
  ap_mc6840_write(&ptm, AP_MC6840_RS_CONTROL_2_OR_STATUS, CONTINUOUS | 0x01u);
  ap_mc6840_write(&ptm, AP_MC6840_RS_CONTROL_1_OR_3, CONTINUOUS | 0x01u);
  TEST_ASSERT_FALSE(ap_mc6840_irq(&ptm));
}

static void test_all_timers_preset_holds_every_counter(void) {
  ap_mc6840_t ptm;
  program_continuous(&ptm, 3, CONTINUOUS);

  /* §3.6.1, CR1 bit 0: "1 ALL TIMERS PRESET". The bit lives in control register
   * one but acts on the whole part -- so holding timer 1 holds timers 2 and 3
   * as well, which a per-timer reading would miss. */
  ap_mc6840_write(&ptm, AP_MC6840_RS_CONTROL_2_OR_STATUS, CONTINUOUS | 0x01u);
  ap_mc6840_write(&ptm, AP_MC6840_RS_CONTROL_1_OR_3, CONTINUOUS | 0x01u);

  for (unsigned index = 0; index < AP_MC6840_TIMERS; index++) {
    for (unsigned i = 0; i < 32; i++) {
      ap_mc6840_clock(&ptm, index);
    }
  }
  TEST_ASSERT_FALSE(ap_mc6840_irq(&ptm));
}

static void test_a_high_gate_holds_its_own_timer_only(void) {
  ap_mc6840_t ptm;
  program_continuous(&ptm, 3, CONTINUOUS);

  /* §3.7: counter enable is "Reset clear[,] Gate pin is low". Per timer, unlike
   * the preset bit. */
  ap_mc6840_set_gate(&ptm, 0, true);
  for (unsigned i = 0; i < 32; i++) {
    ap_mc6840_clock(&ptm, 0);
  }
  TEST_ASSERT_FALSE(ap_mc6840_irq(&ptm));

  TEST_ASSERT_EQUAL_UINT(4u, clocks_to_interrupt(&ptm, 1, 32));
}

static void test_the_gate_going_low_reloads_the_counter(void) {
  ap_mc6840_t ptm;
  program_continuous(&ptm, 7, CONTINUOUS);

  /* §3.7.1's counter initialization for continuous mode: "Reset OR Gate pin
   * goes low". An edge, not a level -- which is why the pin is stored. */
  for (unsigned i = 0; i < 5; i++) {
    ap_mc6840_clock(&ptm, 0);
  }
  ap_mc6840_set_gate(&ptm, 0, true);
  ap_mc6840_set_gate(&ptm, 0, false);

  /* Reloaded, so the full period is ahead again rather than the remaining 3. */
  TEST_ASSERT_EQUAL_UINT(8u, clocks_to_interrupt(&ptm, 0, 32));
}

static void test_only_timer_three_has_a_prescaler(void) {
  ap_mc6840_t ptm;
  program_continuous(&ptm, 1, CONTINUOUS | AP_MC6840_CR3_PRESCALE);

  /* §3.6.1: "Bit 0 in control register #3 is a clock prescalar and is available
   * only in control register #3." Bit 0 means something different in each
   * register, so the same value programs a prescaler in one and a reset in
   * another -- and `program_continuous` set bit 0 in CR3 only.
   *
   * A latch of 1 is a two-clock period, so timer 3 needs 16 and timer 2 needs
   * 2. That factor of eight is `008778-03` §3.8's "16-microsecond period ...
   * prescaled to make the effective input signal have a 128-microsecond
   * period", from the other manual entirely. */
  TEST_ASSERT_EQUAL_UINT(2u, clocks_to_interrupt(&ptm, 1, 64));
  ap_mc6840_write(&ptm, AP_MC6840_RS_CONTROL_2_OR_STATUS, CONTINUOUS | 0x01u);
  (void)ap_mc6840_read(&ptm, AP_MC6840_RS_CONTROL_2_OR_STATUS);
  (void)ap_mc6840_read(&ptm, AP_MC6840_RS_TIMER2_MSB);

  ap_mc6840_t fresh;
  program_continuous(&fresh, 1, CONTINUOUS | AP_MC6840_CR3_PRESCALE);
  TEST_ASSERT_EQUAL_UINT(16u, clocks_to_interrupt(&fresh, 2, 64));
}

static void test_both_continuous_variants_are_recognised(void) {
  ap_mc6840_t ptm;

  /* The regression this whole pass exists for. `[6840]` §3.7.1 gives two
   * control words for 16-bit continuous, `XX0100XX` and `XX0000XX`, differing
   * only in whether a latch write reinitialises the counter. This module used
   * to read bits 5-3 as one mode field and require `010`, so it declined
   * `XX0000XX` -- half of continuous mode -- and nothing caught it because the
   * half that worked was the half the tests used. */
  program_continuous(&ptm, 9, 0x10u | AP_MC6840_CR_IRQ_ENABLE);
  TEST_ASSERT_EQUAL_UINT(AP_MC6840_MODE_CONTINUOUS, ap_mc6840_mode(&ptm, 0));
  TEST_ASSERT_TRUE(ap_mc6840_mode_supported(&ptm, 0));
  TEST_ASSERT_EQUAL_UINT(10u, clocks_to_interrupt(&ptm, 0, 64));

  program_continuous(&ptm, 9, 0x00u | AP_MC6840_CR_IRQ_ENABLE);
  TEST_ASSERT_EQUAL_UINT(AP_MC6840_MODE_CONTINUOUS, ap_mc6840_mode(&ptm, 0));
  TEST_ASSERT_TRUE(ap_mc6840_mode_supported(&ptm, 0));
  TEST_ASSERT_EQUAL_UINT(10u, clocks_to_interrupt(&ptm, 0, 64));
}

static void test_a_latch_write_reinitialises_only_when_bit_four_is_clear(void) {
  ap_mc6840_t ptm;

  /* §3.7.1 lists "Write to the Counter Latches" among the initialisation
   * conditions for the bit-4-clear variant only. With bit 4 set the current
   * period runs to its end and the new latch takes effect after it. */
  program_continuous(&ptm, 100, 0x00u | AP_MC6840_CR_IRQ_ENABLE);
  for (unsigned i = 0; i < 50; i++) {
    ap_mc6840_clock(&ptm, 0);
  }
  ap_mc6840_write(&ptm, AP_MC6840_RS_TIMER1_MSB, 0x00);
  ap_mc6840_write(&ptm, AP_MC6840_RS_TIMER1_LSB, 0x04);
  TEST_ASSERT_EQUAL_UINT(5u, clocks_to_interrupt(&ptm, 0, 64));

  program_continuous(&ptm, 100, 0x10u | AP_MC6840_CR_IRQ_ENABLE);
  for (unsigned i = 0; i < 50; i++) {
    ap_mc6840_clock(&ptm, 0);
  }
  ap_mc6840_write(&ptm, AP_MC6840_RS_TIMER1_MSB, 0x00);
  ap_mc6840_write(&ptm, AP_MC6840_RS_TIMER1_LSB, 0x04);
  /* Fifty-one of the original hundred-and-one remain. */
  TEST_ASSERT_EQUAL_UINT(51u, clocks_to_interrupt(&ptm, 0, 128));
}

static void test_dual_eight_bit_multiplies_the_two_halves(void) {
  ap_mc6840_t ptm;

  /* §3.7.2: "B = The count in the LSB latch +1, times the count in the MSB
   * latch +1, times the period of the clock." So `0x0304` is (4+1)*(3+1) = 20
   * clocks -- where a 16-bit countdown of the same latch would be 773. Reading
   * the latch as one word here is not a small error. */
  program_continuous(&ptm, 0x0304,
                     0x10u | AP_MC6840_CR_DUAL_8BIT | AP_MC6840_CR_IRQ_ENABLE);
  TEST_ASSERT_TRUE(ap_mc6840_mode_supported(&ptm, 0));
  TEST_ASSERT_EQUAL_UINT(20u, clocks_to_interrupt(&ptm, 0, 128));
}

static void test_dual_eight_bit_repeats_at_the_same_interval(void) {
  ap_mc6840_t ptm;
  program_continuous(&ptm, 0x0203,
                     0x10u | AP_MC6840_CR_DUAL_8BIT | AP_MC6840_CR_IRQ_ENABLE);

  /* (3+1)*(2+1) = 12. The reload has to restore *both* halves, which a model
   * keeping only a 16-bit counter would get right the first time and wrong
   * every time after. */
  TEST_ASSERT_EQUAL_UINT(12u, clocks_to_interrupt(&ptm, 0, 64));
  ap_mc6840_write(&ptm, AP_MC6840_RS_CONTROL_2_OR_STATUS, 0x01);
  (void)ap_mc6840_read(&ptm, AP_MC6840_RS_CONTROL_2_OR_STATUS);
  (void)ap_mc6840_read(&ptm, AP_MC6840_RS_TIMER1_MSB);
  TEST_ASSERT_EQUAL_UINT(12u, clocks_to_interrupt(&ptm, 0, 64));
}

static void test_single_shot_interrupts_repeatedly_but_pulses_once(void) {
  ap_mc6840_t ptm;

  /* §3.8: "Internally, the count recycling is continuous as if in the
   * Continuous Mode. Only one pulse is evident on the output pin for each
   * Counter Initialization." Two halves that a simpler model would conflate --
   * either stopping the interrupts too, or pulsing the output every period. */
  program_continuous(&ptm, 4,
                     0x20u | AP_MC6840_CR_IRQ_ENABLE |
                         AP_MC6840_CR_OUTPUT_ENABLE);
  TEST_ASSERT_EQUAL_UINT(AP_MC6840_MODE_SINGLE_SHOT, ap_mc6840_mode(&ptm, 0));

  /* The output is high from initialisation until the first time out. */
  TEST_ASSERT_TRUE(ap_mc6840_output(&ptm, 0));
  TEST_ASSERT_EQUAL_UINT(5u, clocks_to_interrupt(&ptm, 0, 32));
  TEST_ASSERT_FALSE(ap_mc6840_output(&ptm, 0));

  /* But the interrupts keep coming. */
  ap_mc6840_write(&ptm, AP_MC6840_RS_CONTROL_2_OR_STATUS, 0x01);
  (void)ap_mc6840_read(&ptm, AP_MC6840_RS_CONTROL_2_OR_STATUS);
  (void)ap_mc6840_read(&ptm, AP_MC6840_RS_TIMER1_MSB);
  TEST_ASSERT_EQUAL_UINT(5u, clocks_to_interrupt(&ptm, 0, 32));
  /* And the output stays down: one pulse per initialisation. */
  TEST_ASSERT_FALSE(ap_mc6840_output(&ptm, 0));
}

static void test_reinitialising_a_single_shot_arms_it_again(void) {
  ap_mc6840_t ptm;
  program_continuous(&ptm, 4,
                     0x20u | AP_MC6840_CR_IRQ_ENABLE |
                         AP_MC6840_CR_OUTPUT_ENABLE);
  (void)clocks_to_interrupt(&ptm, 0, 32);
  TEST_ASSERT_FALSE(ap_mc6840_output(&ptm, 0));

  /* §3.8.1: "Each initialization causes a single shot (even during a single
   * shot) if the counter is enabled." The gate going low is one such. */
  ap_mc6840_set_gate(&ptm, 0, true);
  ap_mc6840_set_gate(&ptm, 0, false);
  TEST_ASSERT_TRUE(ap_mc6840_output(&ptm, 0));
}

static void test_the_measurement_modes_are_decoded_and_declined(void) {
  ap_mc6840_t ptm;

  /* §3.9 and §3.10. Both are decoded, so a caller is told *which* mode it asked
   * for, and both are refused, because "The digital signal to be measured is
   * applied to the individual gate pin" and on this board nothing is connected
   * to the gates -- the timers take fixed clocks. Declined for want of a signal
   * rather than for want of a transcription. */
  program_continuous(&ptm, 4, 0x08u | AP_MC6840_CR_IRQ_ENABLE);
  TEST_ASSERT_EQUAL_UINT(AP_MC6840_MODE_PERIOD_MEASUREMENT,
                         ap_mc6840_mode(&ptm, 0));
  TEST_ASSERT_FALSE(ap_mc6840_mode_supported(&ptm, 0));

  program_continuous(&ptm, 4, 0x18u | AP_MC6840_CR_IRQ_ENABLE);
  TEST_ASSERT_EQUAL_UINT(AP_MC6840_MODE_PULSE_WIDTH_MEASUREMENT,
                         ap_mc6840_mode(&ptm, 0));
  TEST_ASSERT_FALSE(ap_mc6840_mode_supported(&ptm, 0));

  /* And a declined timer stands still rather than counting plausibly. */
  for (unsigned i = 0; i < 64; i++) {
    ap_mc6840_clock(&ptm, 0);
  }
  TEST_ASSERT_FALSE(ap_mc6840_irq(&ptm));
}

static void test_the_apollo_clock_rates_divide_the_time_base(void) {
  /* `008778-03` §3.8 gives the three inputs as 250 kHz, 125 kHz and 62.5 kHz.
   * `CLAUDE.md`'s rule is that the time base is the LCM of every clock, and
   * that adding one it does not divide means recomputing it. It does divide all
   * three, so this device needs no change to the base -- asserted here so that a
   * future change to either side has to break a test rather than a machine. */
  TEST_ASSERT_TRUE(ap_time_base_divides(250000u));
  TEST_ASSERT_TRUE(ap_time_base_divides(125000u));
  TEST_ASSERT_TRUE(ap_time_base_divides(62500u));

  /* And the prescaled rate is the case that justifies counting in base units
   * rather than hertz: 62.5 kHz / 8 is 7812.5 Hz, not an integer frequency at
   * all, yet its period is an exact whole number of base units. Asserted as
   * 128 microseconds -- the duration, which does not move when the base is
   * recomputed -- rather than as the unit count, which does. */
  TEST_ASSERT_EQUAL_UINT64((AP_TIME_BASE_HZ / 62500u) * 8u,
                           (AP_TIME_BASE_HZ / 1000000u) * 128u);
  TEST_ASSERT_EQUAL_UINT64(0u, ((AP_TIME_BASE_HZ / 62500u) * 8u) % 1u);
}

static void test_two_parts_reset_alike_hold_identical_state(void) {
  ap_mc6840_t a;
  ap_mc6840_t b;
  memset(&a, 0xAA, sizeof a);
  memset(&b, 0x55, sizeof b);
  ap_mc6840_reset(&a);
  ap_mc6840_reset(&b);
  TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof a);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_clock_source_selection_is_reported);
  RUN_TEST(test_the_latches_come_up_all_ones);
  RUN_TEST(test_control_register_three_is_selected_after_reset);
  RUN_TEST(test_one_address_reaches_two_control_registers);
  RUN_TEST(test_a_sixteen_bit_write_takes_the_buffered_high_byte);
  RUN_TEST(test_a_sixteen_bit_read_latches_the_low_byte);
  RUN_TEST(test_a_period_is_the_latch_plus_one_clocks);
  RUN_TEST(test_a_latch_of_zero_still_takes_one_clock);
  RUN_TEST(test_a_continuous_timer_reloads_and_interrupts_again);
  RUN_TEST(test_the_output_toggles_at_each_time_out);
  RUN_TEST(test_a_masked_output_reads_low_however_the_timer_runs);
  RUN_TEST(test_an_individual_interrupt_flag_cannot_be_masked);
  RUN_TEST(test_the_composite_flag_needs_the_interrupt_enabled);
  RUN_TEST(test_reading_status_then_the_timer_clears_the_interrupt);
  RUN_TEST(test_an_interrupt_raised_between_the_two_reads_survives);
  RUN_TEST(test_a_software_reset_clears_every_interrupt);
  RUN_TEST(test_all_timers_preset_holds_every_counter);
  RUN_TEST(test_a_high_gate_holds_its_own_timer_only);
  RUN_TEST(test_the_gate_going_low_reloads_the_counter);
  RUN_TEST(test_only_timer_three_has_a_prescaler);
  RUN_TEST(test_both_continuous_variants_are_recognised);
  RUN_TEST(test_a_latch_write_reinitialises_only_when_bit_four_is_clear);
  RUN_TEST(test_dual_eight_bit_multiplies_the_two_halves);
  RUN_TEST(test_dual_eight_bit_repeats_at_the_same_interval);
  RUN_TEST(test_single_shot_interrupts_repeatedly_but_pulses_once);
  RUN_TEST(test_reinitialising_a_single_shot_arms_it_again);
  RUN_TEST(test_the_measurement_modes_are_decoded_and_declined);
  RUN_TEST(test_the_apollo_clock_rates_divide_the_time_base);
  RUN_TEST(test_two_parts_reset_alike_hold_identical_state);
  return UNITY_END();
}

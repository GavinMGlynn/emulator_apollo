/* Apollo interval timer as the board wires it.
 *
 * Placement and interrupt route are measured (`FINDINGS.md` C12); the rates are
 * `008778-03` §3.8; the part's behaviour is `[6840]`, covered by
 * `mc6840_suite`. What is tested here is the wiring. */

#include "unity.h"

#include "board/ap_intr.h"
#include "board/ap_timer.h"

void setUp(void) {}
void tearDown(void) {}

/* Program timer 1 for continuous 16-bit operation with its interrupt enabled
 * and the given latch. Registers are at odd addresses, stride 2. */
static void program_timer1(ap_timer_t *timer, uint16_t latch) {
  uint32_t rs[8];
  for (unsigned i = 0; i < 8; i++) {
    rs[i] = AP_TIMER_ADDR + 1u + 2u * i;
  }
  ap_timer_write(timer, rs[1], 0x01); /* CR2 bit 0: reach CR1 at RS 0 */
  ap_timer_write(timer, rs[0], 0x01); /* CR1: all timers preset */
  ap_timer_write(timer, rs[2], (uint8_t)(latch >> 8));
  ap_timer_write(timer, rs[3], (uint8_t)(latch & 0xFFu));
  ap_timer_write(timer, rs[1], 0x01);
  ap_timer_write(timer, rs[0], 0x50); /* continuous + IRQ enable, bit 0 = 0 */
}

static void test_the_timer_answers_on_odd_addresses_only(void) {
  ap_mc6840_rs_t rs;

  /* Measured: the region reads `00 00 00 00 00 FF 00 FF ...`, so the part is on
   * the odd bytes and the even ones are the other lane. */
  TEST_ASSERT_FALSE(ap_timer_decode(0x010800u, &rs));
  TEST_ASSERT_TRUE(ap_timer_decode(0x010801u, &rs));
  TEST_ASSERT_EQUAL_UINT(AP_MC6840_RS_CONTROL_1_OR_3, rs);
  TEST_ASSERT_FALSE(ap_timer_decode(0x010802u, &rs));
  TEST_ASSERT_TRUE(ap_timer_decode(0x010803u, &rs));
  TEST_ASSERT_EQUAL_UINT(AP_MC6840_RS_CONTROL_2_OR_STATUS, rs);

  /* Eight registers, so the last is at offset 15. */
  TEST_ASSERT_TRUE(ap_timer_decode(0x01080Fu, &rs));
  TEST_ASSERT_EQUAL_UINT(AP_MC6840_RS_TIMER3_LSB, rs);

  /* Outside the range. */
  TEST_ASSERT_FALSE(ap_timer_decode(0x010900u, &rs)); /* the calendar */
  TEST_ASSERT_FALSE(ap_timer_decode(0x0107FFu, &rs));
}

static void test_an_unwritten_timer_reads_the_measured_pattern(void) {
  ap_timer_t timer;
  TEST_ASSERT_TRUE(ap_timer_reset(&timer));

  /* The dump that established the placement, reproduced from this core: RS0 is
   * "no operation" and RS1 is the status register with nothing pending, both
   * zero; the other six are counters and buffers holding the `$FFFF` latch
   * default.
   *
   * This is the strongest test in the file, because it is the same observation
   * that identified where the part is -- if this core stops reproducing it, the
   * placement it was derived from is no longer what the code implements. */
  static const uint8_t expected[8] = {0x00, 0x00, 0xFF, 0xFF,
                                      0xFF, 0xFF, 0xFF, 0xFF};
  for (unsigned i = 0; i < 8; i++) {
    TEST_ASSERT_EQUAL_HEX8(expected[i],
                           ap_timer_read(&timer, AP_TIMER_ADDR + 1u + 2u * i));
    /* And the even byte beside it is zero, not an alias. */
    TEST_ASSERT_EQUAL_HEX8(0x00,
                           ap_timer_read(&timer, AP_TIMER_ADDR + 2u * i));
  }
}

static void test_the_three_rates_are_representable_in_the_time_base(void) {
  ap_timer_t timer;

  /* `CLAUDE.md`: the time base is the LCM of every clock, and `ap_clock_init`
   * "rejects an unrepresentable frequency rather than rounding it". Reset
   * propagates that refusal, so this passing means all three rates are exact. */
  TEST_ASSERT_TRUE(ap_timer_reset(&timer));

  TEST_ASSERT_EQUAL_UINT64(79200u, timer.clock[0].period);
  TEST_ASSERT_EQUAL_UINT64(158400u, timer.clock[1].period);
  TEST_ASSERT_EQUAL_UINT64(316800u, timer.clock[2].period);

  /* And the ratios `008778-03` §3.8 states: 4, 8 and 16 microseconds. */
  TEST_ASSERT_EQUAL_UINT64(timer.clock[0].period * 2u, timer.clock[1].period);
  TEST_ASSERT_EQUAL_UINT64(timer.clock[0].period * 4u, timer.clock[2].period);
}

static void test_a_timer_interrupts_after_the_documented_interval(void) {
  ap_timer_t timer;
  TEST_ASSERT_TRUE(ap_timer_reset(&timer));

  /* Timer 1 runs at 4 microseconds a pulse, so a latch of 249 is a period of
   * 250 pulses -- one millisecond exactly. Chosen because it is the figure a
   * scheduler tick would actually use, and because it makes the arithmetic
   * checkable by eye. */
  program_timer1(&timer, 249);

  ap_time_t one_millisecond = (AP_TIME_BASE_HZ / 1000u);
  ap_timer_advance(&timer, one_millisecond - 1u);
  TEST_ASSERT_FALSE(ap_timer_irq(&timer));

  ap_timer_advance(&timer, one_millisecond);
  TEST_ASSERT_TRUE(ap_timer_irq(&timer));
}

static void test_advancing_in_small_steps_gives_the_same_interval(void) {
  ap_timer_t whole;
  ap_timer_t pieces;
  TEST_ASSERT_TRUE(ap_timer_reset(&whole));
  TEST_ASSERT_TRUE(ap_timer_reset(&pieces));
  program_timer1(&whole, 249);
  program_timer1(&pieces, 249);

  /* The rate must not depend on how often the timer is polled. Advancing in
   * steps that are not whole periods is exactly what would expose a cursor set
   * to `now` instead of to the last completed pulse -- each call would discard
   * a fraction and the timer would run slow by an amount decided by the
   * scheduler. */
  ap_time_t one_millisecond = (AP_TIME_BASE_HZ / 1000u);
  ap_timer_advance(&whole, one_millisecond);

  for (ap_time_t t = 1000u; t <= one_millisecond; t += 1000u) {
    ap_timer_advance(&pieces, t);
  }

  TEST_ASSERT_TRUE(ap_timer_irq(&whole));
  TEST_ASSERT_TRUE(ap_timer_irq(&pieces));
  TEST_ASSERT_EQUAL_UINT64(whole.ptm.timer[0].counter,
                           pieces.ptm.timer[0].counter);
}

static void test_advancing_backwards_does_nothing(void) {
  ap_timer_t timer;
  TEST_ASSERT_TRUE(ap_timer_reset(&timer));
  program_timer1(&timer, 249);

  ap_timer_advance(&timer, 1000000u);
  uint16_t counter = timer.ptm.timer[0].counter;

  /* Not a hypothetical: the subtraction would wrap and issue on the order of
   * 2^64 pulses, which is not a wrong answer so much as a hang. */
  ap_timer_advance(&timer, 500000u);
  TEST_ASSERT_EQUAL_HEX16(counter, timer.ptm.timer[0].counter);
}

static void test_each_timer_keeps_its_own_rate(void) {
  ap_timer_t timer;
  TEST_ASSERT_TRUE(ap_timer_reset(&timer));

  /* Same latch on all three, so any difference in when they expire is the
   * difference in their input rates and nothing else. Timer 2 is half timer 1's
   * rate and timer 3 a quarter, per §3.8. */
  uint32_t rs0 = AP_TIMER_ADDR + 1u;
  uint32_t rs1 = AP_TIMER_ADDR + 3u;
  ap_timer_write(&timer, rs1, 0x01);
  ap_timer_write(&timer, rs0, 0x01); /* hold */
  for (unsigned i = 0; i < 3; i++) {
    ap_timer_write(&timer, AP_TIMER_ADDR + 1u + 2u * (2u + 2u * i), 0x00);
    ap_timer_write(&timer, AP_TIMER_ADDR + 1u + 2u * (3u + 2u * i), 0x09);
  }
  /* `[6840]` §4.1's prescribed order, CR3 then CR2 then CR1, and the reason for
   * it is visible here: CR3 is only reachable while CR2 bit 0 is clear, so it
   * has to go first. Programming CR2 and CR1 alone leaves timer 3 in the mode
   * it reset into -- which this core declines, so it silently would not count.
   * That is the declined-mode design earning its keep: the timer stayed still
   * and the test failed, rather than counting plausibly in the wrong mode. */
  ap_timer_write(&timer, rs1, 0x00); /* select CR3 */
  ap_timer_write(&timer, rs0, 0x50); /* CR3: continuous, IRQ, prescale off */
  ap_timer_write(&timer, rs1, 0x51); /* CR2: continuous, IRQ, reselect CR1 */
  ap_timer_write(&timer, rs0, 0x50); /* CR1: run */

  /* Ten pulses of timer 1 is 40 microseconds. */
  ap_timer_advance(&timer, timer.clock[0].period * 10u);
  TEST_ASSERT_TRUE(timer.ptm.timer[0].interrupt_flag);
  TEST_ASSERT_FALSE(timer.ptm.timer[1].interrupt_flag);
  TEST_ASSERT_FALSE(timer.ptm.timer[2].interrupt_flag);

  /* Twenty of timer 1 is ten of timer 2. */
  ap_timer_advance(&timer, timer.clock[0].period * 20u);
  TEST_ASSERT_TRUE(timer.ptm.timer[1].interrupt_flag);
  TEST_ASSERT_FALSE(timer.ptm.timer[2].interrupt_flag);

  /* Forty is ten of timer 3. */
  ap_timer_advance(&timer, timer.clock[0].period * 40u);
  TEST_ASSERT_TRUE(timer.ptm.timer[2].interrupt_flag);
}

static void test_the_timer_raises_the_highest_priority_interrupt(void) {
  ap_timer_t timer;
  ap_intr_t intr;
  TEST_ASSERT_TRUE(ap_timer_reset(&timer));
  ap_intr_reset(&intr);

  /* The firmware's own initialization sequence, then unmasked as a driver
   * would. */
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 0u, 0x11);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 1u, 0xA0);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 1u, 0x08);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 1u, 0x01);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 1u, 0x00);

  program_timer1(&timer, 249);
  ap_timer_advance(&timer, AP_TIME_BASE_HZ / 1000u);

  /* The board wires the pin; neither module reaches for the other. */
  ap_intr_set_request(&intr, AP_TIMER_IRQ, ap_timer_irq(&timer));

  TEST_ASSERT_TRUE(ap_intr_pending(&intr));
  /* Vector `A0`: the master's measured base plus level 0. `008778-03` Table 2-3
   * puts the timer at IRQ0, priority 1 -- the highest line in the machine -- and
   * the oracle's in-service register agreed. */
  TEST_ASSERT_EQUAL_HEX8(0xA0, ap_intr_acknowledge(&intr));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_timer_answers_on_odd_addresses_only);
  RUN_TEST(test_an_unwritten_timer_reads_the_measured_pattern);
  RUN_TEST(test_the_three_rates_are_representable_in_the_time_base);
  RUN_TEST(test_a_timer_interrupts_after_the_documented_interval);
  RUN_TEST(test_advancing_in_small_steps_gives_the_same_interval);
  RUN_TEST(test_advancing_backwards_does_nothing);
  RUN_TEST(test_each_timer_keeps_its_own_rate);
  RUN_TEST(test_the_timer_raises_the_highest_priority_interrupt);
  return UNITY_END();
}

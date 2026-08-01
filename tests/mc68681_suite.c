/* MC68681 / SCN2681 DUART, `[68681]` Sep 1985. */

#include "unity.h"

#include <string.h>

#include "device/ap_mc68681.h"

void setUp(void) {}
void tearDown(void) {}

static void enable_a(ap_mc68681_t *d) {
  ap_mc68681_write(d, AP_MC68681_CR_A, 0x05); /* enable receiver and transmitter */
}

static void test_an_idle_transmitter_is_ready_and_empty(void) {
  ap_mc68681_t d;
  ap_mc68681_reset(&d);

  /* Getting this wrong at reset makes a driver wait for a transmitter that
   * never announces itself -- a hang with no error anywhere. */
  uint8_t sr = ap_mc68681_read(&d, AP_MC68681_SR_CSR_A);
  TEST_ASSERT_EQUAL_HEX8(AP_MC68681_SR_TXRDY, sr & AP_MC68681_SR_TXRDY);
  TEST_ASSERT_EQUAL_HEX8(AP_MC68681_SR_TXEMT, sr & AP_MC68681_SR_TXEMT);
  TEST_ASSERT_EQUAL_HEX8(0, sr & AP_MC68681_SR_RXRDY);
}

static void test_the_mode_register_pointer_advances_then_sticks(void) {
  ap_mc68681_t d;
  ap_mc68681_reset(&d);

  /* One address reaches two registers, and the pointer only goes forward until
   * a command resets it. A driver that reads MR twice gets MR1 then MR2. */
  ap_mc68681_write(&d, AP_MC68681_MR_A, 0x11); /* MR1A */
  ap_mc68681_write(&d, AP_MC68681_MR_A, 0x22); /* MR2A */
  ap_mc68681_write(&d, AP_MC68681_MR_A, 0x33); /* still MR2A */

  TEST_ASSERT_EQUAL_HEX8(0x11, d.channel[0].mr[0]);
  TEST_ASSERT_EQUAL_HEX8(0x33, d.channel[0].mr[1]);

  /* §4.2.7.2: "0 0 1  Reset MR Pointer to MR1". */
  ap_mc68681_write(&d, AP_MC68681_CR_A, 0x10);
  TEST_ASSERT_EQUAL_HEX8(0x11, ap_mc68681_read(&d, AP_MC68681_MR_A));
  TEST_ASSERT_EQUAL_HEX8(0x33, ap_mc68681_read(&d, AP_MC68681_MR_A));
}

static void test_the_receive_fifo_holds_three_and_then_overruns(void) {
  ap_mc68681_t d;
  ap_mc68681_reset(&d);
  enable_a(&d);

  /* `[68681]` §1: "Quadruple-Buffered Receiver Data Registers" -- three FIFO
   * positions behind the shift register. */
  ap_mc68681_receive(&d, 0, 'a');
  TEST_ASSERT_EQUAL_HEX8(AP_MC68681_SR_RXRDY,
                         ap_mc68681_read(&d, AP_MC68681_SR_CSR_A) &
                             AP_MC68681_SR_RXRDY);
  ap_mc68681_receive(&d, 0, 'b');
  ap_mc68681_receive(&d, 0, 'c');
  TEST_ASSERT_EQUAL_HEX8(AP_MC68681_SR_FFULL,
                         ap_mc68681_read(&d, AP_MC68681_SR_CSR_A) &
                             AP_MC68681_SR_FFULL);

  /* A fourth is lost and flagged. */
  ap_mc68681_receive(&d, 0, 'd');
  TEST_ASSERT_EQUAL_HEX8(AP_MC68681_SR_OVERRUN,
                         ap_mc68681_read(&d, AP_MC68681_SR_CSR_A) &
                             AP_MC68681_SR_OVERRUN);

  /* And the three already held are intact: an overrun discards the newest, not
   * the oldest, so a driver reading after one still gets valid earlier data. */
  TEST_ASSERT_EQUAL_HEX8('a', ap_mc68681_read(&d, AP_MC68681_RB_TB_A));
  TEST_ASSERT_EQUAL_HEX8('b', ap_mc68681_read(&d, AP_MC68681_RB_TB_A));
  TEST_ASSERT_EQUAL_HEX8('c', ap_mc68681_read(&d, AP_MC68681_RB_TB_A));
  TEST_ASSERT_EQUAL_HEX8(0, ap_mc68681_read(&d, AP_MC68681_SR_CSR_A) &
                                AP_MC68681_SR_RXRDY);
}

static void test_a_disabled_receiver_takes_nothing(void) {
  ap_mc68681_t d;
  ap_mc68681_reset(&d);

  ap_mc68681_receive(&d, 0, 'x');
  TEST_ASSERT_EQUAL_HEX8(0, ap_mc68681_read(&d, AP_MC68681_SR_CSR_A) &
                                AP_MC68681_SR_RXRDY);
}

static void test_a_character_written_comes_back_out(void) {
  ap_mc68681_t d;
  uint8_t byte = 0;
  ap_mc68681_reset(&d);
  enable_a(&d);

  ap_mc68681_write(&d, AP_MC68681_RB_TB_A, 'Q');
  /* Busy until taken. */
  TEST_ASSERT_EQUAL_HEX8(0, ap_mc68681_read(&d, AP_MC68681_SR_CSR_A) &
                                AP_MC68681_SR_TXRDY);

  TEST_ASSERT_TRUE(ap_mc68681_transmit(&d, 0, &byte));
  TEST_ASSERT_EQUAL_HEX8('Q', byte);
  TEST_ASSERT_FALSE(ap_mc68681_transmit(&d, 0, &byte));
  TEST_ASSERT_EQUAL_HEX8(AP_MC68681_SR_TXRDY,
                         ap_mc68681_read(&d, AP_MC68681_SR_CSR_A) &
                             AP_MC68681_SR_TXRDY);
}

static void test_the_two_channels_are_independent(void) {
  ap_mc68681_t d;
  uint8_t byte = 0;
  ap_mc68681_reset(&d);
  ap_mc68681_write(&d, AP_MC68681_CR_A, 0x05);
  ap_mc68681_write(&d, AP_MC68681_CR_B, 0x05);

  ap_mc68681_receive(&d, 0, 'A');
  ap_mc68681_receive(&d, 1, 'B');
  TEST_ASSERT_EQUAL_HEX8('A', ap_mc68681_read(&d, AP_MC68681_RB_TB_A));
  TEST_ASSERT_EQUAL_HEX8('B', ap_mc68681_read(&d, AP_MC68681_RB_TB_B));

  ap_mc68681_write(&d, AP_MC68681_RB_TB_B, 'z');
  TEST_ASSERT_FALSE(ap_mc68681_transmit(&d, 0, &byte));
  TEST_ASSERT_TRUE(ap_mc68681_transmit(&d, 1, &byte));
  TEST_ASSERT_EQUAL_HEX8('z', byte);
}

static void test_reading_the_input_change_register_clears_it(void) {
  ap_mc68681_t d;
  ap_mc68681_reset(&d);

  /* Measured in the oracle before it was read in the manual: a dump of the real
   * machine read `10` and then `00` from the two bytes of this one register,
   * which is what identified it. `FINDINGS.md` C14. */
  ap_mc68681_set_input(&d, 0x01);
  uint8_t first = ap_mc68681_read(&d, AP_MC68681_IPCR_ACR);
  uint8_t second = ap_mc68681_read(&d, AP_MC68681_IPCR_ACR);

  TEST_ASSERT_NOT_EQUAL(0, first & 0xF0u);
  TEST_ASSERT_EQUAL_HEX8(0, second & 0xF0u);
}

static void test_an_input_change_raises_its_interrupt(void) {
  ap_mc68681_t d;
  ap_mc68681_reset(&d);
  ap_mc68681_write(&d, AP_MC68681_ISR_IMR, AP_MC68681_ISR_INPUT);

  TEST_ASSERT_FALSE(ap_mc68681_irq(&d));
  ap_mc68681_set_input(&d, 0x04);
  TEST_ASSERT_TRUE(ap_mc68681_irq(&d));

  /* And reading the change register drops it. */
  (void)ap_mc68681_read(&d, AP_MC68681_IPCR_ACR);
  TEST_ASSERT_FALSE(ap_mc68681_irq(&d));
}

static void test_a_masked_interrupt_still_shows_in_the_status_register(void) {
  ap_mc68681_t d;
  ap_mc68681_reset(&d);
  enable_a(&d);
  ap_mc68681_write(&d, AP_MC68681_ISR_IMR, 0x00); /* everything masked */

  ap_mc68681_receive(&d, 0, 'k');
  TEST_ASSERT_FALSE(ap_mc68681_irq(&d));
  TEST_ASSERT_EQUAL_HEX8(AP_MC68681_ISR_RXRDY_A,
                         ap_mc68681_read(&d, AP_MC68681_ISR_IMR) &
                             AP_MC68681_ISR_RXRDY_A);
}

static void test_reading_the_command_address_changes_nothing(void) {
  ap_mc68681_t d;
  ap_mc68681_t before;
  ap_mc68681_reset(&d);
  enable_a(&d);
  ap_mc68681_receive(&d, 0, 'm');
  before = d;

  /* Table 4-1 marks this "Do Not Access": "Reading this location will result in
   * undesired effects and possible incorrect transmission or reception of
   * characters. Register contents may also be changed."
   *
   * The hardware's answer is explicitly undefined, so this core does the one
   * thing that cannot be wrong in a way that matters -- nothing at all. */
  TEST_ASSERT_EQUAL_HEX8(0x00, ap_mc68681_read(&d, AP_MC68681_CR_A));
  TEST_ASSERT_EQUAL_MEMORY(&before, &d, sizeof d);
}

static void test_the_counter_reaches_terminal_count_twice_a_period(void) {
  ap_mc68681_t d;
  ap_mc68681_reset(&d);
  ap_mc68681_write(&d, AP_MC68681_CUR_CTUR, 0x00);
  ap_mc68681_write(&d, AP_MC68681_CLR_CTLR, 0x03);
  (void)ap_mc68681_read(&d, AP_MC68681_START_OPR_SET);

  /* §3: the output inverts at every terminal count, and the ready bit is set
   * only on the second -- "the timer inverts its output, reinitializes itself
   * ... and repeats the countdown sequence. After reaching terminal count this
   * time, the timer sets the counter/timer-ready bit".
   *
   * That is what makes a countdown into a square wave: two terminal counts to
   * one period, so the interrupt rate is half the toggle rate. A model setting
   * the flag on every terminal count would run the refresh clock at twice its
   * frequency. */
  bool started = d.counter_output;
  for (unsigned i = 0; i < 4u; i++) {
    ap_mc68681_clock(&d);
  }
  TEST_ASSERT_NOT_EQUAL(started, d.counter_output);
  TEST_ASSERT_EQUAL_HEX8(0, d.isr & AP_MC68681_ISR_COUNTER);

  for (unsigned i = 0; i < 4u; i++) {
    ap_mc68681_clock(&d);
  }
  TEST_ASSERT_EQUAL(started, d.counter_output);
  TEST_ASSERT_EQUAL_HEX8(AP_MC68681_ISR_COUNTER, d.isr & AP_MC68681_ISR_COUNTER);
}

static void test_the_stop_command_stops_a_counter_but_not_a_timer(void) {
  ap_mc68681_t d;
  ap_mc68681_reset(&d);
  ap_mc68681_write(&d, AP_MC68681_CLR_CTLR, 0x02);

  /* §3: one address, two behaviours, chosen by ACR. In counter mode "the
   * counter stops the countdown sequence and clears ISR[3]"; in timer mode
   * "the timer clears ISR[3] but does not stop". */
  ap_mc68681_write(&d, AP_MC68681_IPCR_ACR, 0x00); /* counter mode */
  (void)ap_mc68681_read(&d, AP_MC68681_START_OPR_SET);
  (void)ap_mc68681_read(&d, AP_MC68681_STOP_OPR_CLEAR);
  TEST_ASSERT_FALSE(d.counter_running);

  ap_mc68681_write(&d, AP_MC68681_IPCR_ACR, 0x60); /* a timer mode */
  TEST_ASSERT_TRUE(ap_mc68681_timer_mode(&d));
  (void)ap_mc68681_read(&d, AP_MC68681_START_OPR_SET);
  (void)ap_mc68681_read(&d, AP_MC68681_STOP_OPR_CLEAR);
  /* Still counting: a timer cannot be stopped by the CPU. */
  uint16_t before = d.counter;
  ap_mc68681_clock(&d);
  TEST_ASSERT_NOT_EQUAL(before, d.counter);
}

static void test_the_output_port_has_separate_set_and_clear_addresses(void) {
  ap_mc68681_t d;
  ap_mc68681_reset(&d);

  /* Table 4-1: the same two addresses that are counter commands when read are
   * the output port's bit-set and bit-reset commands when written. */
  ap_mc68681_write(&d, AP_MC68681_START_OPR_SET, 0x0C);
  TEST_ASSERT_EQUAL_HEX8(0x0C, d.opr);
  ap_mc68681_write(&d, AP_MC68681_STOP_OPR_CLEAR, 0x04);
  TEST_ASSERT_EQUAL_HEX8(0x08, d.opr);
}

static void test_resetting_the_receiver_empties_the_fifo(void) {
  ap_mc68681_t d;
  ap_mc68681_reset(&d);
  enable_a(&d);
  ap_mc68681_receive(&d, 0, 'p');
  ap_mc68681_receive(&d, 0, 'q');

  /* §4.2.7.2: "This command resets the channel A receiver. The receiver is
   * immediately disabled" -- and what it was holding goes with it. */
  ap_mc68681_write(&d, AP_MC68681_CR_A, 0x20);
  TEST_ASSERT_EQUAL_HEX8(0, ap_mc68681_read(&d, AP_MC68681_SR_CSR_A) &
                                AP_MC68681_SR_RXRDY);
  ap_mc68681_receive(&d, 0, 'r');
  TEST_ASSERT_EQUAL_HEX8(0, ap_mc68681_read(&d, AP_MC68681_SR_CSR_A) &
                                AP_MC68681_SR_RXRDY);
}

static void test_two_duarts_reset_alike_hold_identical_state(void) {
  ap_mc68681_t a;
  ap_mc68681_t b;
  memset(&a, 0xAA, sizeof a);
  memset(&b, 0x55, sizeof b);
  ap_mc68681_reset(&a);
  ap_mc68681_reset(&b);
  TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof a);
}


/* Framing: a character sent at a rate the receiver is not using does not decode.
 *
 * This is the behaviour the DN3500's console negotiation is built on. The boot
 * PROM autobauds by cycling channel B's clock select and waiting for a byte that
 * arrives cleanly, so a model where every byte arrives intact whatever the rate
 * would let that succeed at the first rate tried. The *failure* is the signal
 * the firmware reads. */
static void test_a_character_sent_at_the_wrong_rate_sets_a_framing_error(void) {
  ap_mc68681_t duart;
  ap_mc68681_reset(&duart);
  ap_mc68681_write(&duart, AP_MC68681_CR_A, 0x01u);          /* rx enable */
  ap_mc68681_write(&duart, AP_MC68681_SR_CSR_A, 0x77u);      /* our rate */

  ap_mc68681_receive_at(&duart, 0u, 0x41u, 0xBBu);           /* their rate */

  const uint8_t sr = ap_mc68681_read(&duart, AP_MC68681_SR_CSR_A);
  TEST_ASSERT_TRUE((sr & AP_MC68681_SR_FRAMING) != 0u);
  /* The byte still arrives -- the part does not discard it -- so a driver sees
   * a character *and* an error, which is what lets it tell a wrong rate from
   * silence. Discarding it would look identical to nothing being sent. */
  TEST_ASSERT_TRUE((sr & AP_MC68681_SR_RXRDY) != 0u);
}

/* The matching rate must not, or the autobaud never terminates. */
static void test_a_character_sent_at_the_right_rate_does_not(void) {
  ap_mc68681_t duart;
  ap_mc68681_reset(&duart);
  ap_mc68681_write(&duart, AP_MC68681_CR_A, 0x01u);
  ap_mc68681_write(&duart, AP_MC68681_SR_CSR_A, 0x77u);

  ap_mc68681_receive_at(&duart, 0u, 0x41u, 0x77u);

  const uint8_t sr = ap_mc68681_read(&duart, AP_MC68681_SR_CSR_A);
  TEST_ASSERT_FALSE((sr & AP_MC68681_SR_FRAMING) != 0u);
  TEST_ASSERT_TRUE((sr & AP_MC68681_SR_RXRDY) != 0u);
  TEST_ASSERT_EQUAL_HEX8(0x41u, ap_mc68681_read(&duart, AP_MC68681_RB_TB_A));
}

/* Only the receiver's nibble decides. The lower nibble is the *transmitter's*
 * clock select, and two ports agreeing on receive while differing on transmit
 * is an ordinary configuration -- flagging it would break every such link. */
static void test_only_the_receiver_nibble_decides_the_match(void) {
  ap_mc68681_t duart;
  ap_mc68681_reset(&duart);
  ap_mc68681_write(&duart, AP_MC68681_CR_A, 0x01u);
  ap_mc68681_write(&duart, AP_MC68681_SR_CSR_A, 0x70u);

  ap_mc68681_receive_at(&duart, 0u, 0x41u, 0x7Fu);

  TEST_ASSERT_FALSE(
      (ap_mc68681_read(&duart, AP_MC68681_SR_CSR_A) & AP_MC68681_SR_FRAMING) != 0u);
}

/* A disabled receiver never sampled anything, so it cannot have mis-sampled a
 * stop bit. Without this the flag would appear on a port nothing is listening
 * to, and a later enable would find an error that never happened. */
static void test_a_disabled_receiver_reports_no_framing_error(void) {
  ap_mc68681_t duart;
  ap_mc68681_reset(&duart);
  ap_mc68681_write(&duart, AP_MC68681_SR_CSR_A, 0x77u);

  ap_mc68681_receive_at(&duart, 0u, 0x41u, 0xBBu);

  TEST_ASSERT_FALSE(
      (ap_mc68681_read(&duart, AP_MC68681_SR_CSR_A) & AP_MC68681_SR_FRAMING) != 0u);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_character_sent_at_the_wrong_rate_sets_a_framing_error);
  RUN_TEST(test_a_character_sent_at_the_right_rate_does_not);
  RUN_TEST(test_only_the_receiver_nibble_decides_the_match);
  RUN_TEST(test_a_disabled_receiver_reports_no_framing_error);
  RUN_TEST(test_an_idle_transmitter_is_ready_and_empty);
  RUN_TEST(test_the_mode_register_pointer_advances_then_sticks);
  RUN_TEST(test_the_receive_fifo_holds_three_and_then_overruns);
  RUN_TEST(test_a_disabled_receiver_takes_nothing);
  RUN_TEST(test_a_character_written_comes_back_out);
  RUN_TEST(test_the_two_channels_are_independent);
  RUN_TEST(test_reading_the_input_change_register_clears_it);
  RUN_TEST(test_an_input_change_raises_its_interrupt);
  RUN_TEST(test_a_masked_interrupt_still_shows_in_the_status_register);
  RUN_TEST(test_reading_the_command_address_changes_nothing);
  RUN_TEST(test_the_counter_reaches_terminal_count_twice_a_period);
  RUN_TEST(test_the_stop_command_stops_a_counter_but_not_a_timer);
  RUN_TEST(test_the_output_port_has_separate_set_and_clear_addresses);
  RUN_TEST(test_resetting_the_receiver_empties_the_fifo);
  RUN_TEST(test_two_duarts_reset_alike_hold_identical_state);
  return UNITY_END();
}

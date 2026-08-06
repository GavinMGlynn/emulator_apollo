/* MC68681 / SCN2681 DUART, `[68681]` Sep 1985. */

#include "unity.h"

#include <string.h>

#include "device/ap_mc68681.h"

void setUp(void) {}
void tearDown(void) {}

static void enable_a(ap_mc68681_t *d) {
  ap_mc68681_write(d, AP_MC68681_CR_A, 0x05); /* enable receiver and transmitter */
}

/* ## §4.2.7.2's paragraph is three statements, and none of them was implemented
 *
 * "Reset Transmitter ... the TxRDY and TxEMT bits in the SRA are **cleared**",
 * "Enable Transmitter ... The transmitter-ready status bit will be asserted",
 * and "Disable Transmitter ... resets the transmitter-ready and
 * transmitter-empty status bits". This file *set* the bits on reset -- the
 * opposite -- and did nothing at all on enable or disable, and the whole suite
 * still passed, because nothing asked.
 *
 * They are consistent only together: reset clears, enable asserts. A model that
 * cleared on reset and left enable alone would hang a driver that resets its
 * transmitter and then waits for TxRDY.
 */
static void test_the_transmitter_commands_move_the_ready_bits_as_documented(void) {
  ap_mc68681_t d;
  ap_mc68681_reset(&d);

  /* Reset: both down, and the transmitter disabled with them. */
  ap_mc68681_write(&d, AP_MC68681_CR_A, 0x30); /* misc = 011, reset transmitter */
  uint8_t sr = ap_mc68681_read(&d, AP_MC68681_SR_CSR_A);
  TEST_ASSERT_EQUAL_HEX8(0, sr & AP_MC68681_SR_TXRDY);
  TEST_ASSERT_EQUAL_HEX8(0, sr & AP_MC68681_SR_TXEMT);

  /* Enable: ready asserted, which is what makes the reset above survivable. */
  ap_mc68681_write(&d, AP_MC68681_CR_A, 0x04); /* enable transmitter */
  sr = ap_mc68681_read(&d, AP_MC68681_SR_CSR_A);
  TEST_ASSERT_EQUAL_HEX8(AP_MC68681_SR_TXRDY, sr & AP_MC68681_SR_TXRDY);

  /* Disable: both down again. */
  ap_mc68681_write(&d, AP_MC68681_CR_A, 0x08); /* disable transmitter */
  sr = ap_mc68681_read(&d, AP_MC68681_SR_CSR_A);
  TEST_ASSERT_EQUAL_HEX8(0, sr & AP_MC68681_SR_TXRDY);
  TEST_ASSERT_EQUAL_HEX8(0, sr & AP_MC68681_SR_TXEMT);
}

/* §4.2.7.2's last three commands, `101` through `111`, all of which fell
 * through a bare `default: break;`. */
/* A break sent in local loopback reaches this channel's own receiver.
 *
 * Two sections have to be read together to get this right. §2.12: TxD "is held
 * high (mark condition) when the transmitter is disabled, idle, or operating in
 * the local loopback mode" -- which on its own says a break goes nowhere.
 * §3.3.2: "the transmitter output is internally connected to the receiver
 * input" -- the *pin* is held high, and the internal path still carries it.
 * `tx_break` sat stored and inert while only the first of those was read. */
/* `MR1[6]` chooses what the ISR's receiver bit *means*: `RxRDY` when clear,
 * `FFULL` when set. Table 4-5 labels it `RxRDY/FFULLA` for that reason.
 *
 * The select was not modelled at all -- not declined, not commented, simply
 * absent -- so the bit always followed `RxRDY`. A host that selects `FFULL` and
 * waits would have been satisfied by one character instead of three. */
static void test_the_isr_receiver_bit_follows_the_mr1_selection(void) {
  ap_mc68681_t d;
  ap_mc68681_reset(&d);
  enable_a(&d);

  /* Default: MR1[6] clear, so one character sets it. */
  ap_mc68681_receive(&d, 0u, 0x41u);
  TEST_ASSERT_EQUAL_HEX8(AP_MC68681_ISR_RXRDY_A,
                         d.isr & AP_MC68681_ISR_RXRDY_A);

  /* Select FFULL. One character is no longer enough -- the FIFO is three deep
   * on this part, and `FFULL` means full. */
  ap_mc68681_t full;
  ap_mc68681_reset(&full);
  ap_mc68681_write(&full, AP_MC68681_MR_A, AP_MC68681_MR1_RXRDY_IS_FFULL);
  enable_a(&full);
  ap_mc68681_receive(&full, 0u, 0x41u);
  TEST_ASSERT_EQUAL_HEX8(0u, full.isr & AP_MC68681_ISR_RXRDY_A);

  /* Fill it, and the bit sets. */
  ap_mc68681_receive(&full, 0u, 0x42u);
  ap_mc68681_receive(&full, 0u, 0x43u);
  TEST_ASSERT_EQUAL_HEX8(AP_MC68681_SR_FFULL,
                         full.channel[0].sr & AP_MC68681_SR_FFULL);
  TEST_ASSERT_EQUAL_HEX8(AP_MC68681_ISR_RXRDY_A,
                         full.isr & AP_MC68681_ISR_RXRDY_A);
}

static void test_a_break_in_local_loopback_reaches_the_receiver(void) {
  ap_mc68681_t d;
  ap_mc68681_reset(&d);

  /* MR2's channel mode: local loopback. MR1 first, then MR2, sharing the
   * pointer this part keeps. */
  ap_mc68681_write(&d, AP_MC68681_MR_A, 0x00u);
  ap_mc68681_write(&d, AP_MC68681_MR_A,
                   (uint8_t)(AP_MC68681_MODE_LOCAL_LOOPBACK << 6));
  enable_a(&d);

  TEST_ASSERT_EQUAL_HEX8(0, ap_mc68681_read(&d, AP_MC68681_SR_CSR_A) &
                                AP_MC68681_SR_BREAK);

  ap_mc68681_write(&d, AP_MC68681_CR_A, 0x60u); /* START BREAK */
  TEST_ASSERT_EQUAL_HEX8(AP_MC68681_SR_BREAK,
                         ap_mc68681_read(&d, AP_MC68681_SR_CSR_A) &
                             AP_MC68681_SR_BREAK);
  /* And the *change* is flagged, which is what a driver waits on. */
  TEST_ASSERT_EQUAL_HEX8(AP_MC68681_ISR_BREAK_A,
                         d.isr & AP_MC68681_ISR_BREAK_A);

  /* Both edges: stopping the break is a change too. */
  ap_mc68681_write(&d, AP_MC68681_CR_A, 0x50u); /* clear the change flag */
  TEST_ASSERT_EQUAL_HEX8(0, d.isr & AP_MC68681_ISR_BREAK_A);
  ap_mc68681_write(&d, AP_MC68681_CR_A, 0x70u); /* STOP BREAK */
  TEST_ASSERT_EQUAL_HEX8(0, ap_mc68681_read(&d, AP_MC68681_SR_CSR_A) &
                                AP_MC68681_SR_BREAK);
  TEST_ASSERT_EQUAL_HEX8(AP_MC68681_ISR_BREAK_A,
                         d.isr & AP_MC68681_ISR_BREAK_A);

  /* Not in normal mode: there the break goes out of the pin and nothing here
   * is listening to it. */
  ap_mc68681_t normal;
  ap_mc68681_reset(&normal);
  enable_a(&normal);
  ap_mc68681_write(&normal, AP_MC68681_CR_A, 0x60u);
  TEST_ASSERT_EQUAL_HEX8(0, ap_mc68681_read(&normal, AP_MC68681_SR_CSR_A) &
                                AP_MC68681_SR_BREAK);
}

static void test_the_break_commands_are_obeyed_rather_than_dropped(void) {
  ap_mc68681_t d;
  ap_mc68681_reset(&d);
  enable_a(&d);

  /* START BREAK: "The transmitter must be enabled for this command to be
   * accepted." Enabled here, so it is. */
  ap_mc68681_write(&d, AP_MC68681_CR_A, 0x60);
  TEST_ASSERT_TRUE(d.channel[0].tx_break);
  ap_mc68681_write(&d, AP_MC68681_CR_A, 0x70); /* STOP BREAK */
  TEST_ASSERT_FALSE(d.channel[0].tx_break);

  /* And the condition is real: with the transmitter disabled the command is
   * not accepted. A model that ignored the sentence would enter a break state
   * the hardware refuses to enter. */
  ap_mc68681_write(&d, AP_MC68681_CR_A, 0x08); /* disable transmitter */
  ap_mc68681_write(&d, AP_MC68681_CR_A, 0x60);
  TEST_ASSERT_FALSE(d.channel[0].tx_break);
}

/* `101 Reset Channel A Break Change Interrupt`: "causes the channel A break
 * detect change bit in the interrupt status register (ISR[2]) to be cleared to
 * zero", and channel B's is ISR[6].
 *
 * A driver that enables the break-change interrupt and cannot clear it never
 * leaves its handler, so a silently ignored command here is worse than a
 * refused one. */
static void test_the_break_change_interrupt_can_be_cleared_per_channel(void) {
  ap_mc68681_t d;
  ap_mc68681_reset(&d);

  d.isr = (uint8_t)(AP_MC68681_ISR_BREAK_A | AP_MC68681_ISR_BREAK_B);

  ap_mc68681_write(&d, AP_MC68681_CR_A, 0x50);
  TEST_ASSERT_EQUAL_HEX8(0, d.isr & AP_MC68681_ISR_BREAK_A);
  /* Channel B's is untouched -- the command is per channel, and a model
   * clearing both would hide a break the other channel had seen. */
  TEST_ASSERT_EQUAL_HEX8(AP_MC68681_ISR_BREAK_B, d.isr & AP_MC68681_ISR_BREAK_B);

  ap_mc68681_write(&d, AP_MC68681_CR_B, 0x50);
  TEST_ASSERT_EQUAL_HEX8(0, d.isr & AP_MC68681_ISR_BREAK_B);
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
  /* Eight bits, explicitly. `MR1` resets to `00`, which is a **five-bit** link
   * — so an unprogrammed port delivers `41` as `01`, and this test read as a
   * rate test while quietly depending on framing not being modelled. */
  ap_mc68681_write(&duart, AP_MC68681_MR_A, 0x07u);

  ap_mc68681_receive_at(&duart, 0u, 0x41u, 0x77u);

  const uint8_t sr = ap_mc68681_read(&duart, AP_MC68681_SR_CSR_A);
  TEST_ASSERT_FALSE((sr & AP_MC68681_SR_FRAMING) != 0u);
  TEST_ASSERT_TRUE((sr & AP_MC68681_SR_RXRDY) != 0u);
  TEST_ASSERT_EQUAL_HEX8(0x41u, ap_mc68681_read(&duart, AP_MC68681_RB_TB_A));
}

/* The comparison is our receive rate against the far end's **transmit** rate --
 * the lower nibble of the sender's `CSR`, not its upper. This asserted the
 * opposite for a while, on the reasoning that "two ports agreeing on receive
 * while differing on transmit is an ordinary configuration"; it is an ordinary
 * configuration and it says nothing about whether *this* link frames, since
 * what reaches this receiver is what the far end transmitted.
 *
 * The case below still reports no error, and for a better reason: code `F` is
 * the external clock, whose rate this core does not know. An unknown rate
 * cannot be disagreed with, so no error is claimed -- a refusal to invent one
 * rather than an assumption that the link is good. */
static void test_an_unknown_sender_rate_claims_no_disagreement(void) {
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


/* `MR1[1:0]` is a *count*, not a table index: `00` is five bits and `11` is
 * eight. Reading it as an index happens to give the same answers and hides why
 * `11` is eight rather than nine, so the test walks all four. */
static void test_character_length_is_five_plus_the_field(void) {
  for (unsigned v = 0; v < 4u; v++) {
    TEST_ASSERT_EQUAL_UINT(5u + v, ap_mc68681_character_bits((uint8_t)v));
  }
  /* And the upper bits do not leak in — parity and receiver control live
   * there, and a mask error would make an 8-bit link report 5. */
  TEST_ASSERT_EQUAL_UINT(8u, ap_mc68681_character_bits(0xFFu));
  TEST_ASSERT_EQUAL_UINT(5u, ap_mc68681_character_bits(0xFCu));
}

/* Bit 2 **clear** means with parity. This is the inversion most easily got
 * backwards, and getting it backwards yields a link that works until the first
 * character with an odd number of set bits — which is to say, one that passes
 * a test written with `0x00` and fails in service. */
static void test_parity_is_enabled_when_the_bit_is_clear(void) {
  TEST_ASSERT_TRUE(ap_mc68681_parity_enabled(0x00u));
  TEST_ASSERT_FALSE(ap_mc68681_parity_enabled(AP_MC68681_MR1_PARITY_ENABLE));
  /* Independent of the character length beside it. */
  TEST_ASSERT_TRUE(ap_mc68681_parity_enabled(0x03u));
  TEST_ASSERT_FALSE(ap_mc68681_parity_enabled(0x07u));
}

/* `MR2[3:0]` is sixteen encodings from 0.5 to 2 stop bits, not a one-or-two
 * flag. The two common lengths are named; the rest must survive as their own
 * code rather than being folded into the nearest named one, because a driver
 * that programmed 1.5 stop bits meant it. */
static void test_the_stop_field_keeps_its_uncommon_codes(void) {
  TEST_ASSERT_EQUAL_UINT(AP_MC68681_MR2_STOP_ONE,
                         ap_mc68681_stop_code(AP_MC68681_MR2_STOP_ONE));
  TEST_ASSERT_EQUAL_UINT(AP_MC68681_MR2_STOP_TWO,
                         ap_mc68681_stop_code(AP_MC68681_MR2_STOP_TWO));
  /* A code between the two named ones is neither, and stays itself. */
  TEST_ASSERT_EQUAL_UINT(0x0Bu, ap_mc68681_stop_code(0x0Bu));
  /* The upper nibble is CTS/RTS control and the channel mode; it must not
   * reach the stop-bit answer. */
  TEST_ASSERT_EQUAL_UINT(AP_MC68681_MR2_STOP_ONE, ap_mc68681_stop_code(0xF7u));
}


/* A character arrives with only as many bits as the link carries. A receiver
 * programmed for seven never sees an eighth -- the bit is not transmitted, so
 * this is the absence of a signal rather than truncation of a value.
 *
 * It is also why a seven-bit console shows `A` for both `41` and `C1`, and why
 * a driver that programmed seven and then sent eight-bit data gets a silently
 * altered stream and no error: nothing went wrong on the wire. */
static void test_a_character_arrives_with_the_links_bit_count(void) {
  ap_mc68681_t duart;
  ap_mc68681_reset(&duart);
  ap_mc68681_write(&duart, AP_MC68681_CR_A, 0x01u);
  ap_mc68681_write(&duart, AP_MC68681_SR_CSR_A, 0x77u);
  /* MR1 = seven bits (field 10), parity disabled (bit 2 set). */
  ap_mc68681_write(&duart, AP_MC68681_MR_A, 0x06u);

  ap_mc68681_receive_at(&duart, 0u, 0xC1u, 0x77u);

  TEST_ASSERT_EQUAL_HEX8(0x41u, ap_mc68681_read(&duart, AP_MC68681_RB_TB_A));
}

/* Eight bits passes the byte through unchanged, which is the control the test
 * above needs: without it, a model that always masked to seven would satisfy
 * it. */
static void test_an_eight_bit_link_passes_the_whole_byte(void) {
  ap_mc68681_t duart;
  ap_mc68681_reset(&duart);
  ap_mc68681_write(&duart, AP_MC68681_CR_A, 0x01u);
  ap_mc68681_write(&duart, AP_MC68681_SR_CSR_A, 0x77u);
  ap_mc68681_write(&duart, AP_MC68681_MR_A, 0x07u); /* eight bits */

  ap_mc68681_receive_at(&duart, 0u, 0xC1u, 0x77u);

  TEST_ASSERT_EQUAL_HEX8(0xC1u, ap_mc68681_read(&duart, AP_MC68681_RB_TB_A));
}


/* `MR2[7:6]` is the channel mode. Normal is a wire to the outside; the other
 * three connect the channel to itself, and a self-test uses them to check the
 * part with nothing attached. */
static void test_the_channel_mode_is_the_top_two_bits_of_mr2(void) {
  TEST_ASSERT_EQUAL_UINT(AP_MC68681_MODE_NORMAL, ap_mc68681_channel_mode(0x3Fu));
  TEST_ASSERT_EQUAL_UINT(AP_MC68681_MODE_AUTO_ECHO,
                         ap_mc68681_channel_mode(0x40u));
  TEST_ASSERT_EQUAL_UINT(AP_MC68681_MODE_LOCAL_LOOPBACK,
                         ap_mc68681_channel_mode(0x80u));
  TEST_ASSERT_EQUAL_UINT(AP_MC68681_MODE_REMOTE_LOOPBACK,
                         ap_mc68681_channel_mode(0xC0u));
}

/* Local loopback: "the transmitter output is internally connected to the
 * receiver input". A transmitted character comes back on the same channel,
 * framed by that channel's own settings — a self-test that bypassed framing
 * would be checking the FIFO rather than the link. */
static void test_local_loopback_returns_the_character_framed(void) {
  ap_mc68681_t duart;
  ap_mc68681_reset(&duart);
  ap_mc68681_write(&duart, AP_MC68681_MR_A, 0x06u); /* MR1: seven bits */
  ap_mc68681_write(&duart, AP_MC68681_MR_A, 0x87u); /* MR2: local loopback */
  ap_mc68681_write(&duart, AP_MC68681_CR_A, 0x05u); /* rx and tx enable */

  ap_mc68681_write(&duart, AP_MC68681_RB_TB_A, 0xC1u);

  TEST_ASSERT_TRUE((ap_mc68681_read(&duart, AP_MC68681_SR_CSR_A) &
                    AP_MC68681_SR_RXRDY) != 0u);
  TEST_ASSERT_EQUAL_HEX8(0x41u, ap_mc68681_read(&duart, AP_MC68681_RB_TB_A));
}

/* And it must *not* also reach the pin. The character never leaves the part, so
 * a caller collecting transmitted bytes sees nothing — a model that both looped
 * back and transmitted would let a self-test pass while the outside world saw
 * traffic it should never have seen. */
static void test_local_loopback_transmits_nothing_outward(void) {
  ap_mc68681_t duart;
  ap_mc68681_reset(&duart);
  ap_mc68681_write(&duart, AP_MC68681_MR_A, 0x07u);
  ap_mc68681_write(&duart, AP_MC68681_MR_A, 0x87u);
  ap_mc68681_write(&duart, AP_MC68681_CR_A, 0x05u);

  ap_mc68681_write(&duart, AP_MC68681_RB_TB_A, 0x5Au);

  uint8_t out = 0;
  TEST_ASSERT_FALSE(ap_mc68681_transmit(&duart, 0u, &out));
}

/* Normal mode still transmits outward, which is the control the two above need:
 * a model that never transmitted would satisfy both. */
static void test_normal_mode_still_transmits_outward(void) {
  ap_mc68681_t duart;
  ap_mc68681_reset(&duart);
  ap_mc68681_write(&duart, AP_MC68681_MR_A, 0x07u);
  ap_mc68681_write(&duart, AP_MC68681_MR_A, 0x07u); /* MR2: normal */
  ap_mc68681_write(&duart, AP_MC68681_CR_A, 0x05u);

  ap_mc68681_write(&duart, AP_MC68681_RB_TB_A, 0x5Au);

  uint8_t out = 0;
  TEST_ASSERT_TRUE(ap_mc68681_transmit(&duart, 0u, &out));
  TEST_ASSERT_EQUAL_HEX8(0x5Au, out);
}


/* Auto-echo passes the character on *and* delivers it: a terminal sees its own
 * typing echoed by the part rather than by software. */
static void test_auto_echo_both_retransmits_and_delivers(void) {
  ap_mc68681_t duart;
  ap_mc68681_reset(&duart);
  ap_mc68681_write(&duart, AP_MC68681_MR_A, 0x07u); /* MR1: eight bits */
  ap_mc68681_write(&duart, AP_MC68681_MR_A, 0x47u); /* MR2: auto-echo */
  ap_mc68681_write(&duart, AP_MC68681_CR_A, 0x05u);
  ap_mc68681_write(&duart, AP_MC68681_SR_CSR_A, 0x77u);

  ap_mc68681_receive_at(&duart, 0u, 0x5Au, 0x77u);

  uint8_t out = 0;
  TEST_ASSERT_TRUE(ap_mc68681_transmit(&duart, 0u, &out));
  TEST_ASSERT_EQUAL_HEX8(0x5Au, out);
  TEST_ASSERT_EQUAL_HEX8(0x5Au, ap_mc68681_read(&duart, AP_MC68681_RB_TB_A));
}

/* Remote loopback retransmits and does **not** deliver. The channel is a mirror
 * for someone else's test, and a local program must not see traffic that was
 * never addressed to it — delivering in both modes would make remote loopback
 * indistinguishable from auto-echo, which is the one thing separating them. */
static void test_remote_loopback_retransmits_without_delivering(void) {
  ap_mc68681_t duart;
  ap_mc68681_reset(&duart);
  ap_mc68681_write(&duart, AP_MC68681_MR_A, 0x07u);
  ap_mc68681_write(&duart, AP_MC68681_MR_A, 0xC7u); /* MR2: remote loopback */
  ap_mc68681_write(&duart, AP_MC68681_CR_A, 0x05u);
  ap_mc68681_write(&duart, AP_MC68681_SR_CSR_A, 0x77u);

  ap_mc68681_receive_at(&duart, 0u, 0x5Au, 0x77u);

  uint8_t out = 0;
  TEST_ASSERT_TRUE(ap_mc68681_transmit(&duart, 0u, &out));
  TEST_ASSERT_EQUAL_HEX8(0x5Au, out);
  /* Nothing for the program to read. */
  TEST_ASSERT_FALSE((ap_mc68681_read(&duart, AP_MC68681_SR_CSR_A) &
                     AP_MC68681_SR_RXRDY) != 0u);
}

/* Normal mode does neither, which is the control both need: a model that always
 * echoed would satisfy the first and a model that never delivered would satisfy
 * the second. */
static void test_normal_mode_neither_echoes_nor_withholds(void) {
  ap_mc68681_t duart;
  ap_mc68681_reset(&duart);
  ap_mc68681_write(&duart, AP_MC68681_MR_A, 0x07u);
  ap_mc68681_write(&duart, AP_MC68681_MR_A, 0x07u); /* MR2: normal */
  ap_mc68681_write(&duart, AP_MC68681_CR_A, 0x05u);
  ap_mc68681_write(&duart, AP_MC68681_SR_CSR_A, 0x77u);

  ap_mc68681_receive_at(&duart, 0u, 0x5Au, 0x77u);

  uint8_t out = 0;
  TEST_ASSERT_FALSE(ap_mc68681_transmit(&duart, 0u, &out));
  TEST_ASSERT_EQUAL_HEX8(0x5Au, ap_mc68681_read(&duart, AP_MC68681_RB_TB_A));
}


/* Parity compared as **enable and type together**. Two ports both using parity
 * but disagreeing on odd against even produce a wrong bit on roughly half of
 * all characters — a link that works intermittently, which is worse than one
 * that never works and invisible to a test that sends a single character. */
static void test_a_parity_type_disagreement_is_a_parity_error(void) {
  ap_mc68681_t duart;
  ap_mc68681_reset(&duart);
  ap_mc68681_write(&duart, AP_MC68681_CR_A, 0x01u);
  ap_mc68681_write(&duart, AP_MC68681_SR_CSR_A, 0x77u);
  /* Eight bits, parity on (bit 2 clear), type 00. */
  ap_mc68681_write(&duart, AP_MC68681_MR_A, 0x03u);

  /* Same rate, same enable, different type. */
  ap_mc68681_receive_framed(&duart, 0u, 0x41u, 0x77u, 0x0Bu);

  TEST_ASSERT_TRUE((ap_mc68681_read(&duart, AP_MC68681_SR_CSR_A) &
                    AP_MC68681_SR_PARITY) != 0u);
}

/* Agreement does not, or a correctly configured link would report errors. */
static void test_matching_parity_is_not_an_error(void) {
  ap_mc68681_t duart;
  ap_mc68681_reset(&duart);
  ap_mc68681_write(&duart, AP_MC68681_CR_A, 0x01u);
  ap_mc68681_write(&duart, AP_MC68681_SR_CSR_A, 0x77u);
  ap_mc68681_write(&duart, AP_MC68681_MR_A, 0x03u);

  ap_mc68681_receive_framed(&duart, 0u, 0x41u, 0x77u, 0x03u);

  TEST_ASSERT_FALSE((ap_mc68681_read(&duart, AP_MC68681_SR_CSR_A) &
                     AP_MC68681_SR_PARITY) != 0u);
}

/* A receiver not using parity cannot find a parity bit wrong, however the
 * sender was configured. Without this, a no-parity console would report errors
 * against any sender that used parity — and the DN3500's own ports are
 * configured by firmware we do not control. */
static void test_a_receiver_without_parity_reports_none(void) {
  ap_mc68681_t duart;
  ap_mc68681_reset(&duart);
  ap_mc68681_write(&duart, AP_MC68681_CR_A, 0x01u);
  ap_mc68681_write(&duart, AP_MC68681_SR_CSR_A, 0x77u);
  ap_mc68681_write(&duart, AP_MC68681_MR_A, 0x07u); /* bit 2 set: no parity */

  ap_mc68681_receive_framed(&duart, 0u, 0x41u, 0x77u, 0x03u);

  TEST_ASSERT_FALSE((ap_mc68681_read(&duart, AP_MC68681_SR_CSR_A) &
                     AP_MC68681_SR_PARITY) != 0u);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_character_sent_at_the_wrong_rate_sets_a_framing_error);
  RUN_TEST(test_a_character_sent_at_the_right_rate_does_not);
  RUN_TEST(test_an_unknown_sender_rate_claims_no_disagreement);
  RUN_TEST(test_a_disabled_receiver_reports_no_framing_error);
  RUN_TEST(test_character_length_is_five_plus_the_field);
  RUN_TEST(test_parity_is_enabled_when_the_bit_is_clear);
  RUN_TEST(test_the_stop_field_keeps_its_uncommon_codes);
  RUN_TEST(test_a_character_arrives_with_the_links_bit_count);
  RUN_TEST(test_an_eight_bit_link_passes_the_whole_byte);
  RUN_TEST(test_the_channel_mode_is_the_top_two_bits_of_mr2);
  RUN_TEST(test_local_loopback_returns_the_character_framed);
  RUN_TEST(test_local_loopback_transmits_nothing_outward);
  RUN_TEST(test_normal_mode_still_transmits_outward);
  RUN_TEST(test_auto_echo_both_retransmits_and_delivers);
  RUN_TEST(test_remote_loopback_retransmits_without_delivering);
  RUN_TEST(test_normal_mode_neither_echoes_nor_withholds);
  RUN_TEST(test_a_parity_type_disagreement_is_a_parity_error);
  RUN_TEST(test_matching_parity_is_not_an_error);
  RUN_TEST(test_a_receiver_without_parity_reports_none);
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
  RUN_TEST(test_the_transmitter_commands_move_the_ready_bits_as_documented);
  RUN_TEST(test_the_isr_receiver_bit_follows_the_mr1_selection);
  RUN_TEST(test_a_break_in_local_loopback_reaches_the_receiver);
  RUN_TEST(test_the_break_commands_are_obeyed_rather_than_dropped);
  RUN_TEST(test_the_break_change_interrupt_can_be_cleared_per_channel);
  RUN_TEST(test_two_duarts_reset_alike_hold_identical_state);
  return UNITY_END();
}

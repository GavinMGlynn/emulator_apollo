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
/* `MR2[4]`, clear-to-send control. §4.2.2.3: with it set "the transmitter
 * checks the state of CTSA (IP0) each time it is ready to send a character. If
 * IP0 is asserted (low), the character is transmitted. If it is negated (high),
 * the ... transmission is delayed until CTSA goes low."
 *
 * Delayed, not dropped -- and asserted is **low**, which is the half a model
 * gets backwards and then holds off exactly when the hardware transmits. */
/* Table 4-5 sheet 5's footnote: "Bit seven has no external pin. Upon reading
 * the input port, bit seven will always be read as a one." A pinless bit that
 * reads as a constant is exactly the kind a register-table walk finds and a
 * search for admissions cannot -- nobody wrote a note about it. */
/* Table 4-5 sheet 2's two baud sets, and the three codes where set 2 was a
 * copy of set 1. `ACR[7]` chooses between them.
 *
 * The comment above this table asserted the sets "differ only at codes 0 and
 * 3", and they differ at five. Code 7 is one of them -- and the firmware writes
 * `CSRB = 77`, so it reads correctly only because this board leaves `ACR[7]`
 * clear. */
/* Table 4-5 sheet 3 gives `OPCR` six independent selects. This core acted on
 * one -- `OPCR[7]` -- because a board register happened to need it, and stored
 * the other five inertly. */
static void test_every_opcr_select_reaches_its_output_pin(void) {
  ap_mc68681_t d;
  ap_mc68681_reset(&d);
  enable_a(&d);

  /* Default: every pin is its `OPR` bit, **complemented**. */
  d.opr = 0x00u;
  for (unsigned pin = 0u; pin < 8u; pin++) {
    TEST_ASSERT_TRUE(ap_mc68681_output_pin(&d, pin));
  }
  d.opr = 0xFFu;
  TEST_ASSERT_FALSE(ap_mc68681_output_pin(&d, 1u));
  TEST_ASSERT_FALSE(ap_mc68681_output_pin(&d, 0u));

  /* `OP6` follows channel A's `TxRDY` once selected -- and not before, which
   * is the half that was missing. */
  ap_mc68681_write(&d, AP_MC68681_IP_OPCR, 0x40u);
  TEST_ASSERT_TRUE((d.channel[0].sr & AP_MC68681_SR_TXRDY) != 0u);
  TEST_ASSERT_TRUE(ap_mc68681_output_pin(&d, 6u));
  ap_mc68681_write(&d, AP_MC68681_RB_TB_A, 0x41u); /* clears TxRDY */
  TEST_ASSERT_FALSE(ap_mc68681_output_pin(&d, 6u));

  /* `OP4` follows channel A's receiver bit, and *which* bit is `MR1[6]`'s
   * choice -- §4.2.1.2: the selection "also causes the selected bit to be
   * output on the parallel output OP4". */
  ap_mc68681_t rx;
  ap_mc68681_reset(&rx);
  enable_a(&rx);
  ap_mc68681_write(&rx, AP_MC68681_IP_OPCR, 0x10u);
  ap_mc68681_receive(&rx, 0u, 0x41u);
  TEST_ASSERT_TRUE(ap_mc68681_output_pin(&rx, 4u)); /* RxRDY */

  ap_mc68681_t full;
  ap_mc68681_reset(&full);
  ap_mc68681_write(&full, AP_MC68681_MR_A, AP_MC68681_MR1_RXRDY_IS_FFULL);
  enable_a(&full);
  ap_mc68681_write(&full, AP_MC68681_IP_OPCR, 0x10u);
  ap_mc68681_receive(&full, 0u, 0x41u);
  TEST_ASSERT_FALSE(ap_mc68681_output_pin(&full, 4u)); /* FFULL: not yet */
  ap_mc68681_receive(&full, 0u, 0x42u);
  ap_mc68681_receive(&full, 0u, 0x43u);
  TEST_ASSERT_TRUE(ap_mc68681_output_pin(&full, 4u));

  /* `OP3` code 01 is the counter/timer output, which this core does model. */
  ap_mc68681_t timer;
  ap_mc68681_reset(&timer);
  ap_mc68681_write(&timer, AP_MC68681_IP_OPCR, 0x04u);
  timer.counter_output = true;
  TEST_ASSERT_TRUE(ap_mc68681_output_pin(&timer, 3u));
  timer.counter_output = false;
  TEST_ASSERT_FALSE(ap_mc68681_output_pin(&timer, 3u));
}

static void test_the_second_baud_set_is_not_a_copy_of_the_first(void) {
  /* Where they agree. */
  TEST_ASSERT_EQUAL_UINT(110u, ap_mc68681_baud(0x1u, false));
  TEST_ASSERT_EQUAL_UINT(110u, ap_mc68681_baud(0x1u, true));
  TEST_ASSERT_EQUAL_UINT(9600u, ap_mc68681_baud(0xBu, false));
  TEST_ASSERT_EQUAL_UINT(9600u, ap_mc68681_baud(0xBu, true));

  /* All five where they do not. */
  TEST_ASSERT_EQUAL_UINT(50u, ap_mc68681_baud(0x0u, false));
  TEST_ASSERT_EQUAL_UINT(75u, ap_mc68681_baud(0x0u, true));
  TEST_ASSERT_EQUAL_UINT(200u, ap_mc68681_baud(0x3u, false));
  TEST_ASSERT_EQUAL_UINT(150u, ap_mc68681_baud(0x3u, true));
  TEST_ASSERT_EQUAL_UINT(1050u, ap_mc68681_baud(0x7u, false));
  TEST_ASSERT_EQUAL_UINT(2000u, ap_mc68681_baud(0x7u, true));
  TEST_ASSERT_EQUAL_UINT(7200u, ap_mc68681_baud(0xAu, false));
  TEST_ASSERT_EQUAL_UINT(1800u, ap_mc68681_baud(0xAu, true));
  TEST_ASSERT_EQUAL_UINT(38400u, ap_mc68681_baud(0xCu, false));
  TEST_ASSERT_EQUAL_UINT(19200u, ap_mc68681_baud(0xCu, true));

  /* `D`, `E` and `F` are the timer and the two external clocks, not rates. */
  for (uint8_t code = 0xDu; code <= 0xFu; code++) {
    TEST_ASSERT_EQUAL_UINT(0u, ap_mc68681_baud(code, false));
    TEST_ASSERT_EQUAL_UINT(0u, ap_mc68681_baud(code, true));
  }
}

static void test_the_input_port_reads_bit_seven_as_one(void) {
  ap_mc68681_t d;
  ap_mc68681_reset(&d);

  TEST_ASSERT_EQUAL_HEX8(0x80u,
                         ap_mc68681_read(&d, AP_MC68681_IP_OPCR) & 0x80u);
  /* And it survives whatever the pins are driven to. */
  ap_mc68681_set_input(&d, 0x0Fu);
  TEST_ASSERT_EQUAL_HEX8(0x8Fu, ap_mc68681_read(&d, AP_MC68681_IP_OPCR));
}

static void test_cts_gates_the_transmitter_when_mr2_selects_it(void) {
  ap_mc68681_t d;
  ap_mc68681_reset(&d);
  enable_a(&d);
  /* MR1 then MR2, sharing the pointer: CTS enable in MR2. */
  ap_mc68681_write(&d, AP_MC68681_MR_A, 0x03u);
  ap_mc68681_write(&d, AP_MC68681_MR_A, AP_MC68681_MR2_CTS_ENABLE);

  /* CTS negated -- the pin *set* -- holds the byte. */
  ap_mc68681_set_input(&d, AP_MC68681_IP_CTS(0u));
  ap_mc68681_write(&d, AP_MC68681_RB_TB_A, 0x41u);
  uint8_t out = 0u;
  TEST_ASSERT_FALSE(ap_mc68681_transmit(&d, 0u, &out));

  /* It is held, not lost: asserting CTS releases the same byte. */
  ap_mc68681_set_input(&d, 0u);
  TEST_ASSERT_TRUE(ap_mc68681_transmit(&d, 0u, &out));
  TEST_ASSERT_EQUAL_HEX8(0x41u, out);

  /* And with the bit clear, CTS "has no effect on the transmitter". */
  ap_mc68681_t free_running;
  ap_mc68681_reset(&free_running);
  enable_a(&free_running);
  ap_mc68681_set_input(&free_running, AP_MC68681_IP_CTS(0u));
  ap_mc68681_write(&free_running, AP_MC68681_RB_TB_A, 0x42u);
  TEST_ASSERT_TRUE(ap_mc68681_transmit(&free_running, 0u, &out));
  TEST_ASSERT_EQUAL_HEX8(0x42u, out);
}

/* `MR1[5]`, error mode. §4.2.1.3: character mode's three FIFOed status bits
 * "apply only to the character at the top of the FIFO"; block mode accumulates
 * them until `RESET ERROR STATUS`. This core was block mode unconditionally. */
static void test_character_error_mode_reports_the_top_of_the_fifo(void) {
  ap_mc68681_t d;
  ap_mc68681_reset(&d);
  enable_a(&d);
  ap_mc68681_write(&d, AP_MC68681_MR_A, 0x03u); /* MR1: character mode */
  ap_mc68681_write(&d, AP_MC68681_SR_CSR_A, 0x77u);

  /* One character at the wrong rate -- a framing error -- then one at the
   * right rate behind it. */
  ap_mc68681_receive_framed(&d, 0u, 0x41u, 0xBBu, 0x03u);
  ap_mc68681_receive_framed(&d, 0u, 0x42u, 0x77u, 0x03u);
  TEST_ASSERT_TRUE((ap_mc68681_read(&d, AP_MC68681_SR_CSR_A) &
                    AP_MC68681_SR_FRAMING) != 0u);

  /* Taking the bad character republishes the good one's status. */
  (void)ap_mc68681_read(&d, AP_MC68681_RB_TB_A);
  TEST_ASSERT_FALSE((ap_mc68681_read(&d, AP_MC68681_SR_CSR_A) &
                     AP_MC68681_SR_FRAMING) != 0u);

  /* Block mode keeps it: the accumulation stands until RESET ERROR STATUS. */
  ap_mc68681_t block;
  ap_mc68681_reset(&block);
  enable_a(&block);
  ap_mc68681_write(&block, AP_MC68681_MR_A,
                   (uint8_t)(0x03u | AP_MC68681_MR1_ERROR_BLOCK));
  ap_mc68681_write(&block, AP_MC68681_SR_CSR_A, 0x77u);
  ap_mc68681_receive_framed(&block, 0u, 0x41u, 0xBBu, 0x03u);
  ap_mc68681_receive_framed(&block, 0u, 0x42u, 0x77u, 0x03u);
  (void)ap_mc68681_read(&block, AP_MC68681_RB_TB_A);
  TEST_ASSERT_TRUE((ap_mc68681_read(&block, AP_MC68681_SR_CSR_A) &
                    AP_MC68681_SR_FRAMING) != 0u);
}

/* `MR1[7]` and `MR2[5]`, the two RTS controls. §4.2.1.1 exists to "prevent
 * overrun in the receiver by using the RTSA output signal to control the
 * clear-to-send CTS input of the transmitting device"; §4.2.2.2 clears the same
 * output once the transmitter has drained. */
static void test_the_rts_controls_drive_the_output_port(void) {
  ap_mc68681_t d;
  ap_mc68681_reset(&d);
  enable_a(&d);
  ap_mc68681_write(&d, AP_MC68681_MR_A, AP_MC68681_MR1_RX_RTS | 0x03u);
  d.opr = AP_MC68681_OP_RTS(0u);

  /* A full FIFO negates RTS ... */
  ap_mc68681_receive(&d, 0u, 0x41u);
  TEST_ASSERT_TRUE((d.opr & AP_MC68681_OP_RTS(0u)) != 0u);
  ap_mc68681_receive(&d, 0u, 0x42u);
  ap_mc68681_receive(&d, 0u, 0x43u);
  TEST_ASSERT_EQUAL_HEX8(0u, d.opr & AP_MC68681_OP_RTS(0u));

  /* ... and room releases it. */
  (void)ap_mc68681_read(&d, AP_MC68681_RB_TB_A);
  TEST_ASSERT_TRUE((d.opr & AP_MC68681_OP_RTS(0u)) != 0u);

  /* `MR2[5]`: the transmitter clears it once the holding register drains. */
  ap_mc68681_t tx;
  ap_mc68681_reset(&tx);
  enable_a(&tx);
  ap_mc68681_write(&tx, AP_MC68681_MR_A, 0x03u);
  ap_mc68681_write(&tx, AP_MC68681_MR_A, AP_MC68681_MR2_TX_RTS);
  tx.opr = AP_MC68681_OP_RTS(0u);
  ap_mc68681_write(&tx, AP_MC68681_RB_TB_A, 0x41u);
  TEST_ASSERT_TRUE((tx.opr & AP_MC68681_OP_RTS(0u)) != 0u);
  uint8_t byte = 0u;
  TEST_ASSERT_TRUE(ap_mc68681_transmit(&tx, 0u, &byte));
  TEST_ASSERT_EQUAL_HEX8(0u, tx.opr & AP_MC68681_OP_RTS(0u));
}

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

/* **Reset clears the status register; it does not announce a transmitter.**
 *
 * §2.4: "A hardware reset, assertion of RESET, clears status registers A and B
 * (SRA and SRB) ... and places channels A and B in the inactive state".
 *
 * This test previously asserted the opposite -- that an idle transmitter reads
 * ready and empty at reset -- and the code matched it. Both were wrong in the
 * same way, which is why the suite stayed green: the reasoning "an idle
 * transmitter is ready and empty" describes an **enabled** transmitter, and
 * reset leaves the channel inactive. The oracle differential found it, three of
 * four channels reading `0C` where the reference read `00` for a whole boot.
 *
 * The worry the old test encoded -- a driver waiting for a transmitter that
 * never announces itself -- is real and is answered by the enable path, which
 * is asserted here rather than assumed. */
static void test_reset_clears_the_status_register(void) {
  ap_mc68681_t d;
  ap_mc68681_reset(&d);

  TEST_ASSERT_EQUAL_HEX8(0x00u, ap_mc68681_read(&d, AP_MC68681_SR_CSR_A));
  TEST_ASSERT_EQUAL_HEX8(0x00u, ap_mc68681_read(&d, AP_MC68681_SR_CSR_B));

  /* And the transmitter announces itself when it is enabled, which is what
   * stops the cleared reset value from being a hang. §4.2.9.6: transmitter
   * ready "is set when the transmitter is first enabled". */
  ap_mc68681_write(&d, AP_MC68681_CR_A, 0x04u); /* enable transmitter */
  const uint8_t sr = ap_mc68681_read(&d, AP_MC68681_SR_CSR_A);
  TEST_ASSERT_EQUAL_HEX8(AP_MC68681_SR_TXRDY, sr & AP_MC68681_SR_TXRDY);
  TEST_ASSERT_EQUAL_HEX8(0, sr & AP_MC68681_SR_RXRDY);

  /* **But not transmitter-empty**, which is a different event. §4.2.9.5: TxEMT
   * "will be set when the channel A transmitter underruns ... It is set after
   * transmission of the last stop bit of a character if no character is in the
   * transmit holding register awaiting transmission." A transmitter that has
   * just been enabled has not transmitted anything, so it has not underrun.
   *
   * Asserted because the first draft of this test assumed the opposite -- an
   * enabled idle transmitter "is obviously empty" -- and MAME's model does set
   * both here. The manual distinguishes them, so this core does too. */
  TEST_ASSERT_EQUAL_HEX8(0, sr & AP_MC68681_SR_TXEMT);
}

/* §2.4 again, and the only register reset gives a value rather than clearing:
 * "RESET initializes the interrupt vector register (IVR) to 0F16". A `memset`
 * reset gets every other register right and this one wrong. */
static void test_reset_initialises_the_interrupt_vector_to_0f(void) {
  ap_mc68681_t d;
  ap_mc68681_reset(&d);
  TEST_ASSERT_EQUAL_HEX8(0x0Fu, ap_mc68681_read(&d, AP_MC68681_IVR));
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

  /* A fourth is **held in the receive shift register, not lost** -- which is
   * what "quadruple-buffered" means and what §4.2.9.4 requires: overrun is set
   * "upon receipt of a new character when the FIFO is full **and a character is
   * already in the receive shift register**". This test asserted the opposite
   * for as long as the model implemented it. */
  ap_mc68681_receive(&d, 0, 'd');
  TEST_ASSERT_EQUAL_HEX8(0, ap_mc68681_read(&d, AP_MC68681_SR_CSR_A) &
                                AP_MC68681_SR_OVERRUN);

  /* The *fifth* overruns, and it is the newest that is discarded. */
  ap_mc68681_receive(&d, 0, 'e');
  TEST_ASSERT_EQUAL_HEX8(AP_MC68681_SR_OVERRUN,
                         ap_mc68681_read(&d, AP_MC68681_SR_CSR_A) &
                             AP_MC68681_SR_OVERRUN);

  /* Reading frees a position, the held character takes it, and §4.2.9.7's
   * consequence follows: `FFULL` is **still set** after the read, because the
   * FIFO is full again. */
  TEST_ASSERT_EQUAL_HEX8('a', ap_mc68681_read(&d, AP_MC68681_RB_TB_A));
  TEST_ASSERT_EQUAL_HEX8(AP_MC68681_SR_FFULL,
                         ap_mc68681_read(&d, AP_MC68681_SR_CSR_A) &
                             AP_MC68681_SR_FFULL);

  /* All four buffered characters come out, in order, and only then is the
   * receiver empty. */
  TEST_ASSERT_EQUAL_HEX8('b', ap_mc68681_read(&d, AP_MC68681_RB_TB_A));
  TEST_ASSERT_EQUAL_HEX8(0, ap_mc68681_read(&d, AP_MC68681_SR_CSR_A) &
                                AP_MC68681_SR_FFULL);
  TEST_ASSERT_EQUAL_HEX8('c', ap_mc68681_read(&d, AP_MC68681_RB_TB_A));
  TEST_ASSERT_EQUAL_HEX8('d', ap_mc68681_read(&d, AP_MC68681_RB_TB_A));
  TEST_ASSERT_EQUAL_HEX8(0, ap_mc68681_read(&d, AP_MC68681_SR_CSR_A) &
                                AP_MC68681_SR_RXRDY);
}

/* The shift register belongs to the receiver, so `RESET RECEIVER` destroys a
 * character held there along with the FIFO's -- and counts it, since a host
 * typing at this channel cannot otherwise tell a consumed character from a
 * discarded one. */
static void test_resetting_the_receiver_also_discards_the_held_character(void) {
  ap_mc68681_t d;
  ap_mc68681_reset(&d);
  enable_a(&d);

  ap_mc68681_receive(&d, 0, 'a');
  ap_mc68681_receive(&d, 0, 'b');
  ap_mc68681_receive(&d, 0, 'c');
  ap_mc68681_receive(&d, 0, 'd'); /* into the shift register */

  const unsigned before = d.channel[0].rx_flushed;
  ap_mc68681_write(&d, AP_MC68681_CR_A, 0x20u); /* RESET RECEIVER */
  TEST_ASSERT_EQUAL_UINT(before + 4u, d.channel[0].rx_flushed);

  /* And nothing is left to hand back once the receiver is enabled again. */
  enable_a(&d);
  TEST_ASSERT_EQUAL_HEX8(0, ap_mc68681_read(&d, AP_MC68681_SR_CSR_A) &
                                (AP_MC68681_SR_RXRDY | AP_MC68681_SR_FFULL));
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
  /* **And the per-pin enable.** §4.2.13.3: `ACR[3:0]` "selects which bits of
   * the input port change register can cause ... ISR[7] to be set", so the mask
   * alone is not enough. This test set only the mask and passed because the
   * model raised the interrupt for every pin. */
  ap_mc68681_write(&d, AP_MC68681_IPCR_ACR, 0x0Fu);

  TEST_ASSERT_FALSE(ap_mc68681_irq(&d));
  ap_mc68681_set_input(&d, 0x04);
  TEST_ASSERT_TRUE(ap_mc68681_irq(&d));

  /* A pin whose enable is clear records its change and raises nothing. */
  ap_mc68681_t gated;
  ap_mc68681_reset(&gated);
  ap_mc68681_write(&gated, AP_MC68681_ISR_IMR, AP_MC68681_ISR_INPUT);
  ap_mc68681_write(&gated, AP_MC68681_IPCR_ACR, 0x01u); /* IP0 only */
  ap_mc68681_set_input(&gated, 0x04u);                  /* IP2 changed */
  TEST_ASSERT_FALSE(ap_mc68681_irq(&gated));
  TEST_ASSERT_TRUE((ap_mc68681_read(&gated, AP_MC68681_IPCR_ACR) & 0x40u) != 0u);

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

  /* Nothing is exempt: reset is the initialiser and must leave *every* byte
   * the same, quirk set included. Two structs filled with different garbage
   * and then reset must be indistinguishable, or some field is being carried
   * over from before the machine existed -- which is exactly the defect this
   * suite caught on Windows. */
  TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof a);
}

/* **Reset clears the quirk set, and that is the safe direction.**
 *
 * This test asserted the opposite for one commit, and the opposite was wrong in
 * a way Linux could not show. `ap_mc68681_reset` is the *initialiser* -- every
 * caller writes `ap_mc68681_t d; ap_mc68681_reset(&d);` -- so preserving the
 * field meant reading it before anything had written it. That is undefined
 * behaviour, it read zero here and did not on Windows, and CI failed with a
 * transmitter reporting TxEMT it had not earned.
 *
 * Nothing is lost by clearing: `ap_board_set_quirks` applies the set after
 * `ap_board_init`, and nothing resets a device after that point. */
static void test_reset_clears_the_selected_quirks(void) {
  ap_mc68681_t d;
  ap_mc68681_reset(&d);
  ap_quirk_select(&d.quirks, AP_QUIRK_DUART_ENABLE_SETS_TXEMT);
  ap_mc68681_reset(&d);
  TEST_ASSERT_FALSE(
      ap_quirk_selected(d.quirks, AP_QUIRK_DUART_ENABLE_SETS_TXEMT));
}

/* The quirk's whole behaviour: with it selected, enabling the transmitter
 * asserts TxEMT as MAME does; without it, only TxRDY, as §4.2.9.5/6 say. */
static void test_the_txemt_quirk_matches_the_oracle_when_selected(void) {
  ap_mc68681_t reference;
  ap_mc68681_reset(&reference);
  ap_mc68681_write(&reference, AP_MC68681_CR_A, 0x04u);
  TEST_ASSERT_EQUAL_HEX8(AP_MC68681_SR_TXRDY,
                         ap_mc68681_read(&reference, AP_MC68681_SR_CSR_A));

  ap_mc68681_t like_mame;
  ap_mc68681_reset(&like_mame);
  ap_quirk_select(&like_mame.quirks, AP_QUIRK_DUART_ENABLE_SETS_TXEMT);
  ap_mc68681_write(&like_mame, AP_MC68681_CR_A, 0x04u);
  TEST_ASSERT_EQUAL_HEX8(
      (uint8_t)(AP_MC68681_SR_TXRDY | AP_MC68681_SR_TXEMT),
      ap_mc68681_read(&like_mame, AP_MC68681_SR_CSR_A));
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

/* Parity is the **mode field**, `MR1[4:3]`, and only its `00` code is "with
 * parity". This test used to assert bit 2 -- the parity *type* -- and called
 * that "the inversion most easily got backwards", which it then got backwards:
 * the two adjacent fields were exchanged, so the predicate answered even/odd
 * when asked on/off.
 *
 * The mode has four values and a boolean cannot carry them. `Force Parity`
 * sends a fixed bit and `Multidrop` uses the position as an address tag; both
 * are neither on nor off, and both used to read as one or the other by
 * accident. */
static void test_parity_is_on_only_in_the_with_parity_mode(void) {
  /* `00`: with parity. */
  TEST_ASSERT_TRUE(ap_mc68681_parity_enabled(0x00u));
  /* `01` force, `10` none, `11` multidrop: none of them is ordinary parity. */
  TEST_ASSERT_FALSE(ap_mc68681_parity_enabled(0x08u));
  TEST_ASSERT_FALSE(ap_mc68681_parity_enabled(0x10u));
  TEST_ASSERT_FALSE(ap_mc68681_parity_enabled(0x18u));

  /* Independent of the character length beside it, and of the *type* bit --
   * which is what this used to be reading. `07` is eight bits, odd, with
   * parity, and used to answer "no parity" because bit 2 was set. */
  TEST_ASSERT_TRUE(ap_mc68681_parity_enabled(0x03u));
  TEST_ASSERT_TRUE(ap_mc68681_parity_enabled(0x07u));
  TEST_ASSERT_TRUE(ap_mc68681_parity_enabled(AP_MC68681_MR1_PARITY_TYPE));
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
  /* No parity is mode `10`, `MR1[4:3]` -- `0x13` is eight bits with no parity.
   * This wrote `0x07`, which the swapped fields made "no parity" and the table
   * makes eight bits with *odd* parity. */
  ap_mc68681_write(&duart, AP_MC68681_MR_A,
                   (uint8_t)(0x03u | (AP_MC68681_MR1_PARITY_MODE_NONE
                                      << AP_MC68681_MR1_PARITY_MODE_SHIFT)));

  /* The sender uses parity -- mode `00` -- and this receiver still reports
   * none, which is the point of the test. */
  ap_mc68681_receive_framed(&duart, 0u, 0x41u, 0x77u, 0x03u);

  TEST_ASSERT_FALSE((ap_mc68681_read(&duart, AP_MC68681_SR_CSR_A) &
                     AP_MC68681_SR_PARITY) != 0u);
}

/* §4.2.7.2's reset-receiver command destroys the FIFO's contents, and tells
 * nobody. A host feeding this channel cannot otherwise distinguish a character
 * that was read from one that was thrown away -- both leave an empty FIFO --
 * so the count of discarded characters is kept. Measured on a Domain/OS boot:
 * the console handover writes `CRA` `2A` and a keystroke delivered moments
 * before it is destroyed unread. */
static void test_resetting_the_receiver_counts_the_characters_it_discards(
    void) {
  ap_mc68681_t duart;
  ap_mc68681_reset(&duart);
  ap_mc68681_write(&duart, AP_MC68681_CR_A, 0x01u); /* enable the receiver */
  ap_mc68681_write(&duart, AP_MC68681_SR_CSR_A, 0x77u);
  ap_mc68681_write(&duart, AP_MC68681_MR_A, 0x13u);

  ap_mc68681_receive_framed(&duart, 0u, 0x79u, 0x77u, 0x13u);
  ap_mc68681_receive_framed(&duart, 0u, 0x0Du, 0x77u, 0x13u);
  TEST_ASSERT_EQUAL_UINT(2u, duart.channel[0].fifo_count);
  TEST_ASSERT_EQUAL_UINT(0u, duart.channel[0].rx_flushed);

  ap_mc68681_write(&duart, AP_MC68681_CR_A, 0x2Au);

  TEST_ASSERT_EQUAL_UINT(0u, duart.channel[0].fifo_count);
  TEST_ASSERT_EQUAL_UINT(2u, duart.channel[0].rx_flushed);
}

/* The counter has to mean *discarded*, not *emptied*, or a host would resend a
 * character the machine had already acted on. A FIFO emptied by reads and then
 * reset counts nothing. */
static void test_a_character_read_before_the_reset_is_not_counted_as_lost(
    void) {
  ap_mc68681_t duart;
  ap_mc68681_reset(&duart);
  ap_mc68681_write(&duart, AP_MC68681_CR_A, 0x01u);
  ap_mc68681_write(&duart, AP_MC68681_SR_CSR_A, 0x77u);
  ap_mc68681_write(&duart, AP_MC68681_MR_A, 0x13u);

  ap_mc68681_receive_framed(&duart, 0u, 0x79u, 0x77u, 0x13u);
  TEST_ASSERT_EQUAL_UINT8(0x79u, ap_mc68681_read(&duart, AP_MC68681_RB_TB_A));

  ap_mc68681_write(&duart, AP_MC68681_CR_A, 0x2Au);

  TEST_ASSERT_EQUAL_UINT(0u, duart.channel[0].rx_flushed);
}

/* §4.2.11.5 and §4.2.11.6 put the channels' bit clocks on `OP3` and `OP2`, and
 * both say the clock free-runs: "a free running 1X clock is always output in
 * this mode". So the pin is a square wave at the channel's programmed rate, and
 * what makes it a clock rather than a level is that it changes with time. */
static void test_the_output_port_clock_codes_carry_a_waveform(void) {
  ap_mc68681_t d;
  ap_mc68681_reset(&d);

  /* 9600 baud on both halves of channel A's clock select, and OP2 programmed to
   * the transmitter's 1X clock. */
  ap_mc68681_write(&d, AP_MC68681_SR_CSR_A, 0xBBu);
  ap_mc68681_write(&d, AP_MC68681_IP_OPCR, 0x02u);

  const ap_time_t bit = AP_TIME_BASE_HZ / 9600u;
  bool seen_high = false;
  bool seen_low = false;
  for (unsigned i = 0; i < 8u; i++) {
    d.now = (ap_time_t)i * (bit / 2u);
    if (ap_mc68681_output_pin(&d, 2u)) {
      seen_high = true;
    } else {
      seen_low = true;
    }
  }
  TEST_ASSERT_TRUE(seen_high);
  TEST_ASSERT_TRUE(seen_low);

  /* Code 0 is the register bit again, and does not move with time. */
  ap_mc68681_write(&d, AP_MC68681_IP_OPCR, 0x00u);
  const bool level = ap_mc68681_output_pin(&d, 2u);
  d.now += bit * 3u;
  TEST_ASSERT_EQUAL_INT((int)level, (int)ap_mc68681_output_pin(&d, 2u));
}

/* OP3's `10` and `11` are channel B's clocks, and its `01` is the counter/timer
 * output that this core already had -- so the three cases must differ. */
static void test_op3_selects_between_the_counter_and_channel_b_clocks(void) {
  ap_mc68681_t d;
  ap_mc68681_reset(&d);
  ap_mc68681_write(&d, AP_MC68681_SR_CSR_B, 0xBBu);

  ap_mc68681_write(&d, AP_MC68681_IP_OPCR, 0x04u); /* OPCR[3:2]=01 */
  d.counter_output = true;
  TEST_ASSERT_TRUE(ap_mc68681_output_pin(&d, 3u));
  d.counter_output = false;
  TEST_ASSERT_FALSE(ap_mc68681_output_pin(&d, 3u));

  /* OPCR[3:2]=10, the channel B transmitter's 1X clock, which moves with time
   * whatever the counter output happens to be. */
  ap_mc68681_write(&d, AP_MC68681_IP_OPCR, 0x08u);
  const ap_time_t bit = AP_TIME_BASE_HZ / 9600u;
  bool changed = false;
  const bool first = ap_mc68681_output_pin(&d, 3u);
  for (unsigned i = 1; i < 6u; i++) {
    d.now = (ap_time_t)i * (bit / 2u);
    if (ap_mc68681_output_pin(&d, 3u) != first) {
      changed = true;
    }
  }
  TEST_ASSERT_TRUE(changed);
}

/* **The boot PROM's own command sequence**, which the oracle gets wrong.
 *
 * At `006768`-`006774` the firmware writes `CRA = 45, 35, 25` back to back.
 * Decoded per §4.2.7 -- `[6:4]` miscellaneous, `[3:2]` transmitter, `[1:0]`
 * receiver -- those are: reset error status + enable both; reset transmitter +
 * enable both; reset receiver + enable both.
 *
 * The middle one specifies "reset transmitter" and "enable transmitter" in a
 * single word, which §4.2.7 says "cannot be specified" -- so what a part does
 * with it is undefined and this test does not assert on it. **The third write
 * is not undefined**: misc = 2 touches the receiver only, and the transmitter
 * field is a plain enable. After it the transmitter must be enabled with
 * TxRDY asserted, whatever the second write did.
 *
 * MAME gated enable-transmitter on an *edge* against the previous command
 * register, so all three writes carrying bit 2 meant only the first enabled;
 * the transmitter stayed reset, TxRDY never returned, and its DN3500 polled
 * `0067A2` for ever without drawing. This test is that defect, stated as the
 * hardware fact it violates, so this core cannot acquire it. */
static void
test_the_prom_command_sequence_leaves_the_transmitter_enabled(void) {
  ap_mc68681_t d;
  ap_mc68681_reset(&d);

  ap_mc68681_write(&d, AP_MC68681_CR_A, 0x45u);
  ap_mc68681_write(&d, AP_MC68681_CR_A, 0x35u);
  ap_mc68681_write(&d, AP_MC68681_CR_A, 0x25u);

  const uint8_t sr = ap_mc68681_read(&d, AP_MC68681_SR_CSR_A);
  TEST_ASSERT_EQUAL_HEX8(AP_MC68681_SR_TXRDY, sr & AP_MC68681_SR_TXRDY);

  /* And it really transmits, which is the thing the firmware was waiting on. */
  uint8_t out = 0u;
  ap_mc68681_write(&d, AP_MC68681_RB_TB_A, 0x41u);
  TEST_ASSERT_TRUE(ap_mc68681_transmit(&d, 0u, &out));
  TEST_ASSERT_EQUAL_HEX8(0x41u, out);
}

/* The command register is **not state**: a command is acted on when it is
 * written, so re-issuing "enable transmitter" is idempotent and never depends
 * on what the previous write happened to contain. Asserted directly, because
 * the alternative reading is what the oracle implemented. */
static void test_enabling_an_already_enabled_transmitter_is_idempotent(void) {
  ap_mc68681_t d;
  ap_mc68681_reset(&d);

  ap_mc68681_write(&d, AP_MC68681_CR_A, 0x04u); /* enable */
  const uint8_t first = ap_mc68681_read(&d, AP_MC68681_SR_CSR_A);
  ap_mc68681_write(&d, AP_MC68681_CR_A, 0x04u); /* again */
  const uint8_t second = ap_mc68681_read(&d, AP_MC68681_SR_CSR_A);
  TEST_ASSERT_EQUAL_HEX8(first, second);
  TEST_ASSERT_EQUAL_HEX8(AP_MC68681_SR_TXRDY, second & AP_MC68681_SR_TXRDY);

  /* A reset in between, then the same enable, must still enable -- this is the
   * firmware's sequence reduced to its two load-bearing writes. */
  ap_mc68681_write(&d, AP_MC68681_CR_A, 0x30u); /* reset transmitter */
  TEST_ASSERT_EQUAL_HEX8(
      0u, ap_mc68681_read(&d, AP_MC68681_SR_CSR_A) & AP_MC68681_SR_TXRDY);
  ap_mc68681_write(&d, AP_MC68681_CR_A, 0x04u); /* enable */
  TEST_ASSERT_EQUAL_HEX8(
      AP_MC68681_SR_TXRDY,
      ap_mc68681_read(&d, AP_MC68681_SR_CSR_A) & AP_MC68681_SR_TXRDY);
}

/* The same hazard on this part, made reproducible the same way: reset is the
 * initialiser, so a struct full of garbage must come out of it identical to one
 * full of zeroes -- including the quirk set, whose bit 0 `0xAA` happens to
 * set. */
static void test_reset_does_not_inherit_the_callers_stack(void) {
  ap_mc68681_t d;
  /* `0xFF` so every quirk bit is set; `0xAA` leaves the even-numbered ones
   * clear and would miss half the set. */
  memset(&d, 0xFF, sizeof d);
  ap_mc68681_reset(&d);

  TEST_ASSERT_FALSE(
      ap_quirk_selected(d.quirks, AP_QUIRK_DUART_ENABLE_SETS_TXEMT));
  /* And the behaviour that bit would have changed: enabling asserts TxRDY
   * alone, per §4.2.9.5/6. */
  ap_mc68681_write(&d, AP_MC68681_CR_A, 0x04u);
  TEST_ASSERT_EQUAL_HEX8(AP_MC68681_SR_TXRDY,
                         ap_mc68681_read(&d, AP_MC68681_SR_CSR_A));
}

/* **The boot PROM's autobaud is built to invert exactly this function.**
 * `[ROM3500]` `000844`-`0008B8` matches the byte it read against `$FF`, `$FE`,
 * `$C7`, `$72` and `$C0` and writes the clock select that names the *sender's*
 * rate -- so a carriage return from a 9600 terminal into a receiver still at
 * 1050 must resample to `$FF`, the entry selecting code `B`. It does: every
 * sample position lands at or past bit 13, well beyond the stop bit, so all
 * eight data bits read idle.
 *
 * Pinned because three rounds of a cartridge boot were spent reading `$FE` and
 * `$F9` off a live machine and theorising about the harness, when the function
 * is pure and says in microseconds which sender/receiver pair produces each. */
static void test_a_carriage_return_resamples_to_the_rate_the_autobaud_names(
    void) {
  const unsigned r9600 = ap_mc68681_baud(0xBu, false);
  const unsigned r1050 = ap_mc68681_baud(0x7u, false);
  const unsigned r4800 = ap_mc68681_baud(0x9u, false);
  TEST_ASSERT_EQUAL_UINT(9600u, r9600);
  TEST_ASSERT_EQUAL_UINT(1050u, r1050);
  TEST_ASSERT_EQUAL_UINT(4800u, r4800);

  /* The convergent case: 9600 into 1050 reads `$FF`, and `$FF` is the entry
   * that selects `$BB` -- 9600, which is what the sender was. */
  TEST_ASSERT_EQUAL_HEX8(0xFFu, ap_mc68681_resample(0x0Du, 8u, r9600, r1050));
  /* The two that a mismatched harness produces, and neither converges: 4800
   * into 1050 names `$99`, and 9600 into 4800 names nothing at all. */
  TEST_ASSERT_EQUAL_HEX8(0xFEu, ap_mc68681_resample(0x0Du, 8u, r4800, r1050));
  TEST_ASSERT_EQUAL_HEX8(0xF9u, ap_mc68681_resample(0x0Du, 8u, r9600, r4800));
  /* And equal rates are not a corruption. */
  TEST_ASSERT_EQUAL_HEX8(0x0Du, ap_mc68681_resample(0x0Du, 8u, r9600, r9600));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_carriage_return_resamples_to_the_rate_the_autobaud_names);
  RUN_TEST(test_reset_does_not_inherit_the_callers_stack);
  RUN_TEST(test_the_prom_command_sequence_leaves_the_transmitter_enabled);
  RUN_TEST(test_enabling_an_already_enabled_transmitter_is_idempotent);
  RUN_TEST(test_the_output_port_clock_codes_carry_a_waveform);
  RUN_TEST(test_op3_selects_between_the_counter_and_channel_b_clocks);
  RUN_TEST(test_resetting_the_receiver_counts_the_characters_it_discards);
  RUN_TEST(test_a_character_read_before_the_reset_is_not_counted_as_lost);
  RUN_TEST(test_a_character_sent_at_the_wrong_rate_sets_a_framing_error);
  RUN_TEST(test_a_character_sent_at_the_right_rate_does_not);
  RUN_TEST(test_an_unknown_sender_rate_claims_no_disagreement);
  RUN_TEST(test_a_disabled_receiver_reports_no_framing_error);
  RUN_TEST(test_character_length_is_five_plus_the_field);
  RUN_TEST(test_parity_is_on_only_in_the_with_parity_mode);
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
  RUN_TEST(test_reset_clears_the_status_register);
  RUN_TEST(test_reset_initialises_the_interrupt_vector_to_0f);
  RUN_TEST(test_the_mode_register_pointer_advances_then_sticks);
  RUN_TEST(test_the_receive_fifo_holds_three_and_then_overruns);
  RUN_TEST(test_resetting_the_receiver_also_discards_the_held_character);
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
  RUN_TEST(test_every_opcr_select_reaches_its_output_pin);
  RUN_TEST(test_the_second_baud_set_is_not_a_copy_of_the_first);
  RUN_TEST(test_the_input_port_reads_bit_seven_as_one);
  RUN_TEST(test_cts_gates_the_transmitter_when_mr2_selects_it);
  RUN_TEST(test_character_error_mode_reports_the_top_of_the_fifo);
  RUN_TEST(test_the_rts_controls_drive_the_output_port);
  RUN_TEST(test_the_isr_receiver_bit_follows_the_mr1_selection);
  RUN_TEST(test_a_break_in_local_loopback_reaches_the_receiver);
  RUN_TEST(test_the_break_commands_are_obeyed_rather_than_dropped);
  RUN_TEST(test_the_break_change_interrupt_can_be_cleared_per_channel);
  RUN_TEST(test_two_duarts_reset_alike_hold_identical_state);
  RUN_TEST(test_reset_clears_the_selected_quirks);
  RUN_TEST(test_the_txemt_quirk_matches_the_oracle_when_selected);
  return UNITY_END();
}

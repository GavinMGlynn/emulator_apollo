/* Apollo serial ports as the board wires them. Placement measured;
 * `FINDINGS.md` C14. */

#include "unity.h"

#include "board/ap_intr.h"
#include "board/ap_sio.h"

void setUp(void) {}
void tearDown(void) {}


/* `OPCR[7]` set retargets OP7. `[MC68681]` §4.2.11.1: it provides "either the
 * complement of OPR[7] or the channel B transmitter interrupt output, which is
 * the complement of the channel B transmitter ready status bit ... not masked
 * by the contents of the interrupt mask register."
 *
 * This returned false unconditionally, on the grounds that no firmware here
 * selects the alternate source and a board asking for it should get nothing
 * "rather than a guess". The datasheet says exactly what OP7 carries, so there
 * was no guess to avoid -- only an unread section. */
static void test_op7_can_carry_channel_bs_transmitter_ready(void) {
  ap_sio_t sio;
  TEST_ASSERT_TRUE(ap_sio_reset(&sio));

  /* The default source: the complement of OPR[7], so the line is up while the
   * bit is clear. */
  ap_sio_write(&sio, AP_SIO1_ADDR + (13u << 1), 0x00u); /* OPCR */
  TEST_ASSERT_TRUE(ap_sio_diagnostic_interrupt(&sio));

  /* Select the alternate source. A reset transmitter is *not* ready -- the
   * datasheet has RESET TRANSMITTER clear TxRDY -- so the line follows it. */
  ap_sio_write(&sio, AP_SIO1_ADDR + (13u << 1),
               AP_SIO_OPCR_OP7_IS_TXRDYB);
  ap_mc68681_write(&sio.port[0], AP_MC68681_CR_B, 0x30u); /* reset transmitter */
  TEST_ASSERT_FALSE(ap_sio_diagnostic_interrupt(&sio));

  /* Enabling channel B's transmitter asserts TxRDY, and OP7 follows it. */
  ap_mc68681_write(&sio.port[0], AP_MC68681_CR_B, 0x04u); /* enable transmitter */
  TEST_ASSERT_TRUE(ap_sio_diagnostic_interrupt(&sio));

  /* And it is channel *B*: channel A's transmitter must not move the line. */
  ap_mc68681_write(&sio.port[0], AP_MC68681_CR_A, 0x08u); /* disable A */
  TEST_ASSERT_TRUE(ap_sio_diagnostic_interrupt(&sio));
}

static void test_both_ports_decode_at_stride_two(void) {
  unsigned unit;
  unsigned reg;

  /* Both bytes of a word select the same register -- which is exactly why the
   * measured dump reads every value twice. */
  TEST_ASSERT_TRUE(ap_sio_decode(AP_SIO1_ADDR + 0u, &unit, &reg));
  TEST_ASSERT_EQUAL_UINT(0u, unit);
  TEST_ASSERT_EQUAL_UINT(0u, reg);
  TEST_ASSERT_TRUE(ap_sio_decode(AP_SIO1_ADDR + 1u, &unit, &reg));
  TEST_ASSERT_EQUAL_UINT(0u, reg);
  TEST_ASSERT_TRUE(ap_sio_decode(AP_SIO1_ADDR + 2u, &unit, &reg));
  TEST_ASSERT_EQUAL_UINT(1u, reg);

  /* Sixteen registers over thirty-two bytes, then aliased. */
  TEST_ASSERT_TRUE(ap_sio_decode(AP_SIO1_ADDR + 30u, &unit, &reg));
  TEST_ASSERT_EQUAL_UINT(15u, reg);
  TEST_ASSERT_TRUE(ap_sio_decode(AP_SIO1_ADDR + 32u, &unit, &reg));
  TEST_ASSERT_EQUAL_UINT(0u, reg);

  TEST_ASSERT_TRUE(ap_sio_decode(AP_SIO2_ADDR, &unit, &reg));
  TEST_ASSERT_EQUAL_UINT(1u, unit);

  TEST_ASSERT_FALSE(ap_sio_decode(0x010600u, &unit, &reg));
  TEST_ASSERT_FALSE(ap_sio_decode(0x010300u, &unit, &reg));
}

static void test_the_paired_bytes_reach_one_register(void) {
  ap_sio_t sio;
  TEST_ASSERT_TRUE(ap_sio_reset(&sio));

  /* Writing through the odd byte and reading through the even one must reach
   * the same register, which is what "stride 2" means and what the measured
   * dump's paired values show. */
  ap_sio_write(&sio, AP_SIO1_ADDR + 25u, 0x5A); /* register 12, odd byte */
  TEST_ASSERT_EQUAL_HEX8(0x5A, ap_sio_read(&sio, AP_SIO1_ADDR + 24u));
}

static void test_the_two_ports_are_independent(void) {
  ap_sio_t sio;
  TEST_ASSERT_TRUE(ap_sio_reset(&sio));

  ap_sio_write(&sio, AP_SIO1_ADDR + 24u, 0x11); /* interrupt vector, port 1 */
  ap_sio_write(&sio, AP_SIO2_ADDR + 24u, 0x22);
  TEST_ASSERT_EQUAL_HEX8(0x11, ap_sio_read(&sio, AP_SIO1_ADDR + 24u));
  TEST_ASSERT_EQUAL_HEX8(0x22, ap_sio_read(&sio, AP_SIO2_ADDR + 24u));
}

static void test_a_character_crosses_the_keyboard_port(void) {
  ap_sio_t sio;
  TEST_ASSERT_TRUE(ap_sio_reset(&sio));

  /* §3.9: "SIO_O is used for the keyboard and supports full-duplex operation
   * for bidirectional keyboards." Channel A of the first part. */
  ap_sio_write(&sio, AP_SIO1_ADDR + 4u, 0x05); /* command A: enable rx and tx */
  ap_mc68681_receive(&sio.port[AP_SIO_KEYBOARD_PORT], AP_SIO_KEYBOARD_CHANNEL,
                     0x41);

  /* Status A shows a character waiting, and the receive buffer yields it. */
  TEST_ASSERT_EQUAL_HEX8(AP_MC68681_SR_RXRDY,
                         ap_sio_read(&sio, AP_SIO1_ADDR + 2u) &
                             AP_MC68681_SR_RXRDY);
  TEST_ASSERT_EQUAL_HEX8(0x41, ap_sio_read(&sio, AP_SIO1_ADDR + 6u));
}

static void test_the_refresh_period_is_exact_in_base_units(void) {
  /* §3.9: "The period of the output is 15 microseconds."
   *
   * Its frequency, 66666.67 Hz, is not an integer -- a model counting in hertz
   * could not represent this board's memory refresh clock at all. Counting in
   * base units represents it exactly. Second such case after the interval
   * timer's prescaled 7812.5 Hz, and worth pinning so that a future change to
   * the time base has to break a test rather than a machine. */
  TEST_ASSERT_EQUAL_UINT64(0u, (AP_TIME_BASE_HZ * 15u) % 1000000u);
  TEST_ASSERT_EQUAL_UINT64(AP_SIO_REFRESH_PERIOD,
                           (AP_TIME_BASE_HZ * 15u) / 1000000u);
}

static void test_the_serial_ports_raise_the_second_priority_interrupt(void) {
  ap_sio_t sio;
  ap_intr_t intr;
  TEST_ASSERT_TRUE(ap_sio_reset(&sio));
  ap_intr_reset(&intr);

  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 0u, 0x11);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 1u, 0xA0);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 1u, 0x08);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 1u, 0x01);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 1u, 0x00);

  /* Unmask the input-port-change interrupt **and enable the pin's delta**:
   * §4.2.13.3 gates `ISR[7]` on `ACR[3:0]` as well as on the mask. This set
   * only the mask, and passed because the model raised it for every pin. */
  ap_sio_write(&sio, AP_SIO1_ADDR + 10u, AP_MC68681_ISR_INPUT);
  ap_sio_write(&sio, AP_SIO1_ADDR + 8u, 0x0Fu); /* ACR: all four deltas */
  ap_mc68681_set_input(&sio.port[0], 0x02);
  TEST_ASSERT_TRUE(ap_sio_irq(&sio));

  ap_intr_set_request(&intr, AP_SIO_IRQ, ap_sio_irq(&sio));
  /* `008778-03` Table 2-3 puts the SIO at IRQ1, so vector `A1`. */
  TEST_ASSERT_EQUAL_HEX8(0xA1, ap_intr_acknowledge(&intr));
}


/* The console path out, and the reason it has a test of its own: a run that
 * produces no output is ambiguous. It can mean the firmware has not printed
 * anything, or that the path from the transmitter to the caller is broken, and
 * those need opposite responses. This settles one of them, so a silent boot is
 * evidence about the firmware.
 *
 * The byte goes in through the register the program writes, not through a back
 * door, so the test exercises what the machine exercises. */
static void test_what_the_program_transmits_reaches_the_caller(void) {
  ap_sio_t sio;
  TEST_ASSERT_TRUE(ap_sio_reset(&sio));

  /* Enable the transmitter on port 2 channel A, the way a driver does: command
   * register, transmit enable. */
  ap_sio_write(&sio, AP_SIO2_ADDR + (AP_MC68681_CR_A * 2u), 0x04u);
  ap_sio_write(&sio, AP_SIO2_ADDR + (AP_MC68681_RB_TB_A * 2u), 0x41u);

  uint8_t byte = 0;
  TEST_ASSERT_TRUE(ap_sio_transmit(&sio, 1u, 0u, &byte));
  TEST_ASSERT_EQUAL_HEX8(0x41u, byte);

  /* And it is taken, not merely peeked at: a second call finds nothing. */
  TEST_ASSERT_FALSE(ap_sio_transmit(&sio, 1u, 0u, &byte));
}

/* A byte handed to the receiver reaches the program through the register it
 * reads, which is the other half of the same wire. */
static void test_a_delivered_byte_reaches_the_program(void) {
  ap_sio_t sio;
  TEST_ASSERT_TRUE(ap_sio_reset(&sio));

  ap_sio_write(&sio, AP_SIO2_ADDR + (AP_MC68681_CR_A * 2u), 0x01u); /* rx on */
  TEST_ASSERT_FALSE(ap_sio_receiver_ready(&sio, 1u, 0u));

  ap_sio_receive(&sio, 1u, 0u, 0x5Au);
  TEST_ASSERT_TRUE(ap_sio_receiver_ready(&sio, 1u, 0u));
  TEST_ASSERT_EQUAL_HEX8(
      0x5Au, ap_sio_read(&sio, AP_SIO2_ADDR + (AP_MC68681_RB_TB_A * 2u)));
}


/* The board's rate- and parity-aware path, end to end through the registers a
 * program writes. A device with its own configuration says so, and the DUART
 * decides whether the link works rather than the caller assuming it does. */
static void test_a_mis_rated_sender_reaches_the_port_as_an_error(void) {
  ap_sio_t sio;
  TEST_ASSERT_TRUE(ap_sio_reset(&sio));
  ap_sio_write(&sio, AP_SIO2_ADDR + (AP_MC68681_CR_A * 2u), 0x01u);
  ap_sio_write(&sio, AP_SIO2_ADDR + (AP_MC68681_SR_CSR_A * 2u), 0x77u);
  ap_sio_write(&sio, AP_SIO2_ADDR + (AP_MC68681_MR_A * 2u), 0x07u);

  ap_sio_receive_framed(&sio, 1u, 0u, 0x41u, 0xBBu, 0x07u);

  const uint8_t sr = ap_sio_read(&sio, AP_SIO2_ADDR + (AP_MC68681_SR_CSR_A * 2u));
  TEST_ASSERT_TRUE((sr & AP_MC68681_SR_FRAMING) != 0u);
  /* And the byte is still there, so a driver can tell a wrong rate from
   * silence — which is the whole reason the part does not discard it. */
  TEST_ASSERT_TRUE((sr & AP_MC68681_SR_RXRDY) != 0u);
}

/* A correctly configured sender produces neither error, which is the control:
 * a board path that always flagged would satisfy the test above. */
static void test_a_matching_sender_reaches_the_port_cleanly(void) {
  ap_sio_t sio;
  TEST_ASSERT_TRUE(ap_sio_reset(&sio));
  ap_sio_write(&sio, AP_SIO2_ADDR + (AP_MC68681_CR_A * 2u), 0x01u);
  ap_sio_write(&sio, AP_SIO2_ADDR + (AP_MC68681_SR_CSR_A * 2u), 0x77u);
  ap_sio_write(&sio, AP_SIO2_ADDR + (AP_MC68681_MR_A * 2u), 0x07u);

  ap_sio_receive_framed(&sio, 1u, 0u, 0x41u, 0x77u, 0x07u);

  const uint8_t sr = ap_sio_read(&sio, AP_SIO2_ADDR + (AP_MC68681_SR_CSR_A * 2u));
  TEST_ASSERT_FALSE((sr & (AP_MC68681_SR_FRAMING | AP_MC68681_SR_PARITY)) != 0u);
  TEST_ASSERT_EQUAL_HEX8(
      0x41u, ap_sio_read(&sio, AP_SIO2_ADDR + (AP_MC68681_RB_TB_A * 2u)));
}

/* ---------------------------------------------------------------------------
 * The memory refresh
 *
 * §3.9: the counter/timer "is set up in the timer mode to produce a square wave
 * output on output OP3. The period of the output is 15 microseconds." Until
 * something advanced the counter, that square wave had no period at all.
 * ------------------------------------------------------------------------- */

/* Program serial 1's counter exactly as the boot PROM does -- `ACR E0`, which
 * is "Timer, clock source X1/CLK", and the preload read out of this core after
 * a boot of `3500_BOOT_12191_7`. Then start it, which is a *read* of register
 * 14 and not a write. */
static void program_refresh_timer(ap_sio_t *sio) {
  ap_sio_write(sio, AP_SIO1_ADDR + AP_MC68681_IPCR_ACR * 2u, 0xE0u);
  ap_sio_write(sio, AP_SIO1_ADDR + AP_MC68681_CUR_CTUR * 2u,
               (uint8_t)(AP_SIO_MEASURED_REFRESH_PRELOAD >> 8));
  ap_sio_write(sio, AP_SIO1_ADDR + AP_MC68681_CLR_CTLR * 2u,
               (uint8_t)(AP_SIO_MEASURED_REFRESH_PRELOAD & 0xFFu));
  (void)ap_sio_read(sio, AP_SIO1_ADDR + AP_MC68681_START_OPR_SET * 2u);
}

/* The derivation, checked against both facts it rests on rather than restated.
 *
 * X1 is not in any manual here. §3.9 gives the output period and the firmware
 * gives the preload; `AP_SIO_X1_HZ` is what makes those two agree. So this
 * asserts the *agreement*: at that rate, with that preload, the square wave
 * turns over exactly every `AP_SIO_REFRESH_PERIOD`. Change either input and
 * this fails rather than quietly redefining the crystal. */
static void test_the_firmwares_preload_gives_the_documented_refresh_period(void) {
  ap_sio_t sio;
  TEST_ASSERT_TRUE(ap_sio_reset(&sio));
  program_refresh_timer(&sio);

  /* Two terminal counts to a period -- §3: the timer inverts its output at
   * terminal count, so a full square wave is two of them. */
  TEST_ASSERT_EQUAL_UINT64(
      AP_SIO_REFRESH_PERIOD,
      2u * AP_SIO_MEASURED_REFRESH_PRELOAD * (AP_TIME_BASE_HZ / AP_SIO_X1_HZ));

  /* And 15 microseconds is what that period is, exactly. */
  TEST_ASSERT_EQUAL_UINT64(AP_SIO_REFRESH_PERIOD,
                           (AP_TIME_BASE_HZ * 15u) / 1000000u);
}

/* The square wave itself, advanced by time rather than by a caller counting
 * pulses: it inverts every half period and returns to where it started after a
 * whole one. */
static void test_the_refresh_output_is_a_square_wave_of_that_period(void) {
  ap_sio_t sio;
  TEST_ASSERT_TRUE(ap_sio_reset(&sio));
  program_refresh_timer(&sio);

  const bool start = ap_sio_refresh_output(&sio);

  /* A hair under half a period: nothing has turned over. */
  ap_sio_advance(&sio, AP_SIO_REFRESH_PERIOD / 2u - 1u);
  TEST_ASSERT_EQUAL_INT(start, ap_sio_refresh_output(&sio));

  /* Half a period: inverted. */
  ap_sio_advance(&sio, AP_SIO_REFRESH_PERIOD / 2u);
  TEST_ASSERT_NOT_EQUAL_INT(start, ap_sio_refresh_output(&sio));

  /* A whole one: back where it began. That is what makes it a square wave and
   * not a pulse. */
  ap_sio_advance(&sio, AP_SIO_REFRESH_PERIOD);
  TEST_ASSERT_EQUAL_INT(start, ap_sio_refresh_output(&sio));

  /* Ten periods, to show it is periodic rather than merely symmetric once. */
  ap_sio_advance(&sio, AP_SIO_REFRESH_PERIOD * 11u);
  TEST_ASSERT_EQUAL_INT(start, ap_sio_refresh_output(&sio));
}

/* The counter-ready bit follows the wave at half its rate, which is §3's rule:
 * the output inverts at every terminal count and the flag is set on the second.
 * A model that raised it on both would interrupt a refresh driver twice as
 * often as the hardware. */
static void test_the_counter_ready_bit_comes_once_per_period(void) {
  ap_sio_t sio;
  TEST_ASSERT_TRUE(ap_sio_reset(&sio));
  program_refresh_timer(&sio);

  ap_sio_advance(&sio, AP_SIO_REFRESH_PERIOD / 2u);
  TEST_ASSERT_EQUAL_HEX8(0u, (uint8_t)(sio.port[0].isr &
                                       AP_MC68681_ISR_COUNTER));

  ap_sio_advance(&sio, AP_SIO_REFRESH_PERIOD);
  TEST_ASSERT_EQUAL_HEX8(AP_MC68681_ISR_COUNTER,
                         (uint8_t)(sio.port[0].isr & AP_MC68681_ISR_COUNTER));
}

/* Advancing in ragged steps reaches the same place as advancing in one, because
 * the remainder is carried. The refresh must not depend on how often the tick
 * loop happens to ask. */
static void test_the_refresh_does_not_depend_on_the_call_rate(void) {
  ap_sio_t one;
  ap_sio_t many;
  TEST_ASSERT_TRUE(ap_sio_reset(&one));
  TEST_ASSERT_TRUE(ap_sio_reset(&many));
  program_refresh_timer(&one);
  program_refresh_timer(&many);

  const ap_time_t target = AP_SIO_REFRESH_PERIOD * 7u + 12345u;
  ap_sio_advance(&one, target);
  /* Steps that are not a whole X1 period, so the remainder is exercised. */
  for (ap_time_t t = 137u; t < target; t += 137u) {
    ap_sio_advance(&many, t);
  }
  ap_sio_advance(&many, target);

  TEST_ASSERT_EQUAL_INT(ap_sio_refresh_output(&one),
                        ap_sio_refresh_output(&many));
  TEST_ASSERT_EQUAL_HEX16(one.port[0].counter, many.port[0].counter);
}

/* ---- Character time, `[68681]` Table 4-5 ---------------------------------- */

/* The stop-bit field is sixteen encodings from 0.5 to 2 bits, carried in
 * sixteenths because every entry of the table is an exact one: 0.563 is 9/16,
 * 1.063 is 17/16, 2.000 is 32/16. */
static void test_the_stop_length_is_the_table_s_sixteenths(void) {
  /* An 8-bit character: codes 0-7 run 0.563 to 1.000. */
  const uint8_t eight = 0x03u; /* MR1[1:0] = 11 */
  TEST_ASSERT_EQUAL_UINT(9u, ap_mc68681_stop_sixteenths(eight, 0x00u));
  TEST_ASSERT_EQUAL_UINT(16u, ap_mc68681_stop_sixteenths(eight, 0x07u));
  TEST_ASSERT_EQUAL_UINT(25u, ap_mc68681_stop_sixteenths(eight, 0x08u));
  TEST_ASSERT_EQUAL_UINT(32u, ap_mc68681_stop_sixteenths(eight, 0x0Fu));

  /* A 5-bit character takes the other column: codes 0-7 are half a bit longer,
   * and 8-15 are identical. A stop length read from `MR2` alone is right for
   * three character lengths and wrong for this one. */
  const uint8_t five = 0x00u;
  TEST_ASSERT_EQUAL_UINT(17u, ap_mc68681_stop_sixteenths(five, 0x00u));
  TEST_ASSERT_EQUAL_UINT(24u, ap_mc68681_stop_sixteenths(five, 0x07u));
  TEST_ASSERT_EQUAL_UINT(25u, ap_mc68681_stop_sixteenths(five, 0x08u));
  TEST_ASSERT_EQUAL_UINT(32u, ap_mc68681_stop_sixteenths(five, 0x0Fu));
}

/* Ten bit times is 8N1 and nothing else. This is the figure the frontend used
 * to assume for every link. */
static void test_a_character_takes_as_long_as_its_framing_says(void) {
  /* 8N1 at 9600: eight data bits, no parity, one stop -- ten bit times. */
  /* No parity is the mode field's `10`, `MR1[4:3]` -- not a bit-2 flag, which
   * is the parity *type*. This read `0x03 | 0x04` and got eight bits with
   * *even* parity, then asserted it took ten bit times. */
  const uint8_t mr1_8n =
      0x03u | (uint8_t)(AP_MC68681_MR1_PARITY_MODE_NONE
                        << AP_MC68681_MR1_PARITY_MODE_SHIFT);
  const ap_time_t ten_bits = (AP_TIME_BASE_HZ * 10u) / 9600u;
  TEST_ASSERT_EQUAL_UINT64(
      ten_bits, ap_mc68681_character_time(mr1_8n, AP_MC68681_MR2_STOP_ONE, 9600u));

  /* With parity it is eleven, not ten -- a tenth longer, which is the error the
   * assumption made on every link that used it. */
  const uint8_t mr1_8p = 0x03u; /* bit 2 clear = with parity */
  TEST_ASSERT_EQUAL_UINT64(
      (AP_TIME_BASE_HZ * 11u) / 9600u,
      ap_mc68681_character_time(mr1_8p, AP_MC68681_MR2_STOP_ONE, 9600u));

  /* And two stop bits make eleven as well, by a different route. */
  TEST_ASSERT_EQUAL_UINT64(
      (AP_TIME_BASE_HZ * 11u) / 9600u,
      ap_mc68681_character_time(mr1_8n, AP_MC68681_MR2_STOP_TWO, 9600u));
}

/* A rate the clock select does not name is refused rather than divided by. */
static void test_a_rateless_clock_select_has_no_character_time(void) {
  TEST_ASSERT_EQUAL_UINT64(
      0u, ap_mc68681_character_time(0x03u, AP_MC68681_MR2_STOP_ONE, 0u));
}

/* ## Serial 1's input port carries the RAM configuration
 *
 * `IP0`-`IP6` of the first DUART are not handshake lines: they are strapped to
 * a byte describing which memory banks are populated and how large, and the
 * boot PROM reads them to size memory before it does anything else. A machine
 * whose input port answers zero is a machine with no memory fitted.
 * `FINDINGS.md` C115.
 */

/* **A table, not an encoder**, and the reason is in the data: `20` is
 * "8-8-8-8" on a DN3500 and "2-2-2-2" on a DN3000, so the byte is not a
 * per-bank size field and the same value means different machines. Four points
 * do not determine a scheme, and no manual in `docs/references/` describes one.
 */
static void test_the_ram_config_byte_is_a_table_and_not_a_rule(void) {
  uint8_t byte = 0u;

  TEST_ASSERT_TRUE(ap_sio_ram_config_byte(AP_MODEL_DN3500, 8u * 1024u * 1024u,
                                          &byte));
  TEST_ASSERT_EQUAL_HEX8(0x64u, byte); /* 4-4-0-0 */
  TEST_ASSERT_TRUE(ap_sio_ram_config_byte(AP_MODEL_DN3500, 16u * 1024u * 1024u,
                                          &byte));
  TEST_ASSERT_EQUAL_HEX8(0x60u, byte); /* 4-4-4-4 */
  TEST_ASSERT_TRUE(ap_sio_ram_config_byte(AP_MODEL_DN3500, 32u * 1024u * 1024u,
                                          &byte));
  TEST_ASSERT_EQUAL_HEX8(0x20u, byte); /* 8-8-8-8 */

  /* The same byte, a different machine, a different bank layout -- which is
   * what makes this a table. */
  TEST_ASSERT_TRUE(ap_sio_ram_config_byte(AP_MODEL_DN3000, 8u * 1024u * 1024u,
                                          &byte));
  TEST_ASSERT_EQUAL_HEX8(0x20u, byte); /* 2-2-2-2, and 8 MB not 32 */
}

/* A pair the table does not cover is **refused**, not approximated. A wrong
 * configuration byte is a machine that sizes memory it does not have and finds
 * out by bus-erroring in the middle of its own self-test -- a failure that
 * looks like a defect in whatever it touches next. */
static void test_an_unlisted_size_is_refused_rather_than_approximated(void) {
  uint8_t byte = 0xAAu;
  TEST_ASSERT_FALSE(ap_sio_ram_config_byte(AP_MODEL_DN3500, 4u * 1024u * 1024u,
                                           &byte));
  TEST_ASSERT_FALSE(ap_sio_ram_config_byte(AP_MODEL_DN3500, 64u * 1024u * 1024u,
                                           &byte));
  /* And it did not write anything on the way out. */
  TEST_ASSERT_EQUAL_HEX8(0xAAu, byte);
}

/* The strap reaches the pins, and only the seven the part has. */
static void test_the_strap_drives_seven_input_pins(void) {
  ap_sio_t sio;
  TEST_ASSERT_TRUE(ap_sio_reset(&sio));
  ap_sio_set_ram_config(&sio, 0xE0u);
  /* Bit 7 is not an input this part has, so it is dropped rather than stored. */
  TEST_ASSERT_EQUAL_HEX8(0x60u, sio.port[AP_SIO_RAM_CONFIG_UNIT].input);
}

/* **OP3 is wired back to IP0**, and that loopback is the whole reason the
 * refresh square wave is observable to a program.
 *
 * The boot PROM programs the timer, routes it to OP3, starts the counter and
 * then polls `IPCR` for a *change* on IP0, counting five whole cycles. Without
 * the wire that poll never ends — 9,982,874 reads of one register in a
 * 30,000,000 instruction run. `FINDINGS.md` C116.
 *
 * The refresh wave itself was already here, at the right period, with its own
 * tests. It was connected to nothing. */
static void test_the_refresh_output_returns_to_the_input_port(void) {
  ap_sio_t sio;
  TEST_ASSERT_TRUE(ap_sio_reset(&sio));
  program_refresh_timer(&sio);

  ap_time_t now = 0u;
  ap_sio_advance(&sio, now);
  const bool start = ap_sio_refresh_output(&sio);
  TEST_ASSERT_EQUAL_INT(start ? 1 : 0,
                        sio.port[AP_SIO_RAM_CONFIG_UNIT].input & 0x01u);

  /* Half a period: the wave inverts, and so must the pin. */
  now += AP_SIO_REFRESH_PERIOD / 2u;
  ap_sio_advance(&sio, now);
  TEST_ASSERT_NOT_EQUAL_INT(start, ap_sio_refresh_output(&sio));
  TEST_ASSERT_EQUAL_INT(ap_sio_refresh_output(&sio) ? 1 : 0,
                        sio.port[AP_SIO_RAM_CONFIG_UNIT].input & 0x01u);

  /* And the change is *visible* in the change register, which is the bit the
   * firmware actually polls -- a pin that changed with no delta flag would
   * leave the PROM spinning exactly as it did before the wire existed. */
  TEST_ASSERT_TRUE((sio.port[AP_SIO_RAM_CONFIG_UNIT].ipcr & 0x10u) != 0u);
}

/* The loopback must not disturb the RAM configuration on the other six pins:
 * IP0 is the refresh and IP1-IP6 are the strap, and a model driving the whole
 * port from the wave would tell the firmware its memory had vanished. */
static void test_the_loopback_leaves_the_ram_config_pins_alone(void) {
  ap_sio_t sio;
  TEST_ASSERT_TRUE(ap_sio_reset(&sio));
  ap_sio_set_ram_config(&sio, 0x60u);
  program_refresh_timer(&sio);

  for (unsigned i = 1; i <= 4u; i++) {
    ap_sio_advance(&sio, AP_SIO_REFRESH_PERIOD * i / 2u);
    TEST_ASSERT_EQUAL_HEX8(
        0x60u, (uint8_t)(sio.port[AP_SIO_RAM_CONFIG_UNIT].input & 0xFEu));
  }
}

/* ## IRQ13 is serial 1's OP7, and it is a wire with no device on it
 *
 * `008778-03` §2.5: "Note that IRQ13 is not available on the bus. In the DS3000,
 * it is connected to Output Port Bit 7 of the 2681 SIO chip and is used by
 * diagnostics to verify the integrity of the interrupt controllers."
 *
 * The pin is the **complement** of the register bit -- `[68681]` gives OP7 as
 * "the complement of OPR[7]" -- so the command that *sets* the bit drives the
 * line low and the one that *clears* it raises the interrupt. The loaded
 * `SELF_TEST` diagnostic does exactly that pair and checks the controller
 * between them.
 */
static void test_serial_ones_op7_is_the_diagnostic_interrupt(void) {
  ap_sio_t sio;
  TEST_ASSERT_TRUE(ap_sio_reset(&sio));

  /* `OPCR = 04`, which is what the diagnostic writes: bit 7 clear, so OP7
   * follows the output port register rather than channel B's transmitter. */
  ap_sio_write(&sio, AP_SIO1_ADDR + 2u * AP_MC68681_IP_OPCR, 0x04u);

  /* Set the bit: the pin goes low, and a low line is no interrupt. */
  ap_sio_write(&sio, AP_SIO1_ADDR + 2u * AP_MC68681_START_OPR_SET,
               AP_SIO_OPR_DIAGNOSTIC);
  TEST_ASSERT_FALSE(ap_sio_diagnostic_interrupt(&sio));

  /* Clear it: the pin goes high, which is the edge §2.5 calls an interrupt. */
  ap_sio_write(&sio, AP_SIO1_ADDR + 2u * AP_MC68681_STOP_OPR_CLEAR,
               AP_SIO_OPR_DIAGNOSTIC);
  TEST_ASSERT_TRUE(ap_sio_diagnostic_interrupt(&sio));

  /* And with OP7 assigned to channel B's transmitter instead, the line stops
   * being the output-port bit and becomes `TxRDY`'s complement -- `[MC68681]`
   * §4.2.11.1. This used to assert that the model reported *nothing* in that
   * configuration, "rather than a guess"; the datasheet states what OP7
   * carries, so there was no guess to avoid. Channel B is enabled by this
   * suite's fixture, so the line is up. */
  ap_sio_write(&sio, AP_SIO1_ADDR + 2u * AP_MC68681_IP_OPCR, 0x80u);
  TEST_ASSERT_EQUAL_INT((sio.port[0].channel[1].sr & AP_MC68681_SR_TXRDY) != 0u,
                        ap_sio_diagnostic_interrupt(&sio));

  /* Serial *2* is not this wire: §2.5 names one chip and this is the other.
   * Driving its OP7 while serial 1 holds the line low must change nothing. */
  ap_sio_t other;
  TEST_ASSERT_TRUE(ap_sio_reset(&other));
  ap_sio_write(&other, AP_SIO1_ADDR + 2u * AP_MC68681_IP_OPCR, 0x04u);
  ap_sio_write(&other, AP_SIO1_ADDR + 2u * AP_MC68681_START_OPR_SET,
               AP_SIO_OPR_DIAGNOSTIC);
  TEST_ASSERT_FALSE(ap_sio_diagnostic_interrupt(&other));
  ap_sio_write(&other, AP_SIO2_ADDR + 2u * AP_MC68681_IP_OPCR, 0x04u);
  ap_sio_write(&other, AP_SIO2_ADDR + 2u * AP_MC68681_STOP_OPR_CLEAR,
               AP_SIO_OPR_DIAGNOSTIC);
  TEST_ASSERT_FALSE(ap_sio_diagnostic_interrupt(&other));
}

/* ## A reset machine asserts it, because the pin is a complement
 *
 * `OPR` resets to zero and OP7 is its complement, so the line idles **high** --
 * which §2.5 calls an interrupt. That is the hardware and not a modelling
 * choice: the firmware masks the controllers before it cares, and the
 * diagnostic drives `OPR[7]` *set* before its first check for exactly this
 * reason. Asserted here so the value is a statement rather than an accident.
 */
static void test_the_diagnostic_line_idles_asserted_after_reset(void) {
  ap_sio_t sio;
  TEST_ASSERT_TRUE(ap_sio_reset(&sio));
  TEST_ASSERT_TRUE(ap_sio_diagnostic_interrupt(&sio));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_op7_can_carry_channel_bs_transmitter_ready);
  RUN_TEST(test_serial_ones_op7_is_the_diagnostic_interrupt);
  RUN_TEST(test_the_diagnostic_line_idles_asserted_after_reset);
  RUN_TEST(test_the_firmwares_preload_gives_the_documented_refresh_period);
  RUN_TEST(test_the_refresh_output_is_a_square_wave_of_that_period);
  RUN_TEST(test_the_counter_ready_bit_comes_once_per_period);
  RUN_TEST(test_the_refresh_does_not_depend_on_the_call_rate);
  RUN_TEST(test_what_the_program_transmits_reaches_the_caller);
  RUN_TEST(test_a_delivered_byte_reaches_the_program);
  RUN_TEST(test_a_mis_rated_sender_reaches_the_port_as_an_error);
  RUN_TEST(test_a_matching_sender_reaches_the_port_cleanly);
  RUN_TEST(test_both_ports_decode_at_stride_two);
  RUN_TEST(test_the_paired_bytes_reach_one_register);
  RUN_TEST(test_the_two_ports_are_independent);
  RUN_TEST(test_a_character_crosses_the_keyboard_port);
  RUN_TEST(test_the_refresh_period_is_exact_in_base_units);
  RUN_TEST(test_the_serial_ports_raise_the_second_priority_interrupt);
  RUN_TEST(test_the_stop_length_is_the_table_s_sixteenths);
  RUN_TEST(test_a_character_takes_as_long_as_its_framing_says);
  RUN_TEST(test_a_rateless_clock_select_has_no_character_time);
  RUN_TEST(test_the_ram_config_byte_is_a_table_and_not_a_rule);
  RUN_TEST(test_an_unlisted_size_is_refused_rather_than_approximated);
  RUN_TEST(test_the_strap_drives_seven_input_pins);
  RUN_TEST(test_the_refresh_output_returns_to_the_input_port);
  RUN_TEST(test_the_loopback_leaves_the_ram_config_pins_alone);
  return UNITY_END();
}

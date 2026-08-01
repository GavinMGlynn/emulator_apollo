/* Apollo serial ports as the board wires them. Placement measured;
 * `FINDINGS.md` C14. */

#include "unity.h"

#include "board/ap_intr.h"
#include "board/ap_sio.h"

void setUp(void) {}
void tearDown(void) {}

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
  ap_sio_reset(&sio);

  /* Writing through the odd byte and reading through the even one must reach
   * the same register, which is what "stride 2" means and what the measured
   * dump's paired values show. */
  ap_sio_write(&sio, AP_SIO1_ADDR + 25u, 0x5A); /* register 12, odd byte */
  TEST_ASSERT_EQUAL_HEX8(0x5A, ap_sio_read(&sio, AP_SIO1_ADDR + 24u));
}

static void test_the_two_ports_are_independent(void) {
  ap_sio_t sio;
  ap_sio_reset(&sio);

  ap_sio_write(&sio, AP_SIO1_ADDR + 24u, 0x11); /* interrupt vector, port 1 */
  ap_sio_write(&sio, AP_SIO2_ADDR + 24u, 0x22);
  TEST_ASSERT_EQUAL_HEX8(0x11, ap_sio_read(&sio, AP_SIO1_ADDR + 24u));
  TEST_ASSERT_EQUAL_HEX8(0x22, ap_sio_read(&sio, AP_SIO2_ADDR + 24u));
}

static void test_a_character_crosses_the_keyboard_port(void) {
  ap_sio_t sio;
  ap_sio_reset(&sio);

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
  TEST_ASSERT_EQUAL_UINT64(99000u, AP_SIO_REFRESH_PERIOD);
  TEST_ASSERT_EQUAL_UINT64(0u, (AP_TIME_BASE_HZ * 15u) % 1000000u);
  TEST_ASSERT_EQUAL_UINT64(AP_SIO_REFRESH_PERIOD,
                           (AP_TIME_BASE_HZ * 15u) / 1000000u);
}

static void test_the_serial_ports_raise_the_second_priority_interrupt(void) {
  ap_sio_t sio;
  ap_intr_t intr;
  ap_sio_reset(&sio);
  ap_intr_reset(&intr);

  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 0u, 0x11);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 1u, 0xA0);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 1u, 0x08);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 1u, 0x01);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 1u, 0x00);

  /* Unmask the input-port-change interrupt and change a pin. */
  ap_sio_write(&sio, AP_SIO1_ADDR + 10u, AP_MC68681_ISR_INPUT);
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
  ap_sio_reset(&sio);

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
  ap_sio_reset(&sio);

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
  ap_sio_reset(&sio);
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
  ap_sio_reset(&sio);
  ap_sio_write(&sio, AP_SIO2_ADDR + (AP_MC68681_CR_A * 2u), 0x01u);
  ap_sio_write(&sio, AP_SIO2_ADDR + (AP_MC68681_SR_CSR_A * 2u), 0x77u);
  ap_sio_write(&sio, AP_SIO2_ADDR + (AP_MC68681_MR_A * 2u), 0x07u);

  ap_sio_receive_framed(&sio, 1u, 0u, 0x41u, 0x77u, 0x07u);

  const uint8_t sr = ap_sio_read(&sio, AP_SIO2_ADDR + (AP_MC68681_SR_CSR_A * 2u));
  TEST_ASSERT_FALSE((sr & (AP_MC68681_SR_FRAMING | AP_MC68681_SR_PARITY)) != 0u);
  TEST_ASSERT_EQUAL_HEX8(
      0x41u, ap_sio_read(&sio, AP_SIO2_ADDR + (AP_MC68681_RB_TB_A * 2u)));
}

int main(void) {
  UNITY_BEGIN();
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
  return UNITY_END();
}

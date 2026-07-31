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

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_both_ports_decode_at_stride_two);
  RUN_TEST(test_the_paired_bytes_reach_one_register);
  RUN_TEST(test_the_two_ports_are_independent);
  RUN_TEST(test_a_character_crosses_the_keyboard_port);
  RUN_TEST(test_the_refresh_period_is_exact_in_base_units);
  RUN_TEST(test_the_serial_ports_raise_the_second_priority_interrupt);
  return UNITY_END();
}

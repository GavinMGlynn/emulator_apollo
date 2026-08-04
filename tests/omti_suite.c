/* OMTI 862X ESDI/floppy controller, `[OMTI]` Jan 1987. */

#include "unity.h"

#include <string.h>

#include "device/ap_omti.h"

void setUp(void) {}
void tearDown(void) {}

static void test_the_measured_fixed_disk_ports_are_reproduced(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  /* The oracle's idle controller reads `FF C0 FC 00` across the four ports.
   * The `C0` is the confirmation: Table 4-2 gives bits 7 and 6 as "Not Used
   * (Set to 1)" and every other bit as a condition an idle controller does not
   * meet, so `C0` is the only value the table permits here. Manual and machine
   * agreeing on a byte. */
  static const uint8_t expected[4] = {0xFF, 0xC0, 0xFC, 0x00};
  for (unsigned i = 0; i < 4u; i++) {
    TEST_ASSERT_EQUAL_HEX8(expected[i], ap_omti_disk_read(&o, i));
  }
}

static void test_the_status_bits_seven_and_six_cannot_be_cleared(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  /* Table 4-2 gives them as constants, not state. Re-asserted on every read so
   * that nothing -- not a reset, not a select -- can put the register in a state
   * the table says is impossible. */
  ap_omti_disk_write(&o, AP_OMTI_DISK_CONFIG, 0x00); /* SELECT */
  TEST_ASSERT_EQUAL_HEX8(AP_OMTI_ST_FIXED,
                         ap_omti_disk_read(&o, AP_OMTI_DISK_STATUS) &
                             AP_OMTI_ST_FIXED);
}

static void test_selecting_the_controller_makes_it_busy(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  /* Table 4-1's write side of port 2 is "SELECT (Function)", and Table 4-2's
   * BSY bit is "1 = Controller Selected". So the function has an observable
   * effect rather than merely being accepted. */
  TEST_ASSERT_EQUAL_HEX8(0, ap_omti_disk_read(&o, AP_OMTI_DISK_STATUS) &
                                AP_OMTI_ST_BSY);
  ap_omti_disk_write(&o, AP_OMTI_DISK_CONFIG, 0x00);
  TEST_ASSERT_EQUAL_HEX8(AP_OMTI_ST_BSY,
                         ap_omti_disk_read(&o, AP_OMTI_DISK_STATUS) &
                             AP_OMTI_ST_BSY);
}

static void test_the_reset_port_is_a_function_not_a_store(void) {
  ap_omti_t fresh;
  ap_omti_t used;
  ap_omti_reset(&fresh);
  ap_omti_reset(&used);

  /* Table 4-1: "RESET (Function)". The value written is not a parameter, so two
   * controllers reset with different values must be identical -- and identical
   * to one that was never disturbed. */
  ap_omti_disk_write(&used, AP_OMTI_DISK_CONFIG, 0x00); /* select, sets BSY */
  ap_omti_disk_write(&used, AP_OMTI_DISK_MASK, 0x5A);
  ap_omti_disk_write(&used, AP_OMTI_DISK_STATUS, 0xA5); /* RESET */

  TEST_ASSERT_EQUAL_MEMORY(&fresh, &used, sizeof fresh);
}

static void test_the_data_register_changes_width_with_the_command_bit(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  /* §4.2: "This is an 8 or 16 bit register depending on the state of the
   * controller (determined by the C/D bit in the STATUS register)."
   *
   * The width is exposed rather than hidden because a model with a fixed-width
   * data register would carry commands correctly and corrupt every data word,
   * or the reverse -- and neither failure shows up until a transfer runs. */
  TEST_ASSERT_FALSE(ap_omti_data_is_byte(&o));
  o.status |= AP_OMTI_ST_CD;
  TEST_ASSERT_TRUE(ap_omti_data_is_byte(&o));
}

static void test_the_measured_floppy_block_is_reproduced(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  /* The oracle's floppy half reads `FF FF FF FF 00 FF 00 80`. The last byte is
   * the one the manual predicts: Table 4-3's Digital Input bit 7 comes "from pin
   * 34 of the floppy disk control cable and is normally used for diskette change
   * status", and a drive with no media asserts it. */
  static const uint8_t expected[8] = {0xFF, 0xFF, 0xFF, 0xFF,
                                      0x00, 0xFF, 0x00, 0x80};
  for (unsigned i = 0; i < 8u; i++) {
    TEST_ASSERT_EQUAL_HEX8(expected[i], ap_omti_fdc_read(&o, i));
  }
}

static void test_clearing_the_output_register_holds_the_floppy_in_reset(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  /* Table 4-3, Digital Output bit 2: "Reset floppy disk function when 0. The
   * floppy disk function comes out of reset when this bit is set to 1."
   *
   * That bit runs the opposite way to every other control bit in the part, so a
   * driver clearing the register to stop the motors also asserts reset. A model
   * that missed the inversion would come out of reset exactly when the hardware
   * went into it. */
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DOR, 0x00);
  TEST_ASSERT_TRUE(ap_omti_fdc_in_reset(&o));

  ap_omti_fdc_write(&o, AP_OMTI_FDC_DOR, AP_OMTI_DOR_NOT_RESET);
  TEST_ASSERT_FALSE(ap_omti_fdc_in_reset(&o));

  /* Enabling the motors without setting bit 2 still holds it in reset, which is
   * the case a driver actually gets wrong. */
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DOR, AP_OMTI_DOR_DRIVE_A_MOTOR);
  TEST_ASSERT_TRUE(ap_omti_fdc_in_reset(&o));
}

static void test_the_two_halves_share_nothing(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  /* §4.1: "two independent sets of registers", and §3.4 has them operating
   * concurrently. Resetting the fixed disk must not disturb the floppy's
   * programming, or a disk command would silently stop the drive motors. */
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DOR,
                    (uint8_t)(AP_OMTI_DOR_NOT_RESET | AP_OMTI_DOR_DRIVE_A_MOTOR));

  /* A floppy command part-way through its command phase: two bytes of SEEK's
   * three. This used to be a byte written to the data register and read back,
   * which stopped meaning anything once that register became a command port --
   * a write there now *starts* a command, and the byte that comes back is its
   * result. A command in flight is the stronger thing to leave undisturbed
   * anyway. */
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, AP_OMTI_FDC_SEEK);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, 0x00);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_COMMAND, ap_omti_fdc_phase(&o));

  ap_omti_disk_write(&o, AP_OMTI_DISK_CONFIG, 0x00);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_COMMAND, ap_omti_fdc_phase(&o));
  TEST_ASSERT_FALSE(ap_omti_fdc_in_reset(&o));

  /* And the stronger case, which this test originally missed by exercising
   * SELECT alone: the fixed disk's *reset* must leave the floppy running. A
   * disk reset that stopped the drive motors would be a fault with no register
   * to explain it. */
  ap_omti_disk_write(&o, AP_OMTI_DISK_STATUS, 0x00);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_COMMAND, ap_omti_fdc_phase(&o));
  TEST_ASSERT_FALSE(ap_omti_fdc_in_reset(&o));

  /* The seek completes across all of that, on the cylinder it was given. */
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, 0x11);
  ap_omti_fdc_write(&o, AP_OMTI_FDC_DATA, AP_OMTI_FDC_SENSE_INTERRUPT);
  (void)ap_omti_fdc_read(&o, AP_OMTI_FDC_DATA);
  TEST_ASSERT_EQUAL_HEX8(0x11, ap_omti_fdc_read(&o, AP_OMTI_FDC_DATA));
}

static void test_two_controllers_reset_alike_hold_identical_state(void) {
  ap_omti_t a;
  ap_omti_t b;
  memset(&a, 0xAA, sizeof a);
  memset(&b, 0x55, sizeof b);
  ap_omti_reset(&a);
  ap_omti_reset(&b);
  TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof a);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_measured_fixed_disk_ports_are_reproduced);
  RUN_TEST(test_the_status_bits_seven_and_six_cannot_be_cleared);
  RUN_TEST(test_selecting_the_controller_makes_it_busy);
  RUN_TEST(test_the_reset_port_is_a_function_not_a_store);
  RUN_TEST(test_the_data_register_changes_width_with_the_command_bit);
  RUN_TEST(test_the_measured_floppy_block_is_reproduced);
  RUN_TEST(test_clearing_the_output_register_holds_the_floppy_in_reset);
  RUN_TEST(test_the_two_halves_share_nothing);
  RUN_TEST(test_two_controllers_reset_alike_hold_identical_state);
  return UNITY_END();
}

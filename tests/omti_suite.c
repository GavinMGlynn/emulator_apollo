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

/* Issue a six-byte CDB the way §4.3's command state does: select, then a byte
 * at a time, checking the controller asks for each one. */
static void issue(ap_omti_t *o, const uint8_t cdb[6]) {
  ap_omti_disk_write(o, AP_OMTI_DISK_CONFIG, 0x00); /* SELECT */
  for (unsigned i = 0; i < 6u; i++) {
    /* The command phase: C/D set, travelling from the host, and requested --
     * `CD` with the two fixed bits, which is what the boot PROM checks after
     * every byte it writes. */
    TEST_ASSERT_EQUAL_HEX8(0xCD, ap_omti_disk_read(o, AP_OMTI_DISK_STATUS));
    ap_omti_disk_write(o, AP_OMTI_DISK_DATA, cdb[i]);
  }
}

/* ## `0E READ DATA FROM SECTOR BUFFER`, and the block a reset leaves behind
 *
 * §5.4.13: the transfer is the sector size times byte 4's block count, the
 * controller does not touch the drive, and issued after a reset before any
 * other command the buffer holds the controller's own identification. The boot
 * PROM's Winchester test 1 is precisely this sequence.
 */
static void test_read_sector_buffer_enters_the_data_phase_without_a_drive(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  static const uint8_t cdb[6] = {0x0E, 0, 0, 0, 1, 0};
  issue(&o, cdb);

  /* Data in, not status: C/D **clear**, I/O set, busy and requested. `CB` is
   * the byte the firmware waits for, and this model used to answer `EF` --
   * status phase with an interrupt, which is what it does with a command it
   * does not implement. No drive is fitted, and the command does not want one. */
  TEST_ASSERT_EQUAL_HEX8(0xCB, ap_omti_disk_read(&o, AP_OMTI_DISK_STATUS));
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_IN, ap_omti_disk_phase(&o));
}

static void test_a_reset_leaves_the_identification_block_in_the_buffer(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  static const uint8_t cdb[6] = {0x0E, 0, 0, 0, 1, 0};
  issue(&o, cdb);

  uint8_t block[0x16];
  for (unsigned i = 0; i < sizeof block; i++) {
    block[i] = ap_omti_disk_read(&o, AP_OMTI_DISK_DATA);
  }

  /* `8x2xVW.WMMDDYY` resolved for the part the DN3500 has. */
  TEST_ASSERT_EQUAL_MEMORY(AP_OMTI_IDENTIFICATION, block,
                           AP_OMTI_IDENTIFICATION_BYTES);

  /* The four power-on error bytes, zero on a healthy controller -- and the two
   * words the boot PROM actually compares. A controller reporting a ROM
   * checksum or buffer RAM error here fails the self-test, which is the whole
   * purpose of the block. */
  for (unsigned i = 0; i < 4u; i++) {
    TEST_ASSERT_EQUAL_HEX8(0, block[AP_OMTI_ID_ERROR_FLAGS + i]);
  }
  /* Bits 7 and 6 set: 32K, per §5.4.13's own table. */
  TEST_ASSERT_EQUAL_HEX8(AP_OMTI_ID_BUFFER_32K, block[AP_OMTI_ID_BUFFER_SIZE]);
}

/* ## The data port is sixteen bits, and a word is one cycle
 *
 * Served as two byte reads, the second byte comes from the *status* register
 * and the word can never be what the firmware is waiting for -- it came back
 * `FFFF`. The byte order within the word is `PROVISIONAL`; see
 * `device/ap_omti.h`. This asserts the part that is not: a word read takes two
 * bytes of the buffer and advances by two.
 */
static void test_a_word_read_of_the_data_port_takes_two_buffer_bytes(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  static const uint8_t cdb[6] = {0x0E, 0, 0, 0, 1, 0};
  issue(&o, cdb);

  const uint16_t first = ap_omti_disk_read16(&o);
  const uint16_t second = ap_omti_disk_read16(&o);

  /* Two bytes per read, the earlier one in the **high** half, so the buffer
   * arrives in the order it holds. Settled by the boot PROM: `sysboot` loads to
   * `010FD800` with a long word the firmware names, and reads back as
   * `SYSBOOT VER ` only this way round. The oracle packs it the other way.
   *
   * `8621` are the first four bytes of the identification block. */
  TEST_ASSERT_EQUAL_HEX16(0x3836u, first);  /* '8', '6' */
  TEST_ASSERT_EQUAL_HEX16(0x3231u, second); /* '2', '1' */
  TEST_ASSERT_EQUAL_MEMORY(AP_OMTI_IDENTIFICATION,
                           ((const uint8_t[]){(uint8_t)(first >> 8),
                                              (uint8_t)(first & 0xFFu),
                                              (uint8_t)(second >> 8),
                                              (uint8_t)(second & 0xFFu)}),
                           4u);
}

static void test_a_block_count_past_the_manuals_cap_is_refused(void) {
  ap_omti_t o;
  ap_omti_reset(&o);

  /* §5.4.13 caps the block count at seven for 1056-byte sectors. Eight is
   * refused rather than truncated: a host told the transfer succeeded would
   * read the tail of some earlier command's buffer as data. */
  static const uint8_t cdb[6] = {0x0E, 0, 0, 0, 8, 0};
  issue(&o, cdb);

  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&o));
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
  RUN_TEST(test_read_sector_buffer_enters_the_data_phase_without_a_drive);
  RUN_TEST(test_a_reset_leaves_the_identification_block_in_the_buffer);
  RUN_TEST(test_a_word_read_of_the_data_port_takes_two_buffer_bytes);
  RUN_TEST(test_a_block_count_past_the_manuals_cap_is_refused);
  RUN_TEST(test_two_controllers_reset_alike_hold_identical_state);
  return UNITY_END();
}

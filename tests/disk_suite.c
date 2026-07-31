/* Apollo disk and floppy as the board wires them. Placements measured;
 * `FINDINGS.md` C20, C22, C23. */

#include "unity.h"

#include "board/ap_disk.h"
#include "board/ap_intr.h"

void setUp(void) {}
void tearDown(void) {}

static void test_both_measured_blocks_are_reproduced(void) {
  ap_disk_t d;
  ap_disk_reset(&d);

  /* The two dumps that located the halves, from this core. Each carries a byte
   * the manual predicts: `C0` is the fixed disk's two "Not Used (Set to 1)"
   * status bits, and `80` is the floppy's diskette-change bit with no media. */
  static const uint8_t fixed[4] = {0xFF, 0xC0, 0xFC, 0x00};
  static const uint8_t floppy[8] = {0xFF, 0xFF, 0xFF, 0xFF,
                                    0x00, 0xFF, 0x00, 0x80};
  for (unsigned i = 0; i < 4u; i++) {
    TEST_ASSERT_EQUAL_HEX8(fixed[i], ap_disk_read(&d, AP_DISK_FIXED_ADDR + i));
  }
  for (unsigned i = 0; i < 8u; i++) {
    TEST_ASSERT_EQUAL_HEX8(floppy[i],
                           ap_disk_read(&d, AP_DISK_FLOPPY_ADDR + i));
  }
}

static void test_the_halves_alias_on_different_periods(void) {
  ap_disk_t d;
  ap_disk_reset(&d);

  /* The fixed disk repeats every four bytes within its block and the floppy
   * every eight -- four registers against an eight-address block. Using one
   * period for both would fold the floppy's Digital Input onto its Main Status. */
  TEST_ASSERT_EQUAL_HEX8(0xC0, ap_disk_read(&d, AP_DISK_FIXED_ADDR + 1u + 4u));
  TEST_ASSERT_EQUAL_HEX8(0x80, ap_disk_read(&d, AP_DISK_FLOPPY_ADDR + 7u + 8u));
  TEST_ASSERT_EQUAL_HEX8(0x00, ap_disk_read(&d, AP_DISK_FLOPPY_ADDR + 4u + 8u));
}

static void test_each_block_covers_exactly_one_kilobyte(void) {
  ap_disk_t d;
  bool f;
  unsigned r;
  ap_disk_reset(&d);

  /* Measured: the fixed disk's live addresses ran to `04D3FB` and stopped. */
  TEST_ASSERT_TRUE(ap_disk_decode(AP_DISK_FIXED_ADDR + 0x3FFu, &f, &r));
  TEST_ASSERT_FALSE(ap_disk_decode(AP_DISK_FIXED_ADDR + 0x400u, &f, &r));
  TEST_ASSERT_EQUAL_HEX8(0xFF, ap_disk_read(&d, AP_DISK_FIXED_ADDR + 0x400u));

  TEST_ASSERT_TRUE(ap_disk_decode(AP_DISK_FLOPPY_ADDR + 0x3FFu, &f, &r));
  TEST_ASSERT_FALSE(ap_disk_decode(AP_DISK_FLOPPY_ADDR + 0x400u, &f, &r));
}

static void test_the_two_halves_are_seventy_four_kilobytes_apart(void) {
  /* Not arbitrary: the AT I/O window maps `Apollo = 0x040000 + AT x 0x80`, so
   * the gap is `3F0 - 1A0` multiplied by 128. Asserted as the arithmetic rather
   * than as two constants, so that the rule is what is pinned -- it is what will
   * let the next device's address be predicted instead of hunted for. */
  TEST_ASSERT_EQUAL_HEX32(0x040000u + 0x1A0u * 0x80u, AP_DISK_FIXED_ADDR);
  TEST_ASSERT_EQUAL_HEX32(0x040000u + 0x3F0u * 0x80u, AP_DISK_FLOPPY_ADDR);
  TEST_ASSERT_EQUAL_UINT32((0x3F0u - 0x1A0u) * 0x80u,
                           AP_DISK_FLOPPY_ADDR - AP_DISK_FIXED_ADDR);
}

static void test_writing_one_half_leaves_the_other_alone(void) {
  ap_disk_t d;
  ap_disk_reset(&d);

  /* Two independent register sets, and they are reached through one decode --
   * which is exactly where a board could accidentally join them. */
  ap_disk_write(&d, AP_DISK_FLOPPY_ADDR + AP_OMTI_FDC_DOR,
                (uint8_t)(AP_OMTI_DOR_NOT_RESET | AP_OMTI_DOR_DRIVE_A_MOTOR));
  ap_disk_write(&d, AP_DISK_FIXED_ADDR + AP_OMTI_DISK_STATUS, 0x00); /* RESET */

  /* The fixed-disk reset must not have stopped the floppy's motor. */
  TEST_ASSERT_FALSE(ap_omti_fdc_in_reset(&d.controller));
}

static void test_the_two_halves_interrupt_on_separate_lines(void) {
  ap_intr_t intr;
  ap_intr_reset(&intr);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 0u, 0x11);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 1u, 0xA0);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 1u, 0x08);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 1u, 0x01);
  ap_intr_write(&intr, AP_INTR_MASTER_ADDR + 1u, 0x00);
  ap_intr_write(&intr, AP_INTR_SLAVE_ADDR + 0u, 0x11);
  ap_intr_write(&intr, AP_INTR_SLAVE_ADDR + 1u, 0xA8);
  ap_intr_write(&intr, AP_INTR_SLAVE_ADDR + 1u, 0x03);
  ap_intr_write(&intr, AP_INTR_SLAVE_ADDR + 1u, 0x01);
  ap_intr_write(&intr, AP_INTR_SLAVE_ADDR + 1u, 0x00);

  /* Table 2-3 puts the Winchester on IRQ14 at priority 4+7 and the floppy on
   * IRQ6 at priority 7 -- so the Winchester wins, despite the larger number,
   * because it is on the cascaded controller. Raised together, the disk is
   * acknowledged first. */
  ap_intr_set_request(&intr, AP_DISK_FLOPPY_IRQ, false);
  ap_intr_set_request(&intr, AP_DISK_FLOPPY_IRQ, true);
  ap_intr_set_request(&intr, AP_DISK_FIXED_IRQ, false);
  ap_intr_set_request(&intr, AP_DISK_FIXED_IRQ, true);

  TEST_ASSERT_EQUAL_HEX8(0xAE, ap_intr_acknowledge(&intr)); /* A8 + 6 */
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_both_measured_blocks_are_reproduced);
  RUN_TEST(test_the_halves_alias_on_different_periods);
  RUN_TEST(test_each_block_covers_exactly_one_kilobyte);
  RUN_TEST(test_the_two_halves_are_seventy_four_kilobytes_apart);
  RUN_TEST(test_writing_one_half_leaves_the_other_alone);
  RUN_TEST(test_the_two_halves_interrupt_on_separate_lines);
  return UNITY_END();
}

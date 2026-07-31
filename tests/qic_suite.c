/* QIC-02 cartridge tape drive. Command set from `[SC499]` §1.13.1, as far as
 * the scan is legible -- `FINDINGS.md` C25. */

#include "unity.h"

#include <string.h>

#include "device/ap_qic.h"

void setUp(void) {}
void tearDown(void) {}

static uint8_t image[AP_CT_BLOCK_SIZE * 3u];

static void load(ap_qic_t *q) {
  for (unsigned i = 0; i < sizeof image; i++) {
    image[i] = (uint8_t)(i & 0xFFu);
  }
  ap_qic_reset(q);
  TEST_ASSERT_TRUE(ap_qic_load(q, image, sizeof image, AP_QIC_CARTRIDGE_DC600A));
}

static void test_the_cartridge_type_must_be_supplied(void) {
  ap_qic_t q;
  ap_qic_reset(&q);

  /* `[SC499]` has the controller discriminating cartridges "by measurement of
   * BOT to LOAD POINT distance" -- tape geometry, which a raw block image has
   * none of. So the type comes from the caller, and refusing to load without
   * one is what keeps that from being quietly defaulted. */
  TEST_ASSERT_FALSE(ap_qic_load(&q, image, sizeof image,
                                AP_QIC_CARTRIDGE_NONE));
  TEST_ASSERT_TRUE(ap_qic_load(&q, image, sizeof image,
                               AP_QIC_CARTRIDGE_DC300XL));
  TEST_ASSERT_EQUAL_UINT(AP_QIC_CARTRIDGE_DC300XL, q.cartridge);
}

static void test_a_drive_must_be_selected_before_it_moves(void) {
  ap_qic_t q;
  load(&q);

  /* BOT "positions the tape in the cartridge in the selected device", so an
   * unselected drive does nothing. */
  TEST_ASSERT_FALSE(ap_qic_command(&q, AP_QIC_CMD_BOT));
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT));
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_BOT));
}

static void test_selection_is_sticky(void) {
  ap_qic_t q;
  load(&q);

  /* "The drive shall remain selected until changed by another SELECT command or
   * RESET." State, not a momentary action -- so one SELECT covers every command
   * that follows it. */
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT));
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_BOT));
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_RETENSION));
  TEST_ASSERT_TRUE(q.selected);

  ap_qic_reset(&q);
  TEST_ASSERT_FALSE(q.selected);
}

static void test_a_plain_select_clears_the_soft_lock(void) {
  ap_qic_t q;
  load(&q);

  /* "Execution of the SELECT command or RESET unlocks the cartridge." The two
   * SELECT variants are not independent switches: the unlocking one actively
   * clears what the locking one set. */
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT_LOCK));
  TEST_ASSERT_TRUE(q.soft_lock);

  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT));
  TEST_ASSERT_FALSE(q.soft_lock);
}

static void test_a_locked_cartridge_cannot_be_ejected(void) {
  ap_qic_t q;
  load(&q);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT_LOCK));

  /* Holding the cartridge is the only thing the lock does that a caller can
   * observe. A model that stored the bit and ignored it would make the command
   * inert while looking implemented. */
  ap_qic_eject(&q);
  TEST_ASSERT_TRUE(q.loaded);

  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT));
  ap_qic_eject(&q);
  TEST_ASSERT_FALSE(q.loaded);
}

static void test_a_reset_deselects_but_does_not_eject(void) {
  ap_qic_t q;
  load(&q);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT_LOCK));

  /* RESET is a command to the drive, not to the operator: it unlocks and
   * deselects, and the cartridge stays in. */
  ap_qic_reset(&q);
  TEST_ASSERT_TRUE(q.loaded);
  TEST_ASSERT_FALSE(q.selected);
  TEST_ASSERT_FALSE(q.soft_lock);
}

static void test_reading_returns_blocks_in_order_then_stops(void) {
  ap_qic_t q;
  uint8_t out[AP_CT_BLOCK_SIZE];
  load(&q);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT));
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_READ));

  for (unsigned b = 0; b < 3u; b++) {
    TEST_ASSERT_TRUE(ap_qic_read_block(&q, out));
    TEST_ASSERT_EQUAL_MEMORY(image + b * AP_CT_BLOCK_SIZE, out,
                             AP_CT_BLOCK_SIZE);
  }

  /* Past the end fails and keeps failing: the position does not advance, so a
   * driver that reads on does not wrap to the beginning and quietly reprocess
   * the tape. */
  TEST_ASSERT_FALSE(ap_qic_read_block(&q, out));
  TEST_ASSERT_FALSE(ap_qic_read_block(&q, out));
}

static void test_reading_needs_the_command_first(void) {
  ap_qic_t q;
  uint8_t out[AP_CT_BLOCK_SIZE];
  load(&q);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT));

  TEST_ASSERT_FALSE(ap_qic_read_block(&q, out));
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_READ));
  TEST_ASSERT_TRUE(ap_qic_read_block(&q, out));
}

static void test_rewinding_returns_to_the_first_block(void) {
  ap_qic_t q;
  uint8_t out[AP_CT_BLOCK_SIZE];
  load(&q);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT));
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_READ));
  TEST_ASSERT_TRUE(ap_qic_read_block(&q, out));

  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_BOT));
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_READ));
  TEST_ASSERT_TRUE(ap_qic_read_block(&q, out));
  TEST_ASSERT_EQUAL_MEMORY(image, out, AP_CT_BLOCK_SIZE);
}

static void test_a_status_read_answers_an_unselected_drive(void) {
  ap_qic_t q;
  ap_qic_reset(&q);

  /* Every other command needs the drive selected. This one must not, because a
   * status read is how a driver discovers the drive is not ready. */
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_READ_STATUS));
}

static void test_writing_is_refused_rather_than_discarded(void) {
  ap_qic_t q;
  load(&q);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT));

  /* Recognised commands, refused because there is no write-back path.
   * Accepting them and discarding the data would let an installation appear to
   * succeed and produce a cartridge that had never changed. */
  TEST_ASSERT_TRUE(ap_qic_command_known(AP_QIC_CMD_WRITE));
  TEST_ASSERT_FALSE(ap_qic_command(&q, AP_QIC_CMD_WRITE));
  TEST_ASSERT_FALSE(ap_qic_command(&q, AP_QIC_CMD_WRITE_FILE_MARK));
}

static void test_the_lost_opcodes_are_not_claimed(void) {
  ap_qic_t q;
  load(&q);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT));

  /* ERASE and SELECT Q11 FORMAT are in the manual's list and the scan lost
   * their codes. Both are constrained to the `2x` group, and none of the
   * candidates is claimed -- so if one is ever issued it is refused rather than
   * quietly doing something else. */
  static const uint8_t candidates[] = {0x22, 0x23, 0x25, 0x26};
  for (unsigned i = 0; i < sizeof candidates; i++) {
    TEST_ASSERT_FALSE(ap_qic_command_known(candidates[i]));
    TEST_ASSERT_FALSE(ap_qic_command(&q, candidates[i]));
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_cartridge_type_must_be_supplied);
  RUN_TEST(test_a_drive_must_be_selected_before_it_moves);
  RUN_TEST(test_selection_is_sticky);
  RUN_TEST(test_a_plain_select_clears_the_soft_lock);
  RUN_TEST(test_a_locked_cartridge_cannot_be_ejected);
  RUN_TEST(test_a_reset_deselects_but_does_not_eject);
  RUN_TEST(test_reading_returns_blocks_in_order_then_stops);
  RUN_TEST(test_reading_needs_the_command_first);
  RUN_TEST(test_rewinding_returns_to_the_first_block);
  RUN_TEST(test_a_status_read_answers_an_unselected_drive);
  RUN_TEST(test_writing_is_refused_rather_than_discarded);
  RUN_TEST(test_the_lost_opcodes_are_not_claimed);
  return UNITY_END();
}

/* QIC-02 cartridge tape drive. The whole command set, from `[SC499]` §1.13.1's
 * numbered descriptions -- including the two codes `FINDINGS.md` C25 recorded
 * as lost, which the same section gives in binary. */

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
  ap_qic_init(q);
  TEST_ASSERT_TRUE(ap_qic_load(q, image, sizeof image, AP_QIC_CARTRIDGE_DC600A, true));
}

static void test_the_cartridge_type_must_be_supplied(void) {
  ap_qic_t q;
  ap_qic_init(&q);

  /* `[SC499]` has the controller discriminating cartridges "by measurement of
   * BOT to LOAD POINT distance" -- tape geometry, which a raw block image has
   * none of. So the type comes from the caller, and refusing to load without
   * one is what keeps that from being quietly defaulted. */
  TEST_ASSERT_FALSE(ap_qic_load(&q, image, sizeof image,
                                AP_QIC_CARTRIDGE_NONE, true));
  TEST_ASSERT_TRUE(ap_qic_load(&q, image, sizeof image,
                               AP_QIC_CARTRIDGE_DC300XL, true));
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
  ap_qic_init(&q);

  /* Every other command needs the drive selected. This one must not, because a
   * status read is how a driver discovers the drive is not ready. */
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_READ_STATUS));
}

static void test_writing_is_refused_rather_than_discarded(void) {
  ap_qic_t q;
  load(&q);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT));

  /* WRITE now *places* a block on a cartridge loaded writable -- the image
   * layer carries the distinction, so the drive no longer has to refuse
   * everything to stay honest. */
  TEST_ASSERT_TRUE(ap_qic_command_known(AP_QIC_CMD_WRITE));
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_WRITE));
  static uint8_t block[AP_CT_BLOCK_SIZE];
  memset(block, 0x5Au, sizeof block);
  TEST_ASSERT_TRUE(ap_qic_write_block(&q, block));
  TEST_ASSERT_EQUAL_HEX8(0x5Au, image[0]);

  /* A read-only cartridge refuses, which is the case refusing everything used
   * to stand in for. */
  ap_qic_t locked;
  ap_qic_init(&locked);
  TEST_ASSERT_TRUE(ap_qic_load(&locked, image, sizeof image,
                               AP_QIC_CARTRIDGE_DC600A, false));
  TEST_ASSERT_TRUE(ap_qic_command(&locked, AP_QIC_CMD_SELECT));
  TEST_ASSERT_FALSE(ap_qic_command(&locked, AP_QIC_CMD_WRITE));

  /* WRITE FILE MARK stays refused: a raw block image has no file marks in it,
   * so there is nothing to write one into. */
  TEST_ASSERT_FALSE(ap_qic_command(&q, AP_QIC_CMD_WRITE_FILE_MARK));
}

/* The two "lost" opcodes, recovered from the same manual.
 *
 * This test previously asserted the opposite -- that `22` and `26` were *not*
 * claimed, because §1.13's summary table has a previous owner's pen through
 * both. §1.13.1's numbered descriptions two pages on give them in binary and
 * are untouched: "5) ERASE COMMAND (0010 0010)" and "11) SELECT Q11 FORMAT
 * COMMAND (0010 0110)".
 *
 * What makes them safe to claim rather than merely plausible is the series they
 * sit in: the same numbered descriptions give BOT as `0010 0001`, RETENSION as
 * `0010 0100` and SELECT Q24 as `0010 0111`, and those three are the codes this
 * core already had. Five entries of one series, three independently confirmed. */
static void test_the_two_recovered_opcodes_do_what_the_manual_says(void) {
  ap_qic_t q;
  load(&q);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT));

  /* Both recognised -- which is the whole change. */
  TEST_ASSERT_TRUE(ap_qic_command_known(AP_QIC_CMD_ERASE));
  TEST_ASSERT_TRUE(ap_qic_command_known(AP_QIC_CMD_SELECT_Q11));
  TEST_ASSERT_EQUAL_HEX8(0x22u, AP_QIC_CMD_ERASE);
  TEST_ASSERT_EQUAL_HEX8(0x26u, AP_QIC_CMD_SELECT_Q11);

  /* Out of reset the format is **QIC-24**, which is the board's jumper rather
   * than a language default: `008778-03` Table 8-1 gives the Tape Format jumper
   * at location CC as "IN = QIC-24" and marks that as the configuration the
   * vendor ships. A host that issues no SELECT FORMAT command gets QIC-24. */
  TEST_ASSERT_TRUE(q.q24_format);

  /* The format select is a switch with two settings, not two flags: §1.13.1
   * items 11 and 12 each say the command "selects the ... format as the current
   * format". */
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT_Q11));
  TEST_ASSERT_FALSE(q.q24_format);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT_Q24));
  TEST_ASSERT_TRUE(q.q24_format);

  /* ERASE is recognised and **refused**, as WRITE is: the cartridge is a
   * read-only image and an erase reported as successful would leave a driver
   * believing a tape it is about to write is blank. */
  TEST_ASSERT_FALSE(ap_qic_command(&q, AP_QIC_CMD_ERASE));

  /* The codes between them are still nobody's, and still refused. `[SC499]`
   * §1.13 lists eleven commands and these are not among them. */
  static const uint8_t absent[] = {0x23, 0x25, 0x28};
  for (unsigned i = 0; i < sizeof absent; i++) {
    TEST_ASSERT_FALSE(ap_qic_command_known(absent[i]));
    TEST_ASSERT_FALSE(ap_qic_command(&q, absent[i]));
  }
}

/* ---- The READ STATUS status block ---------------------------------------- */

/* `[SC499]` §1.13.1's READ STATUS entry: "The device transfers the standard six
 * bytes to the host." This document was recorded as not giving the length --
 * the search had been aimed at Figure 1-10, which shows the protocol and not
 * the payload, and the sentence is on the command's own page. Six is the
 * manual's number, not the conventional QIC-02 one, which the plan refused as a
 * source. */
static void test_the_status_block_is_the_six_bytes_the_manual_names(void) {
  ap_qic_t q;
  uint8_t block[AP_QIC_STATUS_BYTES];
  load(&q);
  TEST_ASSERT_EQUAL_UINT(6u, AP_QIC_STATUS_BYTES);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_READ_STATUS));
  TEST_ASSERT_TRUE(ap_qic_read_status(&q, block));
}

/* The block is the command's data phase, not a register. Asking for it without
 * the command is refused rather than answered from stale state. */
static void test_the_status_block_needs_its_command_first(void) {
  ap_qic_t q;
  uint8_t block[AP_QIC_STATUS_BYTES];
  load(&q);
  TEST_ASSERT_FALSE(ap_qic_read_status(&q, block));

  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_READ_STATUS));
  TEST_ASSERT_TRUE(ap_qic_read_status(&q, block));
  /* And the command does not stay armed: one command, one block. */
  TEST_ASSERT_FALSE(ap_qic_read_status(&q, block));
}

/* Three 16-bit fields, **least significant byte first** -- Linux's
 * `struct tpstatus { unsigned short exs, dec, urc; }` with "LSB first", and the
 * oracle keeping the same three. A big-endian reader would see every field
 * byte-swapped, which for a count of zero looks identical and for the exception
 * word puts status byte 0 where byte 1 belongs. */
static void test_the_status_block_is_three_words_least_significant_byte_first(void) {
  ap_qic_t q;
  uint8_t block[AP_QIC_STATUS_BYTES];
  load(&q);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT));
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_READ_STATUS));
  /* Taken *before* the read: the read clears the power-on flag it reports, so
   * the word computed afterwards is legitimately a different one. */
  const uint16_t expected = ap_qic_exception_word(&q);
  TEST_ASSERT_TRUE(ap_qic_read_status(&q, block));

  const uint16_t exs = (uint16_t)(block[0] | ((uint16_t)block[1] << 8));
  TEST_ASSERT_EQUAL_HEX16(expected, exs);

  /* The two counts are genuinely zero rather than unmodelled: this core
   * rewrites no block and never interrupts streaming. */
  TEST_ASSERT_EQUAL_HEX8(0, block[2]);
  TEST_ASSERT_EQUAL_HEX8(0, block[3]);
  TEST_ASSERT_EQUAL_HEX8(0, block[4]);
  TEST_ASSERT_EQUAL_HEX8(0, block[5]);
}

/* An empty drive is the condition the command exists to report, and it must not
 * look like a loaded one at block zero. */
static void test_an_empty_drive_reports_no_cartridge_rather_than_beginning(void) {
  ap_qic_t q;
  ap_qic_init(&q);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_READ_STATUS));
  const uint16_t exs = ap_qic_exception_word(&q);
  TEST_ASSERT_TRUE((exs & AP_QIC_EXS_NO_CARTRIDGE) != 0u);
  TEST_ASSERT_TRUE((exs & AP_QIC_EXS_BEGINNING_OF_MEDIA) == 0u);
}

/* "Power on/reset occurred" is how a driver tells a drive it has already talked
 * to from one that has just come up -- so it must survive until read, and not
 * survive being read. A flag that outlived its own report would have the driver
 * re-initialising forever. */
static void test_the_power_on_flag_survives_until_read_and_not_after(void) {
  ap_qic_t q;
  uint8_t block[AP_QIC_STATUS_BYTES];
  load(&q);
  TEST_ASSERT_TRUE((ap_qic_exception_word(&q) & AP_QIC_EXS_POWER_ON) != 0u);

  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_READ_STATUS));
  TEST_ASSERT_TRUE(ap_qic_read_status(&q, block));
  TEST_ASSERT_TRUE((block[0] & (uint8_t)AP_QIC_EXS_POWER_ON) != 0u);

  /* Reported once, then gone. */
  TEST_ASSERT_TRUE((ap_qic_exception_word(&q) & AP_QIC_EXS_POWER_ON) == 0u);

  /* And a reset raises it again. */
  ap_qic_reset(&q);
  TEST_ASSERT_TRUE((ap_qic_exception_word(&q) & AP_QIC_EXS_POWER_ON) != 0u);
}

/* Reading to the end of the tape is reported as end of media, which is what
 * `[SC499]` §1.12 says the drive does: it "reports END OF MEDIA by means of an
 * EXCEPTION and READ STATUS". */
static void test_reading_off_the_end_reports_end_of_media(void) {
  ap_qic_t q;
  uint8_t sector[AP_CT_BLOCK_SIZE];
  load(&q);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT));
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_READ));
  TEST_ASSERT_TRUE((ap_qic_exception_word(&q) & AP_QIC_EXS_END_OF_MEDIA) == 0u);

  /* The image is three blocks. */
  for (unsigned i = 0; i < 3u; i++) {
    TEST_ASSERT_TRUE(ap_qic_read_block(&q, sector));
  }
  TEST_ASSERT_FALSE(ap_qic_read_block(&q, sector));
  TEST_ASSERT_TRUE((ap_qic_exception_word(&q) & AP_QIC_EXS_END_OF_MEDIA) != 0u);
}

/* ## `002398-04` p. 12-5's STATUS SUMMARY, used as the check it is
 *
 * Fifteen rows, each a condition and the pair of status bytes that reports it.
 * This core sets status bits one at a time, so the summary is the check on
 * whether it can emit a pair the real controller never would.
 *
 * **The column convention had to be established first**, and it is now. Every
 * row's bits 6-0 decode exactly under p. 12-4's own bit maps; only bit 7 looked
 * wrong, printing `1` where byte 0's map draws a literal `0`. It is not a
 * constant: `QIC-02 Rev D` §5.2 gives both bit 7s as summary bits -- "set if
 * any other bit in" that byte "is set" -- which fits the whole table and is
 * what `ap_qic` was corrected to.
 *
 * One cell of the table is a misprint and is recorded rather than followed:
 * "Marginal block detected" prints Status 1 as `00010000`, with `mgn` set and
 * the summary bit clear, where "Read abort" two rows above prints the identical
 * condition as `10010000`. A dropped leading digit, and the only cell in either
 * column that the rule does not explain.
 *
 * `X` is don't-care. The rows below are the ones this model can be *put into*;
 * the rest need media errors it cannot produce, and they are listed in the
 * comment at the end so that the omission is a statement rather than a gap.
 */
/* Read the status once and throw it away, which is what clears `por`. Every row
 * of the summary describes a drive that has already been asked once. */
static void clear_power_on(ap_qic_t *q) {
  uint8_t block[AP_QIC_STATUS_BYTES];
  TEST_ASSERT_TRUE(ap_qic_command(q, AP_QIC_CMD_READ_STATUS));
  TEST_ASSERT_TRUE(ap_qic_read_status(q, block));
}

static void check_summary(const char *what, uint16_t exs, uint8_t status0,
                          uint8_t care0, uint8_t status1, uint8_t care1) {
  const uint8_t got0 = (uint8_t)(exs >> 8);
  const uint8_t got1 = (uint8_t)(exs & 0xFFu);
  UNITY_TEST_ASSERT_EQUAL_HEX8((uint8_t)(status0 & care0),
                               (uint8_t)(got0 & care0), __LINE__, what);
  UNITY_TEST_ASSERT_EQUAL_HEX8((uint8_t)(status1 & care1),
                               (uint8_t)(got1 & care1), __LINE__, what);
}

static void test_every_reachable_status_summary_row_is_reproduced(void) {
  ap_qic_t q;

  /* Row "No cartridge": Status 0 `110X0000`, Status 1 `00000000`. `noc` at bit
   * 6, `wp` a don't-care -- a cartridge that is not there cannot be known
   * write-protected -- and the summary bit above them.
   *
   * **The drive has to be selected**, and the row says so by printing bit 5,
   * `usd`, as a hard `0`: an unselected empty drive reports `E0` and is the
   * *next* row down, "No drive". The two differ by that one bit, which is the
   * distinction between a drive that is there with nothing in it and a drive
   * that is not there -- and it is why this row is worth testing rather than
   * assuming. */
  ap_qic_init(&q);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT));
  /* And the power-on condition has to have been read out first, or byte 1
   * carries `por` and the row's `00000000` cannot be met. That is not a
   * concession to the model: QIC-02 §5.2 has `POR` "set after the host asserts
   * RESET or when the controller is powered up ... reset by a Read Status
   * Sequence", so every row of this table describes a drive that has already
   * been asked once. */
  clear_power_on(&q);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_READ_STATUS));
  check_summary("no cartridge", ap_qic_exception_word(&q),
                0xC0u, 0xEFu, 0x00u, 0xFFu);

  /* Row "Write protected": Status 0 `10010000`, Status 1 `X000X000`. A
   * read-only cartridge, selected, at the beginning of tape -- so `bom` stands
   * too, which the row's don't-care at Status 1 bit 3 permits. */
  ap_qic_init(&q);
  TEST_ASSERT_TRUE(ap_qic_load(&q, image, sizeof image,
                               AP_QIC_CARTRIDGE_DC600A, false));
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT));
  clear_power_on(&q);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_READ_STATUS));
  check_summary("write protected", ap_qic_exception_word(&q),
                0x90u, 0xF0u, 0x00u, 0x77u);

  /* Row "End of media": Status 0 `10001000`, Status 1 `00000000`. Read the
   * cartridge out and the drive is past its last block. */
  load(&q);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT));
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_READ));
  {
    uint8_t block[AP_CT_BLOCK_SIZE];
    while (ap_qic_read_block(&q, block)) {
      /* to the end */
    }
  }
  clear_power_on(&q);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_READ_STATUS));
  check_summary("end of media", ap_qic_exception_word(&q),
                0x88u, 0xF8u, 0x00u, 0xF7u);

  /* Row "Power on/reset": Status 0 `XXXX0000`, Status 1 `1000X001`. Byte 0's
   * top four are don't-care because a freshly loaded drive also reports where
   * the tape is; what the row pins is `por` and the summary bit above it. */
  load(&q);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT));
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_READ_STATUS));
  check_summary("power on/reset", ap_qic_exception_word(&q),
                0x00u, 0x0Fu, 0x81u, 0xF7u);
}

/* The summary's "Illegal command" row, which is reachable now.
 *
 * `QIC-02 Rev D` §5.2 gives `ILL` six causes and this model can distinguish one
 * of them -- "any unimplemented command is issued" -- which is the one the row
 * describes. Status 0 `XXXX0000`, Status 1 `1100X000`: the code in bit 6 and
 * the summary bit above it, with byte 0's top four don't-care because a drive
 * reporting an illegal command is also still reporting where its tape is. */
static void test_an_unimplemented_command_latches_illegal_until_read(void) {
  ap_qic_t q;
  load(&q);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT));
  clear_power_on(&q);

  /* A code outside `[SC499]` §1.13's set entirely. */
  TEST_ASSERT_FALSE(ap_qic_command_known(0x55u));
  TEST_ASSERT_FALSE(ap_qic_command(&q, 0x55u));

  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_READ_STATUS));
  check_summary("illegal command", ap_qic_exception_word(&q),
                0x00u, 0x0Fu, 0xC0u, 0xF7u);

  /* "The bit is reset by a Read Status Sequence" -- so it survives until the
   * status is taken, and not after. A latch that outlived its own report would
   * have a driver rejecting every command that followed one bad one. */
  uint8_t block[AP_QIC_STATUS_BYTES];
  TEST_ASSERT_TRUE(ap_qic_read_status(&q, block));
  TEST_ASSERT_TRUE((block[0] & (uint8_t)AP_QIC_EXS_ILLEGAL) != 0u);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_READ_STATUS));
  TEST_ASSERT_EQUAL_HEX16(
      0u, (uint16_t)(ap_qic_exception_word(&q) & AP_QIC_EXS_ILLEGAL));

  /* And a *known* command that is merely refused is not illegal: WRITE FILE
   * MARK is recognised and declined for want of file marks in a `.ct`, which
   * §5.2 does not make a cause. */
  TEST_ASSERT_FALSE(ap_qic_command(&q, AP_QIC_CMD_WRITE_FILE_MARK));
  TEST_ASSERT_EQUAL_HEX16(
      0u, (uint16_t)(ap_qic_exception_word(&q) & AP_QIC_EXS_ILLEGAL));
}

/* The rows this model cannot be put into, named so the omission above is a
 * statement rather than a gap. Nine of the fifteen describe media faults --
 * read abort, write abort, the four read errors, filemark read, marginal block
 * -- and a `.ct` image is block data with no medium under it, so no read can
 * fail and no block can be marginal. "Drive not ready" and "No drive" are
 * controller-generated conditions for hardware that is absent rather than
 * empty, which this model expresses as an unselected drive. "Illegal command"
 * is reachable at the *controller* -- `ap_tape` raises Exception for a command
 * the drive refuses -- but `ap_qic` does not latch `ill`, which is the one row
 * here that is a real gap rather than an inapplicable one.
 *
 * That gap is deliberate and bounded: latching `ill` means deciding which of
 * QIC-02 §5.2's six causes this model can distinguish, and it refuses commands
 * for reasons the standard does not list (no cartridge, unselected). Named in
 * `docs/PROJECT_STATUS.md` rather than half-implemented. */

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
  RUN_TEST(test_the_two_recovered_opcodes_do_what_the_manual_says);
  RUN_TEST(test_the_status_block_is_the_six_bytes_the_manual_names);
  RUN_TEST(test_the_status_block_needs_its_command_first);
  RUN_TEST(test_the_status_block_is_three_words_least_significant_byte_first);
  RUN_TEST(test_an_empty_drive_reports_no_cartridge_rather_than_beginning);
  RUN_TEST(test_the_power_on_flag_survives_until_read_and_not_after);
  RUN_TEST(test_reading_off_the_end_reports_end_of_media);
  RUN_TEST(test_every_reachable_status_summary_row_is_reproduced);
  RUN_TEST(test_an_unimplemented_command_latches_illegal_until_read);
  return UNITY_END();
}

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

/* `QIC-02 Rev D` §4.2.1: a reset "initializes operating parameters and defaults
 * to drive 0 for subsequent commands", and §3.5's pin 32 says the same of the
 * RESET line. **This test used to assert the opposite** -- that a fresh drive
 * refuses BOT until a SELECT arrives -- which is a drive that would ignore the
 * first command of every driver that resets and reads. */
static void test_a_freshly_reset_drive_is_already_selected(void) {
  ap_qic_t q;
  load(&q);
  TEST_ASSERT_TRUE(q.selected);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_BOT));

  ap_qic_reset(&q);
  TEST_ASSERT_TRUE(q.selected);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_BOT));
}

/* And a drive that is *not* the selected one does nothing: BOT "positions the
 * tape in the cartridge in the selected device", and drive 2 is not this one. */
static void test_a_drive_other_than_the_selected_one_does_not_move(void) {
  ap_qic_t q;
  load(&q);

  /* `0000 0010` -- SELECT DRIVE 2, a legal command naming a drive an SC-499
   * does not have. Accepted, because the drive decoded it. */
  TEST_ASSERT_TRUE(ap_qic_command(&q, 0x02u));
  TEST_ASSERT_FALSE(q.selected);
  TEST_ASSERT_FALSE(ap_qic_command(&q, AP_QIC_CMD_BOT));

  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT));
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_BOT));
}

/* §5.3 row 2, "No drive": byte 0 `11110000`, byte 1 `00000000`. `CNI` and `WRP`
 * are printed as hard ones beside `USL` -- an absent drive answers every
 * condition line the same way -- and row 1, "No cartridge", prints `USL` as a
 * hard zero, which is what keeps a present-but-empty drive distinguishable from
 * a missing one. Both rows were unreachable until SELECT could name a drive
 * other than this one. */
static void test_selecting_an_absent_drive_reports_the_no_drive_row(void) {
  ap_qic_t q;
  load(&q);
  clear_power_on(&q);
  TEST_ASSERT_TRUE(ap_qic_command(&q, 0x04u)); /* SELECT DRIVE 3 */
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_READ_STATUS));
  check_summary("no drive", ap_qic_exception_word(&q),
                0xF0u, 0xFFu, 0x00u, 0xFFu);

  /* Including over a loaded, writable cartridge: the row is what the *bus* sees
   * from a drive that is not there, not an assembly of what this drive holds. */
  TEST_ASSERT_TRUE(q.loaded);
}

/* "The select command selects one of up to four drives" -- §5.2 cause (a) makes
 * anything else illegal, and §4.1's summary marks every such nibble `V(n)`. */
static void test_a_select_naming_no_drive_or_two_is_illegal(void) {
  ap_qic_t q;
  load(&q);
  clear_power_on(&q);

  TEST_ASSERT_FALSE(ap_qic_command(&q, 0x00u)); /* no drives */
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_READ_STATUS));
  check_summary("select, no drives", ap_qic_exception_word(&q),
                0x00u, 0x0Fu, 0xC0u, 0xF7u);
  {
    uint8_t block[AP_QIC_STATUS_BYTES];
    TEST_ASSERT_TRUE(ap_qic_read_status(&q, block));
  }

  TEST_ASSERT_FALSE(ap_qic_command(&q, 0x03u)); /* drives 1 and 2 */
  TEST_ASSERT_TRUE((ap_qic_exception_word(&q) & AP_QIC_EXS_ILLEGAL) != 0u);
  /* And the selection is unchanged: the drive rejected the command rather than
   * acting on half of it. */
  TEST_ASSERT_TRUE(q.selected);

  /* It is a *recognised* command, unlike a code outside the set -- a drive that
   * answers ILL to `0000 0011` decoded it and one that answers ILL to `0101
   * 0101` did not. */
  TEST_ASSERT_TRUE(ap_qic_command_known(0x03u));
  TEST_ASSERT_FALSE(ap_qic_command_known(0x55u));
}

/* §5.2 cause (e): "a drive is deselected by another SELECT command when the
 * cartridge in the currently selected drive is not at beginning of tape, track
 * 0". §5.4 item 12(b) is the same rule from the other side. */
static void test_deselecting_a_drive_away_from_bot_is_illegal(void) {
  ap_qic_t q;
  uint8_t sector[AP_CT_BLOCK_SIZE];
  load(&q);
  clear_power_on(&q);

  /* At BOT, changing selection is fine. */
  TEST_ASSERT_TRUE(ap_qic_command(&q, 0x02u));
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT));
  TEST_ASSERT_EQUAL_HEX16(
      0u, (uint16_t)(ap_qic_exception_word(&q) & AP_QIC_EXS_ILLEGAL));

  /* Move the tape off BOT with a read, then try again. */
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_READ));
  TEST_ASSERT_TRUE(ap_qic_read_block(&q, sector));
  TEST_ASSERT_FALSE(ap_qic_command(&q, 0x02u));
  TEST_ASSERT_TRUE((ap_qic_exception_word(&q) & AP_QIC_EXS_ILLEGAL) != 0u);
  /* Still ours, and still where it was. */
  TEST_ASSERT_TRUE(q.selected);
  TEST_ASSERT_EQUAL_UINT64(1u, q.position);

  /* Re-selecting the *same* drive is not a change of selection, so it is not
   * the cause the standard describes. */
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT));
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

  /* A reset changes it too -- back to the default, which is this drive. */
  TEST_ASSERT_TRUE(ap_qic_command(&q, 0x08u)); /* SELECT DRIVE 4 */
  TEST_ASSERT_FALSE(q.selected);
  ap_qic_reset(&q);
  TEST_ASSERT_TRUE(q.selected);
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

static void test_a_reset_unlocks_but_does_not_eject_or_deselect(void) {
  ap_qic_t q;
  load(&q);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT_LOCK));

  /* RESET is a command to the drive, not to the operator: it unlocks, the
   * cartridge stays in, and the selection goes to the default rather than to
   * nothing -- §4.2.1's "defaults to drive 0 for subsequent commands". */
  ap_qic_reset(&q);
  TEST_ASSERT_TRUE(q.loaded);
  TEST_ASSERT_TRUE(q.selected);
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
 * **And it appears twice.** `002398-04` p. 10-12 prints the same fifteen-row
 * summary for the DN5xx's tape controller, and its "Marginal block detected"
 * row carries the identical `00010000`. So this is not a typesetting slip on one
 * page: it is an error in whatever house table both chapters were set from,
 * reproduced faithfully. That changes nothing about which reading to follow --
 * `QIC-02 Rev D` §5.2's summary-bit rule explains every other cell of both
 * printings -- but it does mean a third copy of the table would be expected to
 * carry the error too, and finding it there is not corroboration.
 *
 * The rest of p. 10-12 agrees with `QIC-02 Rev D` §5.3 cell for cell, including
 * the three rows this session's `NDT` work rests on: "Read error, no data"
 * `100X0110`/`10100000`, "and EOM" `100X1110`/`10100000`, "and BOM"
 * `100X0110`/`101X1XX0`. Four documents, one table.
 *
 * `X` is don't-care. The rows below are the ones this model can be *put into*;
 * the rest need media errors it cannot produce, and they are listed in the
 * comment at the end so that the omission is a statement rather than a gap.
 */

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
   * cartridge out and the drive is at its last block.
   *
   * **Stopping on the last block matters, and it did not used to.** This loop
   * read until a read *failed*, which is a different row: `QIC-02 Rev D` §5.3
   * row 9 is "Read error, no data & EOM", and §5.4 item 4 has plain EOM as a
   * condition detected "during WRITE command" rather than by running out of
   * blocks. Once `NDT` was implemented the overshoot showed up as byte 1 `A0`
   * where the row prints `00`. The row is reachable; it just is not reached by
   * reading one block too many. */
  load(&q);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT));
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_READ));
  {
    uint8_t block[AP_CT_BLOCK_SIZE];
    for (unsigned i = 0; i < 3u; i++) {
      TEST_ASSERT_TRUE(ap_qic_read_block(&q, block));
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

/* ## `QIC-02 Rev D` §5.3 rows 8-10, the reads that find blank tape
 *
 * The standard's exception summary spends three rows on one condition: row 8
 * "Read error, no data" is byte 0 `100X0110` and byte 1 `10100000`, and rows 9
 * and 10 are the same pair with `EOM` or `BOM` added. Domain/OS spends three
 * status codes on the same three -- `002398-04` p. 4-14's `(00280017)` "read no
 * data", `(00280018)` "and end of tape", `(00280019)` "and load point" -- so
 * this is a bit the machine's own software is written to decode.
 *
 * A forward read off the end of a `.ct` lands on row 9: the tape is out of data
 * *and* out of media. That the byte pair is a printed row of the standard is the
 * point of testing it -- `NDT` alone would be a pair the table does not
 * contain. */
static void test_a_read_past_the_last_block_reports_no_data_and_end_of_media(void) {
  ap_qic_t q;
  uint8_t sector[AP_CT_BLOCK_SIZE];
  load(&q);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT));
  clear_power_on(&q);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_READ));
  for (unsigned i = 0; i < 3u; i++) {
    TEST_ASSERT_TRUE(ap_qic_read_block(&q, sector));
  }
  /* Nothing there. Before this the drive returned false and reported nothing at
   * all, so a driver reading to end of data got a failure with no reason. */
  TEST_ASSERT_FALSE(ap_qic_read_block(&q, sector));

  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_READ_STATUS));
  check_summary("read error, no data & EOM", ap_qic_exception_word(&q),
                0x8Eu, 0xEFu, 0xA0u, 0xFFu);
}

/* "This bit is reset by a Read Status Sequence" -- §5.2 bit 5, the same sentence
 * `ILL` and `POR` carry. A no-data latch that outlived its own report would have
 * a driver treating every later read as blank tape. */
static void test_the_no_data_latch_is_reset_by_the_status_read(void) {
  ap_qic_t q;
  uint8_t sector[AP_CT_BLOCK_SIZE];
  uint8_t block[AP_QIC_STATUS_BYTES];
  load(&q);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT));
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_READ));
  for (unsigned i = 0; i < 3u; i++) {
    TEST_ASSERT_TRUE(ap_qic_read_block(&q, sector));
  }
  TEST_ASSERT_FALSE(ap_qic_read_block(&q, sector));

  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_READ_STATUS));
  TEST_ASSERT_TRUE(ap_qic_read_status(&q, block));
  /* Byte 1 of the six-byte block is exception status byte 0, and byte 0 is
   * byte 1 -- three 16-bit fields, least significant byte first. */
  TEST_ASSERT_TRUE((block[0] & (uint8_t)AP_QIC_EXS_NO_DATA) != 0u);
  TEST_ASSERT_TRUE((block[1] & (uint8_t)(AP_QIC_EXS_DATA_ERROR >> 8)) != 0u);
  TEST_ASSERT_TRUE((block[1] & (uint8_t)(AP_QIC_EXS_NO_BLOCK >> 8)) != 0u);

  const uint16_t after = ap_qic_exception_word(&q);
  TEST_ASSERT_EQUAL_HEX16(0u, (uint16_t)(after & AP_QIC_EXS_NO_DATA));
  TEST_ASSERT_EQUAL_HEX16(0u, (uint16_t)(after & AP_QIC_EXS_DATA_ERROR));
  TEST_ASSERT_EQUAL_HEX16(0u, (uint16_t)(after & AP_QIC_EXS_NO_BLOCK));
  /* `EOM` is not in that sentence and must not go with them: §5.2 byte 0 bit 3
   * says outright "The EOM bit will not be reset by a Read Status Sequence", and
   * the tape is still where it was. */
  TEST_ASSERT_TRUE((after & AP_QIC_EXS_END_OF_MEDIA) != 0u);

  /* And a reset clears the latch too -- there is no failed read to report on a
   * drive that has just been reinitialised. */
  ap_qic_reset(&q);
  TEST_ASSERT_EQUAL_HEX16(
      0u, (uint16_t)(ap_qic_exception_word(&q) & AP_QIC_EXS_NO_DATA));
}

/* §5.2 says it once of each counter and in the same words: of `DEC`, "These
 * bytes shall be cleared by a Read Status Sequence", and of `URC` again. Both
 * read zero in this core, so the clearing is invisible in the block -- what the
 * test pins is that the field is *assigned*, which is what keeps "always zero"
 * a property of the model rather than of nothing ever having written it. */
static void test_the_two_status_counters_are_cleared_by_the_status_read(void) {
  ap_qic_t q;
  uint8_t block[AP_QIC_STATUS_BYTES];
  load(&q);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_SELECT));

  /* Put both counters somewhere they cannot have got by themselves. */
  q.data_errors = 0x1234u;
  q.underruns = 0x5678u;
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_READ_STATUS));
  TEST_ASSERT_TRUE(ap_qic_read_status(&q, block));
  /* Reported first, least significant byte first, as the standard's three
   * 16-bit fields. */
  TEST_ASSERT_EQUAL_HEX8(0x34u, block[2]);
  TEST_ASSERT_EQUAL_HEX8(0x12u, block[3]);
  TEST_ASSERT_EQUAL_HEX8(0x78u, block[4]);
  TEST_ASSERT_EQUAL_HEX8(0x56u, block[5]);

  /* Then cleared, so the next read reports the interval and not the lifetime. */
  TEST_ASSERT_EQUAL_HEX16(0u, q.data_errors);
  TEST_ASSERT_EQUAL_HEX16(0u, q.underruns);
  TEST_ASSERT_TRUE(ap_qic_command(&q, AP_QIC_CMD_READ_STATUS));
  TEST_ASSERT_TRUE(ap_qic_read_status(&q, block));
  TEST_ASSERT_EQUAL_HEX8(0u, block[2]);
  TEST_ASSERT_EQUAL_HEX8(0u, block[3]);
  TEST_ASSERT_EQUAL_HEX8(0u, block[4]);
  TEST_ASSERT_EQUAL_HEX8(0u, block[5]);
}

/* The rows this model cannot be put into, named so the omission above is a
 * statement rather than a gap. **The list is shorter than it was**, and the two
 * that left it left for different reasons.
 *
 * `ill` is latched now, so "Illegal command" is a tested row above rather than
 * the real gap this comment used to record. And three of the four read-error
 * rows are reachable: `QIC-02 Rev D` §5.4 item 8 defines "READ ERROR, NO DATA"
 * as "No recorded data found on tape", which is precisely a read past the last
 * block of a `.ct` and needs no medium under it. Rows 8, 9 and 10 differ only in
 * `EOM` and `BOM`, which come from the position.
 *
 * What remains genuinely out of reach is smaller and sharper: "Read or write
 * abort" and "Read error, bad block xfer" need a block that is *there* and
 * unreadable; "Filemark read" needs file marks, which a raw block image has
 * none of; "Marginal block detected" needs a retry count. "Drive not ready" and
 * "No drive" are controller-generated conditions for hardware that is absent
 * rather than empty, which this model expresses as an unselected drive. Each
 * needs a medium model rather than a status bit, and that is a statement about
 * the `.ct` format, not about `ap_qic`. */

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_cartridge_type_must_be_supplied);
  RUN_TEST(test_a_freshly_reset_drive_is_already_selected);
  RUN_TEST(test_a_drive_other_than_the_selected_one_does_not_move);
  RUN_TEST(test_selecting_an_absent_drive_reports_the_no_drive_row);
  RUN_TEST(test_a_select_naming_no_drive_or_two_is_illegal);
  RUN_TEST(test_deselecting_a_drive_away_from_bot_is_illegal);
  RUN_TEST(test_selection_is_sticky);
  RUN_TEST(test_a_plain_select_clears_the_soft_lock);
  RUN_TEST(test_a_locked_cartridge_cannot_be_ejected);
  RUN_TEST(test_a_reset_unlocks_but_does_not_eject_or_deselect);
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
  RUN_TEST(test_a_read_past_the_last_block_reports_no_data_and_end_of_media);
  RUN_TEST(test_the_no_data_latch_is_reset_by_the_status_read);
  RUN_TEST(test_the_two_status_counters_are_cleared_by_the_status_read);
  return UNITY_END();
}

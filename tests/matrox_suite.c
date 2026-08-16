/* DN4500 Matrox graphics controller. Every test replays something the board's
 * own option ROM does, at the ROM address `docs/references/GRAPHICS.md` cites
 * for it -- the same discipline `ring_ctl_suite` follows, and for the same
 * reason: no register-level document for this board exists on disk, on the web,
 * or in any oracle. */

#include "unity.h"

#include "board/ap_board.h"
#include "device/ap_matrox.h"

void setUp(void) {}
void tearDown(void) {}

/* Finding 5's three bases, and that nothing else answers. The extents are the
 * smallest that cover every observed access, which is stated in the header as
 * a limit of the evidence rather than a measurement -- so what this pins is
 * that the *bases* decode and that an address outside them does not. */
static void test_the_three_blocks_decode_and_nothing_else_does(void) {
  uint32_t block = 0;
  uint32_t offset = 0;

  TEST_ASSERT_TRUE(ap_matrox_decode(AP_MATROX_DATA_ADDR, &block, &offset));
  TEST_ASSERT_EQUAL_HEX32(AP_MATROX_DATA_ADDR, block);
  TEST_ASSERT_EQUAL_UINT32(0u, offset);

  /* `$DA0006` is the status byte the firmware polls, and it must resolve to
   * offset 6 of the control block -- finding 5a proved `$DA0000` is a base by
   * catching the firmware take it as one. */
  TEST_ASSERT_TRUE(ap_matrox_decode(0x00DA0006u, &block, &offset));
  TEST_ASSERT_EQUAL_HEX32(AP_MATROX_CTL_ADDR, block);
  TEST_ASSERT_EQUAL_UINT32(6u, offset);

  TEST_ASSERT_TRUE(ap_matrox_decode(0x00D80008u, &block, &offset));
  TEST_ASSERT_EQUAL_HEX32(AP_MATROX_XFER_ADDR, block);
  TEST_ASSERT_EQUAL_UINT32(8u, offset);

  TEST_ASSERT_FALSE(ap_matrox_decode(0x00D30000u, NULL, NULL));
  TEST_ASSERT_FALSE(ap_matrox_decode(0x00DB0000u, NULL, NULL));
  TEST_ASSERT_FALSE(
      ap_matrox_decode(AP_MATROX_CTL_ADDR + AP_MATROX_CTL_RANGE, NULL, NULL));
}

/* **The verdict the firmware computes, and both ways it can fail.**
 *
 * The routine ending at `$5E0` puts `0` in `d3` for pass and `$FFFF` for fail,
 * and reaching the pass arm needs two things of `$DA0006`: bit 3 clear (polled
 * at `$59E` with a 15,728,640-iteration budget) and bit 6 clear (tested once at
 * `$5B8`). A model answering either bit set fails the board's own test, so
 * this is the assertion the whole device exists to satisfy. */
static void test_the_status_byte_reads_the_two_bits_the_rom_requires_clear(
    void) {
  ap_matrox_t matrox;
  ap_matrox_reset(&matrox);

  const uint8_t status = ap_matrox_read8(&matrox, AP_MATROX_CTL_ADDR, 6u);
  TEST_ASSERT_EQUAL_HEX8(0u, status & AP_MATROX_STATUS_BUSY);
  TEST_ASSERT_EQUAL_HEX8(0u, status & AP_MATROX_STATUS_ERROR);

  /* **Bit 5 is set, and the firmware is why.** `$2EC`-`$310` polls it and
   * leaves early on `bne`, so a set bit ends that wait -- the opposite of bits
   * 3 and 6, and reached only once a display is fitted (`GRAPHICS.md` 13c). */
  TEST_ASSERT_EQUAL_HEX8(AP_MATROX_STATUS_READY,
                         status & AP_MATROX_STATUS_READY);

  /* And nothing beyond those three: bit 4's `btst` at `$3BA` has not been
   * reached, so its polarity is unmeasured and a set bit would be an
   * invention. `RING.md` 62's rule, one bit at a time. */
  TEST_ASSERT_EQUAL_HEX8(AP_MATROX_STATUS_READY, status);
}

/* Finding 4b, and the arithmetic that identifies it: `$504` writes **2358
 * words** from ROM `+B22` to `$DA0000`, which is never incremented, and
 * `$B22 + 2358x2 = $1D8E` is exactly the `length` in the ROM header. So the
 * image runs to the last byte of the checksummed ROM.
 *
 * The port is checked to *accept* every word without the count drifting --
 * nothing executes the microcode, and a port that stalled or double-counted is
 * the failure that would show as the firmware retrying. */
static void test_the_microcode_port_accepts_the_whole_image(void) {
  ap_matrox_t matrox;
  ap_matrox_reset(&matrox);

  const unsigned words = 2358u;
  for (unsigned i = 0; i < words; i++) {
    /* A word arrives as two byte accesses on this bus; the odd half completes
     * it, exactly as the ring's data port does (`RING.md` 61). */
    ap_matrox_write8(&matrox, AP_MATROX_CTL_ADDR, 0u, (uint8_t)(i >> 8));
    ap_matrox_write8(&matrox, AP_MATROX_CTL_ADDR, 1u, (uint8_t)i);
  }
  TEST_ASSERT_EQUAL_UINT(words, matrox.microcode_words);

  /* 2358 words is 4,716 bytes, and `+B22 + 4716` is the header's `length`. The
   * test states the relation rather than the constant so a later reader can
   * check it against the ROM. */
  TEST_ASSERT_EQUAL_UINT32(0x1D8Eu, 0xB22u + words * 2u);
}

/* Finding 7's data port, which the firmware writes four literal words to at
 * `$578`-`$590` (`$5AA5 $A534 $1744 $1345`) and reads back at `$5D6`. The
 * read-back loop discards every value, so what it answers is unconstrained --
 * what *is* checked here is that the port is a port: a word written arrives
 * whole across the two byte halves. */
static void test_the_data_port_latches_a_word_across_both_halves(void) {
  ap_matrox_t matrox;
  ap_matrox_reset(&matrox);

  ap_matrox_write8(&matrox, AP_MATROX_DATA_ADDR, 0u, 0x5Au);
  ap_matrox_write8(&matrox, AP_MATROX_DATA_ADDR, 1u, 0xA5u);
  TEST_ASSERT_EQUAL_HEX8(0x5Au,
                         ap_matrox_read8(&matrox, AP_MATROX_DATA_ADDR, 0u));
  TEST_ASSERT_EQUAL_HEX8(0xA5u,
                         ap_matrox_read8(&matrox, AP_MATROX_DATA_ADDR, 1u));
}

/* Finding 8's transfer block: `+5` is armed with `$80` at `$298`, a longword
 * goes to `+8` (`$334`, and `$FFFFFFFF` at `$35C`), and `+4` bit 7 is polled
 * after each. The ready bit reads **clear**, which is what lets the transfer
 * complete rather than hang -- the branch it feeds has not been traced, so this
 * is the restraint the status byte uses, recorded as such. */
static void test_the_transfer_block_takes_a_longword_and_reads_ready(void) {
  ap_matrox_t matrox;
  ap_matrox_reset(&matrox);

  ap_matrox_write8(&matrox, AP_MATROX_XFER_ADDR, 5u, 0x80u);
  TEST_ASSERT_TRUE(matrox.transfer_armed);

  const uint8_t bytes[4] = {0xFFu, 0xFFu, 0xFFu, 0xFFu};
  for (unsigned i = 0; i < 4u; i++) {
    ap_matrox_write8(&matrox, AP_MATROX_XFER_ADDR, 8u + i, bytes[i]);
  }
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, matrox.last_transfer);
  TEST_ASSERT_EQUAL_HEX8(
      0u, ap_matrox_read8(&matrox, AP_MATROX_XFER_ADDR, 4u) & 0x80u);
}

/* **Absent until fitted, and the empty slot is what every existing boot
 * measures.** The three blocks live inside AT bus *memory* space, so with no
 * card the addresses must still fall through to the window and read `FF` --
 * the same rule the ring and the EtherLink Plus follow, and the reason none of
 * them disturbs the reference boot. */
static void test_the_board_answers_only_when_the_card_is_fitted(void) {
  static uint8_t ram[4u * 1024u * 1024u];
  ap_board_t board;
  const ap_mc146818_time_t epoch = {0};
  bool ok = false;
  TEST_ASSERT_TRUE(ap_board_init(&board, ram, sizeof ram, &epoch, 0x012345u));

  TEST_ASSERT_EQUAL_INT(AP_BOARD_REGION_ATBUS,
                        ap_board_region(&board, AP_MATROX_CTL_ADDR + 6u));
  TEST_ASSERT_EQUAL_HEX8(
      0xFFu, ap_board_read(&board, AP_MATROX_CTL_ADDR + 6u, &ok));
  TEST_ASSERT_TRUE(ok);

  ap_board_attach_matrox(&board, true);
  TEST_ASSERT_EQUAL_INT(AP_BOARD_REGION_MATROX,
                        ap_board_region(&board, AP_MATROX_CTL_ADDR + 6u));
  /* And now the status the ROM requires, through a **bus** read rather than a
   * direct device call -- the distinction `ETHERNET.md` finding 10's test
   * makes, because a device that answers only when called directly is not
   * wired. */
  TEST_ASSERT_EQUAL_HEX8(AP_MATROX_STATUS_READY,
                         ap_board_read(&board, AP_MATROX_CTL_ADDR + 6u, &ok));
  TEST_ASSERT_TRUE(ok);

  ap_board_attach_matrox(&board, false);
  TEST_ASSERT_EQUAL_INT(AP_BOARD_REGION_ATBUS,
                        ap_board_region(&board, AP_MATROX_CTL_ADDR + 6u));
}

/* **The frame's geometry, which is the board's own arithmetic and not ours.**
 * `GRAPHICS.md` 20 reads all four numbers out of `ENTRY_03`'s clear loop --
 * `#$27` longwords is 160 bytes a line, `#$60` skipped makes the stride,
 * `#$3ff` is the line count -- and finding 21 finds the same stride a second
 * time in the glyph routine, where `d2` reaches 256 by `addq.l #$2` on `$FE`.
 *
 * These relationships are asserted rather than the constants restated, so a
 * change that moved one without the others fails here instead of shearing a
 * picture months later. */
static void test_the_frame_geometry_is_the_roms_own_arithmetic(void) {
  /* 1280 visible pixels at one bit each is the 160 bytes the loop clears. */
  TEST_ASSERT_EQUAL_UINT(160u, AP_MATROX_FRAME_WIDTH / 8u);
  /* Plus the 96 the loop skips: `moveq #$60, d2`. */
  TEST_ASSERT_EQUAL_UINT(AP_MATROX_FRAME_STRIDE_BYTES,
                         (AP_MATROX_FRAME_WIDTH / 8u) + 0x60u);
  /* And the whole region is one stride per line, which is `[S3K]` §10.2's
   * "256-KB image memory" for a 1280x1024 monochrome controller. */
  TEST_ASSERT_EQUAL_UINT(AP_MATROX_FRAME_BYTES,
                         AP_MATROX_FRAME_STRIDE_BYTES * AP_MATROX_FRAME_HEIGHT);
  TEST_ASSERT_EQUAL_UINT(0x40000u, AP_MATROX_FRAME_BYTES);
  /* The base is inside `019411-A00` Table 2-5's `100000`-`FFFFFF`, "AT
   * COMPATIBLE BUS MEMORY SPACE", which is where a card's aperture belongs. */
  TEST_ASSERT_TRUE(AP_MATROX_FRAME_ADDR >= 0x100000u);
  TEST_ASSERT_TRUE(AP_MATROX_FRAME_ADDR + AP_MATROX_FRAME_BYTES <= 0x1000000u);
}

/* A pixel's byte is `row * stride + column / 8`, which is what the glyph
 * routine computes at `$450`-`$460`: the column through `and.w #$f` / `lsr.w
 * #$4` / `lsl.w #$1`, and the row as `(d7 & $FFFF0000) >> 8`. Written through
 * the device so the decode is exercised with it. */
static void test_a_pixel_lands_where_the_stride_puts_it(void) {
  ap_matrox_t matrox;
  static uint8_t frame[AP_MATROX_FRAME_BYTES];
  ap_matrox_reset(&matrox);
  ap_matrox_attach_frame(&matrox, frame, sizeof frame);

  /* Row 3, the first byte of the line. */
  const uint32_t at = 3u * AP_MATROX_FRAME_STRIDE_BYTES;
  uint32_t block = 0, offset = 0;
  TEST_ASSERT_TRUE(ap_matrox_decode(AP_MATROX_FRAME_ADDR + at, &block, &offset));
  TEST_ASSERT_EQUAL_HEX32(AP_MATROX_FRAME_ADDR, block);
  TEST_ASSERT_EQUAL_UINT(at, offset);
  ap_matrox_write8(&matrox, block, offset, 0x81u);
  TEST_ASSERT_EQUAL_HEX8(0x81u, frame[at]);

  /* The last visible byte of that line, and the first byte of the gap that
   * follows it -- the 96 bytes no pixel can reach. */
  TEST_ASSERT_EQUAL_UINT(160u, AP_MATROX_FRAME_WIDTH / 8u);
  TEST_ASSERT_EQUAL_HEX8(0u, frame[at + 160u]);

  /* One byte past the whole region does not decode. */
  TEST_ASSERT_FALSE(ap_matrox_decode(
      AP_MATROX_FRAME_ADDR + AP_MATROX_FRAME_BYTES, &block, &offset));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_three_blocks_decode_and_nothing_else_does);
  RUN_TEST(test_the_status_byte_reads_the_two_bits_the_rom_requires_clear);
  RUN_TEST(test_the_microcode_port_accepts_the_whole_image);
  RUN_TEST(test_the_data_port_latches_a_word_across_both_halves);
  RUN_TEST(test_the_transfer_block_takes_a_longword_and_reads_ready);
  RUN_TEST(test_the_board_answers_only_when_the_card_is_fitted);
  RUN_TEST(test_the_frame_geometry_is_the_roms_own_arithmetic);
  RUN_TEST(test_a_pixel_lands_where_the_stride_puts_it);
  return UNITY_END();
}

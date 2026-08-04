/* The Apollo floppy image, and `[OMTI]` §6.3's command set on top of it.
 *
 * The image here is a real one: `.afd` has exactly one geometry, so unlike the
 * Winchester's suite there is no small stand-in to build -- a 1,261,568-byte
 * buffer is the only thing `ap_afd_open` accepts, and that is the fact under
 * test as much as it is the fixture.
 */

#include "unity.h"

#include <stdlib.h>
#include <string.h>

#include "device/ap_omti.h"
#include "image/ap_afd.h"

void setUp(void) {}
void tearDown(void) {}

static uint8_t *backing = nullptr;
static ap_afd_t floppy;
static ap_omti_t omti;

/* Every sector filled with its own linear number, so a read landing on the
 * wrong sector is visible rather than merely different. */
static void build_floppy(bool writable) {
  backing = malloc(AP_AFD_BYTES);
  TEST_ASSERT_NOT_NULL(backing);
  for (unsigned s = 0; s < AP_AFD_BYTES / AP_AFD_SECTOR_BYTES; s++) {
    memset(&backing[s * AP_AFD_SECTOR_BYTES], (int)(s & 0xFFu),
           AP_AFD_SECTOR_BYTES);
  }
  TEST_ASSERT_TRUE(ap_afd_open(&floppy, backing, AP_AFD_BYTES, writable));
}

static void release_floppy(void) {
  free(backing);
  backing = nullptr;
}

/* A controller out of reset, with the diskette attached. Coming out of reset is
 * what arms the command phase, so it is part of every fixture rather than a
 * thing one test does. */
static void build_controller(void) {
  ap_omti_reset(&omti);
  ap_omti_attach_floppy(&omti, &floppy);
  ap_omti_fdc_write(&omti, AP_OMTI_FDC_DOR, AP_OMTI_DOR_NOT_RESET);
}

static void send(const uint8_t *bytes, unsigned count) {
  for (unsigned i = 0; i < count; i++) {
    ap_omti_fdc_write(&omti, AP_OMTI_FDC_DATA, bytes[i]);
  }
}

static uint8_t take(void) {
  return ap_omti_fdc_read(&omti, AP_OMTI_FDC_DATA);
}

static uint8_t status(void) {
  return ap_omti_fdc_read(&omti, AP_OMTI_FDC_MSR);
}

/* Take whatever is left of a result phase. A driver must do this before the
 * next command: the controller stays busy until the last result byte is read,
 * and command bytes written to it meanwhile go nowhere. */
static void drain(void) {
  while (ap_omti_fdc_phase(&omti) != AP_OMTI_PHASE_IDLE) {
    (void)take();
  }
}

/* ---- The image ----------------------------------------------------------- */

/* `apollo_dsk.cpp` gives one format and the extension names it, so a file of
 * another length is a different thing rather than a short one. */
static void test_an_afd_image_is_exactly_one_floppy_or_it_is_refused(void) {
  build_floppy(true);
  ap_afd_t other;
  TEST_ASSERT_FALSE(ap_afd_open(&other, backing, AP_AFD_BYTES - 1u, true));
  TEST_ASSERT_FALSE(ap_afd_open(&other, backing, AP_AFD_BYTES + 1u, true));
  TEST_ASSERT_TRUE(ap_afd_open(&other, backing, AP_AFD_BYTES, true));
  release_floppy();
}

static void test_the_apollo_floppy_is_77_cylinders_of_2_heads_of_8_sectors(void) {
  TEST_ASSERT_EQUAL_UINT(77u, AP_AFD_CYLINDERS);
  TEST_ASSERT_EQUAL_UINT(2u, AP_AFD_HEADS);
  TEST_ASSERT_EQUAL_UINT(8u, AP_AFD_SECTORS);
  TEST_ASSERT_EQUAL_UINT(1024u, AP_AFD_SECTOR_BYTES);
  TEST_ASSERT_EQUAL_UINT(1261568u, AP_AFD_BYTES);
}

/* §6.2's `R` is "the sector number", and the floppy convention numbers sectors
 * from one while cylinders and heads count from zero. A reader treating `R` as
 * an index is off by one sector on every access. */
static void test_sector_numbering_starts_at_one_and_sector_zero_is_refused(void) {
  uint32_t lba = 0u;
  TEST_ASSERT_FALSE(ap_afd_lba(0u, 0u, 0u, &lba));
  TEST_ASSERT_TRUE(ap_afd_lba(0u, 0u, 1u, &lba));
  TEST_ASSERT_EQUAL_UINT32(0u, lba);
  TEST_ASSERT_TRUE(ap_afd_lba(0u, 0u, 8u, &lba));
  TEST_ASSERT_EQUAL_UINT32(7u, lba);
  /* And nine does not exist, rather than rolling onto the next track. */
  TEST_ASSERT_FALSE(ap_afd_lba(0u, 0u, 9u, &lba));
}

/* Head varies fastest, then cylinder: the ordinary CHS mapping. */
static void test_the_second_head_follows_the_first_cylinders_sectors(void) {
  uint32_t lba = 0u;
  TEST_ASSERT_TRUE(ap_afd_lba(0u, 1u, 1u, &lba));
  TEST_ASSERT_EQUAL_UINT32(8u, lba);
  TEST_ASSERT_TRUE(ap_afd_lba(1u, 0u, 1u, &lba));
  TEST_ASSERT_EQUAL_UINT32(16u, lba);
  /* The last sector of the disk. */
  TEST_ASSERT_TRUE(ap_afd_lba(76u, 1u, 8u, &lba));
  TEST_ASSERT_EQUAL_UINT32(77u * 2u * 8u - 1u, lba);
}

static void test_a_cylinder_or_head_past_the_drive_is_refused(void) {
  uint32_t lba = 0u;
  TEST_ASSERT_FALSE(ap_afd_lba(77u, 0u, 1u, &lba));
  TEST_ASSERT_FALSE(ap_afd_lba(0u, 2u, 1u, &lba));
}

static void test_a_read_only_image_refuses_a_write_and_keeps_its_data(void) {
  build_floppy(false);
  uint8_t sector[AP_AFD_SECTOR_BYTES];
  memset(sector, 0xA5, sizeof sector);
  TEST_ASSERT_FALSE(ap_afd_write(&floppy, 0u, sector));
  TEST_ASSERT_TRUE(ap_afd_read(&floppy, 0u, sector));
  TEST_ASSERT_EQUAL_UINT8(0u, sector[0]);
  release_floppy();
}

/* ---- The command phase --------------------------------------------------- */

/* §6.3's lengths, which are what tells the controller when a command has
 * finished arriving. Getting one wrong desynchronises every command after it. */
static void test_each_command_takes_the_number_of_bytes_section_6_3_gives_it(void) {
  TEST_ASSERT_EQUAL_UINT(9u, ap_omti_fdc_command_bytes(AP_OMTI_FDC_READ_DATA));
  TEST_ASSERT_EQUAL_UINT(9u, ap_omti_fdc_command_bytes(AP_OMTI_FDC_SCAN_EQUAL));
  TEST_ASSERT_EQUAL_UINT(
      9u, ap_omti_fdc_command_bytes(AP_OMTI_FDC_SCAN_LOW_EQUAL));
  TEST_ASSERT_EQUAL_UINT(
      9u, ap_omti_fdc_command_bytes(AP_OMTI_FDC_SCAN_HIGH_EQUAL));
  TEST_ASSERT_EQUAL_UINT(6u,
                         ap_omti_fdc_command_bytes(AP_OMTI_FDC_FORMAT_TRACK));
  TEST_ASSERT_EQUAL_UINT(3u, ap_omti_fdc_command_bytes(AP_OMTI_FDC_SPECIFY));
  TEST_ASSERT_EQUAL_UINT(3u, ap_omti_fdc_command_bytes(AP_OMTI_FDC_SEEK));
  TEST_ASSERT_EQUAL_UINT(2u,
                         ap_omti_fdc_command_bytes(AP_OMTI_FDC_RECALIBRATE));
  TEST_ASSERT_EQUAL_UINT(2u,
                         ap_omti_fdc_command_bytes(AP_OMTI_FDC_SENSE_DRIVE));
  TEST_ASSERT_EQUAL_UINT(
      1u, ap_omti_fdc_command_bytes(AP_OMTI_FDC_SENSE_INTERRUPT));
}

/* The three modifier bits sit above the opcode and must not change its length
 * or its identity -- a multitrack MFM read is still a nine-byte READ DATA. */
static void test_the_mt_mf_and_sk_bits_do_not_change_the_command(void) {
  const uint8_t decorated = AP_OMTI_FDC_READ_DATA | AP_OMTI_FDC_MT |
                            AP_OMTI_FDC_MF | AP_OMTI_FDC_SK;
  TEST_ASSERT_EQUAL_UINT(9u, ap_omti_fdc_command_bytes(decorated));
  TEST_ASSERT_EQUAL_UINT(7u, ap_omti_fdc_result_bytes(decorated));
}

/* Three commands produce nothing at all. A driver that waits for a result byte
 * after a SEEK waits forever, which is why this is stated rather than assumed
 * from the other eight. */
static void test_specify_seek_and_recalibrate_have_no_result_phase(void) {
  TEST_ASSERT_EQUAL_UINT(0u, ap_omti_fdc_result_bytes(AP_OMTI_FDC_SPECIFY));
  TEST_ASSERT_EQUAL_UINT(0u, ap_omti_fdc_result_bytes(AP_OMTI_FDC_SEEK));
  TEST_ASSERT_EQUAL_UINT(0u, ap_omti_fdc_result_bytes(AP_OMTI_FDC_RECALIBRATE));
  TEST_ASSERT_EQUAL_UINT(2u,
                         ap_omti_fdc_result_bytes(AP_OMTI_FDC_SENSE_INTERRUPT));
  TEST_ASSERT_EQUAL_UINT(1u, ap_omti_fdc_result_bytes(AP_OMTI_FDC_SENSE_DRIVE));
  TEST_ASSERT_EQUAL_UINT(7u, ap_omti_fdc_result_bytes(AP_OMTI_FDC_READ_DATA));
}

/* Bit 2 of the Digital Output register is the floppy side's reset, and it runs
 * the opposite way to every other control bit: clearing the register asserts
 * it. Held there, the data register does not accept a command. */
static void test_the_command_phase_is_inert_while_the_floppy_is_in_reset(void) {
  build_floppy(true);
  ap_omti_reset(&omti);
  ap_omti_attach_floppy(&omti, &floppy);
  TEST_ASSERT_TRUE(ap_omti_fdc_in_reset(&omti));
  ap_omti_fdc_write(&omti, AP_OMTI_FDC_DATA, AP_OMTI_FDC_SENSE_INTERRUPT);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_IDLE, ap_omti_fdc_phase(&omti));
  /* Out of reset, the same byte starts a command. */
  ap_omti_fdc_write(&omti, AP_OMTI_FDC_DOR, AP_OMTI_DOR_NOT_RESET);
  TEST_ASSERT_EQUAL_UINT8(AP_OMTI_MSR_RQM, status());
  release_floppy();
}

/* §6.3.1: nine command bytes, then 1024 data bytes, then seven result bytes.
 * The data must be the sector the C/H/R name and no other. */
static void test_read_data_returns_the_sector_its_chr_names(void) {
  build_floppy(true);
  build_controller();
  /* Cylinder 1, head 1, sector 3 -- linear sector 1*16 + 8 + 2 = 26. */
  const uint8_t command[] = {AP_OMTI_FDC_READ_DATA | AP_OMTI_FDC_MF,
                             0x04u, /* HD 1, unit 0 */
                             1u,    /* C */
                             1u,    /* H */
                             3u,    /* R */
                             3u,    /* N: 1024 bytes */
                             8u,    /* EOT */
                             0x1Bu, /* GPL */
                             0xFFu /* DTL */};
  send(command, sizeof command);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_IN, ap_omti_fdc_phase(&omti));
  /* Direction reverses for the data phase. */
  TEST_ASSERT_TRUE((status() & AP_OMTI_MSR_DIO) != 0u);

  for (unsigned i = 0; i < AP_AFD_SECTOR_BYTES; i++) {
    TEST_ASSERT_EQUAL_UINT8(26u, take());
  }
  /* The last data byte hands over to the result phase by itself. */
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_fdc_phase(&omti));
  TEST_ASSERT_EQUAL_UINT8(AP_OMTI_ST0_IC_NORMAL, take()); /* ST0 */
  TEST_ASSERT_EQUAL_UINT8(0u, take());                    /* ST1 */
  TEST_ASSERT_EQUAL_UINT8(0u, take());                    /* ST2 */
  TEST_ASSERT_EQUAL_UINT8(1u, take());                    /* C */
  TEST_ASSERT_EQUAL_UINT8(1u, take());                    /* H */
  TEST_ASSERT_EQUAL_UINT8(3u, take());                    /* R */
  TEST_ASSERT_EQUAL_UINT8(3u, take());                    /* N */
  /* And the controller is ready for the next command. */
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_IDLE, ap_omti_fdc_phase(&omti));
  TEST_ASSERT_EQUAL_UINT8(AP_OMTI_MSR_RQM, status());
  release_floppy();
}

/* A cylinder the drive does not have is No Data, not a sector of zeroes: ST1's
 * ND is "set if the controller cannot find the sector specified". */
static void test_a_read_past_the_last_cylinder_reports_no_data(void) {
  build_floppy(true);
  build_controller();
  const uint8_t command[] = {AP_OMTI_FDC_READ_DATA, 0u, 77u, 0u, 1u,
                             3u,                    8u, 0x1Bu, 0xFFu};
  send(command, sizeof command);
  /* No data phase at all -- straight to the result. */
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_fdc_phase(&omti));
  TEST_ASSERT_EQUAL_UINT8(AP_OMTI_ST0_IC_ABRUPT, take());
  TEST_ASSERT_EQUAL_UINT8(AP_OMTI_ST1_NO_DATA, take());
  release_floppy();
}

/* An empty drive is not a blank one. Nothing is attached, so the sector cannot
 * be found *and* the address mark is missing -- which is what distinguishes it
 * from a disk whose track 77 does not exist. */
static void test_a_read_with_no_diskette_reports_a_missing_address_mark(void) {
  ap_omti_reset(&omti);
  ap_omti_attach_floppy(&omti, nullptr);
  ap_omti_fdc_write(&omti, AP_OMTI_FDC_DOR, AP_OMTI_DOR_NOT_RESET);
  const uint8_t command[] = {AP_OMTI_FDC_READ_DATA, 0u, 0u, 0u, 1u,
                             3u,                    8u, 0x1Bu, 0xFFu};
  send(command, sizeof command);
  TEST_ASSERT_EQUAL_UINT8(AP_OMTI_ST0_IC_ABRUPT, take());
  TEST_ASSERT_EQUAL_UINT8(AP_OMTI_ST1_NO_DATA | AP_OMTI_ST1_MISSING_MARK,
                          take());
}

/* §6.3.10 and §6.3.7 are a pair: SEEK reports nothing, and the driver learns
 * where the head went by asking. */
static void test_a_seek_reports_nothing_until_sense_interrupt_status_asks(void) {
  build_floppy(true);
  build_controller();
  const uint8_t seek[] = {AP_OMTI_FDC_SEEK, 0u, 40u};
  send(seek, sizeof seek);
  /* No result phase: idle immediately. */
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_IDLE, ap_omti_fdc_phase(&omti));

  ap_omti_fdc_write(&omti, AP_OMTI_FDC_DATA, AP_OMTI_FDC_SENSE_INTERRUPT);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_fdc_phase(&omti));
  TEST_ASSERT_EQUAL_UINT8(AP_OMTI_ST0_IC_NORMAL | AP_OMTI_ST0_SEEK_END, take());
  TEST_ASSERT_EQUAL_UINT8(40u, take()); /* PCN */
  release_floppy();
}

/* Asked a second time, with no seek outstanding, it is the invalid case. That
 * is how a driver ends its polling loop after a reset rather than by counting
 * iterations. */
static void test_sense_interrupt_status_with_no_seek_pending_is_invalid(void) {
  build_floppy(true);
  build_controller();
  const uint8_t seek[] = {AP_OMTI_FDC_SEEK, 0u, 40u};
  send(seek, sizeof seek);
  ap_omti_fdc_write(&omti, AP_OMTI_FDC_DATA, AP_OMTI_FDC_SENSE_INTERRUPT);
  (void)take();
  (void)take();
  /* Again. */
  ap_omti_fdc_write(&omti, AP_OMTI_FDC_DATA, AP_OMTI_FDC_SENSE_INTERRUPT);
  TEST_ASSERT_EQUAL_UINT8(AP_OMTI_ST0_IC_INVALID, take());
  release_floppy();
}

/* §6.3.6 steps to track 0, and ST3's Track 0 bit is how the drive says it got
 * there. */
static void test_recalibrate_puts_the_head_on_track_zero(void) {
  build_floppy(true);
  build_controller();
  const uint8_t seek[] = {AP_OMTI_FDC_SEEK, 0u, 40u};
  send(seek, sizeof seek);
  ap_omti_fdc_write(&omti, AP_OMTI_FDC_DATA, AP_OMTI_FDC_SENSE_INTERRUPT);
  (void)take();
  TEST_ASSERT_EQUAL_UINT8(40u, take());

  const uint8_t recalibrate[] = {AP_OMTI_FDC_RECALIBRATE, 0u};
  send(recalibrate, sizeof recalibrate);
  ap_omti_fdc_write(&omti, AP_OMTI_FDC_DATA, AP_OMTI_FDC_SENSE_INTERRUPT);
  TEST_ASSERT_EQUAL_UINT8(AP_OMTI_ST0_IC_NORMAL | AP_OMTI_ST0_SEEK_END, take());
  TEST_ASSERT_EQUAL_UINT8(0u, take());

  const uint8_t sense[] = {AP_OMTI_FDC_SENSE_DRIVE, 0u};
  send(sense, sizeof sense);
  const uint8_t st3 = take();
  TEST_ASSERT_TRUE((st3 & AP_OMTI_ST3_TRACK_0) != 0u);
  /* "Bit 0 - Not used - always 1", the one constant bit in the four status
   * registers. */
  TEST_ASSERT_TRUE((st3 & AP_OMTI_ST3_ALWAYS) != 0u);
  release_floppy();
}

/* A read-only image is a write-protected diskette, and ST3 bit 6 is where a
 * driver looks before trying to format. */
static void test_a_read_only_diskette_reads_as_write_protected(void) {
  build_floppy(false);
  build_controller();
  const uint8_t sense[] = {AP_OMTI_FDC_SENSE_DRIVE, 0u};
  send(sense, sizeof sense);
  TEST_ASSERT_TRUE((take() & AP_OMTI_ST3_WRITE_PROTECT) != 0u);
  release_floppy();
}

/* §6.3.11: "the issued command was never started". `05` is WRITE DATA on a
 * generic 765 and is *not* in this controller's set, so it takes this path --
 * the case that would be silently wrong if the command had been invented from
 * general 765 knowledge. */
static void test_an_unlisted_command_including_write_data_is_invalid(void) {
  build_floppy(true);
  build_controller();
  ap_omti_fdc_write(&omti, AP_OMTI_FDC_DATA, 0x05u);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_fdc_phase(&omti));
  TEST_ASSERT_EQUAL_UINT8(AP_OMTI_ST0_IC_INVALID, take());
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_IDLE, ap_omti_fdc_phase(&omti));
  release_floppy();
}

/* §6.3.3's data phase runs towards the controller: the host sends the bytes to
 * compare and ST2 carries the verdict. */
static void test_scan_equal_reports_a_hit_when_every_byte_matches(void) {
  build_floppy(true);
  build_controller();
  /* Cylinder 0, head 0, sector 1 is linear sector 0, filled with zero. */
  const uint8_t command[] = {AP_OMTI_FDC_SCAN_EQUAL, 0u, 0u,    0u, 1u,
                             3u,                     8u, 0x1Bu, 1u};
  send(command, sizeof command);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_OUT, ap_omti_fdc_phase(&omti));
  /* Towards the controller, so DIO is clear. */
  TEST_ASSERT_TRUE((status() & AP_OMTI_MSR_DIO) == 0u);
  for (unsigned i = 0; i < AP_AFD_SECTOR_BYTES; i++) {
    ap_omti_fdc_write(&omti, AP_OMTI_FDC_DATA, 0u);
  }
  TEST_ASSERT_EQUAL_UINT8(AP_OMTI_ST0_IC_NORMAL, take());
  (void)take(); /* ST1 */
  TEST_ASSERT_EQUAL_UINT8(AP_OMTI_ST2_SCAN_HIT, take());
  release_floppy();
}

/* One byte out of a thousand is enough to take the hit away. */
static void test_a_single_mismatched_byte_leaves_the_scan_not_satisfied(void) {
  build_floppy(true);
  build_controller();
  const uint8_t command[] = {AP_OMTI_FDC_SCAN_EQUAL, 0u, 0u,    0u, 1u,
                             3u,                     8u, 0x1Bu, 1u};
  send(command, sizeof command);
  for (unsigned i = 0; i < AP_AFD_SECTOR_BYTES; i++) {
    ap_omti_fdc_write(&omti, AP_OMTI_FDC_DATA, i == 500u ? 0xFFu : 0u);
  }
  (void)take();
  (void)take();
  TEST_ASSERT_EQUAL_UINT8(AP_OMTI_ST2_SCAN_NOT_SATISFIED, take());
  release_floppy();
}

/* §6.3.4 and §6.3.5 differ from §6.3.3 only in the comparison, and the media
 * byte is the left-hand side of it. Sector 2 of track 0 is filled with 1, so a
 * host byte of 1 satisfies all three and a host byte of 0 satisfies only
 * "high or equal". */
static void test_the_low_and_high_scans_compare_the_media_against_the_host(void) {
  build_floppy(true);
  build_controller();
  /* Media 1 >= host 0: high-or-equal is satisfied. */
  const uint8_t high[] = {AP_OMTI_FDC_SCAN_HIGH_EQUAL, 0u, 0u,    0u, 2u,
                          3u,                          8u, 0x1Bu, 1u};
  send(high, sizeof high);
  for (unsigned i = 0; i < AP_AFD_SECTOR_BYTES; i++) {
    ap_omti_fdc_write(&omti, AP_OMTI_FDC_DATA, 0u);
  }
  (void)take();
  (void)take();
  TEST_ASSERT_EQUAL_UINT8(AP_OMTI_ST2_SCAN_HIT, take());
  drain();

  /* Media 1 <= host 0 is false, so low-or-equal is not. */
  const uint8_t low[] = {AP_OMTI_FDC_SCAN_LOW_EQUAL, 0u, 0u,    0u, 2u,
                         3u,                         8u, 0x1Bu, 1u};
  send(low, sizeof low);
  for (unsigned i = 0; i < AP_AFD_SECTOR_BYTES; i++) {
    ap_omti_fdc_write(&omti, AP_OMTI_FDC_DATA, 0u);
  }
  (void)take();
  (void)take();
  TEST_ASSERT_EQUAL_UINT8(AP_OMTI_ST2_SCAN_NOT_SATISFIED, take());
  release_floppy();
}

/* §6.3.2 writes a whole track of sectors carrying the fill byte D. It is the
 * only command in §6 that puts data on the medium. */
static void test_format_a_track_fills_every_sector_of_the_current_cylinder(void) {
  build_floppy(true);
  build_controller();
  const uint8_t seek[] = {AP_OMTI_FDC_SEEK, 0u, 5u};
  send(seek, sizeof seek);
  ap_omti_fdc_write(&omti, AP_OMTI_FDC_DATA, AP_OMTI_FDC_SENSE_INTERRUPT);
  (void)take();
  (void)take();

  const uint8_t format[] = {AP_OMTI_FDC_FORMAT_TRACK | AP_OMTI_FDC_MF,
                            0u,    /* HD 0, unit 0 */
                            3u,    /* N: 1024 bytes */
                            8u,    /* SC: eight sectors */
                            0x54u, /* GPL */
                            0xE5u /* D, the fill */};
  send(format, sizeof format);
  TEST_ASSERT_EQUAL_UINT8(AP_OMTI_ST0_IC_NORMAL, take());
  (void)take();
  (void)take();
  TEST_ASSERT_EQUAL_UINT8(5u, take()); /* C, where the head is */

  /* Every sector of cylinder 5 head 0 now carries the fill, and the next track
   * is untouched. */
  uint8_t sector[AP_AFD_SECTOR_BYTES];
  uint32_t lba = 0u;
  for (uint8_t s = 1u; s <= 8u; s++) {
    TEST_ASSERT_TRUE(ap_afd_lba(5u, 0u, s, &lba));
    TEST_ASSERT_TRUE(ap_afd_read(&floppy, lba, sector));
    TEST_ASSERT_EQUAL_UINT8(0xE5u, sector[0]);
    TEST_ASSERT_EQUAL_UINT8(0xE5u, sector[AP_AFD_SECTOR_BYTES - 1u]);
  }
  TEST_ASSERT_TRUE(ap_afd_lba(5u, 1u, 1u, &lba));
  TEST_ASSERT_TRUE(ap_afd_read(&floppy, lba, sector));
  TEST_ASSERT_NOT_EQUAL_UINT8(0xE5u, sector[0]);
  release_floppy();
}

/* ST1's Not Writeable is documented for exactly this command, and a formatted
 * write-protected diskette would otherwise silently do nothing. */
static void test_formatting_a_write_protected_diskette_reports_not_writeable(void) {
  build_floppy(false);
  build_controller();
  const uint8_t format[] = {AP_OMTI_FDC_FORMAT_TRACK, 0u, 3u, 8u, 0x54u, 0xE5u};
  send(format, sizeof format);
  TEST_ASSERT_EQUAL_UINT8(AP_OMTI_ST0_IC_ABRUPT, take());
  TEST_ASSERT_EQUAL_UINT8(AP_OMTI_ST1_NOT_WRITEABLE, take());
  release_floppy();
}

/* §6.3.8 takes its two bytes and answers nothing. A driver sends it first, and
 * a controller that produced a result byte here would leave one byte in the
 * pipe for every command that followed. */
static void test_specify_consumes_three_bytes_and_produces_none(void) {
  build_floppy(true);
  build_controller();
  const uint8_t specify[] = {AP_OMTI_FDC_SPECIFY, 0xDFu, 0x02u};
  send(specify, sizeof specify);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_IDLE, ap_omti_fdc_phase(&omti));
  TEST_ASSERT_EQUAL_UINT8(AP_OMTI_MSR_RQM, status());
  release_floppy();
}

/* The controller is busy until the last result byte is taken, and a command
 * written before then is not a command -- it goes nowhere and the driver reads
 * the *previous* command's leftovers as though they were its results. Found by
 * a test of the scans that skipped the drain and read C, H and R back as a
 * verdict, which is exactly how the bug would present in a driver. */
static void test_a_command_written_before_the_result_phase_is_drained_is_lost(void) {
  build_floppy(true);
  build_controller();
  const uint8_t read[] = {AP_OMTI_FDC_READ_DATA, 0u, 0u, 0u, 1u,
                          3u,                    8u, 0x1Bu, 0xFFu};
  send(read, sizeof read);
  for (unsigned i = 0; i < AP_AFD_SECTOR_BYTES; i++) {
    (void)take();
  }
  /* Three of seven result bytes taken, so four are still pending. */
  (void)take();
  (void)take();
  (void)take();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_fdc_phase(&omti));
  TEST_ASSERT_TRUE((status() & AP_OMTI_MSR_BUSY) != 0u);

  /* A whole SENSE DRIVE STATUS written here does nothing at all... */
  const uint8_t sense[] = {AP_OMTI_FDC_SENSE_DRIVE, 0u};
  send(sense, sizeof sense);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_fdc_phase(&omti));
  /* ...and what comes back is the read's C, H, R and N, not an ST3. */
  TEST_ASSERT_EQUAL_UINT8(0u, take()); /* C */
  TEST_ASSERT_EQUAL_UINT8(0u, take()); /* H */
  TEST_ASSERT_EQUAL_UINT8(1u, take()); /* R */
  TEST_ASSERT_EQUAL_UINT8(3u, take()); /* N */
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_IDLE, ap_omti_fdc_phase(&omti));
  release_floppy();
}

/* The two halves of the controller share a data path in neither direction:
 * §4.1 has them independent and §3.4 has them concurrent, so a floppy command
 * in flight must not disturb a fixed-disk one. */
static void test_a_floppy_command_does_not_disturb_the_fixed_disk_phase(void) {
  build_floppy(true);
  build_controller();
  const uint8_t command[] = {AP_OMTI_FDC_READ_DATA, 0u, 0u, 0u, 1u,
                             3u,                    8u, 0x1Bu, 0xFFu};
  send(command, sizeof command);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_IN, ap_omti_fdc_phase(&omti));
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_IDLE, ap_omti_disk_phase(&omti));
  release_floppy();
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_an_afd_image_is_exactly_one_floppy_or_it_is_refused);
  RUN_TEST(test_the_apollo_floppy_is_77_cylinders_of_2_heads_of_8_sectors);
  RUN_TEST(test_sector_numbering_starts_at_one_and_sector_zero_is_refused);
  RUN_TEST(test_the_second_head_follows_the_first_cylinders_sectors);
  RUN_TEST(test_a_cylinder_or_head_past_the_drive_is_refused);
  RUN_TEST(test_a_read_only_image_refuses_a_write_and_keeps_its_data);
  RUN_TEST(test_each_command_takes_the_number_of_bytes_section_6_3_gives_it);
  RUN_TEST(test_the_mt_mf_and_sk_bits_do_not_change_the_command);
  RUN_TEST(test_specify_seek_and_recalibrate_have_no_result_phase);
  RUN_TEST(test_the_command_phase_is_inert_while_the_floppy_is_in_reset);
  RUN_TEST(test_read_data_returns_the_sector_its_chr_names);
  RUN_TEST(test_a_read_past_the_last_cylinder_reports_no_data);
  RUN_TEST(test_a_read_with_no_diskette_reports_a_missing_address_mark);
  RUN_TEST(test_a_seek_reports_nothing_until_sense_interrupt_status_asks);
  RUN_TEST(test_sense_interrupt_status_with_no_seek_pending_is_invalid);
  RUN_TEST(test_recalibrate_puts_the_head_on_track_zero);
  RUN_TEST(test_a_read_only_diskette_reads_as_write_protected);
  RUN_TEST(test_an_unlisted_command_including_write_data_is_invalid);
  RUN_TEST(test_scan_equal_reports_a_hit_when_every_byte_matches);
  RUN_TEST(test_a_single_mismatched_byte_leaves_the_scan_not_satisfied);
  RUN_TEST(test_the_low_and_high_scans_compare_the_media_against_the_host);
  RUN_TEST(test_format_a_track_fills_every_sector_of_the_current_cylinder);
  RUN_TEST(test_formatting_a_write_protected_diskette_reports_not_writeable);
  RUN_TEST(test_specify_consumes_three_bytes_and_produces_none);
  RUN_TEST(test_a_command_written_before_the_result_phase_is_drained_is_lost);
  RUN_TEST(test_a_floppy_command_does_not_disturb_the_fixed_disk_phase);
  return UNITY_END();
}

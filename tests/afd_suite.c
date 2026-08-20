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

/* Let whatever the last command set in motion arrive.
 *
 * `008778-03` chapter 7 gives this drive an access time, so a command no longer
 * completes in the instant its last byte is written: the controller is busy and
 * the heads are moving. A real driver polls the Main Status Register across
 * that interval; these tests advance the clock instead, which is the same wait
 * without the polling. The durations themselves are asserted by the timing
 * tests at the end of this file, not here -- everything above them is about
 * what the command *does*, and should not have to restate how long it takes. */
static void settle(void) {
  /* Bounded rather than `while`: each pass retires at least one deadline and
   * three is all there are, so a fourth would mean a deadline that re-arms
   * itself and this should fail the test rather than hang it. */
  for (unsigned pass = 0; pass < 4u; pass++) {
    const ap_time_t next = ap_omti_interrupt_next_change(&omti);
    if (next == AP_TIME_NEVER) {
      return;
    }
    ap_omti_advance(&omti, next);
  }
  TEST_FAIL_MESSAGE("a deadline outlived four advances");
}

static void send(const uint8_t *bytes, unsigned count) {
  for (unsigned i = 0; i < count; i++) {
    ap_omti_fdc_write(&omti, AP_OMTI_FDC_DATA, bytes[i]);
  }
  settle();
}

/* A byte out of the data register, and then the same wait `send` does.
 *
 * The byte that *ends* a data phase is what hands the command to the result
 * phase, and that handover now goes through the drive's access time -- so the
 * settle belongs on every data-register access rather than only after a
 * command's last byte. Everywhere else it costs nothing: outside an execution
 * phase there is no deadline to advance onto. */
static uint8_t take(void) {
  const uint8_t byte = ap_omti_fdc_read(&omti, AP_OMTI_FDC_DATA);
  settle();
  return byte;
}

/* The same, going the other way: the data-out phases of the three scans. */
static void put(uint8_t byte) {
  ap_omti_fdc_write(&omti, AP_OMTI_FDC_DATA, byte);
  settle();
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
    put(0u);
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
    put(i == 500u ? 0xFFu : 0u);
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
    put(0u);
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
    put(0u);
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

/* ---- The drive's access time, `008778-03` chapter 7 ---------------------- */

/* Issue a command without letting anything arrive, so a test can look at the
 * controller mid-flight. `send` settles by design; this is its counterpart. */
static void send_only(const uint8_t *bytes, unsigned count) {
  for (unsigned i = 0; i < count; i++) {
    ap_omti_fdc_write(&omti, AP_OMTI_FDC_DATA, bytes[i]);
  }
}

static void seek_to(uint8_t cylinder) {
  const uint8_t command[] = {AP_OMTI_FDC_SEEK, 0u, cylinder};
  send_only(command, sizeof command);
}

/* Table 7-7: "Track-to-Track Time 3 msec minimum", "Settling Time Less than 15
 * msec (**excluding track-to-track time**)". The exclusion is the whole shape
 * of the model -- the settle is charged once at the end, not per step. */
static void test_a_seek_costs_one_step_a_cylinder_and_a_single_settle(void) {
  build_floppy(true);
  build_controller();
  seek_to(10u);
  TEST_ASSERT_EQUAL_UINT64(10u * AP_OMTI_FDC_TRACK_TO_TRACK +
                               AP_OMTI_FDC_SETTLING,
                           omti.fdc_seek_at[0]);

  /* And it is the *distance* that is paid for, not the destination: the same
   * drive going ten further costs the same again. */
  settle();
  const ap_time_t at_ten = omti.now;
  seek_to(20u);
  TEST_ASSERT_EQUAL_UINT64(at_ten + 10u * AP_OMTI_FDC_TRACK_TO_TRACK +
                               AP_OMTI_FDC_SETTLING,
                           omti.fdc_seek_at[0]);
  release_floppy();
}

/* A head already on the requested cylinder has nowhere to go, and a
 * step-and-settle model that charged the settle anyway would invent 15 ms per
 * redundant seek -- which a driver that re-seeks defensively issues constantly.
 */
static void test_a_seek_to_the_cylinder_the_head_is_on_arrives_at_once(void) {
  build_floppy(true);
  build_controller();
  seek_to(0u);
  TEST_ASSERT_EQUAL_UINT64(AP_TIME_NEVER, omti.fdc_seek_at[0]);
  /* Arrived, so the sense has something to report without any wait. */
  const uint8_t sense[] = {AP_OMTI_FDC_SENSE_INTERRUPT};
  send(sense, sizeof sense);
  TEST_ASSERT_EQUAL_UINT8(AP_OMTI_ST0_IC_NORMAL | AP_OMTI_ST0_SEEK_END, take());
  release_floppy();
}

/* `[OMTI]` Table 4-3, Main Status Register bits 1 and 0: "Drive B/A is in the
 * Seek mode when 1". They were unreachable while every seek finished inside the
 * command that issued it; the drive's step time is what gives them an interval
 * to be observed in, and this is the polled path a driver uses to wait -- §4.5
 * describes no interrupt for a command with no result phase. */
static void test_the_status_register_shows_a_drive_in_the_seek_mode(void) {
  build_floppy(true);
  build_controller();
  seek_to(40u);
  TEST_ASSERT_TRUE((status() & AP_OMTI_MSR_SEEK_A) != 0u);
  TEST_ASSERT_TRUE((status() & AP_OMTI_MSR_SEEK_B) == 0u);
  /* The controller itself is free: §6.3 gives SEEK no result phase, so it is
   * back at idle while the head is still moving. */
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_IDLE, ap_omti_fdc_phase(&omti));

  /* One base unit short of arrival it is still moving. */
  ap_omti_advance(&omti, omti.fdc_seek_at[0] - 1u);
  TEST_ASSERT_TRUE((status() & AP_OMTI_MSR_SEEK_A) != 0u);

  ap_omti_advance(&omti, omti.fdc_seek_at[0]);
  TEST_ASSERT_TRUE((status() & AP_OMTI_MSR_SEEK_A) == 0u);
  release_floppy();
}

/* Two drives, two deadlines: §4.5's status register has a bit each because a
 * seek on drive A does not stop drive B, and one shared deadline would make the
 * second seek cancel the first. */
static void test_the_two_drives_seek_independently(void) {
  build_floppy(true);
  build_controller();
  seek_to(40u);
  /* Select drive B in the Digital Output Register and send it somewhere
   * nearer, so its arrival is strictly the earlier of the two. */
  ap_omti_fdc_write(&omti, AP_OMTI_FDC_DOR,
                    (uint8_t)(AP_OMTI_DOR_NOT_RESET | 0x01u));
  const uint8_t command[] = {AP_OMTI_FDC_SEEK, 0x01u, 5u};
  send_only(command, sizeof command);
  TEST_ASSERT_TRUE((status() & AP_OMTI_MSR_SEEK_A) != 0u);
  TEST_ASSERT_TRUE((status() & AP_OMTI_MSR_SEEK_B) != 0u);
  TEST_ASSERT_TRUE(omti.fdc_seek_at[1] < omti.fdc_seek_at[0]);

  /* B arrives; A is still moving. */
  ap_omti_advance(&omti, omti.fdc_seek_at[1]);
  TEST_ASSERT_TRUE((status() & AP_OMTI_MSR_SEEK_B) == 0u);
  TEST_ASSERT_TRUE((status() & AP_OMTI_MSR_SEEK_A) != 0u);
  TEST_ASSERT_EQUAL_UINT8(5u, omti.fdc_cylinder[1]);
  TEST_ASSERT_EQUAL_UINT8(40u, omti.fdc_cylinder[0]);
  release_floppy();
}

/* **The check that the three published figures describe one mechanism.**
 *
 * Table 7-7 gives track-to-track, settling *and* an aggregate: "Average Track
 * Access Time (including settling time) 94 msec (for 80 cylinders)". Composing
 * the first two over the drive's 80 cylinders has to reproduce the third, or
 * the composition is this core's invention rather than the document's model.
 * It comes out at 94.8 ms -- 0.9% high, which is a rounding difference and not
 * a structural one.
 *
 * The mean is over every ordered pair of cylinders, which is what "average
 * access" means for a drive with no idea where its next request will be. */
static void test_the_step_and_settle_model_gives_table_7_7s_average_of_94_ms(
    void) {
  uint64_t total = 0u;
  for (unsigned from = 0; from < AP_OMTI_FDC_DRIVE_CYLINDERS; from++) {
    for (unsigned to = 0; to < AP_OMTI_FDC_DRIVE_CYLINDERS; to++) {
      const unsigned distance = from > to ? from - to : to - from;
      if (distance == 0u) {
        continue;
      }
      total += (uint64_t)distance * AP_OMTI_FDC_TRACK_TO_TRACK +
               AP_OMTI_FDC_SETTLING;
    }
  }
  const uint64_t pairs =
      (uint64_t)AP_OMTI_FDC_DRIVE_CYLINDERS * AP_OMTI_FDC_DRIVE_CYLINDERS;
  const uint64_t millisecond = AP_TIME_BASE_HZ / 1000u;
  const uint64_t mean_ms = total / pairs / millisecond;
  TEST_ASSERT_EQUAL_UINT64(94u, mean_ms);
}

/* ## `002398-04` p. 6-3's floppy row, which publishes the *unsettled* seek
 *
 * The test above checks the composition against the aggregate it was built
 * from, which is a consistency check on one document. A second document gives
 * the two ends of the same mechanism *without* the settle folded in, and that
 * is a check the model could fail independently. Its floppy row reads
 * `T-to-T 3, AVG 77, MAX 231`, and both figures are arithmetic on the step
 * time over the **format's** 77 cylinders rather than the drive's 80:
 *
 *   - `MAX 231` is 77 x 3 ms exactly -- a full stroke, no settle.
 *   - `AVG 77` is one third of that, the uniformly-random mean of a
 *     step-per-cylinder seek, again with no settle.
 *
 * So the step time this core uses is the one that produces both, and the 15 ms
 * settle is the difference between this page's convention and Table 7-7's. */
static void test_the_step_time_reproduces_p_6_3s_unsettled_seek_figures(void) {
  const uint64_t millisecond = AP_TIME_BASE_HZ / 1000u;

  /* MAX: a full stroke across the format's cylinders, settle excluded. */
  const uint64_t full_stroke =
      (uint64_t)AP_AFD_CYLINDERS * AP_OMTI_FDC_TRACK_TO_TRACK;
  TEST_ASSERT_EQUAL_UINT64(231u, full_stroke / millisecond);

  /* AVG: one third of the stroke, which is what a uniformly-random seek
   * travels. Printed as a whole number of milliseconds, so compare there. */
  TEST_ASSERT_EQUAL_UINT64(77u, full_stroke / 3u / millisecond);

  /* And the page's own AVG READ of 176.0 ms is that seek plus Table 7-1's
   * latency plus one 1024-byte sector at 500 Kbit/s -- the footnote's formula,
   * "Average read = Avg. Seek time + Avg. Latency + Sector Time". Reproducing
   * it is what shows the row describes this drive and this format. */
  const uint64_t sector_time = (uint64_t)AP_AFD_SECTOR_BYTES *
                               AP_TIME_BASE_HZ /
                               AP_OMTI_FDC_TRANSFER_BYTES_PER_SEC;
  const uint64_t average_read =
      full_stroke / 3u + AP_OMTI_FDC_AVERAGE_LATENCY + sector_time;
  TEST_ASSERT_EQUAL_UINT64(176u, average_read / millisecond);
}

/* Table 7-1: "Average Latency Time 83.3 msec" and "Data Transfer Rate 500K"
 * bits a second. A read waits for the sector to come round and then for its
 * bytes to cross the head; it does *not* pay a seek, because the 765 does not
 * position implicitly -- the head is where SEEK left it. */
static void test_a_read_costs_half_a_revolution_and_the_sectors_transfer(void) {
  build_floppy(true);
  build_controller();
  const uint8_t command[] = {AP_OMTI_FDC_READ_DATA, 0u, 0u, 0u, 1u,
                             3u,                    8u, 0x1Bu, 0xFFu};
  send_only(command, sizeof command);
  /* The data phase comes first and is not itself delayed -- the named
   * approximation this shares with the fixed disk, see `ap_omti.h`. */
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_IN, ap_omti_fdc_phase(&omti));
  for (unsigned i = 0; i < AP_AFD_SECTOR_BYTES; i++) {
    (void)ap_omti_fdc_read(&omti, AP_OMTI_FDC_DATA);
  }
  /* And now the drive's time is charged, before the result bytes are offered. */
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_EXECUTING, ap_omti_fdc_phase(&omti));
  TEST_ASSERT_TRUE((status() & AP_OMTI_MSR_BUSY) != 0u);
  TEST_ASSERT_TRUE((status() & AP_OMTI_MSR_RQM) == 0u);
  TEST_ASSERT_EQUAL_UINT64(
      AP_OMTI_FDC_AVERAGE_LATENCY +
          (ap_time_t)((uint64_t)AP_TIME_BASE_HZ * AP_AFD_SECTOR_BYTES /
                      AP_OMTI_FDC_TRANSFER_BYTES_PER_SEC),
      omti.fdc_completion_at);

  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_fdc_phase(&omti));
  release_floppy();
}

/* The same division the fixed disk draws: a command the controller answers out
 * of its own registers touched no surface, so there is nothing to wait for and
 * charging would be inventing time. */
static void test_a_command_that_touches_no_surface_costs_nothing(void) {
  build_floppy(true);
  build_controller();
  const uint8_t sense[] = {AP_OMTI_FDC_SENSE_DRIVE, 0u};
  send_only(sense, sizeof sense);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_fdc_phase(&omti));
  TEST_ASSERT_EQUAL_UINT64(0u, omti.fdc_completion_at);
  drain();

  const uint8_t specify[] = {AP_OMTI_FDC_SPECIFY, 0xDFu, 0x02u};
  send_only(specify, sizeof specify);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_IDLE, ap_omti_fdc_phase(&omti));
  TEST_ASSERT_EQUAL_UINT64(0u, omti.fdc_completion_at);
  release_floppy();
}

/* Holding the floppy half in reset abandons what it was working towards. Left
 * standing, a seek would arrive later and set the seek-done flag on a
 * controller that has forgotten it ever issued one -- and the deadlines are
 * hashed, so two controllers in reset must be the same machine. */
static void test_a_reset_abandons_an_outstanding_seek(void) {
  build_floppy(true);
  build_controller();
  seek_to(40u);
  TEST_ASSERT_TRUE(omti.fdc_seek_at[0] != AP_TIME_NEVER);
  ap_omti_fdc_write(&omti, AP_OMTI_FDC_DOR, 0u);
  TEST_ASSERT_EQUAL_UINT64(AP_TIME_NEVER, omti.fdc_seek_at[0]);
  TEST_ASSERT_FALSE(omti.fdc_seek_done);
  TEST_ASSERT_EQUAL_UINT64(AP_TIME_NEVER, ap_omti_interrupt_next_change(&omti));
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
  RUN_TEST(test_a_seek_costs_one_step_a_cylinder_and_a_single_settle);
  RUN_TEST(test_a_seek_to_the_cylinder_the_head_is_on_arrives_at_once);
  RUN_TEST(test_the_status_register_shows_a_drive_in_the_seek_mode);
  RUN_TEST(test_the_two_drives_seek_independently);
  RUN_TEST(test_the_step_and_settle_model_gives_table_7_7s_average_of_94_ms);
  RUN_TEST(test_the_step_time_reproduces_p_6_3s_unsettled_seek_figures);
  RUN_TEST(test_a_read_costs_half_a_revolution_and_the_sectors_transfer);
  RUN_TEST(test_a_command_that_touches_no_surface_costs_nothing);
  RUN_TEST(test_a_reset_abandons_an_outstanding_seek);
  return UNITY_END();
}

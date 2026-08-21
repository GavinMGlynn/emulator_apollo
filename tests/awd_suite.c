/* An Apollo Winchester image, and the OMTI commands that move data through it.
 *
 * The images in `media/` are 348 MB and gitignored, so everything here is built
 * by the test on a small geometry. That is only possible because the geometry
 * is a parameter of the image rather than a constant, which is itself the point:
 * the file says nothing about its own shape and the controller is told.
 */

#include "unity.h"

#include <string.h>

#include "device/ap_omti.h"
#include "device/ap_omti_cdb.h"
#include "image/ap_awd.h"

void setUp(void) {}
void tearDown(void) {}

/* Two cylinders, two heads, four sectors: sixteen sectors, small enough to hold
 * and large enough that every term of the address arithmetic matters. */
static const ap_awd_geometry_t SMALL = {
    .cylinders = 2u, .heads = 2u, .sectors = 4u};
#define SMALL_SECTORS 16u

static uint8_t backing[SMALL_SECTORS * AP_AWD_SECTOR_BYTES];
static ap_awd_t drive;

static void build_drive(void) {
  /* Every sector filled with its own number, so a read landing on the wrong one
   * is visible rather than merely different. */
  for (unsigned s = 0; s < SMALL_SECTORS; s++) {
    memset(&backing[s * AP_AWD_SECTOR_BYTES], (int)s, AP_AWD_SECTOR_BYTES);
  }
  TEST_ASSERT_TRUE(
      ap_awd_open(&drive, backing, sizeof backing, SMALL, true));
}

/* `omti8621.cpp` configures exactly two drives, and the 348 MB one is what
 * `media/` holds. Pinned because the geometry is not in the image and a wrong
 * one silently addresses the wrong sector rather than failing. */
static void test_the_two_drives_are_the_oracles(void) {
  const ap_awd_geometry_t big = ap_awd_geometry_for(AP_AWD_DRIVE_348MB);
  TEST_ASSERT_EQUAL_UINT(1223u, big.cylinders);
  TEST_ASSERT_EQUAL_UINT(15u, big.heads);
  TEST_ASSERT_EQUAL_UINT(18u, big.sectors);
  TEST_ASSERT_EQUAL_UINT(330210u, ap_awd_sector_count(big));

  const ap_awd_geometry_t small = ap_awd_geometry_for(AP_AWD_DRIVE_155MB);
  TEST_ASSERT_EQUAL_UINT(1023u, small.cylinders);
  TEST_ASSERT_EQUAL_UINT(8u, small.heads);
  TEST_ASSERT_EQUAL_UINT(18u, small.sectors);

  /* And the sector is 1056, not 1024: the file system's block plus thirty-two.
   * A reader assuming 1024 addresses a different sector from the fourth one on,
   * and reads plausible-looking data the whole way. */
  TEST_ASSERT_EQUAL_UINT(1056u, AP_AWD_SECTOR_BYTES);
}

/* `(cylinder * heads + head) * sectors + sector`, which is what the oracle
 * computes in `get_disk_track` and `get_disk_address`. */
static void test_the_address_is_cylinder_head_sector(void) {
  uint32_t lba = 0;
  TEST_ASSERT_TRUE(ap_awd_lba(SMALL, 0u, 0u, 0u, &lba));
  TEST_ASSERT_EQUAL_UINT(0u, lba);
  TEST_ASSERT_TRUE(ap_awd_lba(SMALL, 0u, 0u, 3u, &lba));
  TEST_ASSERT_EQUAL_UINT(3u, lba);
  /* The next head is a whole track on. */
  TEST_ASSERT_TRUE(ap_awd_lba(SMALL, 0u, 1u, 0u, &lba));
  TEST_ASSERT_EQUAL_UINT(4u, lba);
  /* The next cylinder is every head's track on. */
  TEST_ASSERT_TRUE(ap_awd_lba(SMALL, 1u, 0u, 0u, &lba));
  TEST_ASSERT_EQUAL_UINT(8u, lba);
  TEST_ASSERT_TRUE(ap_awd_lba(SMALL, 1u, 1u, 3u, &lba));
  TEST_ASSERT_EQUAL_UINT(15u, lba);
}

/* Out of range fails rather than wrapping. A head beyond the drive's is a
 * driver's mistake, and returning another track's data would hide it. */
static void test_an_address_off_the_drive_is_refused(void) {
  uint32_t lba = 0;
  TEST_ASSERT_FALSE(ap_awd_lba(SMALL, 2u, 0u, 0u, &lba));
  TEST_ASSERT_FALSE(ap_awd_lba(SMALL, 0u, 2u, 0u, &lba));
  /* The sector is **not** bounded by the track: past it the address carries
   * into the next head, which is what the boot PROM's drive test requires --
   * it reads sectors 0 to 24 of a track that has eighteen. See `ap_awd.h`. */
  TEST_ASSERT_TRUE(ap_awd_lba(SMALL, 0u, 0u, 4u, &lba));
  TEST_ASSERT_EQUAL_UINT32(4u, lba);
  TEST_ASSERT_TRUE(ap_awd_lba(SMALL, 0u, 0u, 5u, &lba));
  TEST_ASSERT_EQUAL_UINT32(5u, lba);
  /* And the same sector reached the ordinary way is the same number, which is
   * what makes the carry a *mapping* rather than an accident. */
  {
    uint32_t direct = 0;
    TEST_ASSERT_TRUE(ap_awd_lba(SMALL, 0u, 1u, 0u, &direct));
    TEST_ASSERT_EQUAL_UINT32(direct, 4u);
  }
  /* Past the last sector the drive has is still refused: the geometry is what
   * bounds the address once the fields no longer do. */
  TEST_ASSERT_FALSE(ap_awd_lba(SMALL, 0u, 0u, 63u, &lba));
}

/* An image shorter than its geometry is normal -- `media/`'s files are 348 MiB
 * where the drive wants 348,701,760 bytes -- and a read past what the file holds
 * fails rather than handing back whatever the buffer had. */
static void test_a_short_image_refuses_the_sectors_it_lacks(void) {
  static uint8_t half[8u * AP_AWD_SECTOR_BYTES];
  ap_awd_t truncated;
  TEST_ASSERT_TRUE(ap_awd_open(&truncated, half, sizeof half, SMALL, true));

  uint8_t sector[AP_AWD_SECTOR_BYTES];
  TEST_ASSERT_TRUE(ap_awd_read(&truncated, 7u, sector));
  TEST_ASSERT_FALSE(ap_awd_read(&truncated, 8u, sector));
  /* Inside the geometry and past the file: the distinction that matters. */
  TEST_ASSERT_TRUE(8u < ap_awd_sector_count(SMALL));
}

/* ## The sidecar
 *
 * An `.awd` is sector data and nothing else; a real surface carries an ID field
 * and an ECC field per sector, and `[OMTI]` §5 has commands for both. They live
 * in a companion file because `DOMAINOS_IMAGE.md` pins the image's SHA-256 --
 * see `docs/references/AWD_META.md`.
 */
static uint8_t meta_file[AP_AWD_META_HEADER_BYTES +
                         SMALL_SECTORS * AP_AWD_META_RECORD_BYTES];

static void build_meta(void) {
  memset(meta_file, 0, sizeof meta_file);
  memcpy(meta_file, AP_AWD_META_MAGIC, AP_AWD_META_MAGIC_BYTES);
  meta_file[8] = AP_AWD_META_HEADER_BYTES;
  meta_file[12] = AP_AWD_META_RECORD_BYTES;
}

static void test_without_a_sidecar_the_surface_is_clean(void) {
  build_drive();

  /* No sidecar attached: no flags anywhere, and ECC reads as six zeros. That
   * is a *description* of a raw image rather than a fallback -- it has no
   * defects and no recorded ECC, because it has nowhere to keep either. */
  TEST_ASSERT_EQUAL_HEX8(0u, ap_awd_flags(&drive, 0u));
  TEST_ASSERT_EQUAL_HEX8(0u, ap_awd_flags(&drive, SMALL_SECTORS - 1u));

  uint8_t ecc[AP_AWD_ECC_BYTES];
  memset(ecc, 0xAAu, sizeof ecc);
  ap_awd_ecc(&drive, 0u, ecc);
  for (unsigned i = 0; i < AP_AWD_ECC_BYTES; i++) {
    TEST_ASSERT_EQUAL_HEX8(0u, ecc[i]);
  }

  /* And nothing can be recorded, which a caller must be able to tell. */
  TEST_ASSERT_FALSE(ap_awd_set_flags(&drive, 0u, AP_AWD_FLAG_BAD_TRACK));
  TEST_ASSERT_FALSE(ap_awd_set_ecc(&drive, 0u, ecc));
}

static void test_a_sidecar_carries_flags_and_ecc(void) {
  build_drive();
  build_meta();
  TEST_ASSERT_TRUE(ap_awd_attach_meta(&drive, meta_file, sizeof meta_file));

  TEST_ASSERT_TRUE(ap_awd_set_flags(&drive, 5u, AP_AWD_FLAG_BAD_TRACK));
  TEST_ASSERT_EQUAL_HEX8(AP_AWD_FLAG_BAD_TRACK, ap_awd_flags(&drive, 5u));
  /* Per sector, so its neighbours are untouched. */
  TEST_ASSERT_EQUAL_HEX8(0u, ap_awd_flags(&drive, 4u));
  TEST_ASSERT_EQUAL_HEX8(0u, ap_awd_flags(&drive, 6u));

  static const uint8_t recorded[AP_AWD_ECC_BYTES] = {1u, 2u, 3u, 4u, 5u, 6u};
  TEST_ASSERT_TRUE(ap_awd_set_ecc(&drive, 5u, recorded));
  uint8_t back[AP_AWD_ECC_BYTES];
  ap_awd_ecc(&drive, 5u, back);
  TEST_ASSERT_EQUAL_MEMORY(recorded, back, AP_AWD_ECC_BYTES);
  /* The sector's data is not where the ECC went: the sidecar is beside the
   * image, never inside it. */
  TEST_ASSERT_EQUAL_HEX8(5u, backing[5u * AP_AWD_SECTOR_BYTES]);
}

static void test_a_malformed_or_short_sidecar_is_told_apart(void) {
  build_drive();
  build_meta();

  /* Wrong magic is a refusal: attaching some other file as a defect list would
   * invent defects out of whatever it happened to contain. */
  uint8_t wrong[sizeof meta_file];
  memcpy(wrong, meta_file, sizeof wrong);
  wrong[0] = 'X';
  TEST_ASSERT_FALSE(ap_awd_attach_meta(&drive, wrong, sizeof wrong));

  /* A *short* file is not malformed -- it describes the sectors it covers and
   * no more, the same rule a short image already follows. */
  const size_t two = AP_AWD_META_HEADER_BYTES + 2u * AP_AWD_META_RECORD_BYTES;
  TEST_ASSERT_TRUE(ap_awd_attach_meta(&drive, meta_file, two));
  TEST_ASSERT_TRUE(ap_awd_set_flags(&drive, 1u, AP_AWD_FLAG_IS_ALTERNATE));
  TEST_ASSERT_EQUAL_HEX8(AP_AWD_FLAG_IS_ALTERNATE, ap_awd_flags(&drive, 1u));
  /* Past what it covers: no flags, and nothing can be recorded there. */
  TEST_ASSERT_EQUAL_HEX8(0u, ap_awd_flags(&drive, 2u));
  TEST_ASSERT_FALSE(ap_awd_set_flags(&drive, 2u, AP_AWD_FLAG_BAD_TRACK));
}

/* --------------------------------------------------------------------------
 * The controller's command phase
 * ------------------------------------------------------------------------ */

static ap_omti_t omti;

static void build_controller(void) {
  build_drive();
  ap_omti_reset(&omti);
  ap_omti_attach(&omti, &drive);
  /* §4.3: "The host must wait 100 usec after a -RESET before issuing a
   * SELECT." A freshly reset controller is in the reset state, not idle, and
   * "the IDLE STATE is the only time the controller will respond to a select
   * request" -- so a suite about what commands *do* has to get the host's half
   * of the protocol right before it can issue one. */
  ap_omti_advance(&omti, AP_OMTI_RESET_TIME);
}

/* A six-byte descriptor block, written a byte at a time to the data port as
 * §5.1.1 has it. */
static void issue(uint8_t command, uint16_t cylinder, uint8_t head,
                  uint8_t sector, uint8_t blocks) {
  const uint8_t cdb[6] = {
      command,
      (uint8_t)((head & 0x1Fu) | ((cylinder >> 8) & 0x07u ? 0x80u : 0x00u)),
      (uint8_t)((sector & 0x3Fu) | (uint8_t)(((cylinder >> 8) & 0x03u) << 6)),
      (uint8_t)(cylinder & 0xFFu),
      blocks,
      0u,
  };
  for (unsigned i = 0; i < sizeof cdb; i++) {
    ap_omti_disk_write(&omti, AP_OMTI_DISK_DATA, cdb[i]);
  }
}

/* Wait out the drive's access time.
 *
 * A command that moves the heads no longer completes in the register write that
 * issued it -- see `ap_omti.h`, where the access time exists because a zero one
 * crashes Domain/OS. These tests are about what a command *does*, not how long
 * it takes, so they advance straight to the deadline, which is what a driver's
 * wait amounts to. The one test that is about the duration asserts on it
 * directly instead. */
static void settle(void) {
  if (ap_omti_disk_phase(&omti) == AP_OMTI_PHASE_EXECUTING) {
    ap_omti_advance(&omti, omti.completion_at);
  }
}

static uint8_t take_status(void) {
  settle();
  return ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA);
}

/* The whole cycle: command, data, status. */
static void test_a_read_command_delivers_the_addressed_sector(void) {
  build_controller();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_IDLE, ap_omti_disk_phase(&omti));

  /* Cylinder 1, head 1, sector 3 -- sector 15, filled with 15. */
  issue(AP_OMTI_CMD_READ, 1u, 1u, 3u, 1u);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_IN, ap_omti_disk_phase(&omti));
  /* The byte waiting is data rather than a command or status byte, it travels
   * *to* the host, and it is requested. `REQ` is the handshake §4.3 turns every
   * phase on and this asserted `DREQ` instead -- which is the **DMA** request
   * and is gated on the MASK's DMA ENABLE, not set on every read. A controller
   * asserting it in programmed I/O asks for a cycle nobody arranged. */
  const uint8_t status = ap_omti_disk_read(&omti, AP_OMTI_DISK_STATUS);
  TEST_ASSERT_TRUE((status & AP_OMTI_ST_REQ) != 0u);
  TEST_ASSERT_TRUE((status & AP_OMTI_ST_IO) != 0u);
  TEST_ASSERT_TRUE((status & AP_OMTI_ST_CD) == 0u);
  TEST_ASSERT_TRUE((status & AP_OMTI_ST_DREQ) == 0u);

  for (unsigned i = 0; i < AP_AWD_SECTOR_BYTES; i++) {
    TEST_ASSERT_EQUAL_HEX8(15u, ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA));
  }

  /* The sector ends the command, and the completion byte is waiting.
   *
   * `IREQ` is **not** up, because this controller was never told to interrupt:
   * §4.2 gates the bit on the MASK register's interrupt enable, and this test
   * ran in programmed I/O. Asserting it here was asserting the reading this
   * core first took and Domain/OS later disproved by polling for `CF`. What a
   * polled driver waits on is `REQ`, and that is up. */
  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  const uint8_t completed = ap_omti_disk_read(&omti, AP_OMTI_DISK_STATUS);
  TEST_ASSERT_TRUE((completed & AP_OMTI_ST_IREQ) == 0u);
  TEST_ASSERT_TRUE((completed & AP_OMTI_ST_REQ) != 0u);
  /* The bits, not the byte. On the machine this reads `CF`, which is these
   * plus `BSY` -- and `BSY` is "Controller Selected", which this fixture drives
   * the command without asserting. Pinning the whole byte here would be pinning
   * the fixture. */
  TEST_ASSERT_EQUAL_HEX8(0xC7u, (uint8_t)(completed & ~AP_OMTI_ST_BSY));
  TEST_ASSERT_EQUAL_HEX8(0u, take_status());
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_IDLE, ap_omti_disk_phase(&omti));
}

/* `1E READ DATA TO BUFFER`, §5.4.19: "This command reads data from the disk to
 * the controller's buffer. **It does not transfer the data to the host.**"
 *
 * The pairing is named from both ends -- §5.4.13 says `0E` "is normally used
 * immediately after a Read Data to Sector Buffer (1Eh) command has been issued
 * to enhance performance when data transfers are done using programmed I/O" --
 * and Domain/OS issues exactly that pair, once each. Until this landed, `1E`
 * was accepted by the ESDI command set and fell to the unimplemented arm, which
 * reports `SENSE_ILLEGAL_ADDRESS`; the operating system's jump table turns that
 * sense code into a fatal status, and the machine crashed. */
static void test_a_read_to_buffer_fills_the_buffer_without_a_data_phase(void) {
  build_controller();
  issue(AP_OMTI_CMD_READ_TO_BUFFER, 0u, 0u, 2u, 1u);

  /* Straight to the status phase: no `DATA IN`, because the host is not being
   * offered anything. Offering it would leave a driver reading bytes it never
   * asked for. */
  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  TEST_ASSERT_EQUAL_HEX8(0u, take_status());

  /* And the buffer holds the sector, which `0E` then hands over. That is the
   * whole point of the pair, so reading it back through `0E` is the test that
   * the two halves agree. */
  issue(AP_OMTI_CMD_READ_SECTOR_BUFFER, 0u, 0u, 0u, 1u);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_IN, ap_omti_disk_phase(&omti));
  /* Sector 2, because the fixture fills every sector with its own number: a
   * read that landed on the wrong one is visible rather than merely different,
   * and `0E` carries no address of its own, so this also shows that what comes
   * back is what `1E` put there. */
  for (unsigned i = 0; i < AP_AWD_SECTOR_BYTES; i++) {
    TEST_ASSERT_EQUAL_HEX8(2u, ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA));
  }
}

/* ## The command log, and why the sector list could not be it
 *
 * A list of sectors says what came off the surface. It cannot say which command
 * took them, how many blocks that command asked for, or how many bytes the host
 * then drained -- and this pair is exactly where those come apart, because `1E`
 * fills the buffer in one command and `0E` empties it in another. A buffer
 * refilled before it is drained loses a sector while both commands *succeed*,
 * so no refusal, sense byte or status code records it.
 *
 * The log is instrumentation and this test pins the three things a reader of it
 * has to be able to trust: the pairing is visible, the newest command is index
 * 0, and `drained` counts the bytes that actually left the data port. */
static void test_the_command_log_pairs_a_buffer_fill_with_its_drain(void) {
  build_controller();

  issue(AP_OMTI_CMD_READ_TO_BUFFER, 0u, 0u, 2u, 1u);
  settle();
  TEST_ASSERT_EQUAL_HEX8(0u, take_status());

  issue(AP_OMTI_CMD_READ_SECTOR_BUFFER, 0u, 0u, 0u, 1u);
  for (unsigned i = 0; i < AP_AWD_SECTOR_BYTES; i++) {
    (void)ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA);
  }

  TEST_ASSERT_EQUAL_UINT(2u, ap_omti_commands_recorded(&omti));

  uint8_t command = 0;
  uint16_t blocks = 0;
  uint32_t lba = 0;
  uint32_t drained = 0;

  /* Index 0 is the newest, which is the `0E` that drained the buffer. It names
   * no address at all, and an address it does not have is reported as absent
   * rather than as sector zero -- a log that prints `0` for "no address" is a
   * log that accuses the driver of reading the volume label. */
  TEST_ASSERT_TRUE(
      ap_omti_recent_command(&omti, 0u, &command, &blocks, &lba, &drained));
  TEST_ASSERT_EQUAL_HEX8(AP_OMTI_CMD_READ_SECTOR_BUFFER, command);
  TEST_ASSERT_EQUAL_UINT(1u, blocks);
  TEST_ASSERT_EQUAL_HEX32(UINT32_MAX, lba);
  TEST_ASSERT_EQUAL_UINT(AP_AWD_SECTOR_BYTES, drained);

  /* And behind it the `1E` that filled the buffer: it names the sector, and it
   * handed the host nothing. Those two facts together are what makes a short
   * drain legible in a report. */
  TEST_ASSERT_TRUE(
      ap_omti_recent_command(&omti, 1u, &command, &blocks, &lba, &drained));
  TEST_ASSERT_EQUAL_HEX8(AP_OMTI_CMD_READ_TO_BUFFER, command);
  TEST_ASSERT_EQUAL_UINT(1u, blocks);
  TEST_ASSERT_EQUAL_UINT(2u, lba);
  TEST_ASSERT_EQUAL_UINT(0u, drained);

  /* Past what was recorded answers false rather than a stale entry. */
  TEST_ASSERT_FALSE(
      ap_omti_recent_command(&omti, 2u, &command, &blocks, &lba, &drained));
}

/* §5.4.19: "The number of data blocks that can be read is **limited by the
 * controller's Buffer size** as follows", then the three rows -- and §5.4.20
 * adds "An error will be returned if the block count exceeds the above limits".
 * So the refusal is real and the limit is a *function of the buffer*, not the
 * constant this suite used to assert.
 *
 * Past it, for a part that reports 32K. */
static void test_a_read_to_buffer_past_the_buffer_is_refused(void) {
  build_controller();
  issue(AP_OMTI_CMD_READ_TO_BUFFER, 0u, 0u, 2u,
        (uint8_t)(AP_OMTI_MAX_BUFFER_BLOCKS + 1u));
  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  TEST_ASSERT_TRUE(take_status() != 0u);
}

/* And the case the old constant got wrong, which is not hypothetical: **eight**
 * blocks. Domain/OS's salvage reads a VTOC block with `1E` for eight, and this
 * model refused it with `refuse()`'s sense code -- which the operating system
 * reports as `invalid disk address` (`002398-04` p. 4-3, `00080012`). The table
 * that justified the refusal is an 8K part's: 15x512, 7x1024 and 7x1056 all
 * fall just under 8192, and this controller's identification block reports
 * 32K. */
static void test_eight_blocks_fit_a_thirty_two_kilobyte_buffer(void) {
  build_controller();
  issue(AP_OMTI_CMD_READ_TO_BUFFER, 0u, 0u, 2u, 8u);
  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  TEST_ASSERT_EQUAL_HEX8(0u, take_status());
}

/* A write goes the other way and reaches the image. */
static void test_a_write_command_reaches_the_image(void) {
  build_controller();
  issue(AP_OMTI_CMD_WRITE, 0u, 0u, 2u, 1u);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_OUT, ap_omti_disk_phase(&omti));

  for (unsigned i = 0; i < AP_AWD_SECTOR_BYTES; i++) {
    ap_omti_disk_write(&omti, AP_OMTI_DISK_DATA, 0xA5u);
  }
  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  TEST_ASSERT_EQUAL_HEX8(0u, take_status());

  /* Sector 2, and only sector 2. */
  TEST_ASSERT_EQUAL_HEX8(0xA5u, backing[2u * AP_AWD_SECTOR_BYTES]);
  TEST_ASSERT_EQUAL_HEX8(0xA5u, backing[3u * AP_AWD_SECTOR_BYTES - 1u]);
  TEST_ASSERT_EQUAL_HEX8(3u, backing[3u * AP_AWD_SECTOR_BYTES]);
}

/* §5.1.2's block count is a byte and **zero means 256**. A count of blocks with
 * no blocks in it is not a command anyone issues, and reading one sector where
 * a driver asked for 256 gives a file system that works until the first large
 * transfer. */
static void test_a_block_count_of_zero_means_two_hundred_and_fifty_six(void) {
  build_controller();
  /* This drive has sixteen sectors, so 256 runs off the end -- which is what
   * makes the count observable at all: a count of one would succeed. */
  issue(AP_OMTI_CMD_READ, 0u, 0u, 0u, 0u);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_IN, ap_omti_disk_phase(&omti));

  unsigned sectors = 0;
  while (ap_omti_disk_phase(&omti) == AP_OMTI_PHASE_DATA_IN &&
         sectors < 300u) {
    for (unsigned i = 0; i < AP_AWD_SECTOR_BYTES; i++) {
      (void)ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA);
    }
    sectors++;
  }
  /* It read every sector the drive has and then failed, rather than stopping
   * after one. */
  TEST_ASSERT_EQUAL_UINT(SMALL_SECTORS, sectors);
  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  TEST_ASSERT_TRUE((take_status() & 0x02u) != 0u);
}

/* A multi-sector read walks forward, which is the other half of the count. */
static void test_a_multi_sector_read_walks_forward(void) {
  build_controller();
  issue(AP_OMTI_CMD_READ, 0u, 0u, 0u, 3u);
  for (unsigned s = 0; s < 3u; s++) {
    TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_IN, ap_omti_disk_phase(&omti));
    for (unsigned i = 0; i < AP_AWD_SECTOR_BYTES; i++) {
      TEST_ASSERT_EQUAL_HEX8((uint8_t)s,
                             ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA));
    }
  }
  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  TEST_ASSERT_EQUAL_HEX8(0u, take_status());
}

/* An address off the drive fails, and the sense says why. That is what
 * `REQUEST SENSE` is for: a driver reads it after a completion byte with the
 * error bit set. */
static void test_a_bad_address_fails_and_the_sense_says_so(void) {
  build_controller();
  issue(AP_OMTI_CMD_READ, 7u, 0u, 0u, 1u);
  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  TEST_ASSERT_TRUE((take_status() & 0x02u) != 0u);

  issue(AP_OMTI_CMD_REQUEST_SENSE, 0u, 0u, 0u, 0u);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_IN, ap_omti_disk_phase(&omti));

  /* `A1`, not `21`. §5.4.3: "Bit 7 set to 1 indicates the validity of the
   * sector address. If bit 7 is set to 0, the sector address is not valid."
   * This core used to send `21` with three zero bytes behind it -- a controller
   * that knew where the command failed, had already recorded it for its own
   * report, and told the driver the answer was unavailable. */
  TEST_ASSERT_EQUAL_HEX8(0xA1u, ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA));

  /* And the address itself, in **Appendix A's** SENSE DATA BYTE FORMAT: byte 1
   * is `C10 | 0 | LUN | HEAD NUMBER`, byte 2 `C09 | C08 | SECTOR NUMBER`, byte
   * 3 cylinder low. Cylinder 7 on a two-cylinder drive, head 0, sector 0, and
   * **LUN 0** -- which is byte 1 bit 5 and was clear here for a different
   * reason until 2026-08-22, namely that nothing set it at all. */
  TEST_ASSERT_EQUAL_HEX8(0x00u, ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA));
  TEST_ASSERT_EQUAL_HEX8(0x00u, ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA));
  TEST_ASSERT_EQUAL_HEX8(0x07u, ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA));
  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
}

/* Appendix A's byte 1 is `C10 | 0 | LUN | HEAD NUMBER`, and **bit 5 is not
 * reachable on this machine** -- which is worth pinning rather than leaving as
 * a silence.
 *
 * The appendix's *code list* has been cited here for years -- `17`, `19`, `21`,
 * `22`, `23` all name it -- while the byte format three lines above those codes
 * had never been walked, so nothing set the LUN at all. It is set now. But the
 * only refusal that carries an address is one against a drive, and
 * `ap_omti_attach` fits a single drive at LUN 0: a command to LUN 1 is refused
 * `04 DRIVE NOT READY` **without** an address, where bytes 1-3 are zero and bit
 * 7 says so.
 *
 * So the field is right per the manual and exercised by nothing, which is the
 * same standing `AP_OMTI_ST3_READY` has. Asserted here as the reachable half --
 * LUN 0 reports LUN 0 -- so that a second drive arriving turns this into a real
 * test rather than finding a field nobody had thought about. */
static void test_the_sense_address_reports_lun_zero_and_bit_five_is_unreachable(
    void) {
  build_controller();
  issue(AP_OMTI_CMD_READ, 7u, 0u, 0u, 1u); /* cylinder 7: past the drive */
  settle();
  TEST_ASSERT_TRUE((take_status() & 0x02u) != 0u);

  issue(AP_OMTI_CMD_REQUEST_SENSE, 0u, 0u, 0u, 0u);
  const uint8_t byte0 = ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA);
  TEST_ASSERT_EQUAL_HEX8(AP_OMTI_SENSE_ADDRESS_VALID,
                         byte0 & AP_OMTI_SENSE_ADDRESS_VALID);

  /* The address is valid, so byte 1's LUN field means something -- and it is
   * zero, because the drive that failed is the only one there is. */
  const uint8_t byte1 = ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA);
  TEST_ASSERT_EQUAL_HEX8(0u, byte1 & AP_OMTI_SENSE_LUN);

  /* And the bit sits where Appendix A puts it, clear of the head field below
   * and `C10` above -- the three do not overlap. */
  TEST_ASSERT_EQUAL_HEX8(0x20u, AP_OMTI_SENSE_LUN);
  TEST_ASSERT_EQUAL_HEX8(0u, AP_OMTI_SENSE_LUN & 0x1Fu);
  TEST_ASSERT_EQUAL_HEX8(0u, AP_OMTI_SENSE_LUN & AP_OMTI_SENSE_ADDRESS_VALID);
}

/* ## `21` and `23` divide the same failure by when it happens
 *
 * Appendix A: `21 Illegal Disk Address` is "a command with a Sector Address
 * beyond the capacity of the drive"; `23 Volume Overflow` is "**after the
 * commencement of a multiblock command**, the end of volume was reached". The
 * test above is the first; this is the second, and it used to report `21` --
 * telling a driver its descriptor block was wrong when the descriptor block had
 * been accepted and sectors had already crossed the bus.
 *
 * The small drive holds sixteen sectors. A four-block read from sector 14 is a
 * legal address with an illegal end: two sectors are delivered and the third
 * runs off the volume. */
static void test_a_multiblock_read_off_the_end_is_a_volume_overflow(void) {
  build_controller();

  issue(AP_OMTI_CMD_READ, 1u, 1u, 2u, 4u); /* sector 14 of 16, four blocks */
  settle();

  /* The two sectors that exist arrive first, which is what makes this a
   * commenced command rather than a refused one. */
  for (unsigned block = 14u; block < 16u; block++) {
    TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_IN, ap_omti_disk_phase(&omti));
    for (unsigned i = 0; i < AP_AWD_SECTOR_BYTES; i++) {
      TEST_ASSERT_EQUAL_HEX8(block, ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA));
    }
  }

  /* The refusal ends the command the way any other ending does -- through the
   * drive's access time -- so the status phase is reached by settling, not
   * immediately. */
  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  TEST_ASSERT_TRUE((take_status() & 0x02u) != 0u);

  issue(AP_OMTI_CMD_REQUEST_SENSE, 0u, 0u, 0u, 0u);
  /* `A3`: the code, with §5.4.3's validity bit above it. The address is the
   * block the transfer stopped on -- sector 16, which on a two-cylinder,
   * two-head, four-sector drive normalises to cylinder 2, head 0, sector 0 --
   * and not the one the command named. A controller that reported the command's
   * address here would be describing an address that worked. */
  TEST_ASSERT_EQUAL_HEX8(0xA3u, ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA));
  TEST_ASSERT_EQUAL_HEX8(0x00u, ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA));
  TEST_ASSERT_EQUAL_HEX8(0x00u, ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA));
  TEST_ASSERT_EQUAL_HEX8(0x02u, ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA));
}

/* The same division on the write side, through the streaming data-out path:
 * `0A WRITE` with a block count that outlasts the volume. The host hands over
 * every byte of both sectors and the third has nowhere to go. */
static void test_a_multiblock_write_off_the_end_is_a_volume_overflow(void) {
  build_controller();

  issue(AP_OMTI_CMD_WRITE, 1u, 1u, 2u, 4u);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_OUT, ap_omti_disk_phase(&omti));
  for (unsigned block = 0; block < 3u; block++) {
    for (unsigned i = 0; i < AP_AWD_SECTOR_BYTES; i++) {
      ap_omti_disk_write(&omti, AP_OMTI_DISK_DATA, 0xA5u);
    }
  }
  settle();
  TEST_ASSERT_TRUE((take_status() & 0x02u) != 0u);

  issue(AP_OMTI_CMD_REQUEST_SENSE, 0u, 0u, 0u, 0u);
  TEST_ASSERT_EQUAL_HEX8(0xA3u, ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA));

  /* And the two sectors that fitted were written, rather than the whole command
   * being rolled back: `23` reports where a transfer stopped, not that it never
   * started. */
  TEST_ASSERT_EQUAL_HEX8(0xA5u, backing[14u * AP_AWD_SECTOR_BYTES]);
  TEST_ASSERT_EQUAL_HEX8(0xA5u, backing[15u * AP_AWD_SECTOR_BYTES]);
}

/* The eleven-bit cylinder crosses three bytes, and a refusal above 255 is where
 * a plausible-looking one-byte answer stops being right. Cylinder 1941 is the
 * address Domain/OS's crash path actually asks for on the 348 MB drive. */
static void test_the_refused_address_carries_the_whole_cylinder(void) {
  build_controller();

  static const ap_awd_geometry_t BIG = {
      .cylinders = 1223u, .heads = 15u, .sectors = 18u};
  /* The same backing store, now described by a geometry far larger than it --
   * so every address in it is inside the geometry and past the image, which is
   * the failure this reports. */
  TEST_ASSERT_TRUE(ap_awd_open(&drive, backing, sizeof backing, BIG, true));

  issue(AP_OMTI_CMD_READ, 1941u, 13u, 2u, 1u);
  TEST_ASSERT_TRUE((take_status() & 0x02u) != 0u);

  issue(AP_OMTI_CMD_REQUEST_SENSE, 0u, 0u, 0u, 0u);
  TEST_ASSERT_EQUAL_HEX8(0xA1u, ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA));
  /* 1941 is `111 1001 0101`: C10 set into byte 1's top bit, C09 and C08 -- both
   * set -- into byte 2's top two, and `0x95` in byte 3. */
  TEST_ASSERT_EQUAL_HEX8(0x8Du, ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA));
  TEST_ASSERT_EQUAL_HEX8(0xC2u, ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA));
  TEST_ASSERT_EQUAL_HEX8(0x95u, ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA));
}

/* --------------------------------------------------------------------------
 * The rest of §5.4, read in one pass rather than one command per boot
 * ------------------------------------------------------------------------ */

/* `0F` fills the buffer, `1F` places it. §5.4.14 and §5.4.20 are the two halves
 * of a transfer done in programmed I/O, and neither is any use without the
 * other -- which is why the round trip is the test rather than either alone. */
static void test_write_from_buffer_places_what_write_sector_buffer_staged(void) {
  build_controller();

  issue(AP_OMTI_CMD_WRITE_SECTOR_BUFFER, 0u, 0u, 0u, 1u);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_OUT, ap_omti_disk_phase(&omti));
  for (unsigned i = 0; i < AP_AWD_SECTOR_BYTES; i++) {
    ap_omti_disk_write(&omti, AP_OMTI_DISK_DATA, 0x5Au);
  }
  TEST_ASSERT_EQUAL_HEX8(0u, take_status());

  /* Cylinder 1, head 0, sector 2 is sector 10, which `build_drive` filled with
   * ten -- so a `1F` that wrote nothing is visible rather than merely wrong. */
  issue(AP_OMTI_CMD_WRITE_FROM_BUFFER, 1u, 0u, 2u, 1u);
  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  TEST_ASSERT_EQUAL_HEX8(0u, take_status());
  for (unsigned i = 0; i < AP_AWD_SECTOR_BYTES; i++) {
    TEST_ASSERT_EQUAL_HEX8(0x5Au, backing[10u * AP_AWD_SECTOR_BYTES + i]);
  }
}

/* §5.4.27: a long block is the sector "plus 6 bytes (for ESDI drives) of ECC
 * data". The width is the assertion -- a model transferring 1056 leaves a host
 * six bytes short and the phase never ends. The ECC bytes themselves are zero
 * by deliberate approximation; see `ap_omti.c`. */
static void test_a_long_read_is_the_sector_and_six_more(void) {
  build_controller();

  issue(AP_OMTI_CMD_READ_LONG, 0u, 1u, 1u, 1u); /* sector 5 */
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_IN, ap_omti_disk_phase(&omti));
  for (unsigned i = 0; i < AP_AWD_SECTOR_BYTES; i++) {
    TEST_ASSERT_EQUAL_HEX8(5u, ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA));
  }
  /* Still in the data phase after a whole sector, which a 1056-byte transfer
   * would not be. */
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_IN, ap_omti_disk_phase(&omti));
  for (unsigned i = 0; i < AP_OMTI_ECC_BYTES; i++) {
    TEST_ASSERT_EQUAL_HEX8(0u, ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA));
  }
  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
}

/* §5.4.28 inbound: the same 1062 bytes, of which the sector reaches the image
 * and the ECC is dropped -- it has nowhere in an `.awd` to go. */
static void test_a_long_write_keeps_the_sector_and_drops_the_ecc(void) {
  build_controller();

  issue(AP_OMTI_CMD_WRITE_LONG, 0u, 0u, 3u, 1u); /* sector 3 */
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_OUT, ap_omti_disk_phase(&omti));
  for (unsigned i = 0; i < AP_AWD_SECTOR_BYTES; i++) {
    ap_omti_disk_write(&omti, AP_OMTI_DISK_DATA, 0xC3u);
  }
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_OUT, ap_omti_disk_phase(&omti));
  for (unsigned i = 0; i < AP_OMTI_ECC_BYTES; i++) {
    ap_omti_disk_write(&omti, AP_OMTI_DISK_DATA, 0xEEu);
  }
  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  TEST_ASSERT_EQUAL_HEX8(0u, take_status());

  for (unsigned i = 0; i < AP_AWD_SECTOR_BYTES; i++) {
    TEST_ASSERT_EQUAL_HEX8(0xC3u, backing[3u * AP_AWD_SECTOR_BYTES + i]);
  }
  /* The ECC did not run over the next sector, which is what a model staging
   * 1062 bytes and writing 1062 bytes would have done. */
  TEST_ASSERT_EQUAL_HEX8(4u, backing[4u * AP_AWD_SECTOR_BYTES]);
}

/* §5.4.22: 256 bytes, a six-byte header carrying the head, and "five FFh bytes
 * indicate the end of the DEFECT LIST" -- which follow the header directly on a
 * surface with no defects, and an `.awd` image is exactly that. */
static void test_the_defect_list_is_a_header_and_a_terminator(void) {
  build_controller();

  issue(AP_OMTI_CMD_READ_ESDI_DEFECT_LIST, 0u, 1u, 0u, 0u);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_IN, ap_omti_disk_phase(&omti));

  uint8_t list[AP_OMTI_DEFECT_LIST_BYTES];
  for (unsigned i = 0; i < sizeof list; i++) {
    list[i] = ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA);
  }
  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));

  /* Bytes 0-2 are the date the list was recorded: zero, because nothing
   * recorded one and a wall clock is not available to this core. */
  for (unsigned i = 0; i < 3u; i++) {
    TEST_ASSERT_EQUAL_HEX8(0u, list[i]);
  }
  TEST_ASSERT_EQUAL_HEX8(1u, list[3]); /* the head asked for */
  for (unsigned i = 0; i < 5u; i++) {
    TEST_ASSERT_EQUAL_HEX8(0xFFu, list[6u + i]);
  }
}

/* §5.4.6, and §5.4.4 for the pattern: "all data fields are written with the
 * pattern `6Ch`". One track, and only that track -- a format that ran on is the
 * failure that destroys a disk rather than merely reporting wrongly. */
static void test_a_track_format_writes_six_c_over_that_track_alone(void) {
  build_controller();

  issue(AP_OMTI_CMD_FORMAT_TRACK, 0u, 1u, 0u, 0u); /* sectors 4 through 7 */
  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  TEST_ASSERT_EQUAL_HEX8(0u, take_status());

  for (unsigned s = 4u; s < 8u; s++) {
    TEST_ASSERT_EQUAL_HEX8(0x6Cu, backing[s * AP_AWD_SECTOR_BYTES]);
    TEST_ASSERT_EQUAL_HEX8(
        0x6Cu, backing[s * AP_AWD_SECTOR_BYTES + AP_AWD_SECTOR_BYTES - 1u]);
  }
  TEST_ASSERT_EQUAL_HEX8(3u, backing[3u * AP_AWD_SECTOR_BYTES]);
  TEST_ASSERT_EQUAL_HEX8(8u, backing[8u * AP_AWD_SECTOR_BYTES]);
}

/* §5.4.4: "Formatting starts at the specified track and proceeds until the last
 * track of the unit is formatted." Everything from the named track on, and
 * nothing before it. */
static void test_a_drive_format_runs_to_the_end_of_the_unit(void) {
  build_controller();

  issue(AP_OMTI_CMD_FORMAT_DRIVE, 1u, 0u, 0u, 0u); /* sector 8 onwards */
  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  TEST_ASSERT_EQUAL_HEX8(0u, take_status());

  for (unsigned s = 0; s < 8u; s++) {
    TEST_ASSERT_EQUAL_HEX8((uint8_t)s, backing[s * AP_AWD_SECTOR_BYTES]);
  }
  for (unsigned s = 8u; s < SMALL_SECTORS; s++) {
    TEST_ASSERT_EQUAL_HEX8(0x6Cu, backing[s * AP_AWD_SECTOR_BYTES]);
  }
}

/* §5.4.16's four-byte descriptor arrives in a data-out phase, and the alternate
 * track "is then formatted" -- so the command is not complete until the bytes
 * have been handed over, and the effect lands on the track they name. */
static void test_assigning_an_alternate_formats_the_track_the_host_names(void) {
  build_controller();

  issue(AP_OMTI_CMD_ASSIGN_ALTERNATE, 0u, 0u, 0u, 0u);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_OUT, ap_omti_disk_phase(&omti));

  /* Cylinder 1, head 1: sectors 12 through 15. The descriptor's fields sit
   * where a descriptor block's bytes 1-3 do. */
  static const uint8_t descriptor[4] = {0x01u, 0x00u, 0x01u, 0x00u};
  for (unsigned i = 0; i < sizeof descriptor; i++) {
    TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_OUT, ap_omti_disk_phase(&omti));
    ap_omti_disk_write(&omti, AP_OMTI_DISK_DATA, descriptor[i]);
  }
  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  TEST_ASSERT_EQUAL_HEX8(0u, take_status());

  for (unsigned s = 12u; s < 16u; s++) {
    TEST_ASSERT_EQUAL_HEX8(0x6Cu, backing[s * AP_AWD_SECTOR_BYTES]);
  }
  TEST_ASSERT_EQUAL_HEX8(11u, backing[11u * AP_AWD_SECTOR_BYTES]);
}

/* §5.4.21 COPY: ten bytes, "no data is transferred to the host", and the
 * destination address lives in bytes 5-7 in the layout the source uses in 1-3.
 * Reading the destination back is the only way to tell a copy that ran from one
 * that reported success and did nothing. */
static void test_a_copy_moves_blocks_between_two_addresses(void) {
  build_controller();

  /* Source cylinder 0 head 0 sector 1 (sector 1), destination cylinder 1 head 1
   * sector 0 (sector 12), two blocks. */
  static const uint8_t cdb[AP_OMTI_CDB_LONG] = {
      AP_OMTI_CMD_COPY, 0x00u, 0x01u, 0x00u, 0x02u,
      0x01u,            0x00u, 0x01u, 0x00u, 0x00u};
  ap_omti_disk_write(&omti, AP_OMTI_DISK_CONFIG, 0x00); /* SELECT */
  for (unsigned i = 0; i < sizeof cdb; i++) {
    ap_omti_disk_write(&omti, AP_OMTI_DISK_DATA, cdb[i]);
  }

  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  TEST_ASSERT_EQUAL_HEX8(0u, take_status());
  TEST_ASSERT_EQUAL_HEX8(1u, backing[12u * AP_AWD_SECTOR_BYTES]);
  TEST_ASSERT_EQUAL_HEX8(2u, backing[13u * AP_AWD_SECTOR_BYTES]);
  /* The source is unchanged, and the block after the copy is untouched. */
  TEST_ASSERT_EQUAL_HEX8(1u, backing[1u * AP_AWD_SECTOR_BYTES]);
  TEST_ASSERT_EQUAL_HEX8(14u, backing[14u * AP_AWD_SECTOR_BYTES]);
}

/* Appendix A names this command inside `22`'s own description: "a Change
 * Cartridge command (HEX 1B) was issued to a LUN assigned as a Fixed drive
 * type". The DN3500's Winchester is fixed, so `1B` always fails here -- and for
 * a documented reason rather than by falling off the end of the switch. */
static void test_change_cartridge_on_a_fixed_drive_is_an_illegal_function(void) {
  build_controller();

  issue(AP_OMTI_CMD_CHANGE_CARTRIDGE, 0u, 0u, 0u, 0u);
  TEST_ASSERT_TRUE((take_status() & 0x02u) != 0u);

  issue(AP_OMTI_CMD_REQUEST_SENSE, 0u, 0u, 0u, 0u);
  TEST_ASSERT_EQUAL_HEX8(0x22u, ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA));
}

/* ## A write-protected drive is not a bad address
 *
 * Appendix A, `17 Write Protected`: "during a WRITE/FORMAT command, the
 * controller detected a WRITE PROTECTED signal from the selected Logical Unit
 * Number." An image opened read-only *is* that drive.
 *
 * This cost a boot. Domain/OS's first write is a `1F` to cylinder 0, head 0,
 * sector 1 -- the second sector of the disk, and about as valid an address as
 * exists. It came back `21 ILLEGAL DISK ADDRESS`, which sent the operating
 * system to check a geometry that was correct, and it died there. The same lie
 * the unimplemented-command arm used to tell, from a different arm.
 */
static void test_a_write_to_a_read_only_image_reports_write_protected(void) {
  build_drive();
  /* The same backing store, opened read-only. */
  TEST_ASSERT_TRUE(ap_awd_open(&drive, backing, sizeof backing, SMALL, false));
  ap_omti_reset(&omti);
  ap_omti_attach(&omti, &drive);
  ap_omti_advance(&omti, AP_OMTI_RESET_TIME); /* §4.3's 100 µs */

  /* Every command that puts something on the surface, and the address each is
   * given is *valid* -- which is the point. A model that checked the address
   * first would refuse them all for the wrong reason. */
  static const uint8_t writers[] = {
      AP_OMTI_CMD_WRITE,          AP_OMTI_CMD_WRITE_FROM_BUFFER,
      AP_OMTI_CMD_WRITE_LONG,     AP_OMTI_CMD_FORMAT_DRIVE,
      AP_OMTI_CMD_FORMAT_TRACK,   AP_OMTI_CMD_FORMAT_BAD_TRACK,
      AP_OMTI_CMD_ASSIGN_ALTERNATE,
  };
  for (unsigned i = 0; i < sizeof writers; i++) {
    issue(writers[i], 0u, 0u, 1u, 1u);
    settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
    TEST_ASSERT_TRUE((take_status() & 0x02u) != 0u);

    issue(AP_OMTI_CMD_REQUEST_SENSE, 0u, 0u, 0u, 0u);
    /* `17`, and **bit 7 clear**: §5.4.3 gives that bit as the validity of the
     * sector address, and this failure is not about the sector address. */
    TEST_ASSERT_EQUAL_HEX8(0x17u, ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA));
    for (unsigned b = 1; b < 4u; b++) {
      (void)ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA);
    }
    /* And the status byte the sense read left waiting. §4.3 ends every command
     * with it, and a controller still in the status phase ignores a write to
     * the data port -- so skipping it here would feed the next command's
     * descriptor to a controller that is talking, not listening. */
    (void)take_status();
  }

  /* Reading the same drive is unaffected: write protection protects writes. */
  issue(AP_OMTI_CMD_READ, 0u, 0u, 1u, 1u);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_IN, ap_omti_disk_phase(&omti));
  TEST_ASSERT_EQUAL_HEX8(1u, ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA));
}

/* `07 FORMAT BAD TRACK` is no longer identical to `06`, because the sidecar
 * gives the ID field somewhere to live.
 *
 * Appendix A `19 Bad Track Encountered`: "the specified track has previously
 * been formatted with the BAD TRACK FLAG set in the ID field. It is not
 * possible to access data on this track." Both halves -- setting the flag and
 * refusing on it -- were unreachable while an `.awd` had nowhere to keep it. */
static void test_a_bad_track_format_is_refused_afterwards(void) {
  build_controller();
  build_meta();
  TEST_ASSERT_TRUE(ap_awd_attach_meta(&drive, meta_file, sizeof meta_file));

  /* Cylinder 0 head 1 is sectors 4 through 7, and reads fine to begin with. */
  issue(AP_OMTI_CMD_READ, 0u, 1u, 0u, 1u);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_IN, ap_omti_disk_phase(&omti));
  while (ap_omti_disk_phase(&omti) == AP_OMTI_PHASE_DATA_IN) {
    (void)ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA);
  }
  (void)take_status();

  issue(AP_OMTI_CMD_FORMAT_BAD_TRACK, 0u, 1u, 0u, 0u);
  TEST_ASSERT_EQUAL_HEX8(0u, take_status());
  TEST_ASSERT_EQUAL_HEX8(AP_AWD_FLAG_BAD_TRACK, ap_awd_flags(&drive, 4u));
  TEST_ASSERT_EQUAL_HEX8(AP_AWD_FLAG_BAD_TRACK, ap_awd_flags(&drive, 7u));

  /* Now the track refuses, with the code that says why. */
  issue(AP_OMTI_CMD_READ, 0u, 1u, 0u, 1u);
  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  TEST_ASSERT_TRUE((take_status() & 0x02u) != 0u);
  issue(AP_OMTI_CMD_REQUEST_SENSE, 0u, 0u, 0u, 0u);
  TEST_ASSERT_EQUAL_HEX8(0x19u, ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA));

  /* And its neighbours do not: the flag is the track's, not the drive's. */
  for (unsigned i = 1; i < 4u; i++) {
    (void)ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA);
  }
  (void)take_status();
  issue(AP_OMTI_CMD_READ, 0u, 0u, 0u, 1u);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_IN, ap_omti_disk_phase(&omti));
}

/* §5.4.28 hands the controller six ECC bytes and §5.4.27 returns them. They
 * used to be dropped and zeroed for want of anywhere to keep them. */
static void test_a_long_write_records_its_ecc_for_a_long_read(void) {
  build_controller();
  build_meta();
  TEST_ASSERT_TRUE(ap_awd_attach_meta(&drive, meta_file, sizeof meta_file));

  issue(AP_OMTI_CMD_WRITE_LONG, 0u, 0u, 2u, 1u);
  for (unsigned i = 0; i < AP_AWD_SECTOR_BYTES; i++) {
    ap_omti_disk_write(&omti, AP_OMTI_DISK_DATA, 0x5Au);
  }
  for (unsigned i = 0; i < AP_OMTI_ECC_BYTES; i++) {
    ap_omti_disk_write(&omti, AP_OMTI_DISK_DATA, (uint8_t)(0xE0u + i));
  }
  TEST_ASSERT_EQUAL_HEX8(0u, take_status());

  issue(AP_OMTI_CMD_READ_LONG, 0u, 0u, 2u, 1u);
  for (unsigned i = 0; i < AP_AWD_SECTOR_BYTES; i++) {
    TEST_ASSERT_EQUAL_HEX8(0x5Au, ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA));
  }
  for (unsigned i = 0; i < AP_OMTI_ECC_BYTES; i++) {
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(0xE0u + i),
                           ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA));
  }
}

/* A controller with no drive is a real configuration and must not look like one
 * with a blank disk: the command fails and says the drive is not ready. */
static void test_a_controller_with_no_drive_says_so(void) {
  ap_omti_reset(&omti);
  ap_omti_attach(&omti, NULL);
  ap_omti_advance(&omti, AP_OMTI_RESET_TIME); /* §4.3's 100 µs */

  issue(AP_OMTI_CMD_TEST_DRIVE_READY, 0u, 0u, 0u, 0u);
  TEST_ASSERT_TRUE((take_status() & 0x02u) != 0u);

  issue(AP_OMTI_CMD_REQUEST_SENSE, 0u, 0u, 0u, 0u);
  TEST_ASSERT_EQUAL_HEX8(0x04u, ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA));
}

/* The ST506-only command is refused by an ESDI controller, which the CDB layer
 * already knew and which the command phase must actually act on -- a decoder
 * that says no beside an executor that says yes is worse than neither. */
static void test_the_st506_only_command_is_refused_in_practice(void) {
  build_controller();
  issue(AP_OMTI_CMD_INITIALIZE_DRIVE_CHARACTERISTICS, 0u, 0u, 0u, 1u);
  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  TEST_ASSERT_TRUE((take_status() & 0x02u) != 0u);
}


/* And with DMA enabled the same command *does* request a cycle. The two modes
 * are §4.3's, and the MASK bit is what chooses between them -- so a model
 * setting `DREQ` unconditionally cannot tell them apart. */
static void test_dma_enable_is_what_asks_for_a_cycle(void) {
  build_controller();
  ap_omti_disk_write(&omti, AP_OMTI_DISK_MASK, AP_OMTI_MASK_DMA_ENABLE);
  issue(AP_OMTI_CMD_READ, 1u, 1u, 3u, 1u);

  const uint8_t status = ap_omti_disk_read(&omti, AP_OMTI_DISK_STATUS);
  TEST_ASSERT_TRUE((status & AP_OMTI_ST_DREQ) != 0u);
  /* `REQ` is still the handshake; DMA adds a request, it does not replace one. */
  TEST_ASSERT_TRUE((status & AP_OMTI_ST_REQ) != 0u);
}

/* The selection handshake, which is what a driver waits on before it can send
 * anything. §4.3: the controller asserts `BSY`, "then enters the command
 * state", sets `C/D`, and sets `REQ` "asking for the first command byte". A
 * model asserting only `BSY` leaves the host waiting for a request that never
 * comes -- which is what a `FORCE LOAD` did, timing out six times over. */
static void test_selecting_asks_for_the_first_command_byte(void) {
  build_controller();
  ap_omti_disk_write(&omti, AP_OMTI_DISK_CONFIG, 0x00u); /* SELECT */

  const uint8_t status = ap_omti_disk_read(&omti, AP_OMTI_DISK_STATUS);
  TEST_ASSERT_TRUE((status & AP_OMTI_ST_BSY) != 0u);
  TEST_ASSERT_TRUE((status & AP_OMTI_ST_CD) != 0u);
  TEST_ASSERT_TRUE((status & AP_OMTI_ST_REQ) != 0u);
  /* The transfer is *to* the controller. */
  TEST_ASSERT_TRUE((status & AP_OMTI_ST_IO) == 0u);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_COMMAND, ap_omti_disk_phase(&omti));
}

/* "When the command byte is written, the controller de-asserts the REQ bit" --
 * the write *is* the acknowledgement -- "This handshaking is repeated until all
 * command bytes are transferred. C/D is then de-asserted." */
static void test_each_command_byte_clears_and_re_asserts_the_request(void) {
  build_controller();
  ap_omti_disk_write(&omti, AP_OMTI_DISK_CONFIG, 0x00u);

  /* A six-byte block: five bytes leave the request standing, the sixth does
   * not. */
  for (unsigned i = 0; i < 5u; i++) {
    ap_omti_disk_write(&omti, AP_OMTI_DISK_DATA,
                       i == 0u ? AP_OMTI_CMD_TEST_DRIVE_READY : 0x00u);
    const uint8_t status = ap_omti_disk_read(&omti, AP_OMTI_DISK_STATUS);
    TEST_ASSERT_TRUE((status & AP_OMTI_ST_REQ) != 0u);
    TEST_ASSERT_TRUE((status & AP_OMTI_ST_CD) != 0u);
  }
  ap_omti_disk_write(&omti, AP_OMTI_DISK_DATA, 0x00u);
  /* The command ran, so what is waiting now is a *status* byte -- `C/D` set
   * again for a different reason, and `I/O` with it. */
  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
}

/* "The IDLE STATE is the only time the controller will respond to a select
 * request." A stray select part-way through a command must not restart the
 * sequence and discard what the driver has already sent. */
static void test_a_select_while_busy_is_ignored(void) {
  build_controller();
  ap_omti_disk_write(&omti, AP_OMTI_DISK_CONFIG, 0x00u);
  ap_omti_disk_write(&omti, AP_OMTI_DISK_DATA, AP_OMTI_CMD_READ);
  ap_omti_disk_write(&omti, AP_OMTI_DISK_CONFIG, 0x00u); /* stray */

  /* Still one byte in, not back at the start. */
  ap_omti_disk_write(&omti, AP_OMTI_DISK_DATA, 0x00u);
  ap_omti_disk_write(&omti, AP_OMTI_DISK_DATA, 0x00u);
  ap_omti_disk_write(&omti, AP_OMTI_DISK_DATA, 0x01u);
  ap_omti_disk_write(&omti, AP_OMTI_DISK_DATA, 0x01u);
  ap_omti_disk_write(&omti, AP_OMTI_DISK_DATA, 0x00u);
  TEST_ASSERT_NOT_EQUAL_INT(AP_OMTI_PHASE_COMMAND, ap_omti_disk_phase(&omti));
}


/* §5.4.29's READ CONFIGURATION, which §5.1.2's summary table calls READ
 * CAPACITY -- the same code under two names in one manual. Ten bytes, and the
 * three "(-1)" fields are the trap. */
static void test_read_configuration_reports_the_highest_not_the_count(void) {
  build_controller();
  issue(AP_OMTI_CMD_READ_CONFIGURATION, 0u, 0u, 0u, 0u);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_IN, ap_omti_disk_phase(&omti));

  /* The attached drive's own geometry, not the reference drive's -- this suite
   * builds a small one so a backing store fits on the stack. */
  const ap_awd_geometry_t g = drive.geometry;
  uint8_t reply[AP_OMTI_CONFIGURATION_BYTES];
  for (unsigned i = 0; i < AP_OMTI_CONFIGURATION_BYTES; i++) {
    reply[i] = ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA);
  }

  /* **One less than the count**: the highest valid number, not how many there
   * are. A model returning the counts describes a drive one cylinder, one head
   * and one sector larger than it has. */
  const uint16_t highest = (uint16_t)(g.cylinders - 1u);
  TEST_ASSERT_EQUAL_HEX8((uint8_t)(highest >> 8), reply[0]);
  TEST_ASSERT_EQUAL_HEX8((uint8_t)highest, reply[1]);
  TEST_ASSERT_EQUAL_HEX8((uint8_t)(g.heads - 1u), reply[2]);
  TEST_ASSERT_EQUAL_HEX8((uint8_t)(g.sectors - 1u), reply[3]);
  /* And they are not the counts, which is the assertion that fails on the
   * obvious implementation. */
  TEST_ASSERT_NOT_EQUAL_HEX8((uint8_t)g.heads, reply[2]);
  TEST_ASSERT_NOT_EQUAL_HEX8((uint8_t)g.sectors, reply[3]);

  /* Ten bytes and then the completion, not a whole sector: the transfer length
   * belongs to the command that started it, not to the buffer. */
  /* The drive configuration word, which the manual names and does not define
   * for this drive: the oracle's `set_configuration_data` writes `02 44`. The
   * same function computes bytes 0-3 the way the page image says, which is what
   * makes it a corroboration rather than a substitute. */
  TEST_ASSERT_EQUAL_HEX8(0x02u, reply[4]);
  TEST_ASSERT_EQUAL_HEX8(0x44u, reply[5]);
  /* And the gaps and sync fields are zero on both sides, so these zeros are an
   * answer rather than an omission. */
  for (unsigned i = 6; i < AP_OMTI_CONFIGURATION_BYTES; i++) {
    TEST_ASSERT_EQUAL_HEX8(0x00u, reply[i]);
  }

  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
}

/* The data phase requests each byte in turn, and the read is what acknowledges
 * it. A transfer that left `REQ` standing after the last byte would have a
 * driver reading past the end of what it asked for. */
static void test_the_data_phase_requests_each_byte_and_stops(void) {
  build_controller();
  issue(AP_OMTI_CMD_READ_CONFIGURATION, 0u, 0u, 0u, 0u);

  for (unsigned i = 0; i < AP_OMTI_CONFIGURATION_BYTES - 1u; i++) {
    TEST_ASSERT_TRUE((ap_omti_disk_read(&omti, AP_OMTI_DISK_STATUS) &
                      AP_OMTI_ST_REQ) != 0u);
    (void)ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA);
  }
  /* The last byte ends the data phase, and what is requested next is the
   * completion -- a *status* byte, so `C/D` is set again. */
  (void)ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA);
  const uint8_t status = ap_omti_disk_read(&omti, AP_OMTI_DISK_STATUS);
  TEST_ASSERT_TRUE((status & AP_OMTI_ST_CD) != 0u);
  TEST_ASSERT_TRUE((status & AP_OMTI_ST_REQ) != 0u);
}


/* The same handshake on the way **out**, which is the half that was missing.
 *
 * §4.3's data state: "In the programmed I/O mode, data is transferred by
 * handshaking in the same fashion as the command transfer. When the controller
 * requires a word to be transferred, it will set the REQ bit in the STATUS
 * byte ... Either action will cause REQ to be cleared. These steps will be
 * repeated until all the data required by the controller has been transferred."
 *
 * `take_byte` clears `REQ` on every write, and only the command phase and the
 * data-in side asked for the next byte again. So a controller in `DATA OUT`
 * took one byte and then sat at `C8` -- selected, busy, asking for nothing --
 * and a driver that honours the handshake waits for ever. Domain/OS does honour
 * it: `WIN_$SPIN_DOWN` polled that byte 3,301 times and returned `00080003`,
 * disk controller time-out.
 *
 * Every other write test in this suite writes its bytes without consulting
 * `REQ`, which is exactly why a green suite said nothing about it. This one
 * consults it for every byte. */
static void test_the_data_out_phase_requests_each_byte_too(void) {
  build_controller();
  issue(AP_OMTI_CMD_WRITE, 0u, 0u, 2u, 1u);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_OUT, ap_omti_disk_phase(&omti));

  for (unsigned i = 0; i < AP_AWD_SECTOR_BYTES; i++) {
    /* The controller must be asking for this byte before the host writes it. */
    TEST_ASSERT_TRUE((ap_omti_disk_read(&omti, AP_OMTI_DISK_STATUS) &
                      AP_OMTI_ST_REQ) != 0u);
    ap_omti_disk_write(&omti, AP_OMTI_DISK_DATA, (uint8_t)(i & 0xFFu));
  }

  /* And the sector that arrived that way reached the image. */
  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  TEST_ASSERT_EQUAL_HEX8(0u, take_status());
}

/* A multi-block write asks across the sector boundary as well: the next
 * sector's first byte is data the controller still requires. */
static void test_a_multi_block_write_keeps_asking_across_sectors(void) {
  build_controller();
  issue(AP_OMTI_CMD_WRITE, 0u, 0u, 2u, 2u);

  for (unsigned i = 0; i < 2u * AP_AWD_SECTOR_BYTES; i++) {
    TEST_ASSERT_TRUE((ap_omti_disk_read(&omti, AP_OMTI_DISK_STATUS) &
                      AP_OMTI_ST_REQ) != 0u);
    ap_omti_disk_write(&omti, AP_OMTI_DISK_DATA, 0x5Au);
  }
  settle();
  TEST_ASSERT_EQUAL_HEX8(0u, take_status());
}

/* And the buffer-only direction, `0F WRITE DATA TO SECTOR BUFFER`, which is the
 * command the boot actually hung on. */
static void test_a_buffer_write_requests_each_byte(void) {
  build_controller();
  issue(AP_OMTI_CMD_WRITE_SECTOR_BUFFER, 0u, 0u, 0u, 2u);

  for (unsigned i = 0; i < 2u * AP_AWD_SECTOR_BYTES; i++) {
    TEST_ASSERT_TRUE((ap_omti_disk_read(&omti, AP_OMTI_DISK_STATUS) &
                      AP_OMTI_ST_REQ) != 0u);
    ap_omti_disk_write(&omti, AP_OMTI_DISK_DATA, 0xA5u);
  }
  settle();
  TEST_ASSERT_EQUAL_HEX8(0u, take_status());
}

/* §5.4.24's READ ID: the addressed sector's ID field, four bytes. It is the
 * address written back in the format the *disk* carries, which is why a driver
 * uses it to find out where a head actually is. */
static void test_read_id_returns_the_address_the_disk_carries(void) {
  build_controller();
  issue(AP_OMTI_CMD_READ_ID, 1u, 1u, 3u, 0u);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_IN, ap_omti_disk_phase(&omti));

  uint8_t id[AP_OMTI_READ_ID_BYTES];
  for (unsigned i = 0; i < AP_OMTI_READ_ID_BYTES; i++) {
    id[i] = ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA);
  }
  /* The cylinder is split: the top three bits in byte 0, the low eight in
   * byte 1. Reassembling it from byte 1 alone works on any disk under 256
   * cylinders, which is the shape of bug the CDB decoder exists to avoid and
   * the reply has the same shape. */
  TEST_ASSERT_EQUAL_HEX8(0x00u, id[0]);
  TEST_ASSERT_EQUAL_HEX8(0x01u, id[1]);
  /* Flags clear -- a raw sector image has no bad tracks and no alternates --
   * with the head in the low nibble. */
  TEST_ASSERT_EQUAL_HEX8(0x01u, id[2]);
  TEST_ASSERT_EQUAL_HEX8(0u, (uint8_t)(id[2] & (AP_OMTI_ID_FLAG_BAD |
                                                AP_OMTI_ID_FLAG_ALTERNATE)));
  TEST_ASSERT_EQUAL_HEX8(0x03u, id[3]);
  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
}

/* §5.1.2 gives READ VERIFY zero data bytes: it reads and checks without
 * transferring, so what it reports is whether the sectors are *there*. A model
 * that answered without reading would answer for a disk it never touched. */
static void test_read_verify_transfers_nothing_and_still_reads(void) {
  build_controller();
  issue(AP_OMTI_CMD_READ_VERIFY, 0u, 0u, 1u, 2u);
  /* Straight to the completion: no data phase at all. */
  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  TEST_ASSERT_EQUAL_HEX8(0x00u, take_status());

  /* And a verify off the end of the image fails, which is what makes it a
   * check rather than an acknowledgement. */
  build_controller();
  issue(AP_OMTI_CMD_READ_VERIFY, 0u, 0u, 1u, 255u);
  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  TEST_ASSERT_NOT_EQUAL_HEX8(0x00u, take_status());
}

/* The controller's own diagnostics touch no drive and have no fault to report
 * -- a model failing them would be claiming a defect it does not have. The
 * *drive* diagnostic does need one, and reports what reading every track's
 * sector 0 would. */
static void test_the_diagnostics_report_what_they_can_see(void) {
  build_controller();
  issue(AP_OMTI_CMD_RAM_DIAGNOSTICS, 0u, 0u, 0u, 0u);
  TEST_ASSERT_EQUAL_HEX8(0x00u, take_status());

  build_controller();
  issue(AP_OMTI_CMD_CONTROLLER_DIAGNOSTIC, 0u, 0u, 0u, 0u);
  TEST_ASSERT_EQUAL_HEX8(0x00u, take_status());

  build_controller();
  issue(AP_OMTI_CMD_DRIVE_DIAGNOSTIC, 0u, 0u, 0u, 0u);
  TEST_ASSERT_EQUAL_HEX8(0x00u, take_status());

  /* With no drive attached the controller's own tests still pass and the
   * drive's does not, which is the distinction the three exist to draw. */
  ap_omti_reset(&omti);
  ap_omti_advance(&omti, AP_OMTI_RESET_TIME); /* §4.3's 100 µs */
  issue(AP_OMTI_CMD_RAM_DIAGNOSTICS, 0u, 0u, 0u, 0u);
  TEST_ASSERT_EQUAL_HEX8(0x00u, take_status());
  ap_omti_reset(&omti);
  ap_omti_advance(&omti, AP_OMTI_RESET_TIME);
  issue(AP_OMTI_CMD_DRIVE_DIAGNOSTIC, 0u, 0u, 0u, 0u);
  TEST_ASSERT_NOT_EQUAL_HEX8(0x00u, take_status());
}

/* The discriminating case for §5.1.1's LUN, which needs a drive *attached*:
 * with one fitted, LUN 0 must succeed and LUN 1 must fail. The same pair
 * against a controller with no drive cannot tell the two apart, because both
 * fail then -- so a suite that only tested the bare controller would stay green
 * with the LUN ignored entirely, which is exactly what happened.
 *
 * Measured against the oracle on a Domain/OS boot before this was fixed: `00
 * TEST DRIVE READY` for LUN 1 completed successfully, the firmware printed
 * `DRIVE 1  PASSED.` where MAME prints `DRIVE 1  (NOT FOUND).`, and 271 later
 * reads addressed to the absent drive were answered from drive 0's image. */
static void test_a_command_for_an_unfitted_lun_is_refused(void) {
  build_controller();

  /* LUN 0, the drive that is there. */
  const uint8_t lun0[6] = {AP_OMTI_CMD_TEST_DRIVE_READY, 0x00u, 0, 0, 0, 0};
  for (unsigned i = 0; i < sizeof lun0; i++) {
    ap_omti_disk_write(&omti, AP_OMTI_DISK_DATA, lun0[i]);
  }
  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  /* §5.3 bit 1 clear: the command completed. And bit 5 clear: unit 0. */
  TEST_ASSERT_EQUAL_HEX8(0x00u, take_status());

  /* LUN 1, byte 1 bit 5, which this machine does not have fitted. */
  build_controller();
  const uint8_t lun1[6] = {AP_OMTI_CMD_TEST_DRIVE_READY, 0x20u, 0, 0, 0, 0};
  for (unsigned i = 0; i < sizeof lun1; i++) {
    ap_omti_disk_write(&omti, AP_OMTI_DISK_DATA, lun1[i]);
  }
  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  /* Bit 1 set for the error, and bit 5 set because §5.3 puts the addressed
   * LUN in the completion byte whether the command succeeded or not. */
  TEST_ASSERT_EQUAL_HEX8(0x22u, take_status());
}

/* And the data path, not just the ready test: a READ for the absent unit must
 * not be answered out of the fitted drive's image. */
static void test_a_read_for_an_unfitted_lun_returns_no_data(void) {
  build_controller();
  const uint8_t cdb[6] = {AP_OMTI_CMD_READ, 0x20u, 0u, 0u, 0u, 1u};
  for (unsigned i = 0; i < sizeof cdb; i++) {
    ap_omti_disk_write(&omti, AP_OMTI_DISK_DATA, cdb[i]);
  }
  /* Status, not data: there is nothing to transfer. */
  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  TEST_ASSERT_EQUAL_HEX8(0x22u, take_status());
}

/* ## The access time, which is not a refinement but a requirement
 *
 * A command that moves the heads must not complete in the register write that
 * issued it. Domain/OS is the arbiter: with a zero access time, `IRQ14` from a
 * `READ DATA TO BUFFER` lands inside the kernel's page-fault handler and the
 * kernel calls `crash_system`. MAME carries the same fix and its comment says
 * why -- "Domain/OS doesn't expect zero access time".
 */
static void test_a_read_does_not_complete_in_the_instant_it_is_issued(void) {
  build_controller();
  ap_omti_disk_write(&omti, AP_OMTI_DISK_MASK, AP_OMTI_MASK_INTERRUPT_ENABLE);
  issue(AP_OMTI_CMD_READ_TO_BUFFER, 0u, 0u, 0u, 1u);
  /* The drive is positioning: no completion byte, and above all no interrupt,
   * which is the whole point of the delay. */
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_EXECUTING, ap_omti_disk_phase(&omti));
  TEST_ASSERT_FALSE(ap_omti_disk_irq(&omti));
  const uint8_t busy = ap_omti_disk_read(&omti, AP_OMTI_DISK_STATUS);
  TEST_ASSERT_TRUE((busy & AP_OMTI_ST_BSY) != 0u);
  TEST_ASSERT_TRUE((busy & AP_OMTI_ST_REQ) == 0u);

  /* One base unit short of the deadline it is still positioning. */
  ap_omti_advance(&omti, omti.completion_at - 1u);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_EXECUTING, ap_omti_disk_phase(&omti));
  TEST_ASSERT_FALSE(ap_omti_disk_irq(&omti));

  /* On it, the completion is announced and the interrupt is raised. */
  ap_omti_advance(&omti, omti.completion_at);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  TEST_ASSERT_TRUE(ap_omti_disk_irq(&omti));
}

/* The duration is the drive's figures, not a number chosen to make a boot
 * survive: an average seek, half a revolution, and the transfer itself. */
static void test_the_access_time_is_a_seek_a_half_turn_and_the_transfer(void) {
  build_controller();
  issue(AP_OMTI_CMD_READ_TO_BUFFER, 0u, 0u, 0u, 1u);
  const ap_time_t one_sector =
      AP_OMTI_AVERAGE_SEEK + AP_OMTI_AVERAGE_LATENCY +
      (ap_time_t)((uint64_t)AP_TIME_BASE_HZ * AP_AWD_SECTOR_BYTES /
                  AP_OMTI_TRANSFER_BYTES_PER_SEC);
  /* The **duration**, not the deadline. Once §4.3's reset state exists the
   * controller is no longer issued its first command at time zero, and a test
   * about the drive's figures must not depend on when the harness started. */
  TEST_ASSERT_EQUAL_UINT64(one_sector, omti.completion_at - omti.now);

  /* Two sectors cost one more transfer and no more positioning: the heads are
   * already there and the second sector follows the first. */
  build_controller();
  issue(AP_OMTI_CMD_READ_TO_BUFFER, 0u, 0u, 0u, 2u);
  const ap_time_t two_sectors =
      one_sector + (ap_time_t)((uint64_t)AP_TIME_BASE_HZ * AP_AWD_SECTOR_BYTES /
                               AP_OMTI_TRANSFER_BYTES_PER_SEC);
  TEST_ASSERT_EQUAL_UINT64(two_sectors, omti.completion_at - omti.now);
}

/* The division is physical: a command answered out of the controller's own
 * registers touches no surface, so it costs nothing and completes at once. */
static void test_a_command_that_touches_no_surface_completes_at_once(void) {
  build_controller();
  issue(AP_OMTI_CMD_TEST_DRIVE_READY, 0u, 0u, 0u, 0u);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  TEST_ASSERT_FALSE(ap_omti_cdb_touches_surface(AP_OMTI_CMD_TEST_DRIVE_READY));
  TEST_ASSERT_FALSE(ap_omti_cdb_touches_surface(AP_OMTI_CMD_READ_SECTOR_BUFFER));
  TEST_ASSERT_FALSE(ap_omti_cdb_touches_surface(AP_OMTI_CMD_READ_CAPACITY));
  /* And the ones that do move the heads, including the two that only move
   * them: a SEEK transfers nothing and still costs the positioning. */
  TEST_ASSERT_TRUE(ap_omti_cdb_touches_surface(AP_OMTI_CMD_SEEK));
  TEST_ASSERT_TRUE(ap_omti_cdb_touches_surface(AP_OMTI_CMD_RECALIBRATE));
  TEST_ASSERT_TRUE(ap_omti_cdb_touches_surface(AP_OMTI_CMD_READ));
  TEST_ASSERT_TRUE(ap_omti_cdb_touches_surface(AP_OMTI_CMD_WRITE));
}

/* A refusal costs the seek that discovered it, and -- the regression that the
 * first version of this change introduced -- must still carry the address in
 * its sense bytes after the wait. */
static void test_a_refused_address_still_reports_it_after_the_wait(void) {
  build_controller();
  issue(AP_OMTI_CMD_READ, 0x3FFu, 0u, 0u, 1u);
  settle();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  /* `A1`: illegal address with bit 7, the address-valid flag, still set. The
   * first version of this change wrote the sense bytes when the drive arrived
   * rather than when the command ended, which erased what `refuse` had put
   * there and turned this back into `21`. */
  TEST_ASSERT_EQUAL_HEX8(0xA1u, omti.sense[0]);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_command_for_an_unfitted_lun_is_refused);
  RUN_TEST(test_a_read_for_an_unfitted_lun_returns_no_data);
  RUN_TEST(test_the_two_drives_are_the_oracles);
  RUN_TEST(test_the_address_is_cylinder_head_sector);
  RUN_TEST(test_an_address_off_the_drive_is_refused);
  RUN_TEST(test_a_short_image_refuses_the_sectors_it_lacks);
  RUN_TEST(test_without_a_sidecar_the_surface_is_clean);
  RUN_TEST(test_a_sidecar_carries_flags_and_ecc);
  RUN_TEST(test_a_malformed_or_short_sidecar_is_told_apart);
  RUN_TEST(test_a_read_command_delivers_the_addressed_sector);
  RUN_TEST(test_a_read_to_buffer_fills_the_buffer_without_a_data_phase);
  RUN_TEST(test_the_command_log_pairs_a_buffer_fill_with_its_drain);
  RUN_TEST(test_a_read_to_buffer_past_the_buffer_is_refused);
  RUN_TEST(test_eight_blocks_fit_a_thirty_two_kilobyte_buffer);
  RUN_TEST(test_write_from_buffer_places_what_write_sector_buffer_staged);
  RUN_TEST(test_a_long_read_is_the_sector_and_six_more);
  RUN_TEST(test_a_long_write_keeps_the_sector_and_drops_the_ecc);
  RUN_TEST(test_the_defect_list_is_a_header_and_a_terminator);
  RUN_TEST(test_a_track_format_writes_six_c_over_that_track_alone);
  RUN_TEST(test_a_drive_format_runs_to_the_end_of_the_unit);
  RUN_TEST(test_assigning_an_alternate_formats_the_track_the_host_names);
  RUN_TEST(test_a_copy_moves_blocks_between_two_addresses);
  RUN_TEST(test_change_cartridge_on_a_fixed_drive_is_an_illegal_function);
  RUN_TEST(test_dma_enable_is_what_asks_for_a_cycle);
  RUN_TEST(test_read_configuration_reports_the_highest_not_the_count);
  RUN_TEST(test_the_data_phase_requests_each_byte_and_stops);
  RUN_TEST(test_the_data_out_phase_requests_each_byte_too);
  RUN_TEST(test_a_multi_block_write_keeps_asking_across_sectors);
  RUN_TEST(test_a_buffer_write_requests_each_byte);
  RUN_TEST(test_read_id_returns_the_address_the_disk_carries);
  RUN_TEST(test_read_verify_transfers_nothing_and_still_reads);
  RUN_TEST(test_the_diagnostics_report_what_they_can_see);
  RUN_TEST(test_selecting_asks_for_the_first_command_byte);
  RUN_TEST(test_each_command_byte_clears_and_re_asserts_the_request);
  RUN_TEST(test_a_select_while_busy_is_ignored);
  RUN_TEST(test_a_write_command_reaches_the_image);
  RUN_TEST(test_a_block_count_of_zero_means_two_hundred_and_fifty_six);
  RUN_TEST(test_a_multi_sector_read_walks_forward);
  RUN_TEST(test_a_bad_address_fails_and_the_sense_says_so);
  RUN_TEST(
      test_the_sense_address_reports_lun_zero_and_bit_five_is_unreachable);
  RUN_TEST(test_a_multiblock_read_off_the_end_is_a_volume_overflow);
  RUN_TEST(test_a_multiblock_write_off_the_end_is_a_volume_overflow);
  RUN_TEST(test_the_refused_address_carries_the_whole_cylinder);
  RUN_TEST(test_a_write_to_a_read_only_image_reports_write_protected);
  RUN_TEST(test_a_bad_track_format_is_refused_afterwards);
  RUN_TEST(test_a_long_write_records_its_ecc_for_a_long_read);
  RUN_TEST(test_a_controller_with_no_drive_says_so);
  RUN_TEST(test_the_st506_only_command_is_refused_in_practice);
  RUN_TEST(test_a_read_does_not_complete_in_the_instant_it_is_issued);
  RUN_TEST(test_the_access_time_is_a_seek_a_half_turn_and_the_transfer);
  RUN_TEST(test_a_command_that_touches_no_surface_completes_at_once);
  RUN_TEST(test_a_refused_address_still_reports_it_after_the_wait);
  return UNITY_END();
}

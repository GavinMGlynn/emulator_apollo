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
  TEST_ASSERT_FALSE(ap_awd_lba(SMALL, 0u, 0u, 4u, &lba));
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

/* --------------------------------------------------------------------------
 * The controller's command phase
 * ------------------------------------------------------------------------ */

static ap_omti_t omti;

static void build_controller(void) {
  build_drive();
  ap_omti_reset(&omti);
  ap_omti_attach(&omti, &drive);
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

static uint8_t take_status(void) {
  return ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA);
}

/* The whole cycle: command, data, status. */
static void test_a_read_command_delivers_the_addressed_sector(void) {
  build_controller();
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_IDLE, ap_omti_disk_phase(&omti));

  /* Cylinder 1, head 1, sector 3 -- sector 15, filled with 15. */
  issue(AP_OMTI_CMD_READ, 1u, 1u, 3u, 1u);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_IN, ap_omti_disk_phase(&omti));
  /* "1 = DMA Cycle Requested", and the byte waiting is data rather than a
   * command or status byte. */
  const uint8_t status = ap_omti_disk_read(&omti, AP_OMTI_DISK_STATUS);
  TEST_ASSERT_TRUE((status & AP_OMTI_ST_DREQ) != 0u);
  TEST_ASSERT_TRUE((status & AP_OMTI_ST_CD) == 0u);

  for (unsigned i = 0; i < AP_AWD_SECTOR_BYTES; i++) {
    TEST_ASSERT_EQUAL_HEX8(15u, ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA));
  }

  /* The sector ends the command, and the completion byte is waiting. */
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  TEST_ASSERT_TRUE(
      (ap_omti_disk_read(&omti, AP_OMTI_DISK_STATUS) & AP_OMTI_ST_IREQ) != 0u);
  TEST_ASSERT_EQUAL_HEX8(0u, take_status());
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_IDLE, ap_omti_disk_phase(&omti));
}

/* A write goes the other way and reaches the image. */
static void test_a_write_command_reaches_the_image(void) {
  build_controller();
  issue(AP_OMTI_CMD_WRITE, 0u, 0u, 2u, 1u);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_OUT, ap_omti_disk_phase(&omti));

  for (unsigned i = 0; i < AP_AWD_SECTOR_BYTES; i++) {
    ap_omti_disk_write(&omti, AP_OMTI_DISK_DATA, 0xA5u);
  }
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
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  TEST_ASSERT_EQUAL_HEX8(0u, take_status());
}

/* An address off the drive fails, and the sense says why. That is what
 * `REQUEST SENSE` is for: a driver reads it after a completion byte with the
 * error bit set. */
static void test_a_bad_address_fails_and_the_sense_says_so(void) {
  build_controller();
  issue(AP_OMTI_CMD_READ, 7u, 0u, 0u, 1u);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  TEST_ASSERT_TRUE((take_status() & 0x02u) != 0u);

  issue(AP_OMTI_CMD_REQUEST_SENSE, 0u, 0u, 0u, 0u);
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_DATA_IN, ap_omti_disk_phase(&omti));
  TEST_ASSERT_EQUAL_HEX8(0x21u, ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA));
  for (unsigned i = 1; i < 4u; i++) {
    (void)ap_omti_disk_read(&omti, AP_OMTI_DISK_DATA);
  }
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
}

/* A controller with no drive is a real configuration and must not look like one
 * with a blank disk: the command fails and says the drive is not ready. */
static void test_a_controller_with_no_drive_says_so(void) {
  ap_omti_reset(&omti);
  ap_omti_attach(&omti, NULL);

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
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  TEST_ASSERT_TRUE((take_status() & 0x02u) != 0u);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_two_drives_are_the_oracles);
  RUN_TEST(test_the_address_is_cylinder_head_sector);
  RUN_TEST(test_an_address_off_the_drive_is_refused);
  RUN_TEST(test_a_short_image_refuses_the_sectors_it_lacks);
  RUN_TEST(test_a_read_command_delivers_the_addressed_sector);
  RUN_TEST(test_a_write_command_reaches_the_image);
  RUN_TEST(test_a_block_count_of_zero_means_two_hundred_and_fifty_six);
  RUN_TEST(test_a_multi_sector_read_walks_forward);
  RUN_TEST(test_a_bad_address_fails_and_the_sense_says_so);
  RUN_TEST(test_a_controller_with_no_drive_says_so);
  RUN_TEST(test_the_st506_only_command_is_refused_in_practice);
  return UNITY_END();
}

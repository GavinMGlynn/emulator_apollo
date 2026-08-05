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
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
}

/* §5.1.2 gives READ VERIFY zero data bytes: it reads and checks without
 * transferring, so what it reports is whether the sectors are *there*. A model
 * that answered without reading would answer for a disk it never touched. */
static void test_read_verify_transfers_nothing_and_still_reads(void) {
  build_controller();
  issue(AP_OMTI_CMD_READ_VERIFY, 0u, 0u, 1u, 2u);
  /* Straight to the completion: no data phase at all. */
  TEST_ASSERT_EQUAL_INT(AP_OMTI_PHASE_STATUS, ap_omti_disk_phase(&omti));
  TEST_ASSERT_EQUAL_HEX8(0x00u, take_status());

  /* And a verify off the end of the image fails, which is what makes it a
   * check rather than an acknowledgement. */
  build_controller();
  issue(AP_OMTI_CMD_READ_VERIFY, 0u, 0u, 1u, 255u);
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
  issue(AP_OMTI_CMD_RAM_DIAGNOSTICS, 0u, 0u, 0u, 0u);
  TEST_ASSERT_EQUAL_HEX8(0x00u, take_status());
  ap_omti_reset(&omti);
  issue(AP_OMTI_CMD_DRIVE_DIAGNOSTIC, 0u, 0u, 0u, 0u);
  TEST_ASSERT_NOT_EQUAL_HEX8(0x00u, take_status());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_two_drives_are_the_oracles);
  RUN_TEST(test_the_address_is_cylinder_head_sector);
  RUN_TEST(test_an_address_off_the_drive_is_refused);
  RUN_TEST(test_a_short_image_refuses_the_sectors_it_lacks);
  RUN_TEST(test_a_read_command_delivers_the_addressed_sector);
  RUN_TEST(test_dma_enable_is_what_asks_for_a_cycle);
  RUN_TEST(test_read_configuration_reports_the_highest_not_the_count);
  RUN_TEST(test_the_data_phase_requests_each_byte_and_stops);
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
  RUN_TEST(test_a_controller_with_no_drive_says_so);
  RUN_TEST(test_the_st506_only_command_is_refused_in_practice);
  return UNITY_END();
}

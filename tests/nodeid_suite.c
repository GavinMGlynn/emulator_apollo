/* Apollo node ID PROM. Layout measured; see `ap_nodeid.h`. */

#include "unity.h"

#include "board/ap_nodeid.h"
#include <string.h>
#include "image/ap_volume.h"

void setUp(void) {}
void tearDown(void) {}

static void test_the_measured_dump_is_reproduced(void) {
  ap_nodeid_t prom;
  ap_nodeid_init(&prom, 0x012345u);

  /* The oracle's own PROM, byte for byte over the thirty-two bytes that
   * identified the layout:
   *
   *   011200: 00 00 01 00 23 00 45 00 00 00 00 00 00 00 00 00
   *   011210: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 69 00
   */
  static const uint8_t expected[32] = {
      0x00, 0x00, 0x01, 0x00, 0x23, 0x00, 0x45, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x69, 0x00,
  };
  for (unsigned i = 0; i < 32u; i++) {
    TEST_ASSERT_EQUAL_HEX8(expected[i], ap_nodeid_read(&prom, AP_NODEID_ADDR + i));
  }
}

static void test_the_checksum_is_the_sum_of_the_identifier_bytes(void) {
  ap_nodeid_t prom;
  ap_nodeid_init(&prom, 0x012345u);

  /* `0x01 + 0x23 + 0x45 = 0x69`, exactly the byte the oracle presents. Three
   * bytes and their sum, all four visible in one dump, is what makes this a
   * reading of the layout rather than a guess at it. */
  TEST_ASSERT_EQUAL_HEX8(0x69, ap_nodeid_checksum(&prom));
  TEST_ASSERT_EQUAL_HEX8(0x69,
                         ap_nodeid_read(&prom, AP_NODEID_ADDR +
                                                   2u * AP_NODEID_CHECKSUM_REGISTER));
}

static void test_the_checksum_truncates_rather_than_carrying(void) {
  ap_nodeid_t prom;
  ap_nodeid_init(&prom, 0xFFFFFFu);

  /* Three bytes of `FF` sum to `0x2FD`, and the register is one byte wide. Not
   * observed -- the only PROM seen sums to well under 256 -- so this pins the
   * arithmetic this core performs rather than a fact about the hardware, and
   * says so. */
  TEST_ASSERT_EQUAL_HEX8(0xFD, ap_nodeid_checksum(&prom));
}

static void test_the_identifier_comes_from_the_caller(void) {
  ap_nodeid_t prom;
  ap_nodeid_init(&prom, 0x0ABCDEu);

  /* `012345` is what the oracle's machine happens to hold, not what an Apollo
   * node holds. Baking it in would make every emulated node the same node,
   * which for a device whose whole purpose is to be unique per machine is the
   * one error that matters. */
  TEST_ASSERT_EQUAL_HEX8(0x0A, ap_nodeid_read(&prom, AP_NODEID_ADDR + 2u));
  TEST_ASSERT_EQUAL_HEX8(0xBC, ap_nodeid_read(&prom, AP_NODEID_ADDR + 4u));
  TEST_ASSERT_EQUAL_HEX8(0xDE, ap_nodeid_read(&prom, AP_NODEID_ADDR + 6u));
  /* 0x0A + 0xBC + 0xDE = 0x1A4, truncated to one byte -- in register 15, the
   * last of the sixteen, which is `0112 1E` and where the boot PROM looks. */
  TEST_ASSERT_EQUAL_HEX8(0xA4, ap_nodeid_read(&prom, AP_NODEID_ADDR + 30u));
}

/* ## The boot PROM's own arithmetic, which is what settled the position
 *
 * CPU self-test 8 at `008218` sums the bytes at stride 2 from `0112 00` up to
 * but not including `0112 1E`, then compares the total with the byte *at*
 * `0112 1E`. So the checksum covers registers 0 through 14 and lives in
 * register 15 -- the question this module had recorded as unsettleable from a
 * dump whose other bytes are all zero.
 */
static void test_the_checksum_satisfies_the_boot_proms_own_test(void) {
  ap_nodeid_t prom;
  ap_nodeid_init(&prom, 0x012345u);

  uint8_t sum = 0;
  for (uint32_t at = AP_NODEID_ADDR; at < AP_NODEID_ADDR + 0x1Eu; at += 2u) {
    sum = (uint8_t)(sum + ap_nodeid_read(&prom, at));
  }
  TEST_ASSERT_EQUAL_HEX8(sum, ap_nodeid_read(&prom, AP_NODEID_ADDR + 0x1Eu));

  /* And it is a *sum*, not a complement: the firmware compares the two rather
   * than requiring the total to come out zero. With the byte one register early
   * the sum swallowed it and the compare found nothing -- `0x69 + 0x69 = 0xD2`
   * against a zero, which is exactly what the self-test failure printed. */
  TEST_ASSERT_EQUAL_HEX8(0x69, sum);
}

static void test_the_identifier_is_twenty_four_bits(void) {
  ap_nodeid_t prom;
  ap_nodeid_init(&prom, 0xFF012345u);

  /* The dump's leading `00` is a 32-bit field holding a 24-bit identifier, so
   * anything above bit 23 is not the node's to present. */
  TEST_ASSERT_EQUAL_HEX8(0x00, ap_nodeid_read(&prom, AP_NODEID_ADDR + 0u));
  TEST_ASSERT_EQUAL_HEX8(0x01, ap_nodeid_read(&prom, AP_NODEID_ADDR + 2u));
}

static void test_the_prom_aliases_through_its_range(void) {
  ap_nodeid_t prom;
  ap_nodeid_init(&prom, 0x012345u);
  unsigned reg;

  /* Measured: the second sixteen bytes of the dump repeat the first. */
  TEST_ASSERT_EQUAL_HEX8(0x01, ap_nodeid_read(&prom, AP_NODEID_ADDR + 32u + 2u));
  TEST_ASSERT_EQUAL_HEX8(0x69, ap_nodeid_read(&prom, AP_NODEID_ADDR + 32u + 30u));

  /* Odd bytes read zero here, unlike the serial ports where both bytes of a
   * word reach the register. Same stride, different behaviour, one board. */
  TEST_ASSERT_FALSE(ap_nodeid_decode(AP_NODEID_ADDR + 3u, &reg));
  TEST_ASSERT_EQUAL_HEX8(0x00, ap_nodeid_read(&prom, AP_NODEID_ADDR + 3u));

  TEST_ASSERT_TRUE(ap_nodeid_decode(AP_NODEID_ADDR + 0xFEu, &reg));
  TEST_ASSERT_FALSE(ap_nodeid_decode(0x011300u, &reg)); /* latch-page register */
  TEST_ASSERT_FALSE(ap_nodeid_decode(0x011100u, &reg)); /* the slave PIC */
}

/* The volume is the *source* of the identifier this PROM holds.
 *
 * `ap_nodeid_init` takes it from a caller, deliberately -- "a device whose
 * purpose is to be unique per machine must not be identical on every one" --
 * and `image/ap_volume.h` is the caller's source: a Domain volume records the
 * node that initialised it. This is the join, and it is worth a test because
 * the two halves are twenty bits in different formats: a UID's low bits at one
 * end, four PROM registers at the other. */
static void test_a_volumes_node_reaches_the_proms_registers(void) {
  /* The label eleven real images carry, built here because `media/` is
   * gitignored -- see `volume_suite`. */
  uint8_t blocks[AP_VOLUME_LABEL_BYTES];
  memset(blocks, 0, sizeof blocks);
  const uint32_t magic = AP_VOLUME_MAGIC;
  blocks[AP_VOLUME_MAGIC_OFFSET + 0u] = (uint8_t)(magic >> 24);
  blocks[AP_VOLUME_MAGIC_OFFSET + 1u] = (uint8_t)(magic >> 16);
  blocks[AP_VOLUME_MAGIC_OFFSET + 2u] = (uint8_t)(magic >> 8);
  blocks[AP_VOLUME_MAGIC_OFFSET + 3u] = (uint8_t)magic;
  memset(&blocks[AP_VOLUME_NAME_OFFSET], ' ', AP_VOLUME_NAME_BYTES);
  memcpy(&blocks[AP_VOLUME_APOLLO_OFFSET], "APOLLO",
         AP_VOLUME_APOLLO_BYTES);
  memcpy(&blocks[AP_VOLUME_NAME_OFFSET], "DN3500", 6u);
  const uint8_t uid[8] = {0xA4u, 0x5Au, 0xA6u, 0x73u,
                          0x10u, 0x01u, 0x23u, 0x45u};
  memcpy(&blocks[AP_VOLUME_CREATOR_UID_OFFSET], uid, sizeof uid);

  ap_volume_label_t label;
  TEST_ASSERT_TRUE(ap_volume_read_label(blocks, sizeof blocks, &label));
  TEST_ASSERT_EQUAL_HEX32(0x012345u, label.node_id);

  /* And the PROM built from it presents that identifier, big-endian across
   * registers 0-3, with the checksum the oracle computes. */
  ap_nodeid_t prom;
  ap_nodeid_init(&prom, label.node_id);

  const uint32_t held =
      ((uint32_t)ap_nodeid_read(&prom, AP_NODEID_ADDR + 0u * 2u) << 24) |
      ((uint32_t)ap_nodeid_read(&prom, AP_NODEID_ADDR + 1u * 2u) << 16) |
      ((uint32_t)ap_nodeid_read(&prom, AP_NODEID_ADDR + 2u * 2u) << 8) |
      (uint32_t)ap_nodeid_read(&prom, AP_NODEID_ADDR + 3u * 2u);
  TEST_ASSERT_EQUAL_HEX32(0x012345u, held);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_volumes_node_reaches_the_proms_registers);
  RUN_TEST(test_the_measured_dump_is_reproduced);
  RUN_TEST(test_the_checksum_is_the_sum_of_the_identifier_bytes);
  RUN_TEST(test_the_checksum_truncates_rather_than_carrying);
  RUN_TEST(test_the_identifier_comes_from_the_caller);
  RUN_TEST(test_the_checksum_satisfies_the_boot_proms_own_test);
  RUN_TEST(test_the_identifier_is_twenty_four_bits);
  RUN_TEST(test_the_prom_aliases_through_its_range);
  return UNITY_END();
}

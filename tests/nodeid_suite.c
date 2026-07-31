/* Apollo node ID PROM. Layout measured; see `ap_nodeid.h`. */

#include "unity.h"

#include "board/ap_nodeid.h"

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
      0x00, 0x00, 0x00, 0x00, 0x69, 0x00, 0x00, 0x00,
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
  /* 0x0A + 0xBC + 0xDE = 0x1A4, truncated to one byte. */
  TEST_ASSERT_EQUAL_HEX8(0xA4, ap_nodeid_read(&prom, AP_NODEID_ADDR + 28u));
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
  TEST_ASSERT_EQUAL_HEX8(0x69, ap_nodeid_read(&prom, AP_NODEID_ADDR + 32u + 28u));

  /* Odd bytes read zero here, unlike the serial ports where both bytes of a
   * word reach the register. Same stride, different behaviour, one board. */
  TEST_ASSERT_FALSE(ap_nodeid_decode(AP_NODEID_ADDR + 3u, &reg));
  TEST_ASSERT_EQUAL_HEX8(0x00, ap_nodeid_read(&prom, AP_NODEID_ADDR + 3u));

  TEST_ASSERT_TRUE(ap_nodeid_decode(AP_NODEID_ADDR + 0xFEu, &reg));
  TEST_ASSERT_FALSE(ap_nodeid_decode(0x011300u, &reg)); /* latch-page register */
  TEST_ASSERT_FALSE(ap_nodeid_decode(0x011100u, &reg)); /* the slave PIC */
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_measured_dump_is_reproduced);
  RUN_TEST(test_the_checksum_is_the_sum_of_the_identifier_bytes);
  RUN_TEST(test_the_checksum_truncates_rather_than_carrying);
  RUN_TEST(test_the_identifier_comes_from_the_caller);
  RUN_TEST(test_the_identifier_is_twenty_four_bits);
  RUN_TEST(test_the_prom_aliases_through_its_range);
  return UNITY_END();
}

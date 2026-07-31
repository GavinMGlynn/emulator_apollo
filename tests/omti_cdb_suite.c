/* OMTI Command Descriptor Blocks, `[OMTI]` §5.1. */

#include "unity.h"

#include "device/ap_omti_cdb.h"

void setUp(void) {}
void tearDown(void) {}

static void test_the_cylinder_is_reassembled_from_three_bytes(void) {
  ap_omti_cdb_t cdb;

  /* §5.1.1: C10 in byte 1 bit 7, C09 and C08 in byte 2 bits 7 and 6, and the
   * low eight in byte 3. Cylinder 0x7FF is every one of the eleven bits set.
   *
   * This is the test that matters: a decoder taking the cylinder from byte 3
   * alone passes every case under 256 and fails on every real disk. */
  static const uint8_t bytes[6] = {0x08, 0x80, 0xC0, 0xFF, 0x01, 0x00};
  ap_omti_cdb_decode(bytes, &cdb);
  TEST_ASSERT_EQUAL_HEX16(0x7FFu, cdb.cylinder);

  /* And each of the three contributions alone, so a decoder cannot pass by
   * getting the total right through compensating errors. */
  static const uint8_t only_c10[6] = {0x08, 0x80, 0x00, 0x00, 0x00, 0x00};
  ap_omti_cdb_decode(only_c10, &cdb);
  TEST_ASSERT_EQUAL_HEX16(0x400u, cdb.cylinder);

  static const uint8_t only_c98[6] = {0x08, 0x00, 0xC0, 0x00, 0x00, 0x00};
  ap_omti_cdb_decode(only_c98, &cdb);
  TEST_ASSERT_EQUAL_HEX16(0x300u, cdb.cylinder);

  static const uint8_t only_low[6] = {0x08, 0x00, 0x00, 0xAB, 0x00, 0x00};
  ap_omti_cdb_decode(only_low, &cdb);
  TEST_ASSERT_EQUAL_HEX16(0x0ABu, cdb.cylinder);
}

static void test_the_cylinder_bits_do_not_leak_into_head_or_sector(void) {
  ap_omti_cdb_t cdb;

  /* Byte 1 carries C10 *and* the head; byte 2 carries C09, C08 *and* the
   * sector. Each field must take only its own bits. */
  static const uint8_t bytes[6] = {0x08, 0xFF, 0xFF, 0x00, 0x00, 0x00};
  ap_omti_cdb_decode(bytes, &cdb);

  TEST_ASSERT_EQUAL_HEX8(0x1Fu, cdb.head);   /* five bits */
  TEST_ASSERT_EQUAL_HEX8(0x3Fu, cdb.sector); /* six bits */
  TEST_ASSERT_EQUAL_HEX8(0x01u, cdb.lun);    /* byte 1 bit 5 */
  TEST_ASSERT_EQUAL_HEX16(0x700u, cdb.cylinder);
}

static void test_the_command_byte_is_both_whole_and_split(void) {
  ap_omti_cdb_t cdb;

  /* §5.1.1 splits byte 0 into class and opcode; §5.1.2 lists codes as whole
   * bytes up to `E6`. Both are true, and exposing only the five-bit opcode
   * would make `08 READ` and `E8` indistinguishable. */
  static const uint8_t bytes[6] = {0xE6, 0x00, 0x00, 0x00, 0x00, 0x00};
  ap_omti_cdb_decode(bytes, &cdb);

  TEST_ASSERT_EQUAL_HEX8(0xE6u, cdb.command);
  TEST_ASSERT_EQUAL_HEX8(0x07u, cdb.command_class);
  TEST_ASSERT_EQUAL_HEX8(0x06u, cdb.opcode);
}

static void test_the_control_byte_is_three_bits(void) {
  ap_omti_cdb_t cdb;

  /* §5.1.1: "Bits 7,6,5 contain the command Control Byte. Bits 4,3,2,1,0 are
   * not used." */
  static const uint8_t bytes[6] = {0x08, 0x00, 0x00, 0x00, 0x00, 0xFF};
  ap_omti_cdb_decode(bytes, &cdb);
  TEST_ASSERT_EQUAL_HEX8(0x07u, cdb.control);
}

static void test_an_esdi_controller_refuses_the_st506_command(void) {
  /* §5.1.2 lists INITIALIZE DRIVE CHARACTERISTICS under "COMMANDS SPECIFIC to
   * the ST506/412 drives", and the DN3500's controller is the ESDI variant.
   *
   * Accepting it would make an ESDI drive's geometry look settable, where it is
   * reported by READ CAPACITY instead -- and the two command lists sit adjacent
   * on the page, which is exactly how they get merged by accident. */
  TEST_ASSERT_FALSE(
      ap_omti_cdb_accepted_by_esdi(AP_OMTI_CMD_INITIALIZE_DRIVE_CHARACTERISTICS));

  TEST_ASSERT_TRUE(ap_omti_cdb_accepted_by_esdi(AP_OMTI_CMD_READ_CAPACITY));
  TEST_ASSERT_TRUE(ap_omti_cdb_accepted_by_esdi(AP_OMTI_CMD_CHECK_TRACK_FORMAT));
  TEST_ASSERT_TRUE(
      ap_omti_cdb_accepted_by_esdi(AP_OMTI_CMD_READ_ESDI_DEFECT_LIST));
}

static void test_unlisted_commands_are_refused(void) {
  /* Every code §5.1.2 does not list. Sampled across the gaps rather than
   * exhaustively, since the accepted set is what the switch enumerates. */
  static const uint8_t unlisted[] = {0x02, 0x09, 0x12, 0x21, 0x80, 0xE1, 0xFF};
  for (unsigned i = 0; i < sizeof unlisted; i++) {
    TEST_ASSERT_FALSE(ap_omti_cdb_accepted_by_esdi(unlisted[i]));
  }
}

static void test_copy_is_the_only_long_descriptor(void) {
  TEST_ASSERT_EQUAL_UINT(AP_OMTI_CDB_LONG,
                         ap_omti_cdb_length(AP_OMTI_CMD_COPY));
  TEST_ASSERT_EQUAL_UINT(AP_OMTI_CDB_SHORT,
                         ap_omti_cdb_length(AP_OMTI_CMD_READ));
  TEST_ASSERT_EQUAL_UINT(AP_OMTI_CDB_SHORT,
                         ap_omti_cdb_length(AP_OMTI_CMD_READ_CAPACITY));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_cylinder_is_reassembled_from_three_bytes);
  RUN_TEST(test_the_cylinder_bits_do_not_leak_into_head_or_sector);
  RUN_TEST(test_the_command_byte_is_both_whole_and_split);
  RUN_TEST(test_the_control_byte_is_three_bits);
  RUN_TEST(test_an_esdi_controller_refuses_the_st506_command);
  RUN_TEST(test_unlisted_commands_are_refused);
  RUN_TEST(test_copy_is_the_only_long_descriptor);
  return UNITY_END();
}

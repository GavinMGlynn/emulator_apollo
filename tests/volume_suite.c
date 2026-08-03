/* The Domain physical volume label, and the node ID it carries.
 *
 * Every label here is *built by the test*. `media/` is gitignored -- the volumes
 * are not ours to redistribute -- so a suite that read one would pass on the
 * machine that has them and fail everywhere else. The offsets and the magic
 * come from eleven real images and are recorded in `image/ap_volume.h`; what
 * this checks is that the reader implements what was recorded, and that it
 * refuses what is not a volume.
 */

#include "unity.h"

#include <string.h>

#include "image/ap_volume.h"

void setUp(void) {}
void tearDown(void) {}

static uint8_t label[AP_VOLUME_LABEL_BYTES];

static void put_be32(unsigned offset, uint32_t value) {
  label[offset + 0u] = (uint8_t)(value >> 24);
  label[offset + 1u] = (uint8_t)(value >> 16);
  label[offset + 2u] = (uint8_t)(value >> 8);
  label[offset + 3u] = (uint8_t)value;
}

/* A volume as the images have it: the magic in block 1, a space-padded name,
 * and a creator UID whose low twenty bits are the node. */
static void build(const char *name, uint32_t uid_high, uint32_t uid_low) {
  memset(label, 0, sizeof label);
  put_be32(AP_VOLUME_MAGIC_OFFSET, AP_VOLUME_MAGIC);
  memset(&label[AP_VOLUME_NAME_OFFSET], ' ', AP_VOLUME_NAME_BYTES);
  memcpy(&label[AP_VOLUME_NAME_OFFSET], name, strlen(name));
  put_be32(AP_VOLUME_CREATOR_UID_OFFSET, uid_high);
  put_be32(AP_VOLUME_CREATOR_UID_OFFSET + 4u, uid_low);
}

/* The reading, on the exact bytes eleven images carry. */
static void test_the_measured_label_reads_back_as_measured(void) {
  build("APOLLODN3500", 0xA45AA673u, 0x10012345u);

  ap_volume_label_t out;
  TEST_ASSERT_TRUE(ap_volume_read_label(label, sizeof label, &out));
  TEST_ASSERT_EQUAL_STRING("APOLLODN3500", out.name);
  TEST_ASSERT_EQUAL_HEX32(0xA45AA673u, out.creator.high);
  TEST_ASSERT_EQUAL_HEX32(0x10012345u, out.creator.low);
  /* Twenty bits, which is what the node ID PROM holds -- and `012345` is what
   * this project's boards are built with, which is the agreement that made the
   * field identifiable at all. */
  TEST_ASSERT_EQUAL_HEX32(0x012345u, out.node_id);
}

/* The split is 36 bits of time, eight zero, twenty of node -- so the eight bits
 * above the node are *not* part of it. A reader taking the whole low word would
 * return `10012345` here and configure a machine with a node it cannot hold. */
static void test_the_node_is_twenty_bits_not_the_whole_low_word(void) {
  build("V", 0x00000000u, 0xFFF12345u);

  ap_volume_label_t out;
  TEST_ASSERT_TRUE(ap_volume_read_label(label, sizeof label, &out));
  TEST_ASSERT_EQUAL_HEX32(0x012345u, out.node_id);
  /* And the whole UID survives, because a caller comparing two volumes wants
   * the identity rather than the node. */
  TEST_ASSERT_EQUAL_HEX32(0xFFF12345u, out.creator.low);
}

/* The nil node the same split predicts: block 0's *other* UID has a low word of
 * zero on every image, which is what a UID with no machine behind it looks
 * like. It is the reason to read twenty bits rather than the whole word. */
static void test_a_uid_with_no_machine_behind_it_has_node_zero(void) {
  const ap_uid_t nil = {.high = 0xA45AA8ECu, .low = 0x00000000u};
  TEST_ASSERT_EQUAL_HEX32(0u, ap_uid_node_id(nil));
}

/* The name is space-padded on the volume and trimmed here: a trailing run of
 * spaces makes both comparison and printing wrong in ways nobody looks for. */
static void test_the_name_is_trimmed_of_its_padding(void) {
  build("VOL", 0u, 0u);
  ap_volume_label_t out;
  TEST_ASSERT_TRUE(ap_volume_read_label(label, sizeof label, &out));
  TEST_ASSERT_EQUAL_STRING("VOL", out.name);
  TEST_ASSERT_EQUAL_UINT(3u, (unsigned)strlen(out.name));

  /* And a name filling the field keeps every byte, with room for the
   * terminator -- the one length a fixed buffer gets wrong. */
  char full[AP_VOLUME_NAME_BYTES + 1u];
  memset(full, 'X', AP_VOLUME_NAME_BYTES);
  full[AP_VOLUME_NAME_BYTES] = '\0';
  build(full, 0u, 0u);
  TEST_ASSERT_TRUE(ap_volume_read_label(label, sizeof label, &out));
  TEST_ASSERT_EQUAL_STRING(full, out.name);
}

/* Refused, not defaulted. A node ID invented from a file that is not a Domain
 * volume would configure a machine to lie about its identity, and every object
 * its file system then created would carry it -- a corruption that outlives the
 * run and cannot be traced back to the moment it was chosen. */
static void test_something_that_is_not_a_volume_is_refused(void) {
  build("APOLLODN3500", 0xA45AA673u, 0x10012345u);
  put_be32(AP_VOLUME_MAGIC_OFFSET, 0xDEADBEEFu);

  ap_volume_label_t out;
  TEST_ASSERT_FALSE(ap_volume_read_label(label, sizeof label, &out));

  /* And a file too short to hold a label, which is the other way a caller
   * arrives here with something that is not one. */
  build("APOLLODN3500", 0xA45AA673u, 0x10012345u);
  TEST_ASSERT_FALSE(
      ap_volume_read_label(label, AP_VOLUME_LABEL_BYTES - 1u, &out));
  TEST_ASSERT_FALSE(ap_volume_read_label(nullptr, sizeof label, &out));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_measured_label_reads_back_as_measured);
  RUN_TEST(test_the_node_is_twenty_bits_not_the_whole_low_word);
  RUN_TEST(test_a_uid_with_no_machine_behind_it_has_node_zero);
  RUN_TEST(test_the_name_is_trimmed_of_its_padding);
  RUN_TEST(test_something_that_is_not_a_volume_is_refused);
  return UNITY_END();
}

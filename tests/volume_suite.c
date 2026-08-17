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

/* The mount history, and the one value in it that decides a boot.
 *
 * `002398-04`'s physical-volume-label diagram names these fields; the base
 * `0x440` was found by differencing two real images (`ap_volume.h`). What this
 * checks is that the reader picks up each field from the offset recorded, and
 * that a **zero** dismount time is reported as never-dismounted rather than as
 * a date -- which is the whole point of keeping the ticks raw. */
static void test_the_mount_history_reads_back_from_the_labels_own_base(void) {
  build("APOLLODN3500", 0xA45AA673u, 0x10012345u);
  put_be32(AP_VOLUME_LABEL_WRITE_TIME_OFFSET, 0xA45E5C0Cu);
  put_be32(AP_VOLUME_LAST_MOUNTED_NODE_OFFSET, 0x00012345u);
  put_be32(AP_VOLUME_NODE_BOOT_TIME_OFFSET, 0xA45DF69Bu);
  put_be32(AP_VOLUME_MOUNTED_TIME_OFFSET, 0xA45DF6ABu);
  put_be32(AP_VOLUME_DISMOUNTED_TIME_OFFSET, 0xA45E5C0Cu);
  put_be32(AP_VOLUME_SALVAGE_NODE_OFFSET, 0x00000002u);
  put_be32(AP_VOLUME_SALVAGE_TIME_OFFSET, 0x00010001u);

  ap_volume_label_t out;
  TEST_ASSERT_TRUE(ap_volume_read_label(label, sizeof label, &out));
  TEST_ASSERT_EQUAL_HEX32(0xA45E5C0Cu, out.label_write_time);
  TEST_ASSERT_EQUAL_HEX32(0x00012345u, out.last_mounted_node);
  TEST_ASSERT_EQUAL_HEX32(0xA45DF69Bu, out.node_boot_time);
  TEST_ASSERT_EQUAL_HEX32(0xA45DF6ABu, out.mounted_time);
  TEST_ASSERT_EQUAL_HEX32(0xA45E5C0Cu, out.dismounted_time);
  TEST_ASSERT_EQUAL_HEX32(0x00000002u, out.salvage_node);
  TEST_ASSERT_EQUAL_HEX32(0x00010001u, out.salvage_time);

  /* Those are `dn3500-sr10.4-installed.awd`'s own values, and that volume is
   * the one this project boots -- so this is the label of a bootable machine
   * rather than an invented one. */
  TEST_ASSERT_TRUE(ap_volume_cleanly_dismounted(&out));
}

/* A volume that was never cleanly dismounted, which is what killing an install
 * session leaves behind. Domain/OS's "more than 14 days have elapsed since the
 * last shutdown" check measures from this field, so a zero fails at every
 * possible power-on date -- three were tried against a real one before its label
 * was read. */
static void test_a_zero_dismount_time_is_never_dismounted_at_any_clock(void) {
  build("APOLLODN3500", 0xA45AA673u, 0x10012345u);
  put_be32(AP_VOLUME_MOUNTED_TIME_OFFSET, 0xFFF808EEu);
  put_be32(AP_VOLUME_DISMOUNTED_TIME_OFFSET, 0x00000000u);

  ap_volume_label_t out;
  TEST_ASSERT_TRUE(ap_volume_read_label(label, sizeof label, &out));
  TEST_ASSERT_EQUAL_HEX32(0xFFF808EEu, out.mounted_time);
  TEST_ASSERT_EQUAL_HEX32(0u, out.dismounted_time);
  TEST_ASSERT_FALSE(ap_volume_cleanly_dismounted(&out));

  /* And a mounted time is *not* a substitute for it: this volume has one, and
   * a reader tempted to fall back on it would call the volume bootable. */
  TEST_ASSERT_TRUE(out.mounted_time != 0u);
}

/* The tick unit, pinned against what the machines themselves said.
 *
 * A label time is the high 32 bits of Apollo's 48-bit 4 microsecond clock from
 * 1980-01-01, so one tick is 262144 microseconds. The two values below are real
 * volumes' `.mounted_time`, and each has an independent statement of its date:
 *
 *   FFF808EE  `dn3500-sr10.3-installed.awd`, and its own CALENDAR printed
 *             "last recorded time was 2015/09/03 15:47:46 UTC"
 *   A45DF6AB  `dn3500-sr10.4-installed.awd`, and `FINDINGS.md` C52 recorded
 *             that session's CALENDAR reading as 2002/11/27
 *
 * A first attempt read the tick as a plain quarter-second. That is 4.9% out and
 * put both over a year early -- close enough to look plausible, which is why
 * this is asserted against the machine's own words and not against itself. */
static void test_a_label_tick_is_the_high_half_of_the_four_microsecond_clock(
    void) {
  /* 1,125,763,067 seconds after 1980-01-01 is 2015-09-03 15:57:47. */
  TEST_ASSERT_EQUAL_UINT64(1125763067150336ull,
                           ap_volume_time_microseconds(0xFFF808EEu));
  /*   722,893,909 seconds after 1980-01-01 is 2002-11-27 19:51:49. */
  TEST_ASSERT_EQUAL_UINT64(722893909262336ull,
                           ap_volume_time_microseconds(0xA45DF6ABu));

  /* And the product is computed 64-bit wide. `262144` is 2^18, so a 32-bit
   * multiply wraps for every date this project has on disk: the first value
   * above would come back as 599,261,184 microseconds -- ten minutes after
   * 1980 -- which is a plausible-looking number for a volume from 2015. */
  TEST_ASSERT_TRUE(ap_volume_time_microseconds(0xFFF808EEu) > 0xFFFFFFFFull);
}

/* A null label is not a clean one. The predicate is called on the result of a
 * read that may have failed, so it has to answer rather than fault. */
static void test_no_label_is_not_a_clean_dismount(void) {
  TEST_ASSERT_FALSE(ap_volume_cleanly_dismounted(nullptr));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_measured_label_reads_back_as_measured);
  RUN_TEST(test_the_mount_history_reads_back_from_the_labels_own_base);
  RUN_TEST(test_a_zero_dismount_time_is_never_dismounted_at_any_clock);
  RUN_TEST(test_a_label_tick_is_the_high_half_of_the_four_microsecond_clock);
  RUN_TEST(test_no_label_is_not_a_clean_dismount);
  RUN_TEST(test_the_node_is_twenty_bits_not_the_whole_low_word);
  RUN_TEST(test_a_uid_with_no_machine_behind_it_has_node_zero);
  RUN_TEST(test_the_name_is_trimmed_of_its_padding);
  RUN_TEST(test_something_that_is_not_a_volume_is_refused);
  return UNITY_END();
}

/* The SIMH magtape container, `src/core/image/ap_tap.c`.
 *
 * Not a format this project invented: `simh/doc/simh_magtape.txt`, read and
 * written by SIMH, E11 and MAME, which is why an 8mm tape written here is
 * readable elsewhere. */

#include "unity.h"

#include <string.h>

#include "image/ap_tap.h"

void setUp(void) {}
void tearDown(void) {}

static ap_exb8200_record_t records[16];
static uint8_t tape[4096];
static uint8_t image[4096];

static ap_exb8200_media_t empty(void) {
  memset(records, 0, sizeof records);
  memset(tape, 0, sizeof tape);
  return (ap_exb8200_media_t){.record = records,
                              .records = 0u,
                              .capacity = 16u,
                              .data = tape,
                              .data_bytes = sizeof tape,
                              .data_used = 0u,
                              .writable = true};
}

/* A record is a little-endian length, the data padded to an even count, and
 * the same length again -- which is what makes the format readable backwards
 * and is the one structural check it admits. */
static void test_a_record_is_a_length_its_data_and_the_length_again(void) {
  ap_exb8200_media_t media = empty();
  media.records = 1u;
  media.record[0] = (ap_exb8200_record_t){
      .kind = AP_EXB8200_RECORD_DATA, .offset = 0u, .length = 4u};
  memcpy(media.data, "ABCD", 4u);
  media.data_used = 4u;

  const size_t wrote = ap_tap_save(&media, image, sizeof image);
  TEST_ASSERT_EQUAL_UINT(4u + 4u + 4u + 4u, wrote);
  TEST_ASSERT_EQUAL_HEX8(0x04u, image[0]);
  TEST_ASSERT_EQUAL_HEX8(0x00u, image[1]);
  TEST_ASSERT_EQUAL_MEMORY("ABCD", &image[4], 4u);
  TEST_ASSERT_EQUAL_HEX8(0x04u, image[8]); /* the trailing length */
  TEST_ASSERT_EQUAL_HEX8(0xFFu, image[12]); /* end of medium */
  TEST_ASSERT_EQUAL_HEX8(0xFFu, image[15]);
  TEST_ASSERT_EQUAL_UINT(wrote, ap_tap_size(&media));
}

/* An odd-length record is padded to an even byte count, and the pad is not
 * part of the length. */
static void test_an_odd_record_is_padded_and_the_length_is_not(void) {
  ap_exb8200_media_t media = empty();
  media.records = 1u;
  media.record[0] = (ap_exb8200_record_t){
      .kind = AP_EXB8200_RECORD_DATA, .offset = 0u, .length = 3u};
  memcpy(media.data, "XYZ", 3u);
  media.data_used = 3u;

  const size_t wrote = ap_tap_save(&media, image, sizeof image);
  TEST_ASSERT_EQUAL_UINT(4u + 4u + 4u + 4u, wrote); /* 3 data + 1 pad */
  TEST_ASSERT_EQUAL_HEX8(0x03u, image[0]);
  TEST_ASSERT_EQUAL_HEX8(0x00u, image[7]); /* the pad byte */
  TEST_ASSERT_EQUAL_HEX8(0x03u, image[8]);

  ap_exb8200_media_t back = empty();
  TEST_ASSERT_TRUE(ap_tap_load(&back, image, wrote));
  TEST_ASSERT_EQUAL_UINT(1u, back.records);
  TEST_ASSERT_EQUAL_UINT32(3u, back.record[0].length);
}

/* A length of zero is a tape mark, and it carries no data and no trailing
 * length. */
static void test_a_zero_length_is_a_tape_mark(void) {
  ap_exb8200_media_t media = empty();
  media.records = 1u;
  media.record[0] = (ap_exb8200_record_t){.kind = AP_EXB8200_RECORD_FILEMARK};

  const size_t wrote = ap_tap_save(&media, image, sizeof image);
  TEST_ASSERT_EQUAL_UINT(4u + 4u, wrote);
  TEST_ASSERT_EQUAL_HEX8(0x00u, image[0]);
  TEST_ASSERT_EQUAL_HEX8(0x00u, image[3]);

  ap_exb8200_media_t back = empty();
  TEST_ASSERT_TRUE(ap_tap_load(&back, image, wrote));
  TEST_ASSERT_EQUAL_UINT(1u, back.records);
  TEST_ASSERT_EQUAL_UINT(AP_EXB8200_RECORD_FILEMARK, back.record[0].kind);
}

/* The container has one filemark and the drive has two, so a tape mark comes
 * back **long** -- what a clear byte 05 bit 7 writes, and the only kind a
 * later write may append into. The distinction is lost across a save, which
 * this asserts rather than leaves to be discovered. */
static void test_a_tape_mark_loads_as_a_long_filemark(void) {
  ap_exb8200_media_t media = empty();
  media.records = 2u;
  media.record[0] =
      (ap_exb8200_record_t){.kind = AP_EXB8200_RECORD_FILEMARK,
                            .short_mark = true};
  media.record[1] =
      (ap_exb8200_record_t){.kind = AP_EXB8200_RECORD_FILEMARK,
                            .short_mark = false};
  const size_t wrote = ap_tap_save(&media, image, sizeof image);

  ap_exb8200_media_t back = empty();
  TEST_ASSERT_TRUE(ap_tap_load(&back, image, wrote));
  TEST_ASSERT_EQUAL_UINT(2u, back.records);
  TEST_ASSERT_FALSE(back.record[0].short_mark);
  TEST_ASSERT_FALSE(back.record[1].short_mark);
}

/* A whole tape survives a round trip: data, marks and lengths. */
static void test_a_tape_round_trips(void) {
  ap_exb8200_media_t media = empty();
  media.records = 4u;
  media.record[0] = (ap_exb8200_record_t){
      .kind = AP_EXB8200_RECORD_DATA, .offset = 0u, .length = 512u};
  media.record[1] = (ap_exb8200_record_t){
      .kind = AP_EXB8200_RECORD_DATA, .offset = 512u, .length = 1u};
  media.record[2] = (ap_exb8200_record_t){.kind = AP_EXB8200_RECORD_FILEMARK};
  media.record[3] = (ap_exb8200_record_t){
      .kind = AP_EXB8200_RECORD_DATA, .offset = 513u, .length = 100u};
  for (unsigned i = 0; i < 613u; i++) {
    media.data[i] = (uint8_t)(i * 7u);
  }
  media.data_used = 613u;

  const size_t wrote = ap_tap_save(&media, image, sizeof image);
  TEST_ASSERT_TRUE(wrote > 0u);

  ap_exb8200_media_t back = empty();
  TEST_ASSERT_TRUE(ap_tap_load(&back, image, wrote));
  TEST_ASSERT_EQUAL_UINT(4u, back.records);
  TEST_ASSERT_EQUAL_UINT32(512u, back.record[0].length);
  TEST_ASSERT_EQUAL_UINT32(1u, back.record[1].length);
  TEST_ASSERT_EQUAL_UINT(AP_EXB8200_RECORD_FILEMARK, back.record[2].kind);
  TEST_ASSERT_EQUAL_UINT32(100u, back.record[3].length);
  TEST_ASSERT_EQUAL_UINT32(613u, back.data_used);
  TEST_ASSERT_EQUAL_MEMORY(media.data, back.data, 613u);
}

/* End of medium closes the image: anything after it is not tape. */
static void test_end_of_medium_closes_the_image(void) {
  ap_exb8200_media_t media = empty();
  media.records = 1u;
  media.record[0] = (ap_exb8200_record_t){
      .kind = AP_EXB8200_RECORD_DATA, .offset = 0u, .length = 2u};
  media.data_used = 2u;
  const size_t wrote = ap_tap_save(&media, image, sizeof image);
  /* Append a record after the marker; it must not be read. */
  memset(&image[wrote], 0x02u, 4u);
  ap_exb8200_media_t back = empty();
  TEST_ASSERT_TRUE(ap_tap_load(&back, image, wrote + 16u));
  TEST_ASSERT_EQUAL_UINT(1u, back.records);
}

/* A trailing length that disagrees with its leader is not a tape. */
static void test_a_mismatched_trailing_length_is_refused(void) {
  ap_exb8200_media_t media = empty();
  media.records = 1u;
  media.record[0] = (ap_exb8200_record_t){
      .kind = AP_EXB8200_RECORD_DATA, .offset = 0u, .length = 4u};
  media.data_used = 4u;
  const size_t wrote = ap_tap_save(&media, image, sizeof image);
  image[8] = 0x05u; /* the trailing length, now wrong */
  ap_exb8200_media_t back = empty();
  TEST_ASSERT_FALSE(ap_tap_load(&back, image, wrote));
}

/* A length longer than the image is refused rather than clamped: a container
 * that does not parse is not a tape. */
static void test_a_length_past_the_end_is_refused(void) {
  memset(image, 0, sizeof image);
  image[0] = 0x00u;
  image[1] = 0x10u; /* 4,096 bytes, with nothing behind them */
  ap_exb8200_media_t back = empty();
  TEST_ASSERT_FALSE(ap_tap_load(&back, image, 16u));
}

/* A tape with more records than the caller allowed is refused, not
 * truncated. */
static void test_more_records_than_the_caller_allowed_is_refused(void) {
  memset(image, 0, sizeof image);
  /* Seventeen tape marks into a sixteen-record table. */
  for (unsigned i = 0; i < 17u; i++) {
    memset(&image[i * 4u], 0, 4u);
  }
  ap_exb8200_media_t back = empty();
  TEST_ASSERT_FALSE(ap_tap_load(&back, image, 17u * 4u));
}

/* An erase gap is skipped: it is not a record and carries no trailing
 * length. */
static void test_an_erase_gap_is_skipped(void) {
  memset(image, 0, sizeof image);
  image[0] = 0xFEu;
  image[1] = 0xFFu;
  image[2] = 0xFFu;
  image[3] = 0xFFu;
  image[4] = 0x00u; /* then a tape mark */
  image[8] = 0xFFu;
  image[9] = 0xFFu;
  image[10] = 0xFFu;
  image[11] = 0xFFu;
  ap_exb8200_media_t back = empty();
  TEST_ASSERT_TRUE(ap_tap_load(&back, image, 12u));
  TEST_ASSERT_EQUAL_UINT(1u, back.records);
  TEST_ASSERT_EQUAL_UINT(AP_EXB8200_RECORD_FILEMARK, back.record[0].kind);
}

/* A buffer too small for the whole image writes nothing, rather than a partial
 * tape that would parse as a shorter one. */
static void test_a_short_buffer_writes_nothing(void) {
  ap_exb8200_media_t media = empty();
  media.records = 1u;
  media.record[0] = (ap_exb8200_record_t){
      .kind = AP_EXB8200_RECORD_DATA, .offset = 0u, .length = 8u};
  media.data_used = 8u;
  TEST_ASSERT_EQUAL_UINT(0u, ap_tap_save(&media, image, 8u));
  TEST_ASSERT_EQUAL_UINT(20u, ap_tap_size(&media));
}

/* Two tapes of equal size that differ in a byte do not hash alike. */
static void test_two_tapes_of_equal_size_hash_differently(void) {
  uint8_t a[32] = {0};
  uint8_t b[32] = {0};
  b[17] = 0x01u;
  TEST_ASSERT_NOT_EQUAL_UINT64(ap_tap_digest_of(a, sizeof a),
                               ap_tap_digest_of(b, sizeof b));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_record_is_a_length_its_data_and_the_length_again);
  RUN_TEST(test_an_odd_record_is_padded_and_the_length_is_not);
  RUN_TEST(test_a_zero_length_is_a_tape_mark);
  RUN_TEST(test_a_tape_mark_loads_as_a_long_filemark);
  RUN_TEST(test_a_tape_round_trips);
  RUN_TEST(test_end_of_medium_closes_the_image);
  RUN_TEST(test_a_mismatched_trailing_length_is_refused);
  RUN_TEST(test_a_length_past_the_end_is_refused);
  RUN_TEST(test_more_records_than_the_caller_allowed_is_refused);
  RUN_TEST(test_an_erase_gap_is_skipped);
  RUN_TEST(test_a_short_buffer_writes_nothing);
  RUN_TEST(test_two_tapes_of_equal_size_hash_differently);
  return UNITY_END();
}

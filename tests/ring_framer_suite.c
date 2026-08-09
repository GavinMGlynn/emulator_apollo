/* Whole frames over the ring's bit stream.
 *
 * `[MAC]` 010005-00 §2.2.2 pp. 2-6 ff. Every test cites the sentence it
 * encodes, because there is no runnable oracle for this part: the manual *is*
 * the oracle, and a test that cannot be traced back to a line of it is
 * asserting this core's opinion of itself.
 */

#include "ring/ap_ring_framer.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* A minimum header: 12 bytes, which is what "the Apollo token ring controller
 * will always transmit the first 12 bytes of a packet header" makes the floor.
 * Filled with a destination, type, separator byte, early acknowledge and
 * source, so it is a header rather than twelve arbitrary bytes. */
static void make_header(uint8_t *header) {
  for (unsigned i = 0; i < 12u; i++) {
    header[i] = 0u;
  }
  ap_ring_header_set_destination(header, 0x00001234u);
  ap_ring_header_set_type(header, 0x0080u);
  ap_ring_header_set_early_ack(header, ap_ring_ack_with_parity(0u));
  ap_ring_header_set_source(header, 0x00005678u);
}

/* The round trip the item asks for: a frame emitted and parsed back gives the
 * same header, the same data, the same late acknowledge, and a check that
 * agrees. */
static void test_a_frame_round_trips_through_the_bit_stream(void) {
  uint8_t header[12];
  make_header(header);
  static const uint8_t data[] = {0xDEu, 0xADu, 0xBEu, 0xEFu};

  uint8_t wire[256];
  ap_ring_bitwriter_t w;
  ap_ring_bitwriter_init(&w, wire, sizeof wire);
  const ap_ring_frame_fields_t fields = {
      .header = header,
      .header_bytes = sizeof header,
      .data = data,
      .data_bytes = sizeof data,
      .late_acknowledge = ap_ring_ack_with_parity(AP_RING_LATE_COPIED)};
  TEST_ASSERT_TRUE(ap_ring_frame_emit(&w, &fields));

  uint8_t got_header[64] = {0};
  uint8_t got_data[64] = {0};
  ap_ring_bitreader_t r;
  ap_ring_bitreader_init(&r, wire, w.bit_count);
  const ap_ring_frame_parse_t p = ap_ring_frame_parse(
      &r, got_header, sizeof got_header, got_data, sizeof got_data);

  TEST_ASSERT_EQUAL_INT(AP_RING_FRAME_OK, p.status);
  TEST_ASSERT_EQUAL_size_t(sizeof header, p.header_bytes);
  TEST_ASSERT_EQUAL_size_t(sizeof data, p.data_bytes);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(header, got_header, sizeof header);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(data, got_data, sizeof data);
  TEST_ASSERT_EQUAL_HEX8(fields.late_acknowledge, p.late_acknowledge);
  TEST_ASSERT_EQUAL_HEX32(p.check_computed, p.check_received);
}

/* §2.2.2.1: "The frame start sequence contains the frame start (out-of-band)
 * character, a null separator, and a separator character." In that order, and
 * the null separator is the short one -- a byte of zeros. */
static void test_a_frame_opens_with_start_null_separator_and_separator(void) {
  uint8_t header[12];
  make_header(header);
  uint8_t wire[256];
  ap_ring_bitwriter_t w;
  ap_ring_bitwriter_init(&w, wire, sizeof wire);
  const ap_ring_frame_fields_t fields = {.header = header,
                                         .header_bytes = sizeof header,
                                         .data = NULL,
                                         .data_bytes = 0u,
                                         .late_acknowledge = 0u};
  TEST_ASSERT_TRUE(ap_ring_frame_emit(&w, &fields));

  ap_ring_bitreader_t r;
  ap_ring_bitreader_init(&r, wire, w.bit_count);

  uint16_t symbol = 0u;
  TEST_ASSERT_TRUE(ap_ring_read_oob(&r, &symbol));
  TEST_ASSERT_EQUAL_HEX16(AP_RING_OOB_FRAME_START, symbol);

  uint32_t null_separator = 0xFFu;
  bool violation = false;
  TEST_ASSERT_TRUE(ap_ring_read_data_bits(&r, 8u, &null_separator, &violation));
  TEST_ASSERT_FALSE(violation);
  TEST_ASSERT_EQUAL_HEX32(0u, null_separator);

  TEST_ASSERT_TRUE(ap_ring_read_oob(&r, &symbol));
  TEST_ASSERT_EQUAL_HEX16(AP_RING_OOB_SEPARATOR, symbol);
}

/* §2.2.2.3: "The packet data field can vary in size from 0 to 4096 bytes." Zero
 * is legal, and the sequence still ends with its separator character -- the
 * sequence is what the manual names, not the field. */
static void test_a_frame_with_no_packet_data_is_legal(void) {
  uint8_t header[12];
  make_header(header);
  uint8_t wire[256];
  ap_ring_bitwriter_t w;
  ap_ring_bitwriter_init(&w, wire, sizeof wire);
  const ap_ring_frame_fields_t fields = {.header = header,
                                         .header_bytes = sizeof header,
                                         .data = NULL,
                                         .data_bytes = 0u,
                                         .late_acknowledge = 0u};
  TEST_ASSERT_TRUE(ap_ring_frame_emit(&w, &fields));

  uint8_t got_header[64] = {0};
  uint8_t got_data[64] = {0};
  ap_ring_bitreader_t r;
  ap_ring_bitreader_init(&r, wire, w.bit_count);
  const ap_ring_frame_parse_t p = ap_ring_frame_parse(
      &r, got_header, sizeof got_header, got_data, sizeof got_data);

  TEST_ASSERT_EQUAL_INT(AP_RING_FRAME_OK, p.status);
  TEST_ASSERT_EQUAL_size_t(0u, p.data_bytes);
}

/* The lengths are the manual's, checked before anything is written: "a packet
 * header can vary in size from 12 to 1024 bytes ... it must always consist of
 * an even number of bytes". A refused frame leaves the stream untouched, so the
 * next reader does not find a frame start with no end. */
static void test_a_header_shorter_than_twelve_bytes_is_refused_whole(void) {
  uint8_t header[10];
  for (unsigned i = 0; i < sizeof header; i++) {
    header[i] = 0u;
  }
  uint8_t wire[256];
  ap_ring_bitwriter_t w;
  ap_ring_bitwriter_init(&w, wire, sizeof wire);
  const ap_ring_frame_fields_t fields = {.header = header,
                                         .header_bytes = sizeof header,
                                         .data = NULL,
                                         .data_bytes = 0u,
                                         .late_acknowledge = 0u};

  TEST_ASSERT_FALSE(ap_ring_frame_emit(&w, &fields));
  TEST_ASSERT_EQUAL_size_t(0u, w.bit_count);
}

/* An odd number of header bytes is refused for the same reason, and this is the
 * half of the rule a length range alone would miss. */
static void test_an_odd_header_length_is_refused(void) {
  uint8_t header[13];
  for (unsigned i = 0; i < sizeof header; i++) {
    header[i] = 0u;
  }
  uint8_t wire[256];
  ap_ring_bitwriter_t w;
  ap_ring_bitwriter_init(&w, wire, sizeof wire);
  const ap_ring_frame_fields_t fields = {.header = header,
                                         .header_bytes = sizeof header,
                                         .data = NULL,
                                         .data_bytes = 0u,
                                         .late_acknowledge = 0u};

  TEST_ASSERT_FALSE(ap_ring_frame_emit(&w, &fields));
}

/* A receiver finds the end of a sequence by the separator character and not by
 * any length, so a header of a different size parses back at *its* size with no
 * hint from the caller. This is what `ap_ring_peek_oob` exists for. */
static void test_a_longer_header_parses_back_at_its_own_length(void) {
  uint8_t header[40];
  make_header(header);
  for (unsigned i = 12u; i < sizeof header; i++) {
    header[i] = (uint8_t)(0x40u + i);
  }
  uint8_t wire[256];
  ap_ring_bitwriter_t w;
  ap_ring_bitwriter_init(&w, wire, sizeof wire);
  const ap_ring_frame_fields_t fields = {.header = header,
                                         .header_bytes = sizeof header,
                                         .data = NULL,
                                         .data_bytes = 0u,
                                         .late_acknowledge = 0u};
  TEST_ASSERT_TRUE(ap_ring_frame_emit(&w, &fields));

  uint8_t got_header[64] = {0};
  uint8_t got_data[64] = {0};
  ap_ring_bitreader_t r;
  ap_ring_bitreader_init(&r, wire, w.bit_count);
  const ap_ring_frame_parse_t p = ap_ring_frame_parse(
      &r, got_header, sizeof got_header, got_data, sizeof got_data);

  TEST_ASSERT_EQUAL_INT(AP_RING_FRAME_OK, p.status);
  TEST_ASSERT_EQUAL_size_t(sizeof header, p.header_bytes);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(header, got_header, sizeof header);
}

/* Data that would present six consecutive ones is stuffed on the way out and
 * unstuffed on the way back, so a payload of `0xFF`s must not be mistaken for a
 * separator character. This is the case where a framer that read raw bytes
 * would silently truncate the sequence. */
static void test_a_payload_of_ones_does_not_look_like_a_character(void) {
  uint8_t header[12];
  make_header(header);
  static const uint8_t data[] = {0xFFu, 0xFFu, 0xFFu, 0xFFu};

  uint8_t wire[256];
  ap_ring_bitwriter_t w;
  ap_ring_bitwriter_init(&w, wire, sizeof wire);
  const ap_ring_frame_fields_t fields = {.header = header,
                                         .header_bytes = sizeof header,
                                         .data = data,
                                         .data_bytes = sizeof data,
                                         .late_acknowledge = 0u};
  TEST_ASSERT_TRUE(ap_ring_frame_emit(&w, &fields));

  uint8_t got_header[64] = {0};
  uint8_t got_data[64] = {0};
  ap_ring_bitreader_t r;
  ap_ring_bitreader_init(&r, wire, w.bit_count);
  const ap_ring_frame_parse_t p = ap_ring_frame_parse(
      &r, got_header, sizeof got_header, got_data, sizeof got_data);

  TEST_ASSERT_EQUAL_INT(AP_RING_FRAME_OK, p.status);
  TEST_ASSERT_EQUAL_size_t(sizeof data, p.data_bytes);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(data, got_data, sizeof data);
}

/* A corrupted byte is a frame that arrived whole and fails its check. The two
 * outcomes are kept apart because they are different events for a receiver: one
 * sets the error bit in the late acknowledge field, the other never was a
 * frame. */
static void test_a_corrupted_frame_arrives_whole_and_fails_its_check(void) {
  uint8_t header[12];
  make_header(header);
  static const uint8_t data[] = {0x01u, 0x02u, 0x03u, 0x04u};

  uint8_t wire[256];
  ap_ring_bitwriter_t w;
  ap_ring_bitwriter_init(&w, wire, sizeof wire);
  const ap_ring_frame_fields_t fields = {.header = header,
                                         .header_bytes = sizeof header,
                                         .data = data,
                                         .data_bytes = sizeof data,
                                         .late_acknowledge = 0u};
  TEST_ASSERT_TRUE(ap_ring_frame_emit(&w, &fields));

  /* Flip a bit inside the packet header sequence, past the frame start
   * sequence's 9 + 8 + 9 bits. Chosen to be a zero bit becoming a one only
   * where it cannot manufacture a six-ones violation. */
  wire[4] ^= 0x01u;

  uint8_t got_header[64] = {0};
  uint8_t got_data[64] = {0};
  ap_ring_bitreader_t r;
  ap_ring_bitreader_init(&r, wire, w.bit_count);
  const ap_ring_frame_parse_t p = ap_ring_frame_parse(
      &r, got_header, sizeof got_header, got_data, sizeof got_data);

  TEST_ASSERT_EQUAL_INT(AP_RING_FRAME_CHECK_FAILED, p.status);
  TEST_ASSERT_NOT_EQUAL_HEX32(p.check_computed, p.check_received);
}

/* A stream that does not begin with the frame start character is not a frame,
 * and says which of the two ways it failed. */
static void test_a_stream_that_does_not_begin_with_frame_start_is_refused(void) {
  uint8_t wire[16];
  ap_ring_bitwriter_t w;
  ap_ring_bitwriter_init(&w, wire, sizeof wire);
  ap_ring_write_oob(&w, AP_RING_OOB_FREE_TOKEN);
  ap_ring_write_data_bits(&w, 0u, 8u);

  uint8_t got_header[64] = {0};
  uint8_t got_data[64] = {0};
  ap_ring_bitreader_t r;
  ap_ring_bitreader_init(&r, wire, w.bit_count);
  const ap_ring_frame_parse_t p = ap_ring_frame_parse(
      &r, got_header, sizeof got_header, got_data, sizeof got_data);

  TEST_ASSERT_EQUAL_INT(AP_RING_FRAME_NO_FRAME_START, p.status);
}

/* A frame cut short is truncated rather than malformed: nothing about it was
 * wrong, it simply stopped. A receiver counts that differently. */
static void test_a_frame_cut_short_reports_truncation(void) {
  uint8_t header[12];
  make_header(header);
  uint8_t wire[256];
  ap_ring_bitwriter_t w;
  ap_ring_bitwriter_init(&w, wire, sizeof wire);
  const ap_ring_frame_fields_t fields = {.header = header,
                                         .header_bytes = sizeof header,
                                         .data = NULL,
                                         .data_bytes = 0u,
                                         .late_acknowledge = 0u};
  TEST_ASSERT_TRUE(ap_ring_frame_emit(&w, &fields));

  uint8_t got_header[64] = {0};
  uint8_t got_data[64] = {0};
  ap_ring_bitreader_t r;
  /* Half the frame, so it ends somewhere inside the header sequence. */
  ap_ring_bitreader_init(&r, wire, w.bit_count / 2u);
  const ap_ring_frame_parse_t p = ap_ring_frame_parse(
      &r, got_header, sizeof got_header, got_data, sizeof got_data);

  TEST_ASSERT_EQUAL_INT(AP_RING_FRAME_TRUNCATED, p.status);
}

/* The peek this all rests on: looking does not consume. A reader asked twice
 * gives the same answer, and a read after a peek still gets the character. */
static void test_peeking_at_a_character_does_not_consume_it(void) {
  uint8_t wire[8];
  ap_ring_bitwriter_t w;
  ap_ring_bitwriter_init(&w, wire, sizeof wire);
  ap_ring_write_oob(&w, AP_RING_OOB_SEPARATOR);

  ap_ring_bitreader_t r;
  ap_ring_bitreader_init(&r, wire, w.bit_count);

  uint16_t first = 0u;
  uint16_t second = 0u;
  TEST_ASSERT_TRUE(ap_ring_peek_oob(&r, &first));
  TEST_ASSERT_TRUE(ap_ring_peek_oob(&r, &second));
  TEST_ASSERT_EQUAL_HEX16(AP_RING_OOB_SEPARATOR, first);
  TEST_ASSERT_EQUAL_HEX16(first, second);

  uint16_t read = 0u;
  TEST_ASSERT_TRUE(ap_ring_read_oob(&r, &read));
  TEST_ASSERT_EQUAL_HEX16(first, read);
}

/* And a peek at data is false rather than a wrong character: the stuffing
 * guarantees data can never present six consecutive ones, which is exactly what
 * makes looking safe. */
static void test_a_peek_at_data_finds_no_character(void) {
  uint8_t wire[8];
  ap_ring_bitwriter_t w;
  ap_ring_bitwriter_init(&w, wire, sizeof wire);
  ap_ring_write_data_bits(&w, 0xFFFFu, 16u);

  ap_ring_bitreader_t r;
  ap_ring_bitreader_init(&r, wire, w.bit_count);
  uint16_t symbol = 0u;
  TEST_ASSERT_FALSE(ap_ring_peek_oob(&r, &symbol));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_frame_round_trips_through_the_bit_stream);
  RUN_TEST(test_a_frame_opens_with_start_null_separator_and_separator);
  RUN_TEST(test_a_frame_with_no_packet_data_is_legal);
  RUN_TEST(test_a_header_shorter_than_twelve_bytes_is_refused_whole);
  RUN_TEST(test_an_odd_header_length_is_refused);
  RUN_TEST(test_a_longer_header_parses_back_at_its_own_length);
  RUN_TEST(test_a_payload_of_ones_does_not_look_like_a_character);
  RUN_TEST(test_a_corrupted_frame_arrives_whole_and_fails_its_check);
  RUN_TEST(test_a_stream_that_does_not_begin_with_frame_start_is_refused);
  RUN_TEST(test_a_frame_cut_short_reports_truncation);
  RUN_TEST(test_peeking_at_a_character_does_not_consume_it);
  RUN_TEST(test_a_peek_at_data_finds_no_character);
  return UNITY_END();
}

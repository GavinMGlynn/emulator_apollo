/* Apollo Token Ring MAC, symbol level. Every fact asserted here cites
 * `[MAC]` = 010005-00, and there is no oracle: MAME carries Domain networking
 * over a 3c505 and never touches the ring, so these tests are the only check
 * this layer has. That makes the *citations* load-bearing rather than
 * decorative -- a wrong constant here would be wrong consistently, in the code
 * and in the test, and nothing else would notice. */

#include "ring/ap_ring_mac.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* `[MAC]` §2.2.1 p. 2-4: every out-of-band character is a leading Zero, six
 * Ones, and two type bits, transmitted most-significant-bit first. The four
 * type-bit pairs are Figures 2-2 (p. 2-4), 2-3 and 2-4 (p. 2-5), read from the
 * page images -- `pdftotext` renders those figures as unusable fragments. */
static void test_the_four_out_of_band_characters_are_nine_bits_of_that_shape(
    void) {
  const uint16_t all[] = {AP_RING_OOB_SEPARATOR, AP_RING_OOB_FRAME_START,
                          AP_RING_OOB_FREE_TOKEN, AP_RING_OOB_CLAIMED_TOKEN};
  for (unsigned i = 0; i < 4u; i++) {
    TEST_ASSERT_TRUE(ap_ring_oob_well_formed(all[i]));
    /* The leading zero, then six ones. */
    TEST_ASSERT_EQUAL_UINT(0u, (all[i] >> 8) & 1u);
    TEST_ASSERT_EQUAL_UINT(0x3Fu, (all[i] >> 2) & 0x3Fu);
  }
  /* And the type bits are what the figures show, in that order. */
  TEST_ASSERT_EQUAL_UINT(0u, ap_ring_oob_type(AP_RING_OOB_SEPARATOR));
  TEST_ASSERT_EQUAL_UINT(1u, ap_ring_oob_type(AP_RING_OOB_FRAME_START));
  TEST_ASSERT_EQUAL_UINT(2u, ap_ring_oob_type(AP_RING_OOB_FREE_TOKEN));
  TEST_ASSERT_EQUAL_UINT(3u, ap_ring_oob_type(AP_RING_OOB_CLAIMED_TOKEN));
}

/* `[MAC]` §2.2.1.1 p. 2-4: a node claims the ring "by changing the state of the
 * character's last bit". That is an independent statement of the same fact the
 * figure gives, and the two agreeing is the only cross-check this constant
 * has. */
static void test_claiming_a_free_token_changes_only_its_last_bit(void) {
  const uint16_t claimed = ap_ring_token_claim(AP_RING_OOB_FREE_TOKEN);
  TEST_ASSERT_EQUAL_HEX16(AP_RING_OOB_CLAIMED_TOKEN, claimed);
  TEST_ASSERT_EQUAL_HEX16(AP_RING_OOB_FREE_TOKEN ^ 1u, claimed);
  /* Idempotent: claiming a claimed token leaves it claimed rather than
   * toggling it back to free, which a XOR would do. */
  TEST_ASSERT_EQUAL_HEX16(claimed, ap_ring_token_claim(claimed));
}

/* A value of the wrong shape is not an out-of-band character.
 *
 * Note what is *not* in this list. `0xFE` looks like "the free token's low
 * eight bits" and is not a separate value at all: `0 111111 10` **is** 254, the
 * leading zero costing nothing numerically. That case was written here as a
 * rejection and was wrong -- there is no eight-bit form of these characters to
 * confuse with the nine-bit one, only a nine-bit value that happens to fit in
 * eight. The distinction that matters is on the wire, where the leading zero
 * occupies a bit time. */
static void test_a_value_of_the_wrong_shape_is_not_an_out_of_band_character(
    void) {
  TEST_ASSERT_FALSE(ap_ring_oob_well_formed(0x1FEu)); /* leading bit set */
  TEST_ASSERT_FALSE(ap_ring_oob_well_formed(0x0F8u)); /* ones run too short */
  TEST_ASSERT_FALSE(ap_ring_oob_well_formed(0x0F4u)); /* a zero inside the run */
  TEST_ASSERT_FALSE(ap_ring_oob_well_formed(0x200u)); /* wider than nine bits */
  /* And the boundary the above is easy to break: all four real characters
   * still validate. */
  TEST_ASSERT_TRUE(ap_ring_oob_well_formed(0x0FEu));
}

/* `[MAC]` §2.2.1 p. 2-4: "a transmitting node inserts a Zero ... after every
 * five successive Ones". Six ones of data must therefore leave the writer with
 * a zero between the fifth and the sixth. */
static void test_five_ones_of_data_are_followed_by_a_stuffed_zero(void) {
  uint8_t buffer[4];
  ap_ring_bitwriter_t w;
  ap_ring_bitwriter_init(&w, buffer, sizeof buffer);

  ap_ring_write_data_bits(&w, 0x3Fu, 6u); /* six ones */

  /* Six data bits became seven on the wire. */
  TEST_ASSERT_EQUAL_size_t(7u, w.bit_count);
  /* 1111 1 0 1 -> 0b1111101 in the top seven bits. */
  TEST_ASSERT_EQUAL_HEX8(0xFAu, buffer[0]);
  TEST_ASSERT_FALSE(w.overflow);
}

/* And the run is counted across a call boundary, since the rule is about the
 * stream and not about any one field. Three ones then three ones must stuff in
 * the same place as six ones written at once. */
static void test_the_ones_run_carries_across_writes(void) {
  uint8_t a[4];
  uint8_t b[4];
  ap_ring_bitwriter_t wa;
  ap_ring_bitwriter_t wb;
  ap_ring_bitwriter_init(&wa, a, sizeof a);
  ap_ring_bitwriter_init(&wb, b, sizeof b);

  ap_ring_write_data_bits(&wa, 0x3Fu, 6u);
  ap_ring_write_data_bits(&wb, 0x7u, 3u);
  ap_ring_write_data_bits(&wb, 0x7u, 3u);

  TEST_ASSERT_EQUAL_size_t(wa.bit_count, wb.bit_count);
  TEST_ASSERT_EQUAL_HEX8(a[0], b[0]);
}

/* The whole point of the scheme: an out-of-band character is written raw, so
 * six ones reach the wire unbroken where data could never produce them. */
static void
test_an_out_of_band_character_puts_six_ones_on_the_wire_unstuffed(void) {
  uint8_t buffer[4];
  ap_ring_bitwriter_t w;
  ap_ring_bitwriter_init(&w, buffer, sizeof buffer);

  ap_ring_write_oob(&w, AP_RING_OOB_FREE_TOKEN);

  TEST_ASSERT_EQUAL_size_t(AP_RING_OOB_BITS, w.bit_count);
  /* 0 111111 10, MSB first: 01111111 0....... */
  TEST_ASSERT_EQUAL_HEX8(0x7Fu, buffer[0]);
  TEST_ASSERT_EQUAL_HEX8(0x00u, buffer[1]);
}

/* Round trip, which is the plan item's stated verification: data in, data out,
 * with the stuffing invisible to both ends. `0xFF` bytes are the interesting
 * case because they are all ones and stuff twice. */
static void test_stuffed_data_round_trips_through_the_reader(void) {
  static const uint32_t values[] = {0x00u, 0xFFu, 0xF8u, 0x1Fu, 0x55u, 0xAAu};
  for (unsigned i = 0; i < sizeof values / sizeof values[0]; i++) {
    uint8_t buffer[8];
    ap_ring_bitwriter_t w;
    ap_ring_bitwriter_init(&w, buffer, sizeof buffer);
    ap_ring_write_data_bits(&w, values[i], 8u);
    TEST_ASSERT_FALSE(w.overflow);

    ap_ring_bitreader_t r;
    ap_ring_bitreader_init(&r, buffer, w.bit_count);
    uint32_t got = 0u;
    bool violation = false;
    TEST_ASSERT_TRUE(ap_ring_read_data_bits(&r, 8u, &got, &violation));
    TEST_ASSERT_FALSE(violation);
    TEST_ASSERT_EQUAL_HEX32(values[i], got);
  }
}

/* A long run of ones round trips too, which is where a stuffing bug shows: a
 * reader that discarded the wrong zero would return a shifted value rather
 * than a wrong bit, so the failure is loud. */
static void test_a_long_run_of_ones_round_trips(void) {
  uint8_t buffer[16];
  ap_ring_bitwriter_t w;
  ap_ring_bitwriter_init(&w, buffer, sizeof buffer);
  ap_ring_write_data_bits(&w, 0xFFFFFFFFu, 32u);
  TEST_ASSERT_FALSE(w.overflow);
  /* 32 ones stuff a zero after each fifth: six stuffed zeros in 32 bits. */
  TEST_ASSERT_EQUAL_size_t(32u + 6u, w.bit_count);

  ap_ring_bitreader_t r;
  ap_ring_bitreader_init(&r, buffer, w.bit_count);
  uint32_t got = 0u;
  bool violation = false;
  TEST_ASSERT_TRUE(ap_ring_read_data_bits(&r, 32u, &got, &violation));
  TEST_ASSERT_FALSE(violation);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, got);
}

/* And the receiver's actual job: data, then a control character, told apart by
 * the violation rather than by any length field. `[MAC]` §2.2.1 p. 2-4 -- "the
 * bit-stuffing protocol has been intentionally violated by the transmitter". */
static void test_a_reader_detects_the_violation_that_begins_a_character(void) {
  uint8_t buffer[8];
  ap_ring_bitwriter_t w;
  ap_ring_bitwriter_init(&w, buffer, sizeof buffer);
  ap_ring_write_data_bits(&w, 0xAAu, 8u); /* data with no long run */
  ap_ring_write_oob(&w, AP_RING_OOB_FRAME_START);
  TEST_ASSERT_FALSE(w.overflow);

  ap_ring_bitreader_t r;
  ap_ring_bitreader_init(&r, buffer, w.bit_count);
  uint32_t got = 0u;
  bool violation = false;
  TEST_ASSERT_TRUE(ap_ring_read_data_bits(&r, 8u, &got, &violation));
  TEST_ASSERT_FALSE(violation);
  TEST_ASSERT_EQUAL_HEX32(0xAAu, got);

  uint16_t symbol = 0u;
  TEST_ASSERT_TRUE(ap_ring_read_oob(&r, &symbol));
  TEST_ASSERT_EQUAL_HEX16(AP_RING_OOB_FRAME_START, symbol);
  TEST_ASSERT_TRUE(ap_ring_oob_well_formed(symbol));
}

/* `[MAC]` §2.2.1.3 p. 2-5: the long null separator is "a minimum of 8 bytes of
 * Zeros" and the short one is "a byte of Zeros". A minimum is recorded as a
 * minimum -- a receiver that demanded exactly eight would reject a conforming
 * transmitter that sent nine. */
static void test_the_null_separators_are_the_documented_lengths(void) {
  TEST_ASSERT_EQUAL_UINT(1u, AP_RING_NULL_SEPARATOR_SHORT_BYTES);
  TEST_ASSERT_EQUAL_UINT(8u, AP_RING_NULL_SEPARATOR_LONG_MIN_BYTES);
}

/* A writer that runs out of room says so and stays said, rather than wrapping
 * or writing past the end. Sticky because a caller that checks once at the end
 * is the usual shape. */
static void test_a_full_writer_reports_overflow_and_keeps_reporting_it(void) {
  uint8_t buffer[1];
  ap_ring_bitwriter_t w;
  ap_ring_bitwriter_init(&w, buffer, sizeof buffer);
  ap_ring_write_data_bits(&w, 0u, 8u);
  TEST_ASSERT_FALSE(w.overflow);
  ap_ring_write_data_bit(&w, false);
  TEST_ASSERT_TRUE(w.overflow);
  TEST_ASSERT_EQUAL_size_t(8u, w.bit_count);
  ap_ring_write_data_bit(&w, false);
  TEST_ASSERT_TRUE(w.overflow);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_four_out_of_band_characters_are_nine_bits_of_that_shape);
  RUN_TEST(test_claiming_a_free_token_changes_only_its_last_bit);
  RUN_TEST(test_a_value_of_the_wrong_shape_is_not_an_out_of_band_character);
  RUN_TEST(test_five_ones_of_data_are_followed_by_a_stuffed_zero);
  RUN_TEST(test_the_ones_run_carries_across_writes);
  RUN_TEST(test_an_out_of_band_character_puts_six_ones_on_the_wire_unstuffed);
  RUN_TEST(test_stuffed_data_round_trips_through_the_reader);
  RUN_TEST(test_a_long_run_of_ones_round_trips);
  RUN_TEST(test_a_reader_detects_the_violation_that_begins_a_character);
  RUN_TEST(test_the_null_separators_are_the_documented_lengths);
  RUN_TEST(test_a_full_writer_reports_overflow_and_keeps_reporting_it);
  return UNITY_END();
}

#include <string.h>

#include "state/ap_hash.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static uint64_t hash_of_bytes(const char *s) {
  ap_hash_t st = ap_hash_begin();
  ap_hash_bytes(&st, s, strlen(s));
  return ap_hash_end(&st);
}

/* The point of using a published algorithm: these expected values come from the
 * FNV-1a 64-bit reference, not from our own output. A hash that only agrees
 * with itself proves nothing about portability. */
static void test_the_byte_hash_matches_the_published_fnv1a_64_vectors(void) {
  TEST_ASSERT_EQUAL_HEX64(UINT64_C(0xcbf29ce484222325), hash_of_bytes(""));
  TEST_ASSERT_EQUAL_HEX64(UINT64_C(0xaf63dc4c8601ec8c), hash_of_bytes("a"));
  TEST_ASSERT_EQUAL_HEX64(UINT64_C(0x85944171f73967e8), hash_of_bytes("foobar"));
}

static void test_an_empty_hash_is_the_offset_basis(void) {
  ap_hash_t st = ap_hash_begin();
  TEST_ASSERT_EQUAL_HEX64(AP_HASH_OFFSET_BASIS, ap_hash_end(&st));
}

/* Multi-byte values go in little-endian whatever the host is, so the same
 * machine state hashes the same on a big-endian target. Asserted by feeding the
 * bytes explicitly and requiring the tagged helper to agree. */
static void test_a_u32_is_absorbed_little_endian_whatever_the_host_order(void) {
  ap_hash_t typed = ap_hash_begin();
  ap_hash_u32(&typed, 0x01020304u);

  ap_hash_t manual = ap_hash_begin();
  const uint8_t expect[] = {(uint8_t)AP_HASH_TAG_U32, 0x04u, 0x03u, 0x02u, 0x01u};
  ap_hash_bytes(&manual, expect, sizeof expect);

  TEST_ASSERT_EQUAL_HEX64(ap_hash_end(&manual), ap_hash_end(&typed));
}

static void test_a_u64_is_absorbed_little_endian(void) {
  ap_hash_t typed = ap_hash_begin();
  ap_hash_u64(&typed, UINT64_C(0x0102030405060708));

  ap_hash_t manual = ap_hash_begin();
  const uint8_t expect[] = {(uint8_t)AP_HASH_TAG_U64, 0x08u, 0x07u, 0x06u,
                            0x05u, 0x04u, 0x03u, 0x02u, 0x01u};
  ap_hash_bytes(&manual, expect, sizeof expect);

  TEST_ASSERT_EQUAL_HEX64(ap_hash_end(&manual), ap_hash_end(&typed));
}

/* The structural hazard: without width tags, two 16-bit fields and one 32-bit
 * field holding the same bytes would be indistinguishable, so a state layout
 * change could silently preserve the hash. */
static void test_two_u16_fields_do_not_hash_as_one_u32(void) {
  ap_hash_t pair = ap_hash_begin();
  ap_hash_u16(&pair, 0x0102u);
  ap_hash_u16(&pair, 0x0304u);

  ap_hash_t single = ap_hash_begin();
  ap_hash_u32(&single, 0x03040102u);

  TEST_ASSERT_NOT_EQUAL_UINT64(ap_hash_end(&single), ap_hash_end(&pair));
}

/* A time and a bare 64-bit register holding the same number are different
 * machine state. */
static void test_a_time_and_a_u64_of_the_same_value_hash_differently(void) {
  ap_hash_t as_time = ap_hash_begin();
  ap_hash_time(&as_time, (ap_time_t)6600u);

  ap_hash_t as_u64 = ap_hash_begin();
  ap_hash_u64(&as_u64, UINT64_C(6600));

  TEST_ASSERT_NOT_EQUAL_UINT64(ap_hash_end(&as_u64), ap_hash_end(&as_time));
}

/* State is an ordered thing: two registers with swapped contents is a different
 * machine, and must not collide. */
static void test_field_order_changes_the_hash(void) {
  ap_hash_t ab = ap_hash_begin();
  ap_hash_u32(&ab, 0xAAAAAAAAu);
  ap_hash_u32(&ab, 0xBBBBBBBBu);

  ap_hash_t ba = ap_hash_begin();
  ap_hash_u32(&ba, 0xBBBBBBBBu);
  ap_hash_u32(&ba, 0xAAAAAAAAu);

  TEST_ASSERT_NOT_EQUAL_UINT64(ap_hash_end(&ba), ap_hash_end(&ab));
}

/* Hashing a large state must be expressible as a stream, and streaming must
 * equal the one-shot answer, or a subsystem could not contribute piecemeal. */
static void test_absorbing_in_pieces_equals_absorbing_at_once(void) {
  ap_hash_t whole = ap_hash_begin();
  ap_hash_bytes(&whole, "foobar", 6u);

  ap_hash_t pieces = ap_hash_begin();
  ap_hash_bytes(&pieces, "foo", 3u);
  ap_hash_bytes(&pieces, "bar", 3u);

  TEST_ASSERT_EQUAL_HEX64(ap_hash_end(&whole), ap_hash_end(&pieces));
}

/* ap_hash_end is a read, not a consume: a long run must be able to report an
 * intermediate hash and carry on. */
static void test_ending_a_hash_does_not_consume_it(void) {
  ap_hash_t st = ap_hash_begin();
  ap_hash_u32(&st, 0x12345678u);
  uint64_t first = ap_hash_end(&st);
  TEST_ASSERT_EQUAL_HEX64(first, ap_hash_end(&st));

  ap_hash_u32(&st, 0x9ABCDEF0u);
  TEST_ASSERT_NOT_EQUAL_UINT64(first, ap_hash_end(&st));
}

/* Zero is real state and must be absorbed, not skipped: a cleared register and
 * an absent one are different machines. */
static void test_absorbing_a_zero_field_changes_the_hash(void) {
  ap_hash_t empty = ap_hash_begin();
  ap_hash_t zeroed = ap_hash_begin();
  ap_hash_u32(&zeroed, 0u);
  TEST_ASSERT_NOT_EQUAL_UINT64(ap_hash_end(&empty), ap_hash_end(&zeroed));
}

/* Zero-length absorb is a no-op, so an empty RAM region does not perturb it. */
static void test_absorbing_no_bytes_leaves_the_hash_unchanged(void) {
  ap_hash_t st = ap_hash_begin();
  ap_hash_u16(&st, 0xBEEFu);
  uint64_t before = ap_hash_end(&st);
  ap_hash_bytes(&st, "", 0u);
  TEST_ASSERT_EQUAL_HEX64(before, ap_hash_end(&st));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_byte_hash_matches_the_published_fnv1a_64_vectors);
  RUN_TEST(test_an_empty_hash_is_the_offset_basis);
  RUN_TEST(test_a_u32_is_absorbed_little_endian_whatever_the_host_order);
  RUN_TEST(test_a_u64_is_absorbed_little_endian);
  RUN_TEST(test_two_u16_fields_do_not_hash_as_one_u32);
  RUN_TEST(test_a_time_and_a_u64_of_the_same_value_hash_differently);
  RUN_TEST(test_field_order_changes_the_hash);
  RUN_TEST(test_absorbing_in_pieces_equals_absorbing_at_once);
  RUN_TEST(test_ending_a_hash_does_not_consume_it);
  RUN_TEST(test_absorbing_a_zero_field_changes_the_hash);
  RUN_TEST(test_absorbing_no_bytes_leaves_the_hash_unchanged);
  return UNITY_END();
}

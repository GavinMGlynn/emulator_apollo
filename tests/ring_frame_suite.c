/* Apollo Token Ring MAC frame level, `[MAC]` §2.2.2. No oracle exists for any
 * of this, so as in `ring_mac_suite` the citations carry the weight: a wrong
 * constant would be wrong identically in the code and the test. Where the
 * manual is silent the test says so rather than inventing a check. */

#include "ring/ap_ring_frame.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* `[MAC]` Figure 2-5 p. 2-6: destination at +0 (2 words), type at +4 (1 word),
 * a zero byte and the early acknowledge at +6 and +7, source at +8 (2 words),
 * header data from +C. The fixed part is twelve bytes and the header runs to
 * 1024, so the header data's 1012 completes it exactly. */
static void test_the_packet_header_fields_sit_where_figure_2_5_puts_them(void) {
  TEST_ASSERT_EQUAL_UINT(0u, AP_RING_HDR_DESTINATION);
  TEST_ASSERT_EQUAL_UINT(4u, AP_RING_HDR_TYPE);
  TEST_ASSERT_EQUAL_UINT(6u, AP_RING_HDR_ZEROS);
  TEST_ASSERT_EQUAL_UINT(7u, AP_RING_HDR_EARLY_ACK);
  TEST_ASSERT_EQUAL_UINT(8u, AP_RING_HDR_SOURCE);
  TEST_ASSERT_EQUAL_UINT(12u, AP_RING_HDR_DATA);
  TEST_ASSERT_EQUAL_UINT(AP_RING_HDR_MAX_BYTES,
                         AP_RING_HDR_FIXED_BYTES + AP_RING_HDR_DATA_MAX_BYTES);
}

/* "Although a packet header can vary in size from 12 to 1024 bytes, it must
 * always consist of an even number of bytes" (§2.2.2.2 p. 2-6), and packet data
 * "from 0 to 4096 bytes ... always ... an even number" (§2.2.2.3 p. 2-8). */
static void test_the_documented_length_limits_are_enforced_at_both_ends(void) {
  TEST_ASSERT_FALSE(ap_ring_header_length_valid(10u));
  TEST_ASSERT_TRUE(ap_ring_header_length_valid(12u));
  TEST_ASSERT_FALSE(ap_ring_header_length_valid(13u)); /* odd */
  TEST_ASSERT_TRUE(ap_ring_header_length_valid(1024u));
  TEST_ASSERT_FALSE(ap_ring_header_length_valid(1026u));

  TEST_ASSERT_TRUE(ap_ring_data_length_valid(0u)); /* zero is legal */
  TEST_ASSERT_FALSE(ap_ring_data_length_valid(1u));
  TEST_ASSERT_TRUE(ap_ring_data_length_valid(4096u));
  TEST_ASSERT_FALSE(ap_ring_data_length_valid(4098u));
}

/* `[MAC]` Figure 2-6 p. 2-7: the type field proper is bits 7:1. Bits 15:8 are
 * reserved and **bit 0 is reserved too** -- easy to lose, and it would make a
 * seven-bit field look like an eight-bit one. */
static void test_the_type_bits_are_figure_2_6s_and_bit_zero_is_not_one(void) {
  TEST_ASSERT_EQUAL_HEX16(0x80u, AP_RING_TYPE_BROADCAST);
  TEST_ASSERT_EQUAL_HEX16(0x40u, AP_RING_TYPE_HW_DIAGNOSTICS);
  TEST_ASSERT_EQUAL_HEX16(0x20u, AP_RING_TYPE_THANK_YOU);
  TEST_ASSERT_EQUAL_HEX16(0x10u, AP_RING_TYPE_PLEASE);
  TEST_ASSERT_EQUAL_HEX16(0x08u, AP_RING_TYPE_PAGING);
  TEST_ASSERT_EQUAL_HEX16(0x04u, AP_RING_TYPE_USER);
  TEST_ASSERT_EQUAL_HEX16(0x02u, AP_RING_TYPE_SW_DIAGNOSTICS);

  /* **Corroborated by a second document**, `RING.md` 99: `002398-04` p. 12-33
   * carries a "Hardware Packet Types" table -- `80 broadcast`, `40 hw_diag`,
   * `20 thank you`, `10 please`, `8 paging`, `4 user`, `2 sw_diag`, `1 -` --
   * which is `[MAC]` Figure 2-6 bit for bit, from the Engineering Handbook
   * rather than the MAC specification, including the unnamed bit 0. Two
   * independent typesettings of the same field, eight of eight.
   *
   * It also settles finding 49's `d6 = $4040`, which that finding called
   * "inference, not established": the **low byte is `$40`, `hw_diag`** --
   * exactly the type a board's own self-test packet should carry. The high
   * byte's `$40` is in the range both documents call reserved and stays
   * unexplained. */

  /* Every named bit is outside the reserved mask, and together they are
   * exactly its complement -- so no type bit was dropped and none invented. */
  const uint16_t named =
      AP_RING_TYPE_BROADCAST | AP_RING_TYPE_HW_DIAGNOSTICS |
      AP_RING_TYPE_THANK_YOU | AP_RING_TYPE_PLEASE | AP_RING_TYPE_PAGING |
      AP_RING_TYPE_USER | AP_RING_TYPE_SW_DIAGNOSTICS;
  TEST_ASSERT_EQUAL_HEX16(0u, named & AP_RING_TYPE_RESERVED_MASK);
  TEST_ASSERT_EQUAL_HEX16(0xFFFFu, named | AP_RING_TYPE_RESERVED_MASK);
}

/* §2.2.2.2 p. 2-7: "a node receives a message if the destination address field
 * matches its node address, or if the broadcast bit in the type field ... is
 * set". And when broadcast is set the destination bits "are free for
 * beaconing", so they must not be compared at all. */
static void test_a_node_accepts_its_own_address_or_any_broadcast(void) {
  uint8_t header[AP_RING_HDR_FIXED_BYTES] = {0};
  ap_ring_header_set_destination(header, 0x00012345u);
  ap_ring_header_set_source(header, 0x000ABCDEu);
  ap_ring_header_set_type(header, 0u);

  TEST_ASSERT_TRUE(ap_ring_header_addresses(header, 0x00012345u));
  TEST_ASSERT_FALSE(ap_ring_header_addresses(header, 0x00012346u));

  /* Broadcast, with a destination field holding beacon rubbish. */
  ap_ring_header_set_type(header, AP_RING_TYPE_BROADCAST);
  ap_ring_header_set_destination(header, 0xDEADBEEFu);
  TEST_ASSERT_TRUE(ap_ring_header_addresses(header, 0x00012345u));
  TEST_ASSERT_TRUE(ap_ring_header_addresses(header, 0u));

  /* Addresses are 32 bits and big-endian on the wire. */
  ap_ring_header_set_type(header, 0u);
  ap_ring_header_set_destination(header, 0x01020304u);
  TEST_ASSERT_EQUAL_HEX8(0x01u, header[0]);
  TEST_ASSERT_EQUAL_HEX8(0x04u, header[3]);
  TEST_ASSERT_EQUAL_HEX32(0x01020304u, ap_ring_header_destination(header));
  TEST_ASSERT_EQUAL_HEX32(0x000ABCDEu, ap_ring_header_source(header));
}

/* `[MAC]` Figures 2-7 (p. 2-8) and 2-8 (p. 2-9). Both fields keep bits 7, 4 and
 * 0 zero, and both use bit 1 for odd parity. */
static void test_the_acknowledge_fields_are_figures_2_7_and_2_8s(void) {
  TEST_ASSERT_EQUAL_HEX8(0x08u, AP_RING_EARLY_INTEND_TO_COPY);
  TEST_ASSERT_EQUAL_HEX8(0x02u, AP_RING_EARLY_PARITY);
  TEST_ASSERT_EQUAL_HEX8(0x91u, AP_RING_EARLY_MUST_BE_ZERO);

  TEST_ASSERT_EQUAL_HEX8(0x40u, AP_RING_LATE_COPIED);
  TEST_ASSERT_EQUAL_HEX8(0x20u, AP_RING_LATE_WAIT_ACK);
  TEST_ASSERT_EQUAL_HEX8(0x08u, AP_RING_LATE_INTEND_TO_COPY);
  TEST_ASSERT_EQUAL_HEX8(0x04u, AP_RING_LATE_ERROR);
  TEST_ASSERT_EQUAL_HEX8(0x02u, AP_RING_LATE_PARITY);
  TEST_ASSERT_EQUAL_HEX8(0x91u, AP_RING_LATE_MUST_BE_ZERO);

  /* The two fields agree where the figures show them agreeing: intend-to-copy
   * at bit 3, parity at bit 1, the same must-be-zero bits. */
  TEST_ASSERT_EQUAL_HEX8(AP_RING_EARLY_INTEND_TO_COPY,
                         AP_RING_LATE_INTEND_TO_COPY);
  TEST_ASSERT_EQUAL_HEX8(AP_RING_EARLY_PARITY, AP_RING_LATE_PARITY);
}

/* "This bit is used for odd parity. When it is set, an odd number of Ones
 * appears in the frame's ... acknowledge field." Read as a property of the
 * whole field: a well-formed one always has odd population. */
static void test_acknowledge_parity_makes_the_whole_field_odd(void) {
  /* Empty field: zero ones, so parity must be set to make it odd. */
  TEST_ASSERT_EQUAL_HEX8(AP_RING_EARLY_PARITY, ap_ring_ack_with_parity(0u));
  TEST_ASSERT_TRUE(ap_ring_ack_parity_ok(ap_ring_ack_with_parity(0u)));

  /* One bit set: already odd, so parity stays clear. */
  const uint8_t copied = ap_ring_ack_with_parity(AP_RING_LATE_COPIED);
  TEST_ASSERT_EQUAL_HEX8(AP_RING_LATE_COPIED, copied);
  TEST_ASSERT_TRUE(ap_ring_ack_parity_ok(copied));

  /* Two bits set: parity restores oddness. */
  const uint8_t two =
      ap_ring_ack_with_parity(AP_RING_LATE_COPIED | AP_RING_LATE_ERROR);
  TEST_ASSERT_TRUE(ap_ring_ack_parity_ok(two));
  TEST_ASSERT_TRUE((two & AP_RING_LATE_PARITY) != 0u);

  /* A stale parity bit must not be counted as data: recomputing over a field
   * that already carries one has to give the same answer as computing it
   * fresh. This is the mistake the implementation note warns about. */
  TEST_ASSERT_EQUAL_HEX8(two, ap_ring_ack_with_parity(two));

  /* And a corrupted field is rejected. */
  TEST_ASSERT_FALSE(ap_ring_ack_parity_ok((uint8_t)(two ^ 1u)));
}

/* §2.2.2.4 p. 2-8 gives the generator as a product: g(X) = (X^21 + 1)(X^11 +
 * X^2 + 1). Multiplied out that is X^32 + X^23 + X^21 + X^11 + X^2 + 1, so the
 * register form with X^32 dropped is 0x00A00805.
 *
 * Asserted against the *product*, computed here, rather than against a copy of
 * the constant -- so the test checks the expansion and not merely that two
 * places hold the same number. */
static void test_the_crc_polynomial_is_the_product_the_manual_gives(void) {
  /* (X^21 + 1) * (X^11 + X^2 + 1) over GF(2), as bit positions. */
  uint64_t product = 0u;
  const unsigned a[] = {21u, 0u};
  const unsigned b[] = {11u, 2u, 0u};
  for (unsigned i = 0; i < 2u; i++) {
    for (unsigned j = 0; j < 3u; j++) {
      product ^= (uint64_t)1u << (a[i] + b[j]);
    }
  }
  /* Degree 32, and the X^32 term present -- otherwise it is not a CRC-32. */
  TEST_ASSERT_TRUE((product & ((uint64_t)1u << 32)) != 0u);
  TEST_ASSERT_EQUAL_HEX32(AP_RING_CRC_POLYNOMIAL, (uint32_t)product);

  /* And it is *not* the Ethernet polynomial, which is the mistake this whole
   * constant exists to avoid. */
  TEST_ASSERT_TRUE(AP_RING_CRC_POLYNOMIAL != 0x04C11DB7u);
}

/* The CRC is "initialized to zero", and a CRC of nothing is therefore zero --
 * which also pins that there is no final inversion, since the manual mentions
 * none. */
static void test_the_crc_starts_at_zero_and_detects_a_changed_bit(void) {
  TEST_ASSERT_EQUAL_HEX32(0u, AP_RING_CRC_INIT);

  uint8_t header[AP_RING_HDR_FIXED_BYTES] = {0};
  ap_ring_header_set_destination(header, 0x00001234u);
  ap_ring_header_set_source(header, 0x00005678u);
  ap_ring_header_set_type(header, AP_RING_TYPE_PLEASE);
  const uint8_t data[8] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};

  const uint32_t crc =
      ap_ring_frame_crc(header, sizeof header, data, sizeof data);

  uint8_t flipped[8];
  for (unsigned i = 0; i < sizeof data; i++) {
    flipped[i] = data[i];
  }
  flipped[3] ^= 0x01u;
  const uint32_t other =
      ap_ring_frame_crc(header, sizeof header, flipped, sizeof flipped);
  TEST_ASSERT_TRUE(crc != other);
}

/* The exception that makes the acknowledge scheme work: "ring hardware treats
 * this field as a string of Zeros in its CRC calculation", so a receiver may
 * rewrite the early acknowledge byte in flight and the frame check stays
 * valid. §2.2.2.2 p. 2-8. */
static void test_rewriting_the_early_acknowledge_does_not_change_the_crc(void) {
  uint8_t header[AP_RING_HDR_FIXED_BYTES] = {0};
  ap_ring_header_set_destination(header, 0x00ABCDEFu);
  ap_ring_header_set_source(header, 0x00123456u);
  ap_ring_header_set_type(header, AP_RING_TYPE_USER);
  const uint8_t data[4] = {0xDEu, 0xADu, 0xBEu, 0xEFu};

  const uint32_t before =
      ap_ring_frame_crc(header, sizeof header, data, sizeof data);

  /* A downstream receiver marks intend-to-copy and fixes the parity. */
  ap_ring_header_set_early_ack(
      header, ap_ring_ack_with_parity(AP_RING_EARLY_INTEND_TO_COPY));
  TEST_ASSERT_TRUE(ap_ring_header_early_ack(header) != 0u);

  const uint32_t after =
      ap_ring_frame_crc(header, sizeof header, data, sizeof data);
  TEST_ASSERT_EQUAL_HEX32(before, after);

  /* But a change to any *other* header byte does move it, so the exemption is
   * that one field and not the header generally. */
  header[AP_RING_HDR_ZEROS] = 0xFFu;
  TEST_ASSERT_TRUE(ap_ring_frame_crc(header, sizeof header, data,
                                     sizeof data) != before);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_packet_header_fields_sit_where_figure_2_5_puts_them);
  RUN_TEST(test_the_documented_length_limits_are_enforced_at_both_ends);
  RUN_TEST(test_the_type_bits_are_figure_2_6s_and_bit_zero_is_not_one);
  RUN_TEST(test_a_node_accepts_its_own_address_or_any_broadcast);
  RUN_TEST(test_the_acknowledge_fields_are_figures_2_7_and_2_8s);
  RUN_TEST(test_acknowledge_parity_makes_the_whole_field_odd);
  RUN_TEST(test_the_crc_polynomial_is_the_product_the_manual_gives);
  RUN_TEST(test_the_crc_starts_at_zero_and_detects_a_changed_bit);
  RUN_TEST(test_rewriting_the_early_acknowledge_does_not_change_the_crc);
  return UNITY_END();
}

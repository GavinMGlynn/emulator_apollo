/* Apollo Token Ring MAC frame level. See `ap_ring_frame.h` for the citations
 * and for why the CRC polynomial is written out rather than reused. */

#include "ring/ap_ring_frame.h"

#include "ring/ap_ring_mac.h"

static unsigned popcount8(uint8_t v) {
  unsigned n = 0;
  while (v != 0u) {
    n += (unsigned)(v & 1u);
    v = (uint8_t)(v >> 1);
  }
  return n;
}

bool ap_ring_ack_parity_ok(uint8_t field) {
  return (popcount8(field) & 1u) == 1u;
}

uint8_t ap_ring_ack_with_parity(uint8_t field) {
  /* Clear the parity bit first, then set it if the rest has even population --
   * which makes the whole field odd. Computing it from the field *including* a
   * stale parity bit is the obvious way to get this wrong. */
  const uint8_t without = (uint8_t)(field & ~(unsigned)AP_RING_EARLY_PARITY);
  if ((popcount8(without) & 1u) == 0u) {
    return (uint8_t)(without | AP_RING_EARLY_PARITY);
  }
  return without;
}

uint32_t ap_ring_crc_bits(uint32_t crc, uint32_t value, unsigned bits) {
  for (unsigned i = 0; i < bits; i++) {
    const unsigned shift = bits - 1u - i;
    const uint32_t bit = (value >> shift) & 1u;
    /* Most-significant bit first, shifting left: "The sender transmits the CRC
     * most-significant bit first". No reflection anywhere -- this is the
     * straightforward MSB-first form, and the manual gives no reason to expect
     * the reflected one. */
    const uint32_t top = (crc >> 31) & 1u;
    crc <<= 1;
    if ((top ^ bit) != 0u) {
      crc ^= AP_RING_CRC_POLYNOMIAL;
    }
  }
  return crc;
}

uint32_t ap_ring_crc_bytes(uint32_t crc, const uint8_t *bytes, size_t count) {
  for (size_t i = 0; i < count; i++) {
    crc = ap_ring_crc_bits(crc, bytes[i], 8u);
  }
  return crc;
}

uint32_t ap_ring_frame_crc(const uint8_t *header, size_t header_bytes,
                           const uint8_t *data, size_t data_bytes) {
  uint32_t crc = AP_RING_CRC_INIT;

  /* The header, with the early acknowledge byte contributed as zero.
   * §2.2.2.2 p. 2-8: "ring hardware treats this field as a string of Zeros in
   * its CRC calculation", which is what lets a receiver rewrite it in flight
   * without the frame check going stale. Everything else goes in as it is. */
  for (size_t i = 0; i < header_bytes; i++) {
    const uint8_t byte = (i == AP_RING_HDR_EARLY_ACK) ? 0u : header[i];
    crc = ap_ring_crc_bits(crc, byte, 8u);
  }

  /* The separator that closes the packet header sequence. */
  crc = ap_ring_crc_bits(crc, AP_RING_OOB_SEPARATOR, AP_RING_OOB_BITS);

  crc = ap_ring_crc_bytes(crc, data, data_bytes);

  /* And the separator closing the packet data sequence. */
  crc = ap_ring_crc_bits(crc, AP_RING_OOB_SEPARATOR, AP_RING_OOB_BITS);
  return crc;
}

bool ap_ring_header_length_valid(size_t bytes) {
  return bytes >= AP_RING_HDR_FIXED_BYTES && bytes <= AP_RING_HDR_MAX_BYTES &&
         (bytes % 2u) == 0u;
}

bool ap_ring_data_length_valid(size_t bytes) {
  return bytes <= AP_RING_DATA_MAX_BYTES && (bytes % 2u) == 0u;
}

static uint32_t read_be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void write_be32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}

uint32_t ap_ring_header_destination(const uint8_t *header) {
  return read_be32(header + AP_RING_HDR_DESTINATION);
}

uint32_t ap_ring_header_source(const uint8_t *header) {
  return read_be32(header + AP_RING_HDR_SOURCE);
}

/* **The type word is stored low byte first, and Domain/OS is the witness.**
 *
 * `[MAC]` §2.2.2 gives the field as "a 16-bit word: bits 15:8 reserved, bits
 * 7:1 the type field proper", and `002398-04` p. 7-31's `TMASK` names those
 * seven as a **byte** -- `80 broadcast, 40 hardware diagnostic, 20 thank you,
 * 10 please, ...`. Neither says which half of the word goes first, and this
 * read it big-endian like the two address fields either side of it.
 *
 * The driver settles it. Captured from a real transmit on two booted nodes
 * (`FINDINGS.md` C248), the twelve header bytes are
 *
 *     00 00 00 00  90 00  00 00  00 01 23 45
 *
 * whose source field at 8 is `00012345`, exactly the node's own ID, so the
 * layout and the offsets are right. That makes the type bytes `90 00`, and
 * big-endian they are `9000` -- **entirely reserved bits and no type at all**.
 * Low byte first they are `0090`: `BROADCAST | PLEASE`, which is precisely what
 * `lcnode` sends, a broadcast request that other nodes answer with `THANK_YOU`.
 *
 * Read big-endian, every broadcast on the ring failed
 * `ap_ring_header_addressed_to`'s test and was dropped: two nodes exchanged 115
 * and 122 frames and copied none of them. */
uint16_t ap_ring_header_type(const uint8_t *header) {
  return (uint16_t)(((uint16_t)header[AP_RING_HDR_TYPE + 1u] << 8) |
                    header[AP_RING_HDR_TYPE]);
}

uint8_t ap_ring_header_early_ack(const uint8_t *header) {
  return header[AP_RING_HDR_EARLY_ACK];
}

void ap_ring_header_set_destination(uint8_t *header, uint32_t address) {
  write_be32(header + AP_RING_HDR_DESTINATION, address);
}

void ap_ring_header_set_source(uint8_t *header, uint32_t address) {
  write_be32(header + AP_RING_HDR_SOURCE, address);
}

void ap_ring_header_set_type(uint8_t *header, uint16_t type) {
  /* Low byte first, matching the reader above and the driver it was derived
   * from. The two stay a pair: a round trip through them is what every frame
   * test asserts, so the endianness is only observable against a header this
   * core did not write -- which is why it took a captured one to find. */
  header[AP_RING_HDR_TYPE] = (uint8_t)type;
  header[AP_RING_HDR_TYPE + 1u] = (uint8_t)(type >> 8);
}

void ap_ring_header_set_early_ack(uint8_t *header, uint8_t field) {
  header[AP_RING_HDR_EARLY_ACK] = field;
}

bool ap_ring_header_addresses(const uint8_t *header, uint32_t node) {
  if ((ap_ring_header_type(header) & AP_RING_TYPE_BROADCAST) != 0u) {
    /* "If it is set, receivers ignore the destination address field", and the
     * bits there are then "free for beaconing" -- so a broadcast frame's
     * destination field may hold anything at all and must not be compared. */
    return true;
  }
  return ap_ring_header_destination(header) == node;
}

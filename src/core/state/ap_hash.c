#include "state/ap_hash.h"

ap_hash_t ap_hash_begin(void) {
  return (ap_hash_t){.h = AP_HASH_OFFSET_BASIS};
}

void ap_hash_bytes(ap_hash_t *st, const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  uint64_t h = st->h;
  for (size_t i = 0u; i < len; i++) {
    h ^= (uint64_t)p[i];
    h *= AP_HASH_PRIME;
  }
  st->h = h;
}

/* One byte, no tag: the primitive the rest are built from. */
static void absorb(ap_hash_t *st, uint8_t b) {
  st->h ^= (uint64_t)b;
  st->h *= AP_HASH_PRIME;
}

/* Little-endian by construction, not by memcpy of the object representation --
 * that is what makes the hash identical on a big-endian host. */
static void absorb_le(ap_hash_t *st, uint64_t v, unsigned bytes) {
  for (unsigned i = 0u; i < bytes; i++) {
    absorb(st, (uint8_t)((v >> (8u * i)) & 0xFFu));
  }
}

void ap_hash_u8(ap_hash_t *st, uint8_t v) {
  absorb(st, (uint8_t)AP_HASH_TAG_U8);
  absorb(st, v);
}

void ap_hash_u16(ap_hash_t *st, uint16_t v) {
  absorb(st, (uint8_t)AP_HASH_TAG_U16);
  absorb_le(st, (uint64_t)v, 2u);
}

void ap_hash_u32(ap_hash_t *st, uint32_t v) {
  absorb(st, (uint8_t)AP_HASH_TAG_U32);
  absorb_le(st, (uint64_t)v, 4u);
}

void ap_hash_u64(ap_hash_t *st, uint64_t v) {
  absorb(st, (uint8_t)AP_HASH_TAG_U64);
  absorb_le(st, v, 8u);
}

void ap_hash_time(ap_hash_t *st, ap_time_t t) {
  absorb(st, (uint8_t)AP_HASH_TAG_TIME);
  absorb_le(st, (uint64_t)t, 8u);
}

uint64_t ap_hash_end(const ap_hash_t *st) {
  return st->h;
}

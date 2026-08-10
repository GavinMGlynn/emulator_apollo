#include "state/ap_hash.h"

#include <stdio.h>

/* The dump half. Null `out` is the ordinary case and costs one branch. */
void ap_hash_dump_to(ap_hash_t *st, void *out) { st->out = out; }

void ap_hash_scope(ap_hash_t *st, const char *scope) {
  st->scope = scope;
  st->index = 0u;
}

bool ap_hash_dumping(const ap_hash_t *st) { return st->out != NULL; }

/* One line per field, in traversal order: `scope.index = value` with the width
 * tag beside it. The tag is printed because a field whose *type* changed is a
 * different field to the hash, so a diff that hid the tag would show two
 * machines agreeing when the hash says otherwise. */
void ap_hash_group_begin(ap_hash_t *st, const char *name) {
  st->group = name;
}

void ap_hash_group_end(ap_hash_t *st) {
  const char *name = st->group;
  st->group = NULL;
  if (st->out != NULL && name != NULL) {
    fprintf((FILE *)st->out, "%s.%s grp  %016llX\n",
            st->scope != NULL ? st->scope : "", name,
            (unsigned long long)st->h);
  }
}

static void emit(ap_hash_t *st, const char *type, uint64_t v) {
  if (st->out == NULL || st->group != NULL) {
    return;
  }
  fprintf((FILE *)st->out, "%s.%03u %-4s %016llX\n",
          st->scope != NULL ? st->scope : "", st->index, type,
          (unsigned long long)v);
  st->index++;
}

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
  /* **After absorbing, and the running hash rather than the length.** A blob is
   * main memory or a frame buffer -- printing it would bury the dump in
   * megabytes, and printing only its length would let two machines whose RAM
   * differs dump identically, which is the exact failure a full-state diff
   * exists to catch. The running hash covers both extent and contents, so the
   * line differs whenever the blob does; locating *where* inside it is then a
   * separate question with its own instrument (`--dump-mem`). */
  emit(st, "blob", h);
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
  emit(st, "u8", (uint64_t)v);
  absorb(st, (uint8_t)AP_HASH_TAG_U8);
  absorb(st, v);
}

void ap_hash_u16(ap_hash_t *st, uint16_t v) {
  emit(st, "u16", (uint64_t)v);
  absorb(st, (uint8_t)AP_HASH_TAG_U16);
  absorb_le(st, (uint64_t)v, 2u);
}

void ap_hash_u32(ap_hash_t *st, uint32_t v) {
  emit(st, "u32", (uint64_t)v);
  absorb(st, (uint8_t)AP_HASH_TAG_U32);
  absorb_le(st, (uint64_t)v, 4u);
}

void ap_hash_u64(ap_hash_t *st, uint64_t v) {
  emit(st, "u64", (uint64_t)v);
  absorb(st, (uint8_t)AP_HASH_TAG_U64);
  absorb_le(st, v, 8u);
}

void ap_hash_time(ap_hash_t *st, ap_time_t t) {
  emit(st, "time", (uint64_t)t);
  absorb(st, (uint8_t)AP_HASH_TAG_TIME);
  absorb_le(st, (uint64_t)t, 8u);
}

uint64_t ap_hash_end(const ap_hash_t *st) {
  return st->h;
}

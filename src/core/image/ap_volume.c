/* The Domain physical volume label. See ap_volume.h for what was measured. */

#include "image/ap_volume.h"

static uint32_t be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

uint32_t ap_uid_node_id(ap_uid_t uid) {
  /* Twenty bits, which is also what the node ID PROM holds. The eight bits
   * above them are zero on every volume in hand, and are masked off rather than
   * assumed zero: a caller reading a UID from somewhere else should still get
   * the node and not the node plus whatever sits over it. */
  return uid.low & 0x000FFFFFu;
}

bool ap_volume_read_label(const uint8_t *blocks, size_t bytes,
                          ap_volume_label_t *out) {
  if (blocks == nullptr || out == nullptr || bytes < AP_VOLUME_LABEL_BYTES) {
    return false;
  }
  if (be32(&blocks[AP_VOLUME_MAGIC_OFFSET]) != AP_VOLUME_MAGIC) {
    return false;
  }

  *out = (ap_volume_label_t){0};

  /* Space-padded, and trimmed from the right. Trimming rather than copying the
   * padding because the name is compared and printed, and a trailing run of
   * spaces makes both of those wrong in ways nobody looks for. */
  unsigned length = AP_VOLUME_NAME_BYTES;
  while (length > 0u &&
         blocks[AP_VOLUME_NAME_OFFSET + length - 1u] == (uint8_t)' ') {
    length--;
  }
  for (unsigned i = 0; i < length; i++) {
    out->name[i] = (char)blocks[AP_VOLUME_NAME_OFFSET + i];
  }
  out->name[length] = '\0';

  out->creator.high = be32(&blocks[AP_VOLUME_CREATOR_UID_OFFSET]);
  out->creator.low = be32(&blocks[AP_VOLUME_CREATOR_UID_OFFSET + 4u]);
  out->node_id = ap_uid_node_id(out->creator);
  return true;
}

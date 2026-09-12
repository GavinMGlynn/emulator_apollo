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

size_t ap_volume_find_label(const uint8_t *blocks, size_t bytes,
                            uint32_t uid_high) {
  if (blocks == nullptr) {
    return SIZE_MAX;
  }
  for (unsigned block = 0; block < AP_VOLUME_LABEL_SEARCH_BLOCKS; block++) {
    const size_t at = (size_t)block * AP_VOLUME_BLOCK_BYTES;
    if (at + AP_VOLUME_BLOCK_BYTES > bytes) {
      return SIZE_MAX;
    }
    /* The header's first longword is the high half of the object's UID, and the
     * canned label UIDs have a zero low half -- `002398-04` p. 2-16. Both
     * halves are checked, because `00000200` alone would also match a file
     * whose UID happened to begin that way. */
    if (be32(&blocks[at]) == uid_high && be32(&blocks[at + 4u]) == 0u) {
      return at + AP_VOLUME_BLOCK_HEADER_BYTES;
    }
  }
  return SIZE_MAX;
}

bool ap_volume_read_label(const uint8_t *blocks, size_t bytes,
                          ap_volume_label_t *out) {
  if (blocks == nullptr || out == nullptr) {
    return false;
  }

  const size_t pv = ap_volume_find_label(blocks, bytes,
                                         AP_VOLUME_PV_LABEL_UID_HIGH);
  const size_t lv = ap_volume_find_label(blocks, bytes,
                                         AP_VOLUME_LV_LABEL_UID_HIGH);
  if (pv == SIZE_MAX || lv == SIZE_MAX) {
    return false;
  }

  /* "A block whose label does not begin `APOLLO` is not a Domain physical
   * volume", which is the check this file makes instead of the `0x418` value it
   * used to gate on -- that value is absent from both DS5500 volumes. */
  static const char signature[AP_VOLUME_APOLLO_BYTES] = {'A', 'P', 'O',
                                                         'L', 'L', 'O'};
  for (unsigned i = 0; i < AP_VOLUME_APOLLO_BYTES; i++) {
    if (blocks[pv + AP_VOLUME_APOLLO_OFFSET + i] != (uint8_t)signature[i]) {
      return false;
    }
  }

  *out = (ap_volume_label_t){0};

  /* Space-padded, and trimmed from the right. Trimming rather than copying the
   * padding because the name is compared and printed, and a trailing run of
   * spaces makes both of those wrong in ways nobody looks for. */
  unsigned length = AP_VOLUME_NAME_BYTES;
  while (length > 0u) {
    /* **Both padding characters, not just the space.** p. 2-20's `.name` is a
     * 32-byte field and the reference image fills it as six characters, then
     * spaces, then a **NUL** in the last byte -- so a trim that stopped at the
     * first non-space stopped on that NUL and kept every space in front of it.
     * The volume is called `DN3500`, not `DN3500` and twenty-five blanks. */
    const uint8_t last = blocks[pv + AP_VOLUME_NAME_OFFSET + length - 1u];
    if (last != (uint8_t)' ' && last != 0u) {
      break;
    }
    length--;
  }
  for (unsigned i = 0; i < length; i++) {
    out->name[i] = (char)blocks[pv + AP_VOLUME_NAME_OFFSET + i];
  }
  out->name[length] = '\0';

  out->creator.high = be32(&blocks[pv + AP_VOLUME_CREATOR_UID_OFFSET]);
  out->creator.low = be32(&blocks[pv + AP_VOLUME_CREATOR_UID_OFFSET + 4u]);
  out->node_id = ap_uid_node_id(out->creator);

  out->label_write_time = be32(&blocks[lv + AP_VOLUME_LABEL_WRITE_TIME_OFFSET]);
  out->last_mounted_node =
      be32(&blocks[lv + AP_VOLUME_LAST_MOUNTED_NODE_OFFSET]);
  out->node_boot_time = be32(&blocks[lv + AP_VOLUME_NODE_BOOT_TIME_OFFSET]);
  out->mounted_time = be32(&blocks[lv + AP_VOLUME_MOUNTED_TIME_OFFSET]);
  out->dismounted_time = be32(&blocks[lv + AP_VOLUME_DISMOUNTED_TIME_OFFSET]);
  out->salvage_node = be32(&blocks[lv + AP_VOLUME_SALVAGE_NODE_OFFSET]);
  out->salvage_time = be32(&blocks[lv + AP_VOLUME_SALVAGE_TIME_OFFSET]);
  out->shut_state = (uint16_t)((blocks[lv + AP_VOLUME_SHUT_STATE_OFFSET] << 8) |
                               blocks[lv + AP_VOLUME_SHUT_STATE_OFFSET + 1u]);
  return true;
}

uint64_t ap_volume_time_microseconds(uint32_t ticks) {
  /* Widened before multiplying, not after: `ticks` reaches `FFFFFFFF` on a real
   * volume and `262144` is 2^18, so a 32-bit product would overflow for every
   * date after 1980 and the two most recent volumes in hand would both wrap. */
  return (uint64_t)ticks * (uint64_t)AP_VOLUME_TIME_TICK_MICROSECONDS;
}

bool ap_volume_cleanly_dismounted(const ap_volume_label_t *label) {
  /* Zero and nothing else. Not "older than the mount time", not "before some
   * threshold": the field is either a time a dismount wrote or it is untouched,
   * and Domain/OS's fourteen-day check measures from it either way. */
  return label != nullptr && label->dismounted_time != 0u;
}

#include "image/ap_tap.h"

#include <string.h>

#include "state/ap_hash.h"

static uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static void put_le32(uint8_t *p, uint32_t value) {
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
  p[2] = (uint8_t)(value >> 16);
  p[3] = (uint8_t)(value >> 24);
}

/* A record's data is padded to an even byte count; the trailing length is not
 * part of that padding. */
static size_t padded(uint32_t length) {
  return (size_t)length + (length & 1u);
}

bool ap_tap_load(ap_exb8200_media_t *media, const uint8_t *image, size_t size) {
  if (media == nullptr || image == nullptr || media->record == nullptr ||
      media->data == nullptr) {
    return false;
  }
  media->records = 0u;
  media->data_used = 0u;

  size_t at = 0u;
  while (at + 4u <= size) {
    const uint32_t header = le32(&image[at]);
    at += 4u;
    if (header == AP_TAP_EOM) {
      /* End of medium closes the image; anything after it is not tape. */
      break;
    }
    if (header == AP_TAP_ERASE_GAP) {
      /* An erase gap is not a record and carries no trailing length. This
       * drive has no way to represent one, so it is skipped -- see the
       * header. */
      continue;
    }
    if (header == AP_TAP_MARK) {
      if (media->records >= media->capacity) {
        return false;
      }
      ap_exb8200_record_t *record = &media->record[media->records++];
      record->kind = AP_EXB8200_RECORD_FILEMARK;
      record->offset = 0u;
      record->length = 0u;
      /* The format has one filemark and this drive has two. A tape mark
       * becomes a **long** one: it is what a clear byte 05 bit 7 writes, and
       * it is the only kind a later write may append into. */
      record->short_mark = false;
      continue;
    }

    const uint32_t length = header & AP_TAP_LENGTH_MASK;
    const size_t body = padded(length);
    /* The trailing length must be there and must agree -- the one structural
     * check the format admits, and what makes it readable backwards. */
    if (at + body + 4u > size) {
      return false;
    }
    if (le32(&image[at + body]) != header) {
      return false;
    }
    if (media->records >= media->capacity ||
        (size_t)media->data_used + length > media->data_bytes) {
      return false;
    }
    ap_exb8200_record_t *record = &media->record[media->records++];
    record->kind = AP_EXB8200_RECORD_DATA;
    record->offset = media->data_used;
    record->length = length;
    record->short_mark = false;
    memcpy(&media->data[media->data_used], &image[at], length);
    media->data_used += length;
    at += body + 4u;
  }
  return true;
}

size_t ap_tap_size(const ap_exb8200_media_t *media) {
  if (media == nullptr || media->record == nullptr) {
    return 0u;
  }
  size_t size = 0u;
  for (unsigned i = 0; i < media->records; i++) {
    const ap_exb8200_record_t *record = &media->record[i];
    if (record->kind == AP_EXB8200_RECORD_FILEMARK) {
      size += 4u;
      continue;
    }
    size += 4u + padded(record->length) + 4u;
  }
  return size + 4u; /* the closing end-of-medium marker */
}

size_t ap_tap_save(const ap_exb8200_media_t *media, uint8_t *image,
                   size_t size) {
  if (media == nullptr || image == nullptr) {
    return 0u;
  }
  const size_t wanted = ap_tap_size(media);
  if (wanted == 0u || size < wanted) {
    return 0u;
  }
  size_t at = 0u;
  for (unsigned i = 0; i < media->records; i++) {
    const ap_exb8200_record_t *record = &media->record[i];
    if (record->kind == AP_EXB8200_RECORD_FILEMARK) {
      /* Both kinds write the same tape mark; the short/long distinction does
       * not survive the container, which the header states. */
      put_le32(&image[at], AP_TAP_MARK);
      at += 4u;
      continue;
    }
    put_le32(&image[at], record->length);
    at += 4u;
    memcpy(&image[at], &media->data[record->offset], record->length);
    at += record->length;
    if ((record->length & 1u) != 0u) {
      image[at++] = 0u; /* pad to an even count */
    }
    put_le32(&image[at], record->length);
    at += 4u;
  }
  put_le32(&image[at], AP_TAP_EOM);
  at += 4u;
  return at;
}

uint64_t ap_tap_digest_of(const uint8_t *image, size_t size) {
  /* One pass, by the state hash, for `ap_ct_digest_of`'s reason: a medium's
   * identity is computed the same way every other piece of state is. */
  ap_hash_t st = ap_hash_begin();
  ap_hash_bytes(&st, image, size);
  return ap_hash_end(&st);
}

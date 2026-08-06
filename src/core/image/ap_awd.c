/* An Apollo Winchester image. See ap_awd.h for the format and its sources. */

#include "image/ap_awd.h"

#include <string.h>

ap_awd_geometry_t ap_awd_geometry_for(ap_awd_drive_t drive) {
  switch (drive) {
  case AP_AWD_DRIVE_155MB:
    return (ap_awd_geometry_t){.cylinders = 1023u, .heads = 8u, .sectors = 18u};
  case AP_AWD_DRIVE_348MB:
    break;
  }
  return (ap_awd_geometry_t){.cylinders = 1223u, .heads = 15u, .sectors = 18u};
}

uint32_t ap_awd_sector_count(ap_awd_geometry_t geometry) {
  return (uint32_t)geometry.cylinders * geometry.heads * geometry.sectors;
}

bool ap_awd_open(ap_awd_t *image, uint8_t *data, size_t bytes,
                 ap_awd_geometry_t geometry, bool writable) {
  if (image == nullptr || data == nullptr) {
    return false;
  }
  /* A geometry with a zero in it describes no drive, and would make every
   * address arithmetic below divide the space into nothing. */
  if (geometry.cylinders == 0u || geometry.heads == 0u ||
      geometry.sectors == 0u) {
    return false;
  }
  /* At least one sector, or there is nothing to address. Short of the
   * geometry's full extent is allowed and normal -- see the header. */
  if (bytes < AP_AWD_SECTOR_BYTES) {
    return false;
  }
  image->data = data;
  image->bytes = bytes;
  image->geometry = geometry;
  image->writable = writable;
  /* No sidecar until one is attached. Cleared here rather than left to the
   * caller's zeroing, because `ap_awd_t` is routinely a stack local and a
   * stale pointer here would be read as a defect list. */
  image->meta = NULL;
  image->meta_bytes = 0u;
  image->meta_records = 0u;
  image->meta_record_bytes = 0u;
  return true;
}

bool ap_awd_lba(ap_awd_geometry_t geometry, uint16_t cylinder, uint8_t head,
                uint8_t sector, uint32_t *lba) {
  if (cylinder >= geometry.cylinders || head >= geometry.heads) {
    return false;
  }
  const uint32_t address =
      ((uint32_t)cylinder * geometry.heads + head) * geometry.sectors + sector;
  if (address >= ap_awd_sector_count(geometry)) {
    return false;
  }
  *lba = address;
  return true;
}

/* Where a sector begins, and whether the image reaches it. Separate from the
 * two accessors because both need exactly this question answered and a
 * difference between them would be a read and a write disagreeing about where
 * the disk ends. */
static bool span(const ap_awd_t *image, uint32_t lba, size_t *offset) {
  if (lba >= ap_awd_sector_count(image->geometry)) {
    return false;
  }
  const size_t at = (size_t)lba * AP_AWD_SECTOR_BYTES;
  if (at > image->bytes || image->bytes - at < AP_AWD_SECTOR_BYTES) {
    /* Inside the drive and past the end of the file: these images are shorter
     * than the geometry, and a short read here would hand back whatever the
     * caller's buffer held. */
    return false;
  }
  *offset = at;
  return true;
}

bool ap_awd_read(const ap_awd_t *image, uint32_t lba, uint8_t *out) {
  size_t offset = 0;
  if (out == nullptr || !span(image, lba, &offset)) {
    return false;
  }
  memcpy(out, &image->data[offset], AP_AWD_SECTOR_BYTES);
  return true;
}

bool ap_awd_write(ap_awd_t *image, uint32_t lba, const uint8_t *in) {
  size_t offset = 0;
  if (in == nullptr || !image->writable || !span(image, lba, &offset)) {
    return false;
  }
  memcpy(&image->data[offset], in, AP_AWD_SECTOR_BYTES);
  return true;
}

/* ## The sidecar
 *
 * `AWD_META.md` has the layout and the argument for it being a separate file.
 * Both lengths live in the header so a later version can grow a record without
 * a second magic, and a reader takes the fields it knows. */

static uint32_t meta_le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

/* Where a sector's record starts, or NULL if the sidecar does not cover it. */
static uint8_t *meta_record(const ap_awd_t *image, uint32_t lba) {
  if (image->meta == NULL || lba >= image->meta_records) {
    return NULL;
  }
  return image->meta + AP_AWD_META_HEADER_BYTES +
         (size_t)lba * image->meta_record_bytes;
}

bool ap_awd_attach_meta(ap_awd_t *image, uint8_t *meta, size_t bytes) {
  if (image == NULL || meta == NULL || bytes < AP_AWD_META_HEADER_BYTES) {
    return false;
  }
  if (memcmp(meta, AP_AWD_META_MAGIC, AP_AWD_META_MAGIC_BYTES) != 0) {
    return false;
  }
  const uint32_t header = meta_le32(meta + 8);
  const uint32_t record = meta_le32(meta + 12);
  /* A header shorter than this core's would put the first record inside the
   * fields it is still reading; a record too short to hold the flags byte
   * describes nothing. Both are malformed rather than merely older. */
  if (header < AP_AWD_META_HEADER_BYTES || record < 1u) {
    return false;
  }
  if (bytes < header) {
    return false;
  }
  image->meta = meta;
  image->meta_bytes = bytes;
  image->meta_record_bytes = (unsigned)record;
  /* A short file is not an error: it describes the sectors it covers and no
   * more, which is the rule `ap_awd_read` already applies to a short image. */
  image->meta_records = (uint64_t)((bytes - header) / record);
  return true;
}

uint8_t ap_awd_flags(const ap_awd_t *image, uint32_t lba) {
  const uint8_t *record = meta_record(image, lba);
  return record == NULL ? 0u : record[0];
}

bool ap_awd_set_flags(ap_awd_t *image, uint32_t lba, uint8_t flags) {
  uint8_t *record = meta_record(image, lba);
  if (record == NULL || !image->writable) {
    return false;
  }
  record[0] = flags;
  return true;
}

void ap_awd_ecc(const ap_awd_t *image, uint32_t lba, uint8_t *out) {
  const uint8_t *record = meta_record(image, lba);
  /* Zeros with no sidecar: "none recorded". `[OMTI]` publishes no polynomial,
   * so a computed value here would be indistinguishable from a real one. */
  if (record == NULL || image->meta_record_bytes < 1u + AP_AWD_ECC_BYTES) {
    memset(out, 0, AP_AWD_ECC_BYTES);
    return;
  }
  memcpy(out, record + 1, AP_AWD_ECC_BYTES);
}

bool ap_awd_set_ecc(ap_awd_t *image, uint32_t lba, const uint8_t *ecc) {
  uint8_t *record = meta_record(image, lba);
  if (record == NULL || !image->writable ||
      image->meta_record_bytes < 1u + AP_AWD_ECC_BYTES) {
    return false;
  }
  memcpy(record + 1, ecc, AP_AWD_ECC_BYTES);
  return true;
}

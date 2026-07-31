#include "image/ap_ct.h"

#include <string.h>

bool ap_ct_open(ap_ct_t *ct, const uint8_t *data, size_t size) {
  memset(ct, 0, sizeof *ct);
  if (data == NULL || size == 0u) {
    return false;
  }
  /* C24: the measured image is a whole number of blocks with no remainder, so a
   * remainder means the image is not what it claims to be. Refusing here rather
   * than rounding is the same rule the clock domains follow. */
  if ((size % AP_CT_BLOCK_SIZE) != 0u) {
    return false;
  }
  ct->data = data;
  ct->size = size;
  ct->blocks = (uint64_t)(size / AP_CT_BLOCK_SIZE);
  return true;
}

uint64_t ap_ct_blocks(const ap_ct_t *ct) { return ct->blocks; }

bool ap_ct_read_block(const ap_ct_t *ct, uint64_t index, uint8_t *out) {
  if (ct->data == NULL || index >= ct->blocks) {
    return false;
  }
  memcpy(out, ct->data + (size_t)(index * AP_CT_BLOCK_SIZE),
         AP_CT_BLOCK_SIZE);
  return true;
}

/* Big-endian, as the image is: this is 68000 media and the words are stored the
 * way the processor reads them. */
static uint32_t be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* Whether `text` appears at `offset` in the block. Compared as a fixed span
 * rather than as a C string, because the identification is padded with NULs and
 * spaces -- "SYSBOOT REV \0\0\0\0 M68K    " -- and a string compare would stop
 * at the first NUL and miss the processor field entirely. */
static bool has_text(const uint8_t *block, unsigned offset, const char *text) {
  size_t n = strlen(text);
  if (offset + n > AP_CT_BLOCK_SIZE) {
    return false;
  }
  return memcmp(block + offset, text, n) == 0;
}

bool ap_ct_boot_record(const ap_ct_t *ct, ap_ct_boot_t *out) {
  uint8_t block[AP_CT_BLOCK_SIZE];
  if (!ap_ct_read_block(ct, 0u, block)) {
    return false;
  }
  memset(out, 0, sizeof *out);
  for (unsigned i = 0; i < 4u; i++) {
    out->word[i] = be32(block + i * 4u);
  }
  out->bootable = has_text(block, AP_CT_ID_OFFSET, AP_CT_ID_SYSBOOT);
  /* The processor field sits a little after the identification, and the
   * measured image has a space before it, so the search is anchored on the
   * documented offset rather than scanned for. */
  out->m68k = has_text(block, AP_CT_PROCESSOR_OFFSET + 1u,
                       AP_CT_PROCESSOR_M68K);
  return true;
}

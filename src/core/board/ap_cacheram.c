#include "board/ap_cacheram.h"

#include <string.h>

/* §1.3.1 states "8-KB" and "2048 4-byte ... entries" independently. If they
 * ever stop agreeing here, one of them was transcribed wrong. */
static_assert(AP_CACHERAM_DATA_BYTES == 8192u,
              "8-KB, direct-mapped -- 008778-03 SS1.3.1");
static_assert(AP_CACHERAM_DATA_LIMIT - AP_CACHERAM_DATA_BASE + 1u ==
                  AP_CACHERAM_DATA_BYTES,
              "Table 2-8 CACHE RAM 012000-013FFF is the 8 KB of entries");
static_assert(AP_CACHERAM_CC_LIMIT - AP_CACHERAM_CC_BASE + 1u ==
                  AP_CACHERAM_CC_BYTES,
              "Table 2-8 CACHE CONDITION CODE RAM 014000-015FFF");

void ap_cacheram_init(ap_cacheram_t *cache, bool present) {
  memset(cache, 0, sizeof *cache);
  cache->entries = present ? AP_CACHERAM_ENTRIES : 0u;
}

unsigned ap_cacheram_index(uint32_t virtual_address) {
  return (unsigned)((virtual_address >> AP_CACHERAM_INDEX_SHIFT) &
                    AP_CACHERAM_INDEX_MASK);
}

uint32_t ap_cacheram_tag(uint32_t virtual_address) {
  return virtual_address >> AP_CACHERAM_TAG_SHIFT;
}

uint8_t ap_cacheram_read_data(const ap_cacheram_t *cache, uint32_t address) {
  if (address < AP_CACHERAM_DATA_BASE || address > AP_CACHERAM_DATA_LIMIT) {
    return 0xFFu;
  }
  return cache->data[address - AP_CACHERAM_DATA_BASE];
}

void ap_cacheram_write_data(ap_cacheram_t *cache, uint32_t address,
                            uint8_t value) {
  if (address < AP_CACHERAM_DATA_BASE || address > AP_CACHERAM_DATA_LIMIT) {
    return;
  }
  cache->data[address - AP_CACHERAM_DATA_BASE] = value;
}

uint8_t ap_cacheram_read_cc(const ap_cacheram_t *cache, uint32_t address) {
  if (address < AP_CACHERAM_CC_BASE || address > AP_CACHERAM_CC_LIMIT) {
    return 0xFFu;
  }
  return cache->cc[address - AP_CACHERAM_CC_BASE];
}

void ap_cacheram_write_cc(ap_cacheram_t *cache, uint32_t address,
                          uint8_t value) {
  if (address < AP_CACHERAM_CC_BASE || address > AP_CACHERAM_CC_LIMIT) {
    return;
  }
  cache->cc[address - AP_CACHERAM_CC_BASE] = value;
}

uint32_t ap_cacheram_cc_word(const ap_cacheram_t *cache, unsigned index) {
  if (index >= AP_CACHERAM_ENTRIES) {
    return 0u;
  }
  const uint8_t *b = &cache->cc[index * 4u];
  return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
         ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

void ap_cacheram_set_cc_word(ap_cacheram_t *cache, unsigned index,
                             uint32_t word) {
  if (index >= AP_CACHERAM_ENTRIES) {
    return;
  }
  uint8_t *b = &cache->cc[index * 4u];
  b[0] = (uint8_t)(word >> 24);
  b[1] = (uint8_t)(word >> 16);
  b[2] = (uint8_t)(word >> 8);
  b[3] = (uint8_t)word;
}

bool ap_cacheram_lookup(const ap_cacheram_t *cache, uint32_t virtual_address,
                        uint32_t *value) {
  if (!ap_cacheram_present(cache)) {
    return false;
  }
  const unsigned index = ap_cacheram_index(virtual_address);
  const uint32_t cc = ap_cacheram_cc_word(cache, index);
  if ((cc & AP_CACHERAM_CC_VALID) == 0u) {
    return false;
  }
  if ((cc >> AP_CACHERAM_CC_TAG_SHIFT) != ap_cacheram_tag(virtual_address)) {
    return false;
  }
  const uint8_t *b = &cache->data[index * AP_CACHERAM_ENTRY_BYTES];
  *value = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8) | (uint32_t)b[3];
  return true;
}

void ap_cacheram_fill(ap_cacheram_t *cache, uint32_t virtual_address,
                      uint32_t value) {
  if (!ap_cacheram_present(cache)) {
    return;
  }
  const unsigned index = ap_cacheram_index(virtual_address);
  uint8_t *b = &cache->data[index * AP_CACHERAM_ENTRY_BYTES];
  b[0] = (uint8_t)(value >> 24);
  b[1] = (uint8_t)(value >> 16);
  b[2] = (uint8_t)(value >> 8);
  b[3] = (uint8_t)value;
  ap_cacheram_set_cc_word(cache, index,
                          (ap_cacheram_tag(virtual_address)
                           << AP_CACHERAM_CC_TAG_SHIFT) |
                              AP_CACHERAM_CC_VALID);
}

void ap_cacheram_invalidate(ap_cacheram_t *cache) {
  memset(cache->cc, 0, sizeof cache->cc);
}

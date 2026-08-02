/* MC68040 address translation caches. See the header, including why the tag is
 * sixteen bits where the manual's field definition says thirteen. */

#include <string.h>

#include "cpu/m68040/ap_m68040_atc.h"

void ap_m68040_atc_init(ap_m68040_atc_t *atc) { memset(atc, 0, sizeof *atc); }

/* The page number: what remains of a logical address above the page offset. */
static uint32_t page_number(uint32_t logical_address,
                            ap_m68040_page_size_t page_size) {
  return logical_address >> ((page_size == AP_M68040_PAGE_8K) ? 13u : 12u);
}

unsigned ap_m68040_atc_set(uint32_t logical_address,
                           ap_m68040_page_size_t page_size) {
  /* Sixteen sets, so the low four bits of the page number. */
  return (unsigned)(page_number(logical_address, page_size) & 0xFu);
}

uint32_t ap_m68040_atc_tag(uint32_t logical_address,
                           ap_m68040_page_size_t page_size) {
  return page_number(logical_address, page_size) >> 4;
}

unsigned ap_m68040_atc_lookup(const ap_m68040_atc_t *atc,
                              uint32_t logical_address, bool supervisor,
                              ap_m68040_page_size_t page_size) {
  const unsigned set = ap_m68040_atc_set(logical_address, page_size);
  const uint32_t tag = ap_m68040_atc_tag(logical_address, page_size);
  for (unsigned way = 0; way < AP_M68040_ATC_WAYS; way++) {
    const ap_m68040_atc_entry_t *e = &atc->entry[set][way];
    /* "FC2 is set for supervisor mode accesses and cleared for user mode
     * accesses", and it is the *only* function code bit in the tag -- the
     * separate instruction and data ATCs make the rest unnecessary. */
    if (e->valid && e->logical_tag == tag &&
        e->supervisor_space == supervisor) {
      return way;
    }
  }
  return AP_M68040_ATC_WAYS;
}

unsigned ap_m68040_atc_select_way(const ap_m68040_atc_t *atc,
                                  uint32_t logical_address,
                                  ap_m68040_page_size_t page_size) {
  const unsigned set = ap_m68040_atc_set(logical_address, page_size);
  for (unsigned way = 0; way < AP_M68040_ATC_WAYS; way++) {
    if (!atc->entry[set][way].valid) {
      return way;
    }
  }
  return atc->counter & 0x3u;
}

void ap_m68040_atc_tick(ap_m68040_atc_t *atc) {
  atc->counter = (atc->counter + 1u) & 0x3u;
}

void ap_m68040_atc_fill(ap_m68040_atc_t *atc, unsigned way,
                        uint32_t logical_address,
                        ap_m68040_page_size_t page_size,
                        ap_m68040_atc_entry_t entry) {
  entry.valid = true;
  entry.logical_tag = ap_m68040_atc_tag(logical_address, page_size);
  atc->entry[ap_m68040_atc_set(logical_address, page_size)][way & 0x3u] = entry;
}

void ap_m68040_atc_flush_all(ap_m68040_atc_t *atc) {
  for (unsigned set = 0; set < AP_M68040_ATC_SETS; set++) {
    for (unsigned way = 0; way < AP_M68040_ATC_WAYS; way++) {
      atc->entry[set][way].valid = false;
    }
  }
}

void ap_m68040_atc_flush_nonglobal(ap_m68040_atc_t *atc, bool supervisor) {
  for (unsigned set = 0; set < AP_M68040_ATC_SETS; set++) {
    for (unsigned way = 0; way < AP_M68040_ATC_WAYS; way++) {
      ap_m68040_atc_entry_t *e = &atc->entry[set][way];
      if (!e->valid || e->supervisor_space != supervisor) {
        continue;
      }
      /* "Global entries are not invalidated by the PFLUSH instruction variants
       * that specify nonglobal entries, **even when all other selection
       * criteria are satisfied**." The emphasis is the manual's point: `G`
       * overrides the match rather than being one more criterion. */
      if (e->global) {
        continue;
      }
      e->valid = false;
    }
  }
}

void ap_m68040_atc_flush_page(ap_m68040_atc_t *atc, uint32_t logical_address,
                              bool supervisor,
                              ap_m68040_page_size_t page_size) {
  const unsigned way =
      ap_m68040_atc_lookup(atc, logical_address, supervisor, page_size);
  if (way < AP_M68040_ATC_WAYS) {
    atc->entry[ap_m68040_atc_set(logical_address, page_size)][way].valid =
        false;
  }
}

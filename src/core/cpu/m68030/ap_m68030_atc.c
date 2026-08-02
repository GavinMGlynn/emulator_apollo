/* MC68030 address translation cache. See ap_m68030_atc.h for the citations and
 * for the PROVISIONAL note on the replacement algorithm. */

#include "cpu/m68030/ap_m68030_atc.h"

#include <stddef.h> /* NULL */

/* Logical and physical address fields hold A31-A8. */
#define ATC_ADDRESS_SHIFT AP_M68030_ATC_ADDRESS_SHIFT
#define ATC_ADDRESS_MASK UINT32_C(0x00FFFFFF)

/* "All 24 bits of this field are used in the comparison of this entry to an
 * incoming logical address when the page size is 256 bytes. For larger page
 * sizes, the appropriate number of least significant bits of this field are
 * ignored." The number ignored is the page size in bits less the eight the
 * field is already shifted by. */
static uint32_t tag_mask(uint8_t page_size_bits) {
  if (page_size_bits <= ATC_ADDRESS_SHIFT) {
    return ATC_ADDRESS_MASK;
  }
  const unsigned ignored = page_size_bits - ATC_ADDRESS_SHIFT;
  if (ignored >= 24u) {
    return 0;
  }
  return ATC_ADDRESS_MASK & ~((UINT32_C(1) << ignored) - 1u);
}

static uint32_t tag_of(uint32_t address) {
  return (address >> ATC_ADDRESS_SHIFT) & ATC_ADDRESS_MASK;
}

static bool entry_matches(const ap_m68030_atc_entry_t *e, uint8_t function_code,
                          uint32_t address, uint32_t mask) {
  return e->valid && (e->function_code & 0x07u) == (function_code & 0x07u) &&
         ((e->logical ^ tag_of(address)) & mask) == 0;
}

void ap_m68030_atc_flush(ap_m68030_atc_t *atc) {
  for (unsigned i = 0; i < AP_M68030_ATC_ENTRIES; i++) {
    atc->entry[i].valid = false;
    atc->entry[i].history = false;
  }
}

void ap_m68030_atc_flush_entry(ap_m68030_atc_t *atc, uint8_t function_code,
                               uint32_t address, uint8_t page_size_bits) {
  const uint32_t mask = tag_mask(page_size_bits);
  for (unsigned i = 0; i < AP_M68030_ATC_ENTRIES; i++) {
    if (entry_matches(&atc->entry[i], function_code, address, mask)) {
      atc->entry[i].valid = false;
    }
  }
}

ap_m68030_atc_result_t ap_m68030_atc_lookup(const ap_m68030_atc_t *atc,
                                            uint8_t function_code,
                                            uint32_t address,
                                            uint8_t page_size_bits, bool write,
                                            bool read_modify_write) {
  ap_m68030_atc_result_t result = {
      .status = AP_M68030_ATC_MISS, .physical = 0, .cache_inhibit = false,
      .index = -1};

  const uint32_t mask = tag_mask(page_size_bits);
  const ap_m68030_atc_entry_t *hit = NULL;
  for (unsigned i = 0; i < AP_M68030_ATC_ENTRIES; i++) {
    if (entry_matches(&atc->entry[i], function_code, address, mask)) {
      hit = &atc->entry[i];
      result.index = (int)i;
      break;
    }
  }
  if (hit == NULL) {
    return result;
  }

  result.cache_inhibit = hit->cache_inhibit;

  /* "All page index bits of the logical address are transferred to the bus
   * controller without translation" -- the page offset comes from the logical
   * address, the frame from the entry. The offset width follows the page size,
   * not the 256-byte granularity of the stored field. */
  const uint32_t offset_bits =
      (page_size_bits <= ATC_ADDRESS_SHIFT) ? ATC_ADDRESS_SHIFT : page_size_bits;
  const uint32_t offset_mask = (offset_bits >= 32u)
                                   ? UINT32_C(0xFFFFFFFF)
                                   : ((UINT32_C(1) << offset_bits) - 1u);
  const uint32_t frame = (hit->physical << ATC_ADDRESS_SHIFT) & ~offset_mask;
  result.physical = frame | (address & offset_mask);

  /* B first: it is set for "a bus error, an invalid descriptor, a supervisor
   * violation, or a limit violation", and any access to such an entry faults,
   * read or write alike. */
  if (hit->bus_error) {
    result.status = AP_M68030_ATC_FAULT;
    return result;
  }

  const bool writing = write || read_modify_write;

  /* "When the WP bit is set, a write access or a read-modify-write access to
   * the logical address corresponding to this entry causes a bus error
   * exception to be taken immediately." A read through a write-protected entry
   * is perfectly legal. */
  if (writing && hit->write_protect) {
    result.status = AP_M68030_ATC_FAULT;
    return result;
  }

  /* "If the M bit is clear and a write access to this logical address is
   * attempted, the MC68030 aborts the access and initiates a table search,
   * setting the M bit in the page descriptor, invalidating the old ATC entry,
   * and creating a new entry with the M bit set."
   *
   * This is a hit that still costs a table search, which is why it is its own
   * status rather than folded into HIT. "This assures that the first write
   * operation to a page sets the M bit in both the ATC and the page descriptor
   * ... even when a previous read operation to the page had created an entry
   * for that page in the ATC with the M bit clear." */
  if (writing && !hit->modified) {
    result.status = AP_M68030_ATC_MODIFY;
    return result;
  }

  result.status = AP_M68030_ATC_HIT;
  return result;
}

/* Choose a victim.
 *
 * Documented and implemented exactly: "If possible, when the ATC stores a new
 * address translation, it replaces an entry that is no longer valid."
 *
 * Not documented, and therefore PROVISIONAL: which valid entry is chosen. The
 * manual names a "pseudo least recently used algorithm" built from "a validity
 * bit and an internal history bit" and stops there.
 *
 * The sibling manual has since closed most of it. `MC68851 PMMU User's Manual`
 * §5.2.1.3 states the algorithm outright for the compatible ATC: "locate an
 * invalid entry and use it. If no invalid entries are found, use a psuedo
 * least-recently-used (LRU) algorithm to select an entry without its L bit set
 * and replace that entry." So the **two-step order below is documented rather
 * than inferred** -- invalid first, history bit second -- and the same section
 * says the history bit indicates "that the entry has been recently used", which
 * is why `ap_m68030_atc_mark_used` exists and why the access path calls it on a
 * hit.
 *
 * The 68851's L bit has no counterpart here: that part can lock an entry
 * against replacement and the 68030's ATC cannot, so the exclusion drops out
 * rather than being modelled as always false.
 *
 * Still undocumented after all of that, and the whole of what remains: which
 * entry is chosen among those whose history bit is clear. Implemented as the first such
 * entry, and when every entry has been used since the last sweep, clear them
 * all and start again. A stated approximation rather than an invented
 * precision. */
static unsigned choose_victim(ap_m68030_atc_t *atc) {
  for (unsigned i = 0; i < AP_M68030_ATC_ENTRIES; i++) {
    if (!atc->entry[i].valid) {
      return i;
    }
  }
  for (unsigned i = 0; i < AP_M68030_ATC_ENTRIES; i++) {
    if (!atc->entry[i].history) {
      return i;
    }
  }
  for (unsigned i = 0; i < AP_M68030_ATC_ENTRIES; i++) {
    atc->entry[i].history = false;
  }
  return 0;
}

int ap_m68030_atc_insert(ap_m68030_atc_t *atc, uint8_t function_code,
                         uint32_t address, uint8_t page_size_bits,
                         uint32_t physical_page, bool write_protect,
                         bool cache_inhibit, bool modified, bool bus_error) {
  /* An existing entry for this address is replaced rather than duplicated: a
   * fully associative cache holding two entries for one tag could answer with
   * either, which would make translation depend on search order. */
  ap_m68030_atc_flush_entry(atc, function_code, address, page_size_bits);

  const unsigned victim = choose_victim(atc);
  atc->entry[victim] = (ap_m68030_atc_entry_t){
      .valid = true,
      .function_code = (uint8_t)(function_code & 0x07u),
      .logical = tag_of(address),
      .bus_error = bus_error,
      .cache_inhibit = cache_inhibit,
      .write_protect = write_protect,
      .modified = modified,
      .physical = physical_page & ATC_ADDRESS_MASK,
      .history = true,
  };
  return (int)victim;
}

void ap_m68030_atc_mark_used(ap_m68030_atc_t *atc, int index) {
  if (index < 0 || index >= (int)AP_M68030_ATC_ENTRIES) {
    return; /* a miss reports -1, and marking nothing is the right answer */
  }
  atc->entry[index].history = true;
}

void ap_m68030_atc_flush_function_codes(ap_m68030_atc_t *atc, uint8_t base,
                                        uint8_t mask) {
  const uint8_t effective = (uint8_t)(mask & 0x7u);
  for (unsigned i = 0; i < AP_M68030_ATC_ENTRIES; i++) {
    if (!atc->entry[i].valid) {
      continue;
    }
    if (((atc->entry[i].function_code ^ base) & effective) == 0u) {
      atc->entry[i].valid = false;
    }
  }
}

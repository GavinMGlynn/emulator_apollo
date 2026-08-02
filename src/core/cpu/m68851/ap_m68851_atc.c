/* MC68851 address translation cache. See ap_m68851_atc.h; §5.2 and Figures
 * 5-21 and 5-22, which draw named fields and no bit numbers because the ATC is
 * not programmer-visible. */

#include <stddef.h>

#include "cpu/m68851/ap_m68851_atc.h"

void ap_m68851_atc_flush(ap_m68851_atc_t *atc) {
  for (unsigned i = 0; i < AP_M68851_ATC_ENTRIES; i++) {
    atc->entry[i].valid = false;
    atc->entry[i].history = false;
    /* The lock goes too: an invalid entry is the replacement algorithm's first
     * choice, and a lock left behind would exempt a slot holding nothing. */
    atc->entry[i].lock = false;
  }
}

void ap_m68851_atc_flush_task(ap_m68851_atc_t *atc, unsigned task_alias) {
  for (unsigned i = 0; i < AP_M68851_ATC_ENTRIES; i++) {
    ap_m68851_atc_entry_t *e = &atc->entry[i];
    if (!e->valid) {
      continue;
    }
    /* A globally shared entry belongs to every task, so it survives one task's
     * flush -- which is the whole benefit of `SG`. */
    if (e->shared_globally || e->task_alias != task_alias) {
      continue;
    }
    e->valid = false;
    e->history = false;
    e->lock = false;
  }
}

bool ap_m68851_atc_entry_matches(const ap_m68851_atc_t *atc,
                                 const ap_m68851_atc_entry_t *e,
                                 uint32_t logical_address,
                                 unsigned function_code, uint32_t page_bytes) {
  if (!e->valid) {
    return false;
  }

  /* "The lower order bits of the logical address field are ignored during
   * compare operations if the page size is larger than 256 bytes." The page
   * size is the *current* one, so growing a page widens what an existing entry
   * covers -- which is why `TC` flushes the cache when it is written. */
  const uint32_t page_mask = ~(page_bytes - 1u);
  if ((e->logical_address & page_mask) != (logical_address & page_mask)) {
    return false;
  }

  /* "The FC field must match exactly" -- all of it, not just the
   * supervisor bit, because the DMA root pointer maps other function codes. */
  if (e->function_code != function_code) {
    return false;
  }

  /* "The task alias (TA) field must match the current TA value of the MC68851,
   * or the entry's SG bit must be set." */
  return e->shared_globally || e->task_alias == atc->task_alias;
}

const ap_m68851_atc_entry_t *
ap_m68851_atc_lookup(const ap_m68851_atc_t *atc, uint32_t logical_address,
                     unsigned function_code, uint32_t page_bytes) {
  /* Fully associative: every entry is compared, there is no index. */
  for (unsigned i = 0; i < AP_M68851_ATC_ENTRIES; i++) {
    if (ap_m68851_atc_entry_matches(atc, &atc->entry[i], logical_address,
                                    function_code, page_bytes)) {
      return &atc->entry[i];
    }
  }
  return NULL;
}

void ap_m68851_atc_touch(ap_m68851_atc_t *atc, unsigned index) {
  atc->entry[index].history = true;
}

unsigned ap_m68851_atc_locked_count(const ap_m68851_atc_t *atc) {
  unsigned count = 0;
  for (unsigned i = 0; i < AP_M68851_ATC_ENTRIES; i++) {
    if (atc->entry[i].valid && atc->entry[i].lock) {
      count++;
    }
  }
  return count;
}

bool ap_m68851_atc_lock_warning(const ap_m68851_atc_t *atc) {
  /* "Set when all entries in the ATC but one have been locked." */
  return ap_m68851_atc_locked_count(atc) >= AP_M68851_ATC_LOCK_CEILING;
}

bool ap_m68851_atc_may_lock(const ap_m68851_atc_t *atc) {
  return ap_m68851_atc_locked_count(atc) < AP_M68851_ATC_LOCK_CEILING;
}

unsigned ap_m68851_atc_select_victim(const ap_m68851_atc_t *atc) {
  /* "Locate an invalid entry and use it." */
  for (unsigned i = 0; i < AP_M68851_ATC_ENTRIES; i++) {
    if (!atc->entry[i].valid) {
      return i;
    }
  }

  /* "If no invalid entries are found, use a psuedo least-recently-used (LRU)
   * algorithm to select an entry without its L bit set." The history bit is
   * the whole of the approximation: an entry with it clear has not been used
   * since the last sweep, so prefer one of those. */
  for (unsigned i = 0; i < AP_M68851_ATC_ENTRIES; i++) {
    if (!atc->entry[i].lock && !atc->entry[i].history) {
      return i;
    }
  }

  /* Every unlocked entry has been used recently. Clear the history and take the
   * first unlocked one -- which is what makes this *pseudo*-LRU: the history
   * bits are a single generation, not an ordering, so when they are all set the
   * cache cannot tell which was used longest ago and starts a new generation. */
  for (unsigned i = 0; i < AP_M68851_ATC_ENTRIES; i++) {
    if (!atc->entry[i].lock) {
      return i;
    }
  }

  /* Unreachable: the lock ceiling guarantees one unlocked entry. Returning the
   * last index rather than asserting keeps a corrupted cache from being worse
   * than a stale translation. */
  return AP_M68851_ATC_ENTRIES - 1u;
}

void ap_m68851_atc_fill(ap_m68851_atc_t *atc, unsigned index,
                        ap_m68851_atc_entry_t entry) {
  /* "It will not be a copy of the page descriptor L bit if there are already 63
   * entries with set L bits in the ATC. In this case, the L bit for new entries
   * will always be clear." Checked against the cache as it stands *before* this
   * fill, since the entry being replaced may itself be one of the locked. */
  if (entry.lock && !ap_m68851_atc_may_lock(atc)) {
    entry.lock = false;
  }
  entry.valid = true;
  /* A freshly filled entry counts as used: it was filled because something
   * referenced it. */
  entry.history = true;
  atc->entry[index] = entry;

  /* Starting a new history generation when every unlocked entry was recently
   * used. Done here rather than in the victim search so that the search stays a
   * pure query -- the state change belongs with the fill that caused it. */
  bool any_unused = false;
  for (unsigned i = 0; i < AP_M68851_ATC_ENTRIES; i++) {
    if (!atc->entry[i].lock && !atc->entry[i].history) {
      any_unused = true;
      break;
    }
  }
  if (!any_unused) {
    for (unsigned i = 0; i < AP_M68851_ATC_ENTRIES; i++) {
      if (i != index && !atc->entry[i].lock) {
        atc->entry[i].history = false;
      }
    }
  }
}

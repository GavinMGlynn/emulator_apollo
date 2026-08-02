/* MC68851 address translation cache.
 *
 * `MC68851 PMMU User's Manual, Third Edition` §5.2, Figures 5-21 and 5-22.
 *
 * ## Not programmer-visible, and modelled accordingly
 *
 * "The information contained in the ATC is not directly accessible to the
 * programmer." Figures 5-21 and 5-22 accordingly draw named fields with **no
 * bit numbers** -- there is no layout to transcribe, because no software ever
 * sees one. So this is a struct of fields rather than a packed word, and that
 * is the honest model: inventing a bit assignment would make the state hash
 * depend on a choice the hardware never made.
 *
 * ## A match needs three things, and the third is an escape hatch
 *
 * "For a CAM entry to match a logical address presented by a logical bus
 * master, both the logical address field (exclusive of low order bits
 * representing the page offset) and the FC field must match exactly. In
 * addition, the task alias (TA) field must match the current TA value of the
 * MC68851, or the entry's SG bit must be set in order for a match to occur."
 *
 * The task alias is what lets entries for several tasks live in the cache at
 * once, and `SG` is what lets a globally shared mapping serve all of them from
 * one entry -- which is the performance reason a root pointer carries `SG` at
 * all.
 *
 * The page offset is excluded by the *current* page size, not by the entry:
 * "the lower order bits of the logical address field are ignored during compare
 * operations if the page size is larger than 256 bytes."
 *
 * ## `B` is how a *failure* is cached
 *
 * "The B bit, when set, indicates that no translation should be performed using
 * this ATC entry and that a bus error will be signaled ... The B bit is also
 * used to implement supervisor-only protection and access level protection with
 * the RAL translation descriptor field. In these cases a task may generate the
 * address of a restricted memory page, and instead of maintaining the RAL field
 * and S bit in the ATC, the validity of the access is evaluated when the ATC
 * entry is made. If access is to be denied, an ATC entry is made with the B bit
 * set."
 *
 * So the ATC caches denials as well as translations, and it does so by
 * *evaluating protection once, at fill time*, rather than storing `RAL` and `S`
 * and re-checking on every hit. A model that kept the access level in the entry
 * would be more general than the hardware and would get the *fill* wrong: the
 * decision is baked in when the entry is made.
 *
 * ## Replacement: invalid first, then pseudo-LRU among the unlocked
 *
 * "Locate an invalid entry and use it. If no invalid entries are found, use a
 * psuedo least-recently-used (LRU) algorithm to select an entry without its L
 * bit set and replace that entry." Two extra bits per entry carry it: "one is a
 * valid bit ... the other is a history bit to indicate that the entry has been
 * recently used."
 *
 * The lock bit has a ceiling written into the hardware: "it will not be a copy
 * of the page descriptor L bit if there are already 63 entries with set L bits
 * in the ATC. In this case, the L bit for new entries will always be clear."
 * Sixty-three of sixty-four, so one entry always remains replaceable and the
 * cache can never deadlock against its own locks. That is the same condition
 * `PCSR`'s lock-warning bit reports.
 */

#ifndef APOLLO_CPU_M68851_AP_M68851_ATC_H
#define APOLLO_CPU_M68851_AP_M68851_ATC_H

#include <stdbool.h>
#include <stdint.h>

/* "There are 64 entries in the CAM array and 64 corresponding entries in the
 * RAM array." */
#define AP_M68851_ATC_ENTRIES 64u

/* "The L bit for new entries will always be clear" once this many are locked,
 * leaving one entry always available for replacement. */
#define AP_M68851_ATC_LOCK_CEILING 63u

typedef struct {
  /* Figure 5-21, the tag. */
  bool shared_globally; /* SG */
  unsigned task_alias;  /* TA */
  unsigned function_code; /* FC */
  uint32_t logical_address;

  /* Figure 5-22, the data. */
  bool lock;          /* L, internal: exempts the entry from replacement */
  bool bus_error;     /* B: this entry caches a denial, not a translation */
  bool cache_inhibit; /* C, driven inverted onto CLI */
  bool write_protect; /* W: "the effective write protection determined during
                       * the translation table search" -- accumulated down the
                       * tree, not copied from one descriptor */
  bool modified;      /* M */
  bool gate;          /* G */
  uint32_t physical_address;

  /* §5.2.1.3's two extra bits. */
  bool valid;
  bool history; /* "the entry has been recently used" */
} ap_m68851_atc_entry_t;

typedef struct {
  ap_m68851_atc_entry_t entry[AP_M68851_ATC_ENTRIES];
  /* The MC68851's current task alias, against which tags are matched. Held
   * here rather than passed in, because a match is meaningless without it. */
  unsigned task_alias;
} ap_m68851_atc_t;

/* Invalidate every entry. History and lock bits go with them: an invalid entry
 * is the first choice for replacement, so leaving a lock behind would exempt a
 * slot that holds nothing. */
void ap_m68851_atc_flush(ap_m68851_atc_t *atc);

/* Flush only the entries belonging to one task alias, leaving globally shared
 * entries alone. This is what a write to `CRP` triggers, and what `PCSR`'s
 * `F` bit reports having done. */
void ap_m68851_atc_flush_task(ap_m68851_atc_t *atc, unsigned task_alias);

/* Whether a tag matches, given the current page size. All three conditions:
 * the logical address above the page offset, the function code exactly, and
 * either the task alias or the entry's `SG`. */
[[nodiscard]] bool ap_m68851_atc_entry_matches(const ap_m68851_atc_t *atc,
                                               const ap_m68851_atc_entry_t *e,
                                               uint32_t logical_address,
                                               unsigned function_code,
                                               uint32_t page_bytes);

/* Find a matching entry, or NULL. Does not update the history bit -- the caller
 * decides whether this lookup counts as a use, since a `PTEST` inspects the
 * cache without disturbing it. */
[[nodiscard]] const ap_m68851_atc_entry_t *
ap_m68851_atc_lookup(const ap_m68851_atc_t *atc, uint32_t logical_address,
                     unsigned function_code, uint32_t page_bytes);

/* Mark an entry as recently used. Separate from lookup for the reason above. */
void ap_m68851_atc_touch(ap_m68851_atc_t *atc, unsigned index);

/* How many entries currently hold a lock. */
[[nodiscard]] unsigned ap_m68851_atc_locked_count(const ap_m68851_atc_t *atc);

/* `PCSR`'s `LW`: "set when all entries in the ATC but one have been locked."
 * At that point no further entry may be locked, "regardless of the state of L
 * bits in translation descriptors". */
[[nodiscard]] bool ap_m68851_atc_lock_warning(const ap_m68851_atc_t *atc);

/* Whether a new entry may take a lock. False once the ceiling is reached, which
 * is what keeps one entry replaceable no matter what the tables ask for. */
[[nodiscard]] bool ap_m68851_atc_may_lock(const ap_m68851_atc_t *atc);

/* Choose the entry a new translation will occupy: "locate an invalid entry and
 * use it. If no invalid entries are found, use a psuedo least-recently-used
 * (LRU) algorithm to select an entry without its L bit set." */
[[nodiscard]] unsigned ap_m68851_atc_select_victim(const ap_m68851_atc_t *atc);

/* Install an entry at `index`. `entry.lock` is honoured only if the ceiling
 * allows it, per §5.2.1.2 -- the caller passes what the descriptor asked for
 * and the cache decides what it gets. */
void ap_m68851_atc_fill(ap_m68851_atc_t *atc, unsigned index,
                        ap_m68851_atc_entry_t entry);

#endif /* APOLLO_CPU_M68851_AP_M68851_ATC_H */

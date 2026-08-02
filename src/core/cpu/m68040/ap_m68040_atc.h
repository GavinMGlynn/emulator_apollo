/* MC68040 address translation caches.
 *
 * `MC68040 User's Manual (1993)` §3.3, Figures 3-20 and 3-21.
 *
 * ## Two ATCs, and neither resembles the 68851's
 *
 * "The ATCs in the MMUs are four-way set-associative caches that each store 64
 * logical-to-physical address translations." There are two -- one in the
 * instruction memory unit and one in the data unit -- where the 68851 has one
 * fully-associative cache of 64 entries shared by everything.
 *
 * The tag differs in a way that matters more than the associativity:
 *
 *     68851   SG, task alias, the whole function code, logical address
 *     68040   V, G, **FC2 only**, logical address
 *
 * So the 68040 cannot tell program space from data space in a tag -- it does
 * not need to, having a separate ATC for each -- and it has no task alias at
 * all. Where the 68851 keeps several tasks resident by tagging them, the 68040
 * flushes on a context switch and keeps only *global* entries, which is what
 * `G` is for: "global entries are not invalidated by the PFLUSH instruction
 * variants that specify nonglobal entries".
 *
 * ## The manual contradicts itself about the tag width, and 16 is right
 *
 * §3.3's `Logical Address` field definition reads, in the page image and not
 * merely in an extraction: "This **13-bit** field contains the most significant
 * logical address bits for this entry. All **16** bits of this field are used
 * in the comparison of this entry to an incoming logical address when the page
 * size is 4 Kbytes."
 *
 * Thirteen and sixteen cannot both be right. The `MC68040 Designer's Handbook`
 * does not describe the field, and a web search turned up no transcription or
 * erratum that settles it, so this is derived from numbers the same manual
 * states:
 *
 *   - 64 entries, four-way set associative, so **16 sets** -- and Figure 3-20
 *     draws them as `SET 0` through `SET 15`, which is an independent
 *     confirmation. Sixteen sets need four bits of set select.
 *   - At 4 Kbytes the page number is address bits 31-12, twenty bits.
 *   - Twenty bits of page number less four of set select leaves **sixteen** for
 *     the tag.
 *
 * The 8-Kbyte case checks the same arithmetic from the other side: the page
 * number is bits 31-13, nineteen bits, less four leaves fifteen -- and the
 * manual says "for 8-Kbyte pages, the least significant bit of this field is
 * ignored", which is sixteen stored with fifteen compared. So sixteen is the
 * width and "13-bit" is an error in the manual.
 */

#ifndef APOLLO_CPU_M68040_AP_M68040_ATC_H
#define APOLLO_CPU_M68040_AP_M68040_ATC_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68040/ap_m68040_descriptor.h"

#define AP_M68040_ATC_SETS 16u
#define AP_M68040_ATC_WAYS 4u
#define AP_M68040_ATC_ENTRIES 64u
/* See the header comment for why this is sixteen and not the thirteen the
 * manual's field definition claims. */
#define AP_M68040_ATC_TAG_BITS 16u

typedef struct {
  /* Figure 3-21's tag. */
  bool valid;      /* V */
  bool global;     /* G */
  bool supervisor_space; /* FC2: "set for supervisor mode accesses" */
  uint32_t logical_tag;

  /* Figure 3-21's entry. */
  bool user_attribute_1; /* U1 */
  bool user_attribute_0; /* U0 */
  bool supervisor;       /* S: the page is supervisor-only */
  ap_m68040_cache_mode_t cache_mode; /* CM */
  bool modified;         /* M */
  bool write_protect;    /* W */
  /* R, and note the sense: "set if the table search successfully completes
   * without encountering either a nonresident page or a transfer error". The
   * 68851's equivalent is `B`, set when the *search failed*. Same information,
   * opposite polarity -- and an entry copied across without inverting it would
   * turn every good translation into a bus error. */
  bool resident;
  uint32_t physical_address;
} ap_m68040_atc_entry_t;

typedef struct {
  ap_m68040_atc_entry_t entry[AP_M68040_ATC_SETS][AP_M68040_ATC_WAYS];
  /* "A 2-bit counter, which is incremented for each ATC access, points to the
   * entry to replace when an access misses in the ATC." The same scheme as the
   * caches', and equally deterministic. */
  unsigned counter;
} ap_m68040_atc_t;

void ap_m68040_atc_init(ap_m68040_atc_t *atc);

/* The set index: the low four bits of the page number, so bits 15-12 at 4K and
 * 16-13 at 8K. */
[[nodiscard]] unsigned ap_m68040_atc_set(uint32_t logical_address,
                                         ap_m68040_page_size_t page_size);

/* The tag: the page number above the set select. */
[[nodiscard]] uint32_t ap_m68040_atc_tag(uint32_t logical_address,
                                         ap_m68040_page_size_t page_size);

/* Find the way holding this address, or `AP_M68040_ATC_WAYS` for a miss. The
 * comparison needs `FC2` as well as the address -- one ATC serves both
 * privilege modes even though it does not serve both spaces. */
[[nodiscard]] unsigned ap_m68040_atc_lookup(const ap_m68040_atc_t *atc,
                                            uint32_t logical_address,
                                            bool supervisor,
                                            ap_m68040_page_size_t page_size);

/* Choose the way a new entry will occupy: an invalid one if the set has any,
 * else the one the counter points at. */
[[nodiscard]] unsigned ap_m68040_atc_select_way(const ap_m68040_atc_t *atc,
                                                uint32_t logical_address,
                                                ap_m68040_page_size_t page_size);

void ap_m68040_atc_tick(ap_m68040_atc_t *atc);

void ap_m68040_atc_fill(ap_m68040_atc_t *atc, unsigned way,
                        uint32_t logical_address,
                        ap_m68040_page_size_t page_size,
                        ap_m68040_atc_entry_t entry);

/* `PFLUSHA`: invalidate everything, global entries included. */
void ap_m68040_atc_flush_all(ap_m68040_atc_t *atc);

/* The `PFLUSH` variants that "specify nonglobal entries": global entries
 * survive. `match_supervisor` selects by `FC2`, which is the only function code
 * bit an entry carries. */
void ap_m68040_atc_flush_nonglobal(ap_m68040_atc_t *atc, bool supervisor);

/* Invalidate the entry for one page, if present. */
void ap_m68040_atc_flush_page(ap_m68040_atc_t *atc, uint32_t logical_address,
                              bool supervisor,
                              ap_m68040_page_size_t page_size);

#endif /* APOLLO_CPU_M68040_AP_M68040_ATC_H */

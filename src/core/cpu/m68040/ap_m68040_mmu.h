/* The MC68040's MMU, tied together: transparent translation, the ATC, and the
 * table search, in the order `[040]` §3.5 puts them.
 *
 * **Every part of this existed before this file and none of them was joined.**
 * `ap_m68040_regs.*` decodes the TCR and the TTRs, `ap_m68040_atc.*` is a
 * 64-entry four-way ATC with all four of `PFLUSH`'s variants, and
 * `ap_m68040_search.*` walks the tables. What was missing was the call that
 * uses them, which is why `docs/COMPLETION_PLAN.md` describes the 68040 MMU as
 * "its parts are all built ... what is missing is the join".
 *
 * **It is joined now because software asked.** Domain/OS's DS5500 loader sets
 * `TC` to `00008000` -- Figure 3-4's `E` bit, `P` clear for 4-Kbyte pages --
 * loads `URP` and `SRP` with real addresses, programs all four TTRs, and then
 * addresses memory expecting translation. Until this file that machine ran
 * translated addresses untranslated and died on the first one.
 *
 * ## The order, and why the TTRs come first even when translation is off
 *
 * §3.1.3: the transparent translation registers "operate independently of the
 * E-bit in the TCR". So a TTR match answers whether or not paged translation is
 * enabled, and this call has to be reached even on a machine whose `TC` says
 * off -- which is why the caller asks this module rather than checking `E`
 * itself and skipping.
 *
 * §3.5 then gives the rest: the ATC, and on a miss a table search whose result
 * fills it.
 *
 * ## What this does not do, named rather than left to be discovered
 *
 * - **The instruction and data sides share this code but not their state.** A
 *   68040 has separate ITTRs, DTTRs and ATCs, and the caller passes the pair
 *   and the cache belonging to the access it is making.
 * - **`M` is not written back to the table.** §3.2.2 makes setting the U and M
 *   bits a locked read-modify-write, and this core's table search reads
 *   descriptors through a plain callback with no bus to lock -- the same gap
 *   `docs/COMPLETION_PLAN.md` records for the 68030's walk. The ATC entry's `M`
 *   is set, so a second write to the same page behaves; the table does not see
 *   it. `PROVISIONAL`.
 */
#ifndef APOLLO_CPU_M68040_AP_M68040_MMU_H
#define APOLLO_CPU_M68040_AP_M68040_MMU_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68040/ap_m68040_atc.h"
#include "cpu/m68040/ap_m68040_regs.h"
#include "cpu/m68040/ap_m68040_search.h"

/* One access kind's view of the MMU: the registers are the CPU's, the ATC is
 * this side's. Pointers rather than copies because a `MOVEC` to `TC` must take
 * effect on the next access without anyone re-publishing anything. */
typedef struct {
  const uint32_t *tc;  /* the CPU's `tc_040` */
  const uint32_t *ttr; /* this side's pair: `ittr_040` or `dttr_040` */
  const uint32_t *urp;
  const uint32_t *srp;
  ap_m68040_atc_t *atc; /* this side's */
} ap_m68040_mmu_t;

typedef enum {
  /* No translation was needed or performed: the TCR's `E` is clear and no TTR
   * matched, so the logical address *is* the physical one. §3.5. */
  AP_M68040_MMU_UNTRANSLATED,
  /* A TTR answered. */
  AP_M68040_MMU_TRANSPARENT,
  /* The ATC or a table search answered. */
  AP_M68040_MMU_TRANSLATED,
  /* The page is not resident, is supervisor-only to a user access, or is write
   * protected against a write. The caller takes an access fault. */
  AP_M68040_MMU_FAULT,
} ap_m68040_mmu_status_t;

typedef struct {
  ap_m68040_mmu_status_t status;
  uint32_t physical;
  ap_m68040_cache_mode_t cache_mode;
  /* Descriptors read by a table search, for the report's `atc fills` line. */
  unsigned fetches;
  /* Whether this access filled an ATC entry, which is the other half of that
   * line: a search that faults still caches its outcome. */
  bool filled;
  /* Why a `FAULT` faulted, so a report can say which rather than guessing. The
   * three are distinguishable and the distinction is what a reader needs: an
   * entry that was already known bad, a page the tables say is not there, and a
   * page that is there and refuses this access. */
  enum {
    AP_M68040_MMU_FAULT_NONE,
    AP_M68040_MMU_FAULT_CACHED,     /* an ATC entry answered, and said no */
    AP_M68040_MMU_FAULT_NOT_RESIDENT, /* the search found no valid page */
    AP_M68040_MMU_FAULT_PROTECTION, /* supervisor-only, or write-protected */
    AP_M68040_MMU_FAULT_SEARCH_BUS, /* a descriptor fetch went unanswered */
  } reason;
} ap_m68040_mmu_result_t;

/* Translate one access. `fetch` reads a descriptor longword and returns false
 * on a bus error, exactly as `ap_m68040_search` wants it. */
[[nodiscard]] ap_m68040_mmu_result_t
ap_m68040_mmu_translate(const ap_m68040_mmu_t *mmu, uint32_t logical,
                        unsigned function_code, bool write,
                        ap_m68040_fetch_fn fetch, void *fetch_context);

#endif /* APOLLO_CPU_M68040_AP_M68040_MMU_H */

/* The MC68851 as a fitted part: its state, translating an address, and the
 * instructions that manage it.
 *
 * ## Fitted or not is a machine property
 *
 * A DN3000 has a 68851 and a DN3500 does not -- the 68030 translates on chip.
 * The same reasoning as the 68882's: the part is attached to a CPU rather than
 * compiled into it, and a machine without one keeps whatever it did before.
 *
 * ## Translation is a cache lookup that falls back to a walk
 *
 * `ap_m68851_translate()` is the whole of the read path: match the ATC, and on
 * a miss run the table search and install what it found -- *including* a
 * denial, which is cached with `B` set exactly as §5.2.1.2 describes. The
 * search itself is in `ap_m68851_search.c` and does not touch the bus; this
 * module supplies the fetch and owns the cache.
 *
 * ## `PMOVE`'s side effects are most of what `PMOVE` does
 *
 * Appendix A lists them, and they are not incidental:
 *
 *  - **`CRP`**: "causes the internal root pointer table to be searched for the
 *    new value. If a matching value is not found, an entry in the root pointer
 *    table is selected for replacement, and all ATC entries associated with the
 *    replaced entry are invalidated." That is what the task alias mechanism is
 *    for, and what `PCSR`'s `F` bit reports afterwards.
 *  - **`SRP` and `DRP`**: "causes all entries in the ATC that were formed with
 *    the SRP (even globally shared) to be invalidated." The parenthesis is the
 *    point -- this is the one place a globally shared entry does *not* survive
 *    a flush, and a model that reused the ordinary flush here would leave stale
 *    supervisor mappings behind.
 *  - **`TC`**: a write with `E` clear flushes the entire ATC; a write setting
 *    `E` runs the consistency check first, and on failure "the TC register is
 *    updated with the data except that the E bit is cleared" -- so a rejected
 *    write is not a write that did not happen.
 */

#ifndef APOLLO_CPU_M68851_AP_M68851_H
#define APOLLO_CPU_M68851_AP_M68851_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68851/ap_m68851_atc.h"
#include "cpu/m68851/ap_m68851_cir.h"
#include "cpu/m68851/ap_m68851_decode.h"
#include "cpu/m68851/ap_m68851_regs.h"
#include "cpu/m68851/ap_m68851_rp.h"
#include "cpu/m68851/ap_m68851_search.h"
#include "cpu/m68851/ap_m68851_tc.h"

typedef struct {
  ap_m68851_tc_t tc;
  ap_m68851_rp_t crp;
  ap_m68851_rp_t srp;
  ap_m68851_rp_t drp;
  ap_m68851_pcsr_t pcsr;
  ap_m68851_psr_t psr;
  /* The protection registers, held as raw bytes because only their upper three
   * bits are implemented and software may read back what it wrote. */
  uint8_t cal;
  uint8_t val;
  uint8_t scc;
  ap_m68851_ac_t ac;
  ap_m68851_atc_t atc;
  /* "Motorola assemblers default to ID = 0 for the PMMU", and a system may hold
   * several coprocessors, so this is configured rather than assumed. */
  unsigned cpid;
} ap_m68851_t;

/* §6.1.3.1: "This bit is cleared during reset" for `E`, and §6.1.7.2: `ALC`
 * "is initialized to zero during reset" -- so a reset part translates nothing
 * and checks no access levels, which is what lets a machine boot before its
 * tables exist. */
void ap_m68851_reset(ap_m68851_t *mmu);

/* What a translation produced. */
typedef enum {
  AP_M68851_TRANSLATE_OK,
  /* The ATC entry's `B` bit, or a search that ended invalid: "a bus error will
   * be signaled to the logical bus master". */
  AP_M68851_TRANSLATE_BUS_ERROR,
  /* A write to a write-protected page. Reported apart from a bus error because
   * the two reach different `PSR` bits, though the hardware signals both the
   * same way. */
  AP_M68851_TRANSLATE_WRITE_PROTECTED,
} ap_m68851_translate_status_t;

typedef struct {
  ap_m68851_translate_status_t status;
  uint32_t physical_address;
  /* "The inverse of the CI bit is presented on the CLI output during address
   * translations." Reported as the descriptor's sense; the pin is inverted. */
  bool cache_inhibit;
  /* True when the ATC answered without a table search. */
  bool cache_hit;
} ap_m68851_translation_t;

/* Translate one logical address. `fetch` reads descriptors during a table
 * search and is unused on an ATC hit. */
[[nodiscard]] ap_m68851_translation_t
ap_m68851_translate(ap_m68851_t *mmu, uint32_t logical_address,
                    unsigned function_code, bool is_write,
                    ap_m68851_fetch_fn fetch, void *fetch_context);

/* ---------------------------------------------------------------------------
 * Instructions.
 * ------------------------------------------------------------------------- */

typedef enum {
  AP_M68851_EXECUTED,
  /* Not this coprocessor's encoding, or one Appendix A leaves undefined. */
  AP_M68851_TAKE_LINE_F,
  /* A configuration error: `TC` written with `E` set and an inconsistent
   * geometry, which Table 9-6 gives vector 56. */
  AP_M68851_CONFIGURATION_ERROR,
  /* A form this model has not implemented. Distinct from the traps above
   * because one is the machine and the other is us. */
  AP_M68851_UNIMPLEMENTED,
} ap_m68851_status_t;

/* Execute a `PMOVE` writing `value` into the selected register, with the side
 * effects above. Values wider than the register are truncated by the decode. */
[[nodiscard]] ap_m68851_status_t ap_m68851_pmove_write(ap_m68851_t *mmu,
                                                       ap_m68851_preg_t preg,
                                                       uint64_t value);

/* Read a register for a `PMOVE` in the other direction. Returns zero for a
 * register this part does not implement. */
[[nodiscard]] uint64_t ap_m68851_pmove_read(const ap_m68851_t *mmu,
                                            ap_m68851_preg_t preg);

/* Execute a `PFLUSH`. `address` is the evaluated effective address, used only
 * by the modes that match on one; `function_code` is the resolved value of the
 * instruction's `FC` field, since resolving it may need the CPU's registers. */
[[nodiscard]] ap_m68851_status_t
ap_m68851_pflush(ap_m68851_t *mmu, const ap_m68851_instruction_t *instruction,
                 unsigned function_code, uint32_t address);

#endif /* APOLLO_CPU_M68851_AP_M68851_H */

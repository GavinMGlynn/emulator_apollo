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

/* §6.1.9 and §6.1.10, Figures 6-8 and 6-9. Eight of each, one per 68020
 * breakpoint. `BADx` is sixteen bits of replacement opcode with no other
 * fields; `BACx` is an enable at bit 15 and a skip count at bits 7-0, with
 * bits 14-8 "always read as zeros and must be written as zeros". */
typedef struct {
  uint16_t replacement_opcode; /* BADx */
  bool enable;                 /* BACx bit 15 */
  unsigned skip_count;         /* BACx bits 7-0 */
} ap_m68851_breakpoint_t;

#define AP_M68851_BREAKPOINTS 8u
#define AP_M68851_BAC_IMPLEMENTED_MASK 0x80FFu

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
  ap_m68851_breakpoint_t breakpoint[AP_M68851_BREAKPOINTS];
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
 * search and is unused on an ATC hit.
 *
 * `store` writes the `U` and `M` bits back into the descriptors the search
 * read, which §5.1.5.3.11 requires before the page may be accessed: "updates of
 * the U and M bits are performed before the MC68851 allows a page to be
 * accessed or written". It may be NULL, and then the tables are left
 * unchanged -- which is what a probe harness with no write path wants, and is
 * *not* what a running machine wants: an operating system reads those bits to
 * choose which page to evict.
 *
 * Nothing is written on an ATC hit. The bits were set when the entry was made,
 * and the hardware has no reason to walk the tree again to set them twice. */
[[nodiscard]] ap_m68851_translation_t
ap_m68851_translate(ap_m68851_t *mmu, uint32_t logical_address,
                    unsigned function_code, bool is_write,
                    ap_m68851_fetch_fn fetch, void *fetch_context,
                    ap_m68851_store_fn store, void *store_context);

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

/* The same, for the registers that come in eights. `BADx` and `BACx` need the
 * breakpoint number from the command word's `Num` field, which the other
 * registers have no equivalent of -- hence a separate entry point rather than
 * an argument every caller would pass as zero. */
[[nodiscard]] ap_m68851_status_t
ap_m68851_pmove_write_numbered(ap_m68851_t *mmu, ap_m68851_preg_t preg,
                               unsigned number, uint64_t value);
[[nodiscard]] uint64_t ap_m68851_pmove_read_numbered(const ap_m68851_t *mmu,
                                                     ap_m68851_preg_t preg,
                                                     unsigned number);

/* Execute a `PFLUSH`. `address` is the evaluated effective address, used only
 * by the modes that match on one; `function_code` is the resolved value of the
 * instruction's `FC` field, since resolving it may need the CPU's registers. */
[[nodiscard]] ap_m68851_status_t
ap_m68851_pflush(ap_m68851_t *mmu, const ap_m68851_instruction_t *instruction,
                 unsigned function_code, uint32_t address);

/* Execute a `PLOAD`: search the tables for one address and install the result
 * in the ATC, whether or not anything referenced it. "PLOADR causes U bits in
 * the translation tables to be updated as if a read access had taken place.
 * PLOADW causes U and M bits ... as if a write access had taken place" -- so
 * the direction bit decides which table bits are written back, which is why a
 * `PLOAD` is not simply a warming hint. */
[[nodiscard]] ap_m68851_status_t
ap_m68851_pload(ap_m68851_t *mmu, const ap_m68851_instruction_t *instruction,
                unsigned function_code, uint32_t address,
                ap_m68851_fetch_fn fetch, void *fetch_context,
                ap_m68851_store_fn store, void *store_context);

/* Execute a `PTEST`, which leaves its answer in `PSR` rather than returning it.
 *
 * The level is the instruction's, and zero is a different operation rather than
 * a shallow one: §6.1.8 says throughout "for the PTEST instruction with a level
 * specification of zero" the bits report what the *ATC* held, and several are
 * "always clear" because no table was walked. A model that treated level zero
 * as a zero-deep search would report a fault where the hardware reports a
 * cache miss. */
/* `PTEST` takes no store, and the omission is the specification: "U and M bits
 * in the translation table are not modified by this instruction." A probe that
 * marked a page used would make the operating system's own diagnostic change
 * the history it was diagnosing. Not offering the parameter is how that is made
 * impossible rather than merely discouraged. */
[[nodiscard]] ap_m68851_status_t
ap_m68851_ptest(ap_m68851_t *mmu, const ap_m68851_instruction_t *instruction,
                unsigned function_code, uint32_t address,
                ap_m68851_fetch_fn fetch, void *fetch_context);

/* Execute a `PVALID`. `operand` is the logical address being validated and
 * `surrogate` the access level to test against for the register form -- already
 * extracted from the address register by the caller, since reading it is the
 * main processor's job.
 *
 * "If the operand bits are arithmetically less than the VAL (or surrogate VAL)
 * bits, this instruction causes a trap with the access level violation
 * exception." Lower is *more* privileged, so this traps when a caller passes a
 * pointer more privileged than the caller itself -- which is the confused
 * deputy it exists to prevent. */
typedef enum {
  AP_M68851_PVALID_OK,
  /* Table 9-6's vector 58, post-instruction. */
  AP_M68851_PVALID_ACCESS_VIOLATION,
} ap_m68851_pvalid_result_t;

[[nodiscard]] ap_m68851_pvalid_result_t
ap_m68851_pvalid(const ap_m68851_t *mmu, uint32_t operand, bool use_surrogate,
                 uint8_t surrogate);

/* ---------------------------------------------------------------------------
 * Breakpoints, §6.1.9, §6.1.10 and §8.1.
 *
 * The other half of a mechanism whose CPU side landed in Phase 2: the 68020's
 * `BKPT` runs a breakpoint acknowledge cycle, and this part answers it.
 * ------------------------------------------------------------------------- */

typedef enum {
  /* "The MC68851 will return the corresponding replacement opcode and assert
   * DSACKx" -- the CPU executes the opcode in place of the `BKPT`. */
  AP_M68851_BREAKPOINT_REPLACED,
  /* "The MC68851 terminates the cycle by asserting bus error, causing the
   * MC68020 to initiate illegal instruction exception processing." Reached
   * either because the breakpoint is disabled or because its count ran out. */
  AP_M68851_BREAKPOINT_BUS_ERROR,
} ap_m68851_breakpoint_result_t;

/* Answer a breakpoint acknowledge cycle for breakpoint `number`. Mutating,
 * because "during the breakpoint cycle, the skip count is decremented by one"
 * -- a breakpoint that fires changes what the next one does, which is the
 * whole mechanism. */
[[nodiscard]] ap_m68851_breakpoint_result_t
ap_m68851_breakpoint_acknowledge(ap_m68851_t *mmu, unsigned number,
                                 uint16_t *replacement_opcode);

#endif /* APOLLO_CPU_M68851_AP_M68851_H */

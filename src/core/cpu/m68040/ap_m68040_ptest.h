/* The MC68040's `PTEST`, which is the third of its three MMU instructions and
 * the last one this core was missing.
 *
 * **It is here because software asked for it, and because of where it asked.**
 * A DS5500 running Domain/OS executes `F56C` -- `PTESTR (A4)` -- at boot PROM
 * offset `$58D2`, and it does so *inside the firmware's crash reporter*, four
 * instructions before it reads the answer back:
 *
 * ```
 *   058D2  F56C            PTESTR (A4)
 *   058D4  4E7A 0805       MOVEC MMUSR,D0
 *   058D8  2D40 01D0       MOVE.L D0,$1D0(A6)
 *   058DC  6706            BEQ.B  $58E4
 *   058DE  0800 0000       BTST   #0,D0        ; MMUSR's R bit
 * ```
 *
 * The firmware is asking whether an address is resident before it dereferences
 * it -- exactly what a crash reporter should do. Without this instruction the
 * word takes the line 1111 emulator exception, Domain/OS's handler at
 * `7A42EDBC` runs on the firmware's 384-byte console stack, and the machine
 * dies of a stack overflow while trying to print why it died. `PROJECT_STATUS`
 * has the measurement.
 *
 * **A no-op would have been worse than the trap.** `PFLUSH` landed as a
 * decoded, privilege-checked instruction with nothing to flush, and that was
 * honest because a flush of an unattached ATC has no observable effect.
 * `PTEST`'s whole purpose is the value it leaves in MMUSR: a `PTEST` that
 * executed and wrote nothing would hand the firmware a stale register and a
 * confident wrong answer about whether a page is there.
 *
 * ## What the manual says, from the page image
 *
 * `[PRM]` pp. 6-70 and 6-71, "PTEST -- Test a Logical Address (MC68040,
 * MC68LC040)", read as 300-dpi page images rather than extracted text.
 *
 * Operation: "If Supervisor State / Then Logical Address Status -> MMUSR;
 * Entry -> ATC / Else TRAP". Two effects, and the second is the one a reader
 * skims past: `PTEST` *fills the ATC*.
 *
 * Format, transcribed bit by bit from the figure --
 * `1 1 1 1 0 1 0 1 0 1 R/W 0 1 REGISTER` -- so bits 15-6 are fixed at
 * `1111010101`, bit 5 is R/W (`0` write, `1` read), bit 4 is 0, bit 3 is 1 and
 * bits 2-0 name an address register. `PTESTW` is `$F548 + An`, `PTESTR` is
 * `$F568 + An`, and the mask that identifies both is `$FFD8`.
 *
 * The four terminating conditions, verbatim: "Match with one of the two
 * transparent translation registers", "Transfer Error Assertion (physical
 * transfer error)", "Invalid Descriptor", "Valid Page Descriptor".
 *
 * "The specification of the function code for the test address is in the
 * destination function code (DFC) register. A PTEST instruction with a DFC
 * value of 0, 3, 4, or 7 is undefined and will return an unknown value in the
 * MMUSR."
 *
 * "A matching entry in the address translation cache (data or instruction)
 * specified by the function code will be flushed by PTEST. Completion of PTEST
 * results in the creation of a new address translation cache entry." So the
 * search is unconditional: a `PTEST` may not answer out of the ATC, because the
 * entry that would have answered is the one it just threw away. **That is why
 * this is not a call to `ap_m68040_mmu_translate`**, which consults the ATC
 * first and is right to.
 *
 * "The PTESTR instruction simulates a read access and sets the U-bit in each
 * descriptor during table searches; PTESTW simulates a write access and also
 * sets the M-bit in the descriptors, the address translation cache entry, and
 * the MMU status register."
 *
 * **So `PTEST` writes the tables, and that is the sentence to read twice.** It
 * is not a passive query: `PTESTR` sets `U` in every descriptor it walks and
 * `PTESTW` sets `M` in the page descriptor as well, under exactly the rules
 * `ap_m68040_search` carries from `[040]` Table 3-1 -- so an operating system's
 * access-error handler leaves history bits behind it. The instruction is
 * therefore given the update callback like any other search. The 68030's and
 * 68851's `PTEST` do not do this, and this core's 68030 path is right to pass
 * NULL.
 *
 * ## One place this stops short, named
 *
 * - **An undefined result leaves MMUSR alone.** For a DFC of 0, 3, 4 or 7, and
 *   for a search that cannot happen because the TCR's `E` is clear and no TTR
 *   matched (`[040]` §3.1: "PTEST results are undefined if the MMU is disabled
 *   and no table search occurs"), this reports `defined == false` and computes
 *   no MMUSR. Leaving the register untouched is a legal "unknown value" and is
 *   the only one that invents nothing.
 */
#ifndef APOLLO_CPU_M68040_AP_M68040_PTEST_H
#define APOLLO_CPU_M68040_AP_M68040_PTEST_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68040/ap_m68040_mmu.h"
#include "cpu/m68040/ap_m68040_regs.h"

/* The word is a `PTEST` for this part. `has_68040_mmu_registers` is the
 * caller's gate: on a 68030 `$F5xx` is coprocessor id 2 and must stay F-line,
 * and on an MC68EC040 Appendix B says `PTEST` "causes random bus cycles" and
 * does not trap, which is not something to model. */
#define AP_M68040_PTEST_MASK 0xFFD8u
#define AP_M68040_PTEST_MATCH 0xF548u

[[nodiscard]] static inline bool ap_m68040_is_ptest(uint16_t word) {
  return (word & AP_M68040_PTEST_MASK) == AP_M68040_PTEST_MATCH;
}

/* Bit 5: "0 -- Write, 1 -- Read". */
[[nodiscard]] static inline bool ap_m68040_ptest_is_read(uint16_t word) {
  return (word & 0x0020u) != 0u;
}

[[nodiscard]] static inline unsigned ap_m68040_ptest_register(uint16_t word) {
  return word & 0x0007u;
}

typedef struct {
  ap_m68040_mmusr_t mmusr;
  /* False where the manual leaves the result unspecified, in which case
   * `mmusr` is not meaningful and the caller must not write it. */
  bool defined;
  /* Descriptors read, so a run's `atc fills` line counts a `PTEST`'s bus
   * traffic with everything else's rather than losing it. */
  unsigned fetches;
  /* Whether an ATC entry was created. A bus error during the search creates
   * none, exactly as an ordinary access's search does not. */
  bool filled;
} ap_m68040_ptest_result_t;

/* Perform one `PTEST`. `write` is `PTESTW`; `function_code` is the DFC.
 * `update` writes the history bits back and may be NULL only for a caller that
 * must not disturb the tables -- which the instruction itself never is. */
[[nodiscard]] ap_m68040_ptest_result_t
ap_m68040_ptest(const ap_m68040_mmu_t *mmu, uint32_t logical,
                unsigned function_code, bool write, ap_m68040_fetch_fn fetch,
                ap_m68040_update_fn update, void *context);

#endif /* APOLLO_CPU_M68040_AP_M68040_PTEST_H */

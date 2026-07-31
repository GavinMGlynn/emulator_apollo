/* MC68030 MMU status register (MMUSR).
 *
 * `[030]` §9.7.4 pp. 9-59 f. and Table 9-3, cited throughout.
 *
 * "The MMU status register (MMUSR) is a 16-bit register that contains the
 * status information returned by execution of the PTEST instruction. The PTEST
 * instruction searches either the ATC (PTEST with level 0) or the translation
 * tables (PTEST with levels of 1-7) to determine status information about the
 * translation of a specified logical address."
 *
 * ## Two different registers wearing one name
 *
 * The bits mean *different things* depending on which PTEST produced them, and
 * Table 9-3 gives the two columns separately. A level 0 probe reports what the
 * ATC already holds; a level 1-7 probe reports what a table search found. Bits
 * that one form defines the other clears outright -- L, S and N are always zero
 * after a level 0 probe, and T is always zero after a level 1-7 probe. So this
 * module exposes two constructors rather than one with a mode flag, and neither
 * can accidentally produce the other's answer.
 *
 * ## The bit layout *is* pinned, unlike TTx
 *
 * `[030]` Figure 9-38 lost its field boxes to the scan exactly as Figure 9-37
 * did, leaving only the legend and the column markers 15-6. The layout is
 * nonetheless transcribed rather than deferred, because the `M68000 Family
 * Programmer's Reference Manual` gives it intact under `PTEST` (p. 6-64), as a
 * labelled row of sixteen bit positions:
 *
 *     B(15) L(14) S(13) 0(12) W(11) I(10) M(9) 0(8) 0(7) T(6) 0(5) 0(4) 0(3)
 *     N(2-0)
 *
 * The two documents cross-check: the PRM's row ends its last named single bit
 * at T, and the 68030 manual's surviving column markers stop at 6. That is the
 * difference from `ap_m68030_tt.h`, where no second source pinned the packing
 * and it stayed deferred. A layout is transcribed or deferred, never guessed --
 * this one is transcribed, from two documents that agree.
 *
 * ## "Undefined" is represented as zero, and that is a choice
 *
 * Table 9-3 marks several bits undefined in specific cases -- W, S and M when I
 * is set, and *every* other bit when T is set. Undefined means software may not
 * rely on them, not that the hardware drives a known value. They are cleared
 * here so our own output cannot imply a guarantee the manual does not make.
 * That is a representation decision, not a claim about the hardware, so an
 * oracle diff must mask these bits rather than treat a difference as a fault.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_MMUSR_H
#define APOLLO_CPU_M68030_AP_M68030_MMUSR_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_atc.h"
#include "cpu/m68030/ap_m68030_walk.h"

/* Bit positions, from the `M68000 Family Programmer's Reference Manual` PTEST
 * page. Bits 12, 8, 7, 5, 4 and 3 are shown as zero and have no field. */
#define AP_M68030_MMUSR_B_BIT 15u
#define AP_M68030_MMUSR_L_BIT 14u
#define AP_M68030_MMUSR_S_BIT 13u
#define AP_M68030_MMUSR_W_BIT 11u
#define AP_M68030_MMUSR_I_BIT 10u
#define AP_M68030_MMUSR_M_BIT 9u
#define AP_M68030_MMUSR_T_BIT 6u
#define AP_M68030_MMUSR_N_MASK 0x0007u /* bits 2-0 */

typedef struct {
  bool bus_error;            /* B */
  bool limit_violation;      /* L */
  bool supervisor_violation; /* S */
  bool write_protected;      /* W */
  bool invalid;              /* I */
  bool modified;             /* M */
  bool transparent;          /* T */
  uint8_t levels;            /* N: "the actual number of tables accessed" */
} ap_m68030_mmusr_t;

[[nodiscard]] uint16_t ap_m68030_mmusr_pack(const ap_m68030_mmusr_t *mmusr);
[[nodiscard]] ap_m68030_mmusr_t ap_m68030_mmusr_unpack(uint16_t word);

/* PTEST with level 0: "searches ... the ATC".
 *
 * `transparent_match` says whether the address matched TT0 or TT1, which the
 * caller has already evaluated with `ap_m68030_tt`. "This bit is set if a match
 * occurred in either (or both) of the transparent translation registers ... If
 * the T bit is set, all remaining MMUSR bits are undefined." */
[[nodiscard]] ap_m68030_mmusr_t
ap_m68030_mmusr_probe_atc(const ap_m68030_atc_t *atc, uint8_t function_code,
                          uint32_t address, uint8_t page_size_bits,
                          bool transparent_match);

/* PTEST with levels 1-7: from a completed table search.
 *
 * `function_code` is the one "specified by the PTEST instruction", whose FC2
 * bit decides whether an encountered S bit is a violation. */
[[nodiscard]] ap_m68030_mmusr_t
ap_m68030_mmusr_from_search(const ap_m68030_walk_result_t *result,
                            uint8_t function_code);

#endif /* APOLLO_CPU_M68030_AP_M68030_MMUSR_H */

/* MC68040 MMU registers: URP, SRP, TCR, the four TTRs and MMUSR.
 *
 * `MC68040 User's Manual (1993)` §3.1, Figures 3-3 through 3-6, read from the
 * page images.
 *
 * ## Where this part contradicts the 68851
 *
 * Both are memory management units and several of their rules are opposites,
 * so the risk in writing the third MMU in this project is carrying the second
 * one's assumptions into it. Three are worth naming:
 *
 *  - **Writing `TCR` does not flush the ATCs.** "The operating system must
 *    flush the ATCs before enabling address translation since the TCR accesses
 *    and reset do not flush the ATCs." The 68851 flushes its entire ATC on any
 *    `TC` write with the enable clear. Here the flush is software's job, and a
 *    model that helpfully flushed would hide the bug this warning exists for.
 *  - **`PFLUSH` works with translation disabled.** "The MMU instruction,
 *    PFLUSH, can be executed successfully despite the state of the E-bit." The
 *    68851 "terminates all PTEST, PLOAD, and CALLM/RTM instructions with an
 *    exception" when its `E` is clear.
 *  - **Reset does not clear the page size.** "A reset operation does not affect
 *    this bit. The bit must be initialized after a reset", while `E` *is*
 *    cleared. So a reset leaves the part disabled with an unspecified page
 *    size -- the same shape as the 68851's breakpoint skip count surviving
 *    reset, and the same trap for a model that zeroes a struct.
 *
 * ## The transparent translation registers ignore the enable
 *
 * "The TTRs operate independently of the E-bit in the TCR and the state of the
 * MDIS signal." So a block can be transparently translated while paged
 * translation is off entirely, which is how a system reaches its own tables
 * before the MMU is running.
 */

#ifndef APOLLO_CPU_M68040_AP_M68040_REGS_H
#define APOLLO_CPU_M68040_AP_M68040_REGS_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68040/ap_m68040_descriptor.h"

/* ---------------------------------------------------------------------------
 * URP and SRP, Figure 3-3.
 * ------------------------------------------------------------------------- */

/* "Bits 8-0 of an address loaded into the URP or the SRP must be zero" -- a
 * root table is 512 bytes, so it is 512-byte aligned. */
#define AP_M68040_ROOT_POINTER_MASK 0xFFFFFE00u

[[nodiscard]] uint32_t ap_m68040_root_pointer(uint32_t value);

/* Whether a value is one the manual permits to be loaded. The low bits "must be
 * zero", which is a requirement on software rather than something the part
 * enforces -- so this is a query, not a fault. */
[[nodiscard]] bool ap_m68040_root_pointer_is_aligned(uint32_t value);

/* ---------------------------------------------------------------------------
 * TCR, Figure 3-4. Sixteen bits, of which two are implemented.
 * ------------------------------------------------------------------------- */

typedef struct {
  bool enable;                    /* E, bit 15 */
  ap_m68040_page_size_t page_size; /* P, bit 14 */
} ap_m68040_tcr_t;

/* "Bits 13-0 are undefined (reserved)" and "all unimplemented bits of this
 * register are read as zeros and must always be written as zeros". */
#define AP_M68040_TCR_IMPLEMENTED_MASK 0xC000u

[[nodiscard]] ap_m68040_tcr_t ap_m68040_tcr_decode(uint16_t value);
[[nodiscard]] uint16_t ap_m68040_tcr_encode(const ap_m68040_tcr_t *tcr);

/* ---------------------------------------------------------------------------
 * The transparent translation registers, Figure 3-5.
 * ------------------------------------------------------------------------- */

/* §3.1.3's `S` field: "specifies the way FC2 is used in matching an address".
 * Note that `1X` is one meaning in two encodings -- the low bit is a don't-care
 * only when the high bit is set. */
typedef enum {
  AP_M68040_TT_USER_ONLY = 0,       /* 00: match only if FC2 = 0 */
  AP_M68040_TT_SUPERVISOR_ONLY = 1, /* 01: match only if FC2 = 1 */
  AP_M68040_TT_ANY = 2,             /* 1X: ignore FC2 when matching */
} ap_m68040_tt_mode_t;

typedef struct {
  unsigned logical_base; /* bits 31-24, compared with A31-A24 */
  unsigned logical_mask; /* bits 23-16, a *mask*: a set bit is ignored */
  bool enable;           /* E, bit 15 */
  ap_m68040_tt_mode_t supervisor_mode; /* S, bits 14-13 */
  bool user_attribute_1;               /* U1, bit 9 */
  bool user_attribute_0;               /* U0, bit 8 */
  ap_m68040_cache_mode_t cache_mode;   /* CM, bits 6-5 */
  bool write_protect;                  /* W, bit 2 */
} ap_m68040_ttr_t;

/* "Bits 12-10, 7, 4, 3, 1, and 0 always read as zero." */
#define AP_M68040_TTR_IMPLEMENTED_MASK 0xFFFFE364u

[[nodiscard]] ap_m68040_ttr_t ap_m68040_ttr_decode(uint32_t value);
[[nodiscard]] uint32_t ap_m68040_ttr_encode(const ap_m68040_ttr_t *ttr);

/* Whether an address and function code fall in this register's block.
 * Disabled registers match nothing; a set mask bit makes the corresponding
 * base bit irrelevant, which is how blocks larger than 16 Mbytes are named. */
[[nodiscard]] bool ap_m68040_ttr_matches(const ap_m68040_ttr_t *ttr,
                                         uint32_t address,
                                         unsigned function_code);

/* ---------------------------------------------------------------------------
 * MMUSR, Figure 3-6: what `PTEST` leaves behind.
 * ------------------------------------------------------------------------- */

typedef struct {
  uint32_t physical_address; /* bits 31-12 */
  bool bus_error;            /* B, bit 11 */
  bool global;               /* G, bit 10 */
  bool user_attribute_1;     /* U1, bit 9 */
  bool user_attribute_0;     /* U0, bit 8 */
  bool supervisor;           /* S, bit 7 */
  ap_m68040_cache_mode_t cache_mode; /* CM, bits 6-5 */
  bool modified;             /* M, bit 4 */
  /* Bit 3 is a reserved zero. The manual's field list runs in descending bit
   * order -- B, G, U1, U0, S, CM, M, W, T, R -- and skips it, so the glyph
   * between `M` and `W` in Figure 3-6 is a zero and not a field named `O`. */
  bool write_protect;        /* W, bit 2 */
  bool transparent;          /* T, bit 1 */
  bool resident;             /* R, bit 0 */
} ap_m68040_mmusr_t;

/* Bit 3 excluded. */
#define AP_M68040_MMUSR_IMPLEMENTED_MASK 0xFFFFFFF7u

[[nodiscard]] ap_m68040_mmusr_t ap_m68040_mmusr_decode(uint32_t value);
[[nodiscard]] uint32_t ap_m68040_mmusr_encode(const ap_m68040_mmusr_t *mmusr);

/* Two of the status bits suppress every other: "if the B-bit is set, all other
 * bits are zero", and "if the T-bit is set, then the PTEST address matches an
 * instruction or data TTR, the R-bit is set, and all other bits are zero".
 *
 * So a bus error and a transparent hit are not flags among flags -- each is a
 * *whole* answer, and reporting them alongside a physical address or a cache
 * mode would invent information the hardware did not return. */
[[nodiscard]] ap_m68040_mmusr_t ap_m68040_mmusr_bus_error(void);
[[nodiscard]] ap_m68040_mmusr_t ap_m68040_mmusr_transparent(void);

#endif /* APOLLO_CPU_M68040_AP_M68040_REGS_H */

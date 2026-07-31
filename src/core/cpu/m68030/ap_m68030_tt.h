/* MC68030 transparent translation registers TT0 and TT1.
 *
 * `[030]` §9.3 (p. 9-16) and §9.7.3 (p. 9-57), cited throughout.
 *
 * "Two independent transparent translation registers (TT0 and TT1) in the MMU
 * optionally define two blocks of the logical address space that are directly
 * translated to the physical address spaces." A transparently translated
 * logical address "is used as a physical address, without modification and
 * without protection checking."
 *
 * This matters to this machine specifically. The DN3500 boot PROM runs before
 * any translation tree exists, and Domain/OS maps I/O space through it, so the
 * TTx path is on the critical path of a boot rather than an optional extra.
 *
 * ## Deliberately modelled as decoded fields, not a packed 32-bit register
 *
 * `[030]` Figure 9-37 gives the register's bit layout, and the bit positions of
 * E, CI, R/W, RWM, FC BASE and FC MASK **did not survive the scan** -- the
 * figure's lower half OCRs to nothing but a stray "FC MASK". The upper half is
 * legible (31-24 logical address base, 23-16 logical address mask) and the
 * manual describes every field's *meaning* in prose, which is what this module
 * implements.
 *
 * So the semantics are transcribed and the packing is not, rather than the
 * packing being guessed. Nothing needs it yet: the layout is only required when
 * software writes the register with PMOVE, which is a later item. It is a named
 * tail in `docs/COMPLETION_PLAN.md`.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_TT_H
#define APOLLO_CPU_M68030_AP_M68030_TT_H

#include <stdbool.h>
#include <stdint.h>

/* One transparent translation register, as decoded fields.
 *
 * The comparison uses only the eight high-order address bits, which is why the
 * smallest block a TTx register can describe is 16 Mbytes ("The blocks of
 * addresses defined by the TTx registers include at least 16M bytes of logical
 * address space"): masking no bits still selects a whole A31-A24 block. */
typedef struct {
  uint8_t logical_base; /* value of A31-A24 defining the block */
  uint8_t logical_mask; /* bits of A31-A24 to ignore in the comparison */
  uint8_t fc_base;      /* 3-bit base function code */
  uint8_t fc_mask;      /* 3-bit mask over fc_base */

  bool enabled;          /* E. "A disabled TTx register is completely ignored." */
  bool cache_inhibit;    /* CI, drives CIOUT on a match */
  bool read_transparent; /* R/W: true = read accesses, false = write accesses */
  bool ignore_read_write; /* RWM: true = the R/W field is ignored */
} ap_m68030_tt_t;

/* The access being translated. `read_modify_write` marks a cycle belonging to
 * an indivisible read-modify-write operation, which the TTx rules treat
 * specially rather than as an ordinary read or write. */
typedef struct {
  uint32_t address;
  uint8_t function_code;
  bool read;
  bool read_modify_write;
} ap_m68030_access_t;

typedef struct {
  bool transparent;     /* the access bypassed the translation tables */
  uint32_t physical;    /* equals the logical address when transparent */
  bool cache_inhibit;   /* CIOUT */
} ap_m68030_tt_result_t;

/* True when this one register matches the access, ignoring the other. */
[[nodiscard]] bool ap_m68030_tt_matches(const ap_m68030_tt_t *tt,
                                        const ap_m68030_access_t *access);

/* Translate through TT0 and TT1 together.
 *
 * "For an access, if either of these registers match, the access is
 * transparently translated. If both registers match, the CI bits are ORed
 * together to generate the CIOUT signal." Either pointer may be NULL, meaning
 * that register is not present. */
[[nodiscard]] ap_m68030_tt_result_t
ap_m68030_tt_translate(const ap_m68030_tt_t *tt0, const ap_m68030_tt_t *tt1,
                       const ap_m68030_access_t *access);

#endif /* APOLLO_CPU_M68030_AP_M68030_TT_H */

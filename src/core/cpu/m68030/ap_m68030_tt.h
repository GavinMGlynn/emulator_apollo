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
 * ## Modelled as decoded fields, and now packed as well
 *
 * `[030]` Figure 9-37's lower half **did not survive the scan** -- the bit
 * positions of E, CI, R/W, RWM, FC BASE and FC MASK OCR to nothing but a stray
 * "FC MASK". Only the upper half is legible there (31-24 address base, 23-16
 * address mask), so this module deliberately carried decoded fields and *no*
 * packing for as long as the layout was unknown: a layout is transcribed or
 * deferred, never guessed.
 *
 * It is transcribed now. The `M68000 Family Programmer's Reference Manual`
 * (1992) Figure 1-9 gives the MC68030 register intact:
 *
 *     31-24 ADDRESS BASE   23-16 ADDRESS MASK
 *     E(15) 0(14-11) CI(10) R/W(9) RWM(8) 0(7) FC BASE(6-4) 0(3) FC MASK(2-0)
 *
 * and its prose agrees field for field with `[030]` §9.7.3's, which is what
 * makes this a transcription from two agreeing sources rather than a
 * reconstruction. Note the sense of the two R/W bits, which is easy to invert:
 * "R/W: 0 = Only write accesses permitted, 1 = Only read accesses permitted",
 * and "R/WM: 0 = R/W field used, 1 = R/W field ignored".
 *
 * The decoded struct remains the interface every other module uses; the packing
 * exists because `PMOVE` moves a 32-bit register image, not a struct.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_TT_H
#define APOLLO_CPU_M68030_AP_M68030_TT_H

#include <stdbool.h>
#include <stdint.h>

/* Bit positions, from the PRM's Figure 1-9. Bits 14-11, 7 and 3 carry no
 * field and are shown as zero. */
#define AP_M68030_TT_ADDRESS_BASE_SHIFT 24u
#define AP_M68030_TT_ADDRESS_MASK_SHIFT 16u
#define AP_M68030_TT_E_BIT 15u
#define AP_M68030_TT_CI_BIT 10u
#define AP_M68030_TT_RW_BIT 9u
#define AP_M68030_TT_RWM_BIT 8u
#define AP_M68030_TT_FC_BASE_SHIFT 4u
#define AP_M68030_TT_FC_MASK_SHIFT 0u
#define AP_M68030_TT_FC_FIELD_MASK 0x7u /* both function code fields are 3 bits */

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

/* Convert between the decoded fields and the 32-bit register image `PMOVE`
 * moves. The bits Figure 1-9 shows as zero are written as zero and ignored on
 * unpack, so a round trip through a register image is lossless for every field
 * the register actually has. */
[[nodiscard]] uint32_t ap_m68030_tt_pack(const ap_m68030_tt_t *tt);
[[nodiscard]] ap_m68030_tt_t ap_m68030_tt_unpack(uint32_t word);

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

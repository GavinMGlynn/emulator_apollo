/* MC68030 arithmetic and logic families: 1000, 1001, 1011, 1100 and 1101.
 *
 * `M68000 Family Programmer's Reference Manual 1992` §8.2 and each
 * instruction's page. OR/DIV/SBCD, SUB/SUBX, CMP/EOR, AND/MUL/ABCD/EXG and
 * ADD/ADDX. Five families with one shape, which is why they are one module.
 *
 * ## The shared shape
 *
 *     15-12 family | 11-9 register | 8-6 opmode | 5-0 effective address
 *
 * and the opmode means the same thing in all five:
 *
 *     000 001 010   <ea> op Dn -> Dn, at byte, word and long
 *     011           the *word* wide form
 *     100 101 110   Dn op <ea> -> <ea>, at byte, word and long
 *     111           the *long* wide form
 *
 * What the two wide forms are depends on the family: `1000` and `1100` use them
 * for DIVU/DIVS and MULU/MULS, while `1001`, `1011` and `1101` use them for the
 * address-register forms SUBA, CMPA and ADDA. So opmode `011` is a *word* DIVU
 * in one family and a *word* SUBA in another -- the position is shared, the
 * meaning is not.
 *
 * ## The register-register forms are holes, again
 *
 * The `100`-`110` opmodes write their result to the effective address, so a
 * data or address register destination is not a memory destination and those
 * modes are free. Each family fills them differently:
 *
 *     1001 SUBX      1101 ADDX      1100 opmode 100 ABCD
 *     1011 CMPM      1100 opmodes 101/110 EXG
 *
 * This is the same idiom as SWAP inside PEA and MOVEP inside the bit
 * operations, now appearing in five families at once.
 *
 * ## EOR is not where symmetry suggests
 *
 * `1011` is "CMP/EOR", and they do not overlap: CMP takes opmodes `000`-`010`
 * and `011`/`111`, EOR takes `100`-`110`. There is no `<ea> EOR Dn -> Dn`
 * direction at all, which is why EOR's operand order looks backwards compared
 * with OR and AND.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_ARITH_H
#define APOLLO_CPU_M68030_AP_M68030_ARITH_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_ea.h"

typedef enum {
  AP_M68030_ARITH_OR,
  AP_M68030_ARITH_AND,
  AP_M68030_ARITH_SUB,
  AP_M68030_ARITH_ADD,
  AP_M68030_ARITH_CMP,
  AP_M68030_ARITH_EOR,
  AP_M68030_ARITH_SUBA,
  AP_M68030_ARITH_ADDA,
  AP_M68030_ARITH_CMPA,
  AP_M68030_ARITH_DIVU,
  AP_M68030_ARITH_DIVS,
  AP_M68030_ARITH_MULU,
  AP_M68030_ARITH_MULS,
  AP_M68030_ARITH_SUBX,
  AP_M68030_ARITH_ADDX,
  AP_M68030_ARITH_ABCD,
  AP_M68030_ARITH_SBCD,
  AP_M68030_ARITH_CMPM,
  AP_M68030_ARITH_EXG,
  AP_M68030_ARITH_INVALID,
} ap_m68030_arith_kind_t;

/* Which of EXG's three exchanges, from the opmode field: "01000 -- Data
 * registers, 01001 -- Address registers, 10001 -- Data register and address
 * register". The struct's `memory_operands` cannot express this on its own:
 * both the address-address and the data-address forms have bit 3 set, and they
 * are distinguished only by the opmode above it. */
typedef enum {
  AP_M68030_EXG_NONE = 0, /* not an EXG */
  AP_M68030_EXG_DATA,     /* EXG Dx,Dy */
  AP_M68030_EXG_ADDRESS,  /* EXG Ax,Ay */
  AP_M68030_EXG_MIXED,    /* EXG Dx,Ay -- Rx is always the data register */
} ap_m68030_exg_mode_t;

typedef struct {
  ap_m68030_arith_kind_t kind;
  unsigned size;             /* operand size in bytes, 0 where not applicable */
  unsigned reg;              /* the register field, bits 11-9 */
  bool to_effective_address; /* the 100-110 direction */
  bool memory_operands;      /* the register-register forms' R/M bit */
  unsigned source_reg;       /* those forms' second register */
  ap_m68030_exg_mode_t exg;  /* which exchange, for EXG only */
  ap_m68030_ea_t ea;
} ap_m68030_arith_t;

[[nodiscard]] ap_m68030_arith_t ap_m68030_arith_decode(uint16_t instruction);

#endif /* APOLLO_CPU_M68030_AP_M68030_ARITH_H */

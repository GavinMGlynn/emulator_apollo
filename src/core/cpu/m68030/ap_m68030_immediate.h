/* MC68030 family 0000: immediate, bit manipulation and MOVEP.
 *
 * `M68000 Family Programmer's Reference Manual 1992` §8.2 and each
 * instruction's page. Table 8-2 calls this family "Bit Manipulation/MOVEP/
 * Immediate", and the three really do share one space.
 *
 * ## Bit 8 splits the family in two
 *
 * Clear, and bits 11-9 select the immediate row: ORI, ANDI, SUBI, ADDI, the
 * *static* bit operations, EORI, CMPI, MOVES. Set, and the instruction is a
 * *dynamic* bit operation or MOVEP, with bits 11-9 naming a data register
 * instead.
 *
 * ## MOVEP is inside the dynamic bit operations
 *
 * The dynamic bit instructions use opmodes `100`-`111` for BTST, BCHG, BCLR and
 * BSET; MOVEP uses **the same four**. They are told apart only by the effective
 * address mode: a bit operation cannot address an address register, so mode
 * `001` in that space is MOVEP. This is the same idiom as SWAP inside PEA and
 * EXT inside MOVEM, and here the overlap is total rather than partial -- there
 * is no opmode that is MOVEP and not also a bit operation.
 *
 * ## The immediate-destination encoding is the escape, again
 *
 * `ORI`, `ANDI` and `EORI` have forms that target CCR and SR, and they are not
 * separate opcodes: `$003C` is `ORI` with a *byte* size and an immediate
 * destination, `$007C` the same with a *word* size. An immediate destination is
 * meaningless -- there is nowhere to write -- so the encoding is free, and the
 * size field chooses between CCR and SR. `SUBI`, `ADDI` and `CMPI` have no such
 * forms, so for them that encoding stays unassigned.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_IMMEDIATE_H
#define APOLLO_CPU_M68030_AP_M68030_IMMEDIATE_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_ea.h"

typedef enum {
  AP_M68030_IMM_ORI,
  AP_M68030_IMM_ANDI,
  AP_M68030_IMM_SUBI,
  AP_M68030_IMM_ADDI,
  AP_M68030_IMM_EORI,
  AP_M68030_IMM_CMPI,
  AP_M68030_IMM_MOVES,
  AP_M68030_IMM_ORI_TO_CCR,
  AP_M68030_IMM_ORI_TO_SR,
  AP_M68030_IMM_ANDI_TO_CCR,
  AP_M68030_IMM_ANDI_TO_SR,
  AP_M68030_IMM_EORI_TO_CCR,
  AP_M68030_IMM_EORI_TO_SR,
  AP_M68030_IMM_BTST,
  AP_M68030_IMM_BCHG,
  AP_M68030_IMM_BCLR,
  AP_M68030_IMM_BSET,
  AP_M68030_IMM_MOVEP,
  AP_M68030_IMM_INVALID,
} ap_m68030_immediate_kind_t;

typedef struct {
  ap_m68030_immediate_kind_t kind;
  unsigned size;      /* operand size in bytes for the sized forms, else 0 */
  bool dynamic;       /* bit operations: the bit number is in a register */
  unsigned reg;       /* dynamic bit number register, or MOVEP's data register */
  unsigned address_register; /* MOVEP only */
  bool movep_to_memory;      /* MOVEP direction */
  ap_m68030_ea_t ea;
} ap_m68030_immediate_t;

[[nodiscard]] ap_m68030_immediate_t
ap_m68030_immediate_decode(uint16_t instruction);

/* The SR forms write the status register's system byte, so they are
 * supervisor-only; the CCR forms are not. */
[[nodiscard]] bool ap_m68030_immediate_privileged(
    ap_m68030_immediate_kind_t kind);

#endif /* APOLLO_CPU_M68030_AP_M68030_IMMEDIATE_H */

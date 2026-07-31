/* MC68030 MOVE and MOVEA: families 0001, 0010 and 0011.
 *
 * `M68000 Family Programmer's Reference Manual 1992` §8.2 and the MOVE and
 * MOVEA pages. Three families of the operation code map, one per operand size.
 *
 * ## The destination field is reversed
 *
 * The source is `MODE` then `REGISTER`, as every other instruction in the set
 * writes it. The destination is **`REGISTER` then `MODE`**:
 *
 *     15 14 | 13 12 | 11 10  9 |  8  7  6 |  5  4  3 |  2  1  0
 *      0  0 | SIZE  | DEST REG | DEST MODE| SRC MODE | SRC REG
 *
 * A decoder that reads the destination the same way round as the source gets a
 * plausible wrong answer rather than a fault -- `MOVE.L D0,(A1)` becomes
 * `MOVE.L D0,D1` or similar, depending on the bits. Nothing about the resulting
 * instruction looks malformed.
 *
 * ## The size encoding is the opcode map's, not a natural one
 *
 * `01` is byte, `10` is **long**, `11` is **word** -- the same out-of-order
 * assignment the operation code map gives families 0001, 0010 and 0011, and for
 * the same reason: these bits *are* the low half of the family number. `00`
 * belongs to family 0000 and is not a MOVE at all.
 *
 * ## MOVEA is MOVE with an address register destination
 *
 * "MOVEA ... 0 0 1" in the destination mode field. It is not a separate
 * encoding but the one destination mode that changes the instruction's
 * behaviour: MOVEA does not affect the condition codes, and sign-extends a word
 * source to the full register. There is no byte MOVEA.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_MOVE_H
#define APOLLO_CPU_M68030_AP_M68030_MOVE_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_ea.h"

typedef enum {
  AP_M68030_MOVE_ORDINARY,
  AP_M68030_MOVE_TO_ADDRESS_REGISTER, /* MOVEA */
  AP_M68030_MOVE_INVALID,
} ap_m68030_move_kind_t;

typedef struct {
  ap_m68030_move_kind_t kind;
  unsigned size;              /* operand size in bytes: 1, 2 or 4 */
  ap_m68030_ea_t source;
  ap_m68030_ea_t destination;
} ap_m68030_move_t;

[[nodiscard]] ap_m68030_move_t ap_m68030_move_decode(uint16_t instruction);

/* "MOVEA ... does not affect the condition codes", unlike MOVE which sets N and
 * Z from the operand and clears V and C. */
[[nodiscard]] bool ap_m68030_move_affects_condition_codes(
    const ap_m68030_move_t *move);

#endif /* APOLLO_CPU_M68030_AP_M68030_MOVE_H */

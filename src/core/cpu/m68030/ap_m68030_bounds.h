/* MC68030 family 0000, size field 11: CMP2, CHK2, CAS and CAS2.
 *
 * `M68000 Family Programmer's Reference Manual 1992`, the CMP2, CHK2 and
 * CAS/CAS2 pages.
 *
 * These four occupy the escape that opens when the immediate instructions'
 * size field reads `11`. The rest of family 0000 is `ORI`/`ANDI`/`SUBI`/`ADDI`/
 * `EORI`/`CMPI`/`MOVES` at sizes 00, 01 and 10, plus the bit operations and
 * `MOVEP`; `11` is not a wider operand there but a different instruction, and
 * treating it as one is how a decoder ends up running `CAS` as a long `ORI`.
 *
 * ## Bit 11 splits the escape in two
 *
 *     0000 0 SIZE 011 <ea>   CMP2 and CHK2
 *     0000 1 SIZE 011 <ea>   CAS, and CAS2 when <ea> reads 111100
 *
 * ## The two halves count sizes differently
 *
 * This is the trap. CMP2 and CHK2 use the family's ordinary encoding —
 * "00 Byte, 01 Word, 10 Long" — while CAS uses "01 Byte, 10 Word, 11 Long",
 * one higher throughout. So the same three bits in the same position mean a
 * byte in one half and a word in the other, and a decoder that read the size
 * once for the whole escape would give every CAS the wrong operand width. The
 * unassigned value differs too: `11` for CMP2/CHK2, `00` for CAS.
 *
 * ## CMP2 and CHK2 are the same instruction twice
 *
 * "This instruction is identical to CHK2 except that it sets condition codes
 * rather than taking an exception when the value in Rn is out of bounds", and
 * they are told apart by one bit of the *extension* word — bit 11 — not by
 * anything in the instruction word. A decoder reading only the instruction word
 * cannot distinguish them at all.
 *
 * ## CAS2's effective address field is not an address
 *
 * `111100` in the low six bits is the immediate mode's encoding, which is not
 * legal for CAS — so the pattern is free, and CAS2 uses it as an escape. CAS2
 * then takes two extension words naming two independent memory operands, which
 * is what lets it swap the two ends of a linked list atomically.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_BOUNDS_H
#define APOLLO_CPU_M68030_AP_M68030_BOUNDS_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_ea.h"

typedef enum {
  AP_M68030_BOUNDS_CMP2,
  AP_M68030_BOUNDS_CHK2,
  AP_M68030_BOUNDS_CAS,
  AP_M68030_BOUNDS_CAS2,
  AP_M68030_BOUNDS_INVALID,
} ap_m68030_bounds_kind_t;

typedef struct {
  ap_m68030_bounds_kind_t kind;
  unsigned size;     /* operand size in bytes: 1, 2 or 4 */
  ap_m68030_ea_t ea; /* CMP2/CHK2's bounds pair, CAS's memory operand */
} ap_m68030_bounds_t;

/* True when the instruction word is in family 0000's size-11 escape at all,
 * which is what the immediate decoder tests before handing over. */
[[nodiscard]] bool ap_m68030_bounds_matches(uint16_t instruction);

/* Decode the instruction word. CMP2 and CHK2 both report `CMP2` here, since the
 * bit that separates them lives in the extension word — `ap_m68030_bounds_kind`
 * below resolves the pair once that word has been read, which is the same
 * two-stage shape the indexed addressing modes already need. */
[[nodiscard]] ap_m68030_bounds_t ap_m68030_bounds_decode(uint16_t instruction);

/* Resolve CMP2 against CHK2 from the extension word's bit 11: "0 — CMP2,
 * 1 — CHK2" by the two pages' formats. Returns the kind unchanged for anything
 * else, so it is safe to call on every decode. */
[[nodiscard]] ap_m68030_bounds_kind_t
ap_m68030_bounds_kind(const ap_m68030_bounds_t *bounds, uint16_t extension);

/* CMP2/CHK2's extension word names the register to check: bit 15 is D/A and
 * bits 14-12 the number. */
[[nodiscard]] bool ap_m68030_bounds_register_is_address(uint16_t extension);
[[nodiscard]] unsigned ap_m68030_bounds_register(uint16_t extension);

/* CAS's extension word names two data registers: "Du field — Specifies the data
 * register that contains the update value", bits 8-6, and "Dc field —
 * Specifies the data register that contains the value to be compared", bits
 * 2-0. */
[[nodiscard]] unsigned ap_m68030_cas_update_register(uint16_t extension);
[[nodiscard]] unsigned ap_m68030_cas_compare_register(uint16_t extension);

/* How many extension words the instruction carries: one for CMP2/CHK2 and CAS,
 * two for CAS2, which names two independent operands. */
[[nodiscard]] unsigned ap_m68030_bounds_length(const ap_m68030_bounds_t *bounds);

#endif /* APOLLO_CPU_M68030_AP_M68030_BOUNDS_H */

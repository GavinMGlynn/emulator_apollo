/* MC68030 integer ALU: results and condition codes.
 *
 * `M68000 Family Programmer's Reference Manual 1992` Table 3-18, "Integer Unit
 * Condition Code Computations".
 *
 * ## The table's overbars are lost, so the formulas are checked rather than read
 *
 * Table 3-18 writes its V and C definitions with overbars for negation, and the
 * scan loses them exactly as it lost Table 3-19's -- `ADD`'s overflow comes out
 * as "V = Sm Λ Dm Λ Rm V Sm Λ Dm Λ Rm", which is unreadable as written since
 * both halves are then identical.
 *
 * So the arithmetic here is not transcribed from those formulas. It is written
 * in the standard equivalent form -- carry from the unsigned sum exceeding the
 * operand width, overflow from the operands agreeing in sign while the result
 * differs -- and then **verified exhaustively**: `alu_suite` checks all 65536
 * byte operand pairs against a reference computed independently in wider
 * arithmetic. A misplaced overbar cannot survive that, and neither can a
 * plausible-looking formula that is subtly wrong at one boundary.
 *
 * ## Subtraction is destination minus source, and the operand order matters
 *
 * `SUB` computes "Destination - Source -> Destination". Getting the order
 * backwards produces a result that is merely negated, which looks almost right,
 * but the carry and overflow flags come out wrong in ways that only show up in
 * a conditional branch much later.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_ALU_H
#define APOLLO_CPU_M68030_AP_M68030_ALU_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_shift.h"

typedef struct {
  uint32_t result; /* truncated to the operand size */
  bool n;
  bool z;
  bool v;
  bool c;
  bool x;          /* equals c for the operations that set it */
  bool sets_x;     /* whether X is affected at all */
} ap_m68030_alu_result_t;

/* "Destination + Source -> Destination". */
[[nodiscard]] ap_m68030_alu_result_t
ap_m68030_alu_add(uint32_t destination, uint32_t source, unsigned size);

/* "Destination - Source -> Destination", in that order. */
[[nodiscard]] ap_m68030_alu_result_t
ap_m68030_alu_sub(uint32_t destination, uint32_t source, unsigned size);

/* CMP is a subtraction whose result is discarded and which does not affect X --
 * the only difference from SUB, and the reason it is a separate entry point. */
[[nodiscard]] ap_m68030_alu_result_t
ap_m68030_alu_cmp(uint32_t destination, uint32_t source, unsigned size);

/* AND, OR and EOR: "X — [not affected], N *, Z *, V 0, C 0". */
[[nodiscard]] ap_m68030_alu_result_t
ap_m68030_alu_and(uint32_t destination, uint32_t source, unsigned size);
[[nodiscard]] ap_m68030_alu_result_t
ap_m68030_alu_or(uint32_t destination, uint32_t source, unsigned size);
[[nodiscard]] ap_m68030_alu_result_t
ap_m68030_alu_eor(uint32_t destination, uint32_t source, unsigned size);

/* NEG: "0 - Destination". Table 3-18 gives it "V = Dm Λ Rm, C = Dm V Rm",
 * which is exactly what subtracting from zero produces -- so it is that
 * subtraction rather than a second formula that could disagree with it. */
[[nodiscard]] ap_m68030_alu_result_t
ap_m68030_alu_neg(uint32_t destination, unsigned size);

/* NOT: the ones complement. "X -, N *, Z *, V 0, C 0" -- a logical operation
 * for condition code purposes despite looking arithmetic. */
[[nodiscard]] ap_m68030_alu_result_t
ap_m68030_alu_not(uint32_t destination, unsigned size);

/* TST and CLR set their flags from a value without combining two operands. */
[[nodiscard]] ap_m68030_alu_result_t
ap_m68030_alu_test(uint32_t value, unsigned size);

/* Shifts and rotates, Table 3-18's continued page.
 *
 * Three rules carry most of the weight, and each is a place a plausible
 * implementation goes wrong silently:
 *
 *   **A count of zero is not a no-op.** It leaves X alone and clears V and C --
 *   except for `ROXL`/`ROXR`, where the table gives "C ?  X=C", so C takes X's
 *   value rather than zero. A model that returned early on a zero count would
 *   be right four times out of six.
 *
 *   **Only the arithmetic left shift sets V.** Every other shift and rotate has
 *   a plain `0` in that column. V is set "if the most significant bit is changed
 *   at any time during the shift", which is not the same as the sign differing
 *   at the end -- a value that shifts its sign out and back in sets V even
 *   though it finishes with the sign it started with.
 *
 *   **The rotates split on X.** `ROL`/`ROR` have an em dash under X and do not
 *   touch it; `ROXL`/`ROXR` rotate *through* it, so X is both an input and an
 *   output. Treating all four alike breaks multi-precision shifts, which are
 *   the reason the extend forms exist.
 *
 * `x_in` supplies the current X for the rotate-with-extend forms and for the
 * zero-count case; it is ignored by the others. */
[[nodiscard]] ap_m68030_alu_result_t
ap_m68030_alu_shift(ap_m68030_shift_type_t type, bool left, uint32_t value,
                    unsigned count, unsigned size, bool x_in);

/* Fold a result's flags into a condition code register value, leaving X alone
 * for the operations that do not affect it. */
[[nodiscard]] uint16_t ap_m68030_alu_apply(uint16_t ccr,
                                           const ap_m68030_alu_result_t *r);

#endif /* APOLLO_CPU_M68030_AP_M68030_ALU_H */

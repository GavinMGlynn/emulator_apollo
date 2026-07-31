/* MC68030 operand access: reading and writing through a decoded effective
 * address.
 *
 * `M68000 Family Programmer's Reference Manual 1992`, per instruction. This is
 * the layer between `ap_m68030_addr` (which computes an address) and
 * `ap_m68030_access` (which reaches memory), and it exists mostly to hold two
 * register rules that differ from each other in a way that is easy to get
 * backwards.
 *
 * ## A data register write is partial; an address register write never is
 *
 * A byte or word operation on a **data** register writes only the low 8 or 16
 * bits and leaves the rest of the register alone. A byte or word operation on
 * an **address** register does not exist for bytes at all, and for words "the
 * source operand is sign-extended to a long operand and the operation is
 * performed on the address register using all 32 bits."
 *
 * So `MOVE.W #$FFFF,D0` leaves D0's upper half untouched, while
 * `MOVEA.W #$FFFF,A0` sets A0 to `$FFFFFFFF`. Applying the data register rule
 * to an address register leaves a stale upper half that later long operations
 * silently use; applying the address register rule to a data register destroys
 * data the program still needs. Neither faults.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_OPERAND_H
#define APOLLO_CPU_M68030_AP_M68030_OPERAND_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_access.h"
#include "cpu/m68030/ap_m68030_addr.h"

typedef struct {
  bool ok;
  uint32_t value;  /* zero-extended; the caller interprets it by size */
  uint32_t clocks;
  bool fault;
} ap_m68030_operand_result_t;

/* Read an operand of `size` bytes from an already-calculated address. */
[[nodiscard]] ap_m68030_operand_result_t
ap_m68030_operand_read(ap_m68030_regs_t *regs, ap_m68030_access_ctx_t *access,
                       const ap_m68030_address_t *where, unsigned size,
                       uint8_t function_code);

/* Write an operand of `size` bytes. */
[[nodiscard]] ap_m68030_operand_result_t
ap_m68030_operand_write(ap_m68030_regs_t *regs, ap_m68030_access_ctx_t *access,
                        const ap_m68030_address_t *where, unsigned size,
                        uint32_t value, uint8_t function_code);

/* Sign-extend the low `size` bytes of a value to 32 bits, which several
 * instructions do explicitly and the address register write does implicitly. */
[[nodiscard]] uint32_t ap_m68030_sign_extend(uint32_t value, unsigned size);

#endif /* APOLLO_CPU_M68030_AP_M68030_OPERAND_H */

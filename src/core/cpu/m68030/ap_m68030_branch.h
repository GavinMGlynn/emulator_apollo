/* MC68030 branch family decode: Bcc, BSR and BRA.
 *
 * `M68000 Family Programmer's Reference Manual 1992` §8.2 and the Bcc, BRA and
 * BSR instruction pages. Family `0110` of the operation code map.
 *
 * ## Two different "next addresses", and they are not the same
 *
 * The PRM gives the branch as "PC + dn -> PC", where PC is the address of the
 * instruction *plus two* -- the position of the first extension word. But BSR
 * is "SP - 4 -> SP; PC -> (SP); PC + dn -> PC", and the PC it pushes is the
 * address of the instruction that follows, which is the instruction's whole
 * length away.
 *
 * For the 8-bit form those coincide, because the instruction is two bytes. For
 * the 16- and 32-bit forms they do **not**: the branch base stays at +2 while
 * the return address moves to +4 or +6. Computing the return address as "base"
 * gives a BSR that returns into its own displacement words.
 *
 * ## $00 and $FF are escapes, not displacements
 *
 * "16-BIT DISPLACEMENT IF 8-BIT DISPLACEMENT = $00" and "32-BIT DISPLACEMENT IF
 * 8-BIT DISPLACEMENT = $FF". So the 8-bit field cannot encode 0 or -1, and the
 * manual's own NOTE explains the visible consequence: "A branch to the
 * immediately following instruction automatically uses the 16-bit displacement
 * format because the 8-bit displacement field contains $00 (zero offset)."
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_BRANCH_H
#define APOLLO_CPU_M68030_AP_M68030_BRANCH_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_cond.h"

typedef enum {
  AP_M68030_BRANCH_8BIT,
  AP_M68030_BRANCH_16BIT,
  AP_M68030_BRANCH_32BIT,
} ap_m68030_branch_size_t;

typedef struct {
  ap_m68030_cond_t condition;
  ap_m68030_branch_size_t size;
  bool is_bra;          /* condition 0 -- unconditional, no return address */
  bool is_bsr;          /* condition 1 -- pushes a return address */
  int8_t displacement8; /* meaningful only for the 8-bit form */
} ap_m68030_branch_t;

/* Decode the instruction word alone. The 16- and 32-bit displacements live in
 * following words, which the caller fetches once it knows the size. */
[[nodiscard]] ap_m68030_branch_t ap_m68030_branch_decode(uint16_t instruction);

/* Total instruction length in bytes, including any displacement words. */
[[nodiscard]] unsigned ap_m68030_branch_length(const ap_m68030_branch_t *branch);

/* "PC + dn -> PC", with PC being the instruction address plus two -- the same
 * base for every displacement size. */
[[nodiscard]] uint32_t ap_m68030_branch_target(uint32_t instruction_address,
                                               int32_t displacement);

/* The address BSR pushes: the instruction that follows, which is the whole
 * instruction length away and *not* the branch base. */
[[nodiscard]] uint32_t ap_m68030_branch_return_address(
    uint32_t instruction_address, const ap_m68030_branch_t *branch);

#endif /* APOLLO_CPU_M68030_AP_M68030_BRANCH_H */

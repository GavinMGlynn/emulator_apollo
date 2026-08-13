/* MC68030 family 0100, the $4E group: control and return instructions.
 *
 * `M68000 Family Programmer's Reference Manual 1992` §8.2 and each
 * instruction's page. Family `0100` ("Miscellaneous") is the largest in the
 * map; this module covers its `0100 1110` subtree, which is the control group
 * -- TRAP, LINK, UNLK, MOVE USP, RESET, NOP, STOP, RTE, RTD, RTS, TRAPV, RTR --
 * together with JSR and JMP. The rest of family 0100 (LEA, PEA, MOVEM, CLR,
 * NEG, TST and the others) decodes separately.
 *
 * ## The subtree is a fixed prefix, then a widening field
 *
 * `0100 1110` fixes the top byte. Below it the encoding narrows in stages
 * rather than by one field: bits 7-6 pick JSR (`10`) and JMP (`11`) with a
 * six-bit effective address, while `01` opens the control group where bits 5-3
 * choose TRAP, LINK/UNLK, MOVE USP or the fully-decoded `$4E7x` singles.
 *
 * ## Six of these are privileged
 *
 * RESET, STOP, RTE, MOVE USP and both directions of MOVEC are supervisor-only. Executing one in user
 * state is a privilege violation, `[030]` Table 8-1 vector 8, and the vector
 * comes from `ap_m68030_exception.h` rather than being written again here.
 * Getting this wrong does not fail loudly: a user program would simply be able
 * to halt the processor or forge a return from exception.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_CONTROL_H
#define APOLLO_CPU_M68030_AP_M68030_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_ea.h"

typedef enum {
  AP_M68030_CTL_TRAP,
  AP_M68030_CTL_LINK,
  AP_M68030_CTL_UNLK,
  AP_M68030_CTL_MOVE_TO_USP,
  AP_M68030_CTL_MOVE_FROM_USP,
  AP_M68030_CTL_RESET,
  AP_M68030_CTL_NOP,
  AP_M68030_CTL_STOP,
  AP_M68030_CTL_RTE,
  AP_M68030_CTL_RTD,
  AP_M68030_CTL_RTS,
  AP_M68030_CTL_TRAPV,
  AP_M68030_CTL_RTR,
  AP_M68030_CTL_MOVEC_FROM_CONTROL, /* $4E7A */
  AP_M68030_CTL_MOVEC_TO_CONTROL,   /* $4E7B */
  AP_M68030_CTL_JSR,
  AP_M68030_CTL_JMP,
  AP_M68030_CTL_INVALID,
} ap_m68030_control_kind_t;

typedef struct {
  ap_m68030_control_kind_t kind;
  unsigned reg;      /* LINK, UNLK, MOVE USP: the address register */
  /* LINK only: `$4E5x` carries a 16-bit displacement and the 68020's `$480x`
   * form a 32-bit one. The two are the same instruction and differ in nothing
   * else, so they share a kind and this says which word count to read. */
  bool long_displacement;
  unsigned vector;   /* TRAP: the 4-bit vector number from the instruction */
  ap_m68030_ea_t ea; /* JSR and JMP */
} ap_m68030_control_t;

/* MOVEC's control register codes, `M68000PRM` MOVEC page. The table lists them
 * per part, and these are the MC68030's set: SFC/DFC/USP/VBR from the 68010,
 * plus CACR/CAAR/MSP/ISP from the 68020. The MMU registers are *not* here --
 * on this part they are reached by PMOVE, and the 68040's TC/ITTx/DTTx/MMUSR
 * codes belong to that part alone.
 *
 * The codes are deliberately not contiguous: bit 11 separates the two groups,
 * so $800 is not $002 with a different index. Treating the field as a small
 * dense number is how a model ends up putting the USP where CACR belongs. */
#define AP_M68030_CONTROL_SFC 0x000u
#define AP_M68030_CONTROL_DFC 0x001u
#define AP_M68030_CONTROL_CACR 0x002u
#define AP_M68030_CONTROL_USP 0x800u
#define AP_M68030_CONTROL_VBR 0x801u
#define AP_M68030_CONTROL_CAAR 0x802u
#define AP_M68030_CONTROL_MSP 0x803u
#define AP_M68030_CONTROL_ISP 0x804u

/* True when the instruction word is in the `0100 1110` subtree at all. */
[[nodiscard]] bool ap_m68030_control_matches(uint16_t instruction);

[[nodiscard]] ap_m68030_control_t ap_m68030_control_decode(uint16_t instruction);

/* Length in bytes before any effective address extension words. LINK.W and RTD
 * carry a 16-bit displacement and STOP an immediate word; the rest are two
 * bytes. */
[[nodiscard]] unsigned ap_m68030_control_length(
    const ap_m68030_control_t *control);

/* Whether this instruction may only be executed in supervisor state. */
[[nodiscard]] bool ap_m68030_control_privileged(ap_m68030_control_kind_t kind);

/* The exception vector `TRAP #n` takes. `[030]` Table 8-1 puts TRAP #0-15 at
 * vectors 32-47, so this is the instruction's 4-bit field offset by the trap
 * base rather than the field itself. */
[[nodiscard]] unsigned ap_m68030_control_trap_vector(
    const ap_m68030_control_t *control);

#endif /* APOLLO_CPU_M68030_AP_M68030_CONTROL_H */

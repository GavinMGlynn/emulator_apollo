/* MC68030 family 0100: LEA, CHK, and the $48/$4C subtree.
 *
 * `M68000 Family Programmer's Reference Manual 1992` §8.2 and each
 * instruction's page. This is the second subtree of family `0100`; the `$4E`
 * control group is `ap_m68030_control`, and CLR/NEG/NOT/TST and the
 * `MOVE to/from SR/CCR` group remain.
 *
 * ## The same trick as DBcc, three more times
 *
 * `PEA`, `MOVEM` and `LEA` each take only a subset of the addressing modes, and
 * the encodings they cannot use are occupied by other instructions rather than
 * wasted:
 *
 *   `PEA` pushes an address, so a data or address register source is
 *   meaningless — mode `000` there is `SWAP`, mode `001` is `BKPT`.
 *
 *   `MOVEM` moves registers to or from *memory*, so a data register operand is
 *   meaningless — mode `000` there is `EXT`.
 *
 * So decoding order matters: the register-direct cases must be recognised
 * before falling through to the instruction whose address space they sit in. A
 * decoder that checks `MOVEM` first will happily decode `EXT.W D3` as a
 * register-list move.
 *
 * ## LEA and CHK share the register-field form
 *
 * Both are `0100 rrr xxx EA` with bits 8-6 choosing: `111` is `LEA`, `110` is
 * `CHK.W`, `100` is `CHK.L`. The 68020 added the long form, and its opmode is
 * *below* the word form's rather than above it.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_MISC_H
#define APOLLO_CPU_M68030_AP_M68030_MISC_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_ea.h"

typedef enum {
  AP_M68030_MISC_LEA,
  AP_M68030_MISC_CHK_WORD,
  AP_M68030_MISC_CHK_LONG,
  AP_M68030_MISC_PEA,
  AP_M68030_MISC_SWAP,
  AP_M68030_MISC_BKPT,
  AP_M68030_MISC_EXT_WORD,  /* byte to word */
  AP_M68030_MISC_EXT_LONG,  /* word to long */
  AP_M68030_MISC_EXTB_LONG, /* byte to long, 68020 and later */
  AP_M68030_MISC_NBCD,
  AP_M68030_MISC_MOVEM_TO_MEMORY,
  AP_M68030_MISC_MOVEM_TO_REGISTERS,
  AP_M68030_MISC_INVALID,
} ap_m68030_misc_kind_t;

typedef struct {
  ap_m68030_misc_kind_t kind;
  unsigned reg;      /* LEA/CHK destination, SWAP/EXT operand, BKPT vector */
  unsigned size;     /* MOVEM transfer size in bytes: 2 or 4 */
  ap_m68030_ea_t ea; /* for the forms that take one */
} ap_m68030_misc_t;

[[nodiscard]] ap_m68030_misc_t ap_m68030_misc_decode(uint16_t instruction);

/* MOVEM is followed by a 16-bit register list mask; nothing else here carries a
 * following word. */
[[nodiscard]] unsigned ap_m68030_misc_length(const ap_m68030_misc_t *misc);

#endif /* APOLLO_CPU_M68030_AP_M68030_MISC_H */

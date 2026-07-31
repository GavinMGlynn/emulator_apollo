/* MC68030 family 1110: shifts, rotates and bit field instructions.
 *
 * `M68000 Family Programmer's Reference Manual 1992` §8.2 and each
 * instruction's page. The last of the integer families.
 *
 * ## One family, three shapes
 *
 * Bits 7-6 decide. Anything but `11` is a **register shift**, operating on a
 * data register at byte, word or long. `11` is not a size, so it selects the
 * other two shapes, and bit 11 then chooses: clear is a **memory shift**, which
 * shifts a word in memory by exactly one; set is a **bit field** instruction,
 * the 68020's addition, which takes a further extension word.
 *
 * That is the fourth distinct place in the encoding where an illegal size
 * selects something else, after `ADDQ`'s conditional group, `Bcc`'s
 * displacement escapes and the single-operand group's `MOVE to/from SR`.
 *
 * ## The type field moves between the two shift shapes
 *
 * Both forms choose between arithmetic, logical, rotate-with-extend and rotate,
 * and both use two bits in the same order — but **not the same two bits**. The
 * register form has the type at bits 4-3; the memory form has it at bits 11-9,
 * where the register form keeps its shift count. A decoder that reads one
 * position for both produces a working shift of the wrong kind.
 *
 * ## The count field's zero means eight, again
 *
 * "If i/r = 0, this field contains the shift count (1-7 represent counts of
 * 1-7; 0 represents a count of 8)." The same quirk as `ADDQ`'s quick data, and
 * the same consequence if passed through: a shift by 8 silently becomes a shift
 * by 0, which is an instruction that runs and does nothing.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_SHIFT_H
#define APOLLO_CPU_M68030_AP_M68030_SHIFT_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_ea.h"

/* The four shift kinds, in the order both type fields encode them. */
typedef enum {
  AP_M68030_SHIFT_ARITHMETIC = 0, /* ASL / ASR */
  AP_M68030_SHIFT_LOGICAL = 1,    /* LSL / LSR */
  AP_M68030_SHIFT_ROTATE_EXTEND = 2, /* ROXL / ROXR */
  AP_M68030_SHIFT_ROTATE = 3,     /* ROL / ROR */
} ap_m68030_shift_type_t;

typedef enum {
  AP_M68030_SHIFT_REGISTER, /* shifts a data register by a count */
  AP_M68030_SHIFT_MEMORY,   /* shifts a word in memory by one */
  AP_M68030_SHIFT_BITFIELD,
  AP_M68030_SHIFT_INVALID,
} ap_m68030_shift_form_t;

/* The eight bit field instructions, from bits 11-8. */
typedef enum {
  AP_M68030_BF_TST = 0x8,
  AP_M68030_BF_EXTU = 0x9,
  AP_M68030_BF_CHG = 0xA,
  AP_M68030_BF_EXTS = 0xB,
  AP_M68030_BF_CLR = 0xC,
  AP_M68030_BF_FFO = 0xD,
  AP_M68030_BF_SET = 0xE,
  AP_M68030_BF_INS = 0xF,
} ap_m68030_bitfield_t;

typedef struct {
  ap_m68030_shift_form_t form;
  ap_m68030_shift_type_t type;
  ap_m68030_bitfield_t bitfield;

  bool left;          /* dr: set is left, clear is right */
  bool count_in_register; /* i/r */
  unsigned count;     /* immediate count 1-8, or the register number */
  unsigned size;      /* register form operand size; memory form is always 2 */
  unsigned reg;       /* the register being shifted, register form */
  ap_m68030_ea_t ea;  /* memory and bit field forms */
} ap_m68030_shift_t;

[[nodiscard]] ap_m68030_shift_t ap_m68030_shift_decode(uint16_t instruction);

/* Bit field instructions carry one extension word describing offset and width;
 * BFINS and the extract forms name a register in it too. */
[[nodiscard]] unsigned ap_m68030_shift_length(const ap_m68030_shift_t *shift);

#endif /* APOLLO_CPU_M68030_AP_M68030_SHIFT_H */

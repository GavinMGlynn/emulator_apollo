/* MC68030 effective address: mode encodings and extension word formats.
 *
 * `M68000 Family Programmer's Reference Manual 1992` §2, Figure 2-2 and Tables
 * 2-1, 2-2 and 2-4, all of which survive intact.
 *
 * This is the decode, not the calculation. Turning a decoded extension word
 * into an address means reading registers and, for the memory indirect modes,
 * performing bus cycles — so it belongs with the instruction unit, the same
 * split that kept `ap_m68030_cache`'s structure separate from its cost.
 *
 * ## Mode 7 is not a register number
 *
 * Modes 000-110 use the register field as a register number. Mode **111** uses
 * it as a sub-opcode: `000` is absolute short, `001` absolute long, `010` PC
 * with displacement, `011` PC with index, `100` immediate. Treating mode 7's
 * register field as a register is the classic 68000 decode bug, and it is why
 * this module reports a *kind* rather than handing back (mode, register).
 *
 * ## Two extension word formats, distinguished by one bit
 *
 * Bit 8 selects: clear is the brief format, whose low byte is an 8-bit
 * displacement; set is the full format, which replaces that byte with the
 * suppress bits, the base displacement size and the index/indirect selection,
 * and is followed by up to four more words of displacement.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_EA_H
#define APOLLO_CPU_M68030_AP_M68030_EA_H

#include <stdbool.h>
#include <stdint.h>

/* The addressing modes of Table 2-4, resolved so that mode 7's register field
 * has already been folded in. */
typedef enum {
  AP_M68030_EA_DATA_REGISTER,          /* Dn          mode 000 */
  AP_M68030_EA_ADDRESS_REGISTER,       /* An          mode 001 */
  AP_M68030_EA_ADDRESS_INDIRECT,       /* (An)        mode 010 */
  AP_M68030_EA_POSTINCREMENT,          /* (An)+       mode 011 */
  AP_M68030_EA_PREDECREMENT,           /* -(An)       mode 100 */
  AP_M68030_EA_DISPLACEMENT,           /* (d16,An)    mode 101 */
  AP_M68030_EA_INDEXED,                /* (d8,An,Xn) and the full-format
                                        * modes built on it, mode 110 */
  AP_M68030_EA_ABSOLUTE_SHORT,         /* (xxx).W     mode 111 reg 000 */
  AP_M68030_EA_ABSOLUTE_LONG,          /* (xxx).L     mode 111 reg 001 */
  AP_M68030_EA_PC_DISPLACEMENT,        /* (d16,PC)    mode 111 reg 010 */
  AP_M68030_EA_PC_INDEXED,             /* (d8,PC,Xn)  mode 111 reg 011 */
  AP_M68030_EA_IMMEDIATE,              /* #<xxx>      mode 111 reg 100 */
  AP_M68030_EA_INVALID,                /* mode 111 reg 101-111: not assigned */
} ap_m68030_ea_kind_t;

typedef struct {
  ap_m68030_ea_kind_t kind;
  unsigned reg; /* the register number, meaningless for the mode 111 kinds */
} ap_m68030_ea_t;

/* Resolve the six-bit effective address field of an instruction word. */
[[nodiscard]] ap_m68030_ea_t ap_m68030_ea_decode(unsigned mode, unsigned reg);

/* Whether this kind needs an extension word to follow. */
[[nodiscard]] bool ap_m68030_ea_uses_extension(ap_m68030_ea_kind_t kind);

/* ---------------------------------------------------------------------------
 * Extension words, Figure 2-2 and Table 2-1.
 * ------------------------------------------------------------------------- */

/* "BD SIZE  Base Displacement Size: 00 = Reserved, 01 = Null Displacement,
 * 10 = Word Displacement, 11 = Long Displacement." Reserved is *not* null --
 * it is an encoding the processor does not define, and collapsing the two
 * would silently accept an illegal instruction word. */
typedef enum {
  AP_M68030_BD_RESERVED = 0,
  AP_M68030_BD_NULL = 1,
  AP_M68030_BD_WORD = 2,
  AP_M68030_BD_LONG = 3,
} ap_m68030_bd_size_t;

/* Table 2-2, "IS-I/IS Memory Indirect Action Encodings". The IS bit and the
 * three-bit I/IS field are read *together*: the same I/IS value means something
 * different depending on IS, which is why this is one enum and not two fields. */
typedef enum {
  AP_M68030_INDIRECT_NONE,           /* No Memory Indirect Action */
  AP_M68030_INDIRECT_PREINDEXED,     /* Indirect Preindexed  ([bd,An,Xn],od) */
  AP_M68030_INDIRECT_POSTINDEXED,    /* Indirect Postindexed ([bd,An],Xn,od) */
  AP_M68030_INDIRECT_MEMORY,         /* Memory Indirect, index suppressed */
  AP_M68030_INDIRECT_RESERVED,       /* an encoding the manual reserves */
} ap_m68030_indirect_t;

/* Outer displacement size, which the same I/IS field carries. */
typedef enum {
  AP_M68030_OD_NONE = 0, /* no memory indirect action, so no outer displacement */
  AP_M68030_OD_NULL,
  AP_M68030_OD_WORD,
  AP_M68030_OD_LONG,
} ap_m68030_od_size_t;

typedef struct {
  bool full_format; /* bit 8: set is the full format */

  /* Present in both formats. */
  bool index_is_address_register; /* D/A: "0 = Dn, 1 = An" */
  unsigned index_register;
  bool index_long; /* W/L: "0 = Sign-Extended Word, 1 = Long Word" */
  unsigned scale;  /* 1, 2, 4 or 8 -- the decoded factor, not the encoding */

  /* Brief format only. */
  int8_t displacement;

  /* Full format only. */
  bool base_suppressed;  /* BS */
  bool index_suppressed; /* IS */
  ap_m68030_bd_size_t base_displacement_size;
  ap_m68030_indirect_t indirect;
  ap_m68030_od_size_t outer_displacement_size;
  bool reserved; /* the word uses an encoding the manual reserves */
} ap_m68030_extension_t;

[[nodiscard]] ap_m68030_extension_t ap_m68030_ea_decode_extension(uint16_t word);

/* How many extension words follow this one, so an instruction's length is known
 * before its address is computed: "BASE DISPLACEMENT (0, 1, OR 2 WORDS)" and
 * "OUTER DISPLACEMENT (0, 1, OR 2 WORDS)". */
[[nodiscard]] unsigned
ap_m68030_ea_extension_words(const ap_m68030_extension_t *extension);

/* How many extension words an effective address occupies, which is what an
 * instruction's total length is built from and therefore what advances the PC.
 *
 * `extension_word` is the first extension word, read only for the two indexed
 * modes and ignored otherwise -- a caller that has not fetched one may pass
 * zero for every other mode. `operand_size` is the operand size in bytes, read
 * only for the immediate mode.
 *
 * The immediate mode is the one worth stating, from Table 2-3: a **byte**
 * immediate still occupies a whole extension word -- "Low-order byte of the
 * extension word" -- so byte and word are both one word and only long is two.
 * Sizing the byte case at half a word, or at none, desynchronises every
 * following instruction rather than producing a visible fault. */
[[nodiscard]] unsigned ap_m68030_ea_words(ap_m68030_ea_kind_t kind,
                                          uint16_t extension_word,
                                          unsigned operand_size);

#endif /* APOLLO_CPU_M68030_AP_M68030_EA_H */

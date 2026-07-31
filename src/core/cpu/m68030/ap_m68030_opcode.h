/* MC68030 top-level operation code map.
 *
 * `M68000 Family Programmer's Reference Manual 1992` Table 8-2. Bits 15-12 of
 * the instruction word select the family; everything below that is the
 * family's own business and decodes separately.
 *
 * ## The move families are not in size order
 *
 * `0001` is Move **Byte**, `0010` is Move **Long**, `0011` is Move **Word**.
 * Byte, long, word — not byte, word, long. The natural assumption is wrong, and
 * wrong in a way that produces a working decoder that moves the wrong number of
 * bytes for two thirds of all MOVE instructions. This is the same encoding the
 * MOVE instruction's own size field uses, and it is inherited from the 68000,
 * so it is not a quirk of this table.
 *
 * ## Two families are exception generators
 *
 * `1010` is "(Unassigned, Reserved)" and `1111` is the coprocessor interface.
 * On this processor an unimplemented instruction in either takes an exception
 * rather than faulting generically: `[030]` Table 8-1 assigns vector 10 to the
 * "Line 1010 Emulator" and vector 11 to the "Line 1111 Emulator". That is what
 * lets an operating system emulate an absent coprocessor in software, and it is
 * why these two families are named here rather than lumped in as invalid.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_OPCODE_H
#define APOLLO_CPU_M68030_AP_M68030_OPCODE_H

#include <stdbool.h>
#include <stdint.h>

/* Table 8-2, one entry per value of bits 15-12. */
typedef enum {
  AP_M68030_OP_BIT_MOVEP_IMMEDIATE = 0x0, /* Bit Manipulation/MOVEP/Immediate */
  AP_M68030_OP_MOVE_BYTE = 0x1,
  AP_M68030_OP_MOVE_LONG = 0x2, /* long, not word -- see the header */
  AP_M68030_OP_MOVE_WORD = 0x3,
  AP_M68030_OP_MISCELLANEOUS = 0x4,
  AP_M68030_OP_ADDQ_SUBQ_SCC_DBCC = 0x5, /* ADDQ/SUBQ/Scc/DBcc/TRAPcc */
  AP_M68030_OP_BCC_BSR_BRA = 0x6,
  AP_M68030_OP_MOVEQ = 0x7,
  AP_M68030_OP_OR_DIV_SBCD = 0x8,
  AP_M68030_OP_SUB_SUBX = 0x9,
  AP_M68030_OP_LINE_A = 0xA, /* "(Unassigned, Reserved)" */
  AP_M68030_OP_CMP_EOR = 0xB,
  AP_M68030_OP_AND_MUL_ABCD_EXG = 0xC,
  AP_M68030_OP_ADD_ADDX = 0xD,
  AP_M68030_OP_SHIFT_ROTATE_BITFIELD = 0xE,
  AP_M68030_OP_LINE_F = 0xF, /* Coprocessor Interface and extensions */
} ap_m68030_opcode_family_t;

[[nodiscard]] ap_m68030_opcode_family_t
ap_m68030_opcode_family(uint16_t instruction);

/* The operand size a MOVE family encodes, in bytes; 0 for a family that is not
 * a MOVE. Present so no caller re-derives the out-of-order mapping. */
[[nodiscard]] unsigned ap_m68030_opcode_move_size(
    ap_m68030_opcode_family_t family);

/* Whether this family takes an emulator exception when unimplemented, and which
 * vector. `[030]` Table 8-1: vector 10 is the "Line 1010 Emulator" and vector
 * 11 the "Line 1111 Emulator". Returns 0 for families that do not. */
[[nodiscard]] unsigned ap_m68030_opcode_emulator_vector(
    ap_m68030_opcode_family_t family);

#endif /* APOLLO_CPU_M68030_AP_M68030_OPCODE_H */

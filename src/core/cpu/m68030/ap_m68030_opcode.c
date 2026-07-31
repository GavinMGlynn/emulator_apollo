/* MC68030 operation code map. See ap_m68030_opcode.h for Table 8-2's citation
 * and for why the move families are not in size order. */

#include "cpu/m68030/ap_m68030_opcode.h"

#include "cpu/m68030/ap_m68030_exception.h"

ap_m68030_opcode_family_t ap_m68030_opcode_family(uint16_t instruction) {
  return (ap_m68030_opcode_family_t)((instruction >> 12) & 0xFu);
}

unsigned ap_m68030_opcode_move_size(ap_m68030_opcode_family_t family) {
  /* Byte, long, word -- the order Table 8-2 gives, which is not the order the
   * sizes suggest. Kept as an explicit mapping rather than arithmetic on the
   * family number, because there is no arithmetic that produces it. */
  switch (family) {
  case AP_M68030_OP_MOVE_BYTE:
    return 1;
  case AP_M68030_OP_MOVE_LONG:
    return 4;
  case AP_M68030_OP_MOVE_WORD:
    return 2;
  case AP_M68030_OP_BIT_MOVEP_IMMEDIATE:
  case AP_M68030_OP_MISCELLANEOUS:
  case AP_M68030_OP_ADDQ_SUBQ_SCC_DBCC:
  case AP_M68030_OP_BCC_BSR_BRA:
  case AP_M68030_OP_MOVEQ:
  case AP_M68030_OP_OR_DIV_SBCD:
  case AP_M68030_OP_SUB_SUBX:
  case AP_M68030_OP_LINE_A:
  case AP_M68030_OP_CMP_EOR:
  case AP_M68030_OP_AND_MUL_ABCD_EXG:
  case AP_M68030_OP_ADD_ADDX:
  case AP_M68030_OP_SHIFT_ROTATE_BITFIELD:
  case AP_M68030_OP_LINE_F:
    return 0;
  }
  return 0;
}

unsigned ap_m68030_opcode_emulator_vector(ap_m68030_opcode_family_t family) {
  switch (family) {
  case AP_M68030_OP_LINE_A:
    return AP_M68030_VECTOR_LINE_A;
  case AP_M68030_OP_LINE_F:
    return AP_M68030_VECTOR_LINE_F;
  case AP_M68030_OP_BIT_MOVEP_IMMEDIATE:
  case AP_M68030_OP_MOVE_BYTE:
  case AP_M68030_OP_MOVE_LONG:
  case AP_M68030_OP_MOVE_WORD:
  case AP_M68030_OP_MISCELLANEOUS:
  case AP_M68030_OP_ADDQ_SUBQ_SCC_DBCC:
  case AP_M68030_OP_BCC_BSR_BRA:
  case AP_M68030_OP_MOVEQ:
  case AP_M68030_OP_OR_DIV_SBCD:
  case AP_M68030_OP_SUB_SUBX:
  case AP_M68030_OP_CMP_EOR:
  case AP_M68030_OP_AND_MUL_ABCD_EXG:
  case AP_M68030_OP_ADD_ADDX:
  case AP_M68030_OP_SHIFT_ROTATE_BITFIELD:
    return 0;
  }
  return 0;
}

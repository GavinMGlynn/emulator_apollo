/* MC68030 instruction decode dispatcher. See ap_m68030_decode.h for why family
 * 0100 needs three decoders and in what order. */

#include "cpu/m68030/ap_m68030_decode.h"

#include "cpu/m68030/ap_m68030_opcode.h"

static ap_m68030_decoded_t decode_family_0100(uint16_t instruction) {
  ap_m68030_decoded_t out = {.kind = AP_M68030_DECODED_ILLEGAL};

  /* The $4E control group is a fixed top byte, so it is unambiguous and goes
   * first. */
  if (ap_m68030_control_matches(instruction)) {
    const ap_m68030_control_t control = ap_m68030_control_decode(instruction);
    if (control.kind != AP_M68030_CTL_INVALID) {
      out.kind = AP_M68030_DECODED_CONTROL;
      out.as.control = control;
    }
    return out;
  }

  /* LEA, CHK and the $48/$4C forms all carry bit 8 set; the single-operand
   * group requires it clear, so these two cannot collide. */
  const ap_m68030_misc_t misc = ap_m68030_misc_decode(instruction);
  if (misc.kind != AP_M68030_MISC_INVALID) {
    out.kind = AP_M68030_DECODED_MISC;
    out.as.misc = misc;
    return out;
  }

  const ap_m68030_single_t single = ap_m68030_single_decode(instruction);
  if (single.kind != AP_M68030_SINGLE_INVALID) {
    out.kind = AP_M68030_DECODED_SINGLE;
    out.as.single = single;
  }
  return out;
}

ap_m68030_decoded_t ap_m68030_decode(uint16_t instruction) {
  ap_m68030_decoded_t out = {.kind = AP_M68030_DECODED_ILLEGAL};

  switch (ap_m68030_opcode_family(instruction)) {
  case AP_M68030_OP_BIT_MOVEP_IMMEDIATE: {
    const ap_m68030_immediate_t immediate =
        ap_m68030_immediate_decode(instruction);
    if (immediate.kind != AP_M68030_IMM_INVALID) {
      out.kind = AP_M68030_DECODED_IMMEDIATE;
      out.as.immediate = immediate;
    }
    return out;
  }

  case AP_M68030_OP_MOVE_BYTE:
  case AP_M68030_OP_MOVE_LONG:
  case AP_M68030_OP_MOVE_WORD: {
    const ap_m68030_move_t move = ap_m68030_move_decode(instruction);
    if (move.kind != AP_M68030_MOVE_INVALID) {
      out.kind = AP_M68030_DECODED_MOVE;
      out.as.move = move;
    }
    return out;
  }

  case AP_M68030_OP_MISCELLANEOUS:
    return decode_family_0100(instruction);

  case AP_M68030_OP_ADDQ_SUBQ_SCC_DBCC: {
    const ap_m68030_quick_t quick = ap_m68030_quick_decode(instruction);
    if (quick.kind != AP_M68030_QUICK_INVALID) {
      out.kind = AP_M68030_DECODED_QUICK;
      out.as.quick = quick;
    }
    return out;
  }

  case AP_M68030_OP_BCC_BSR_BRA:
    out.kind = AP_M68030_DECODED_BRANCH;
    out.as.branch = ap_m68030_branch_decode(instruction);
    return out;

  case AP_M68030_OP_MOVEQ:
    /* "0 1 1 1 REGISTER 0 DATA" -- bit 8 must be clear. The encodings with it
     * set are not MOVEQ and are not assigned to anything else. */
    if ((instruction & 0x0100u) != 0u) {
      return out;
    }
    out.kind = AP_M68030_DECODED_MOVEQ;
    out.as.moveq.reg = (unsigned)((instruction >> 9) & 0x7u);
    out.as.moveq.data = (int8_t)(instruction & 0xFFu);
    return out;

  case AP_M68030_OP_OR_DIV_SBCD:
  case AP_M68030_OP_SUB_SUBX:
  case AP_M68030_OP_CMP_EOR:
  case AP_M68030_OP_AND_MUL_ABCD_EXG:
  case AP_M68030_OP_ADD_ADDX: {
    const ap_m68030_arith_t arith = ap_m68030_arith_decode(instruction);
    if (arith.kind != AP_M68030_ARITH_INVALID) {
      out.kind = AP_M68030_DECODED_ARITH;
      out.as.arith = arith;
    }
    return out;
  }

  case AP_M68030_OP_LINE_A:
    /* "(Unassigned, Reserved)" -- a whole family that exists to trap, so it is
     * its own kind rather than illegal. */
    out.kind = AP_M68030_DECODED_LINE_A;
    return out;

  case AP_M68030_OP_SHIFT_ROTATE_BITFIELD: {
    const ap_m68030_shift_t shift = ap_m68030_shift_decode(instruction);
    if (shift.form != AP_M68030_SHIFT_INVALID) {
      out.kind = AP_M68030_DECODED_SHIFT;
      out.as.shift = shift;
    }
    return out;
  }

  case AP_M68030_OP_LINE_F:
    out.kind = AP_M68030_DECODED_COPROC;
    out.as.coproc = ap_m68030_coproc_decode(instruction);
    return out;
  }

  return out;
}

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
    /* The size-11 escape is tried first -- it is a *different* subtree, not a
     * wider operand -- but a failure there **falls through** rather than
     * ending the decode, because the two subtrees interleave.
     *
     * They interleave in one specific place: `CAS` with size field `00` has the
     * same bit pattern as a *static bit operation*, which is exactly why the
     * manual leaves that size unassigned. `BSET #n,(A0)` is `$08D0`, and it
     * matches the escape's shape -- family 0000, bit 8 clear, bits 7-6 reading
     * 11 -- while being an ordinary bit operation. Stopping at the escape turns
     * every static `BTST`/`BCHG`/`BCLR`/`BSET` into an illegal instruction. */
    const ap_m68030_bounds_t bounds = ap_m68030_bounds_decode(instruction);
    if (bounds.kind != AP_M68030_BOUNDS_INVALID) {
      out.kind = AP_M68030_DECODED_BOUNDS;
      out.as.bounds = bounds;
      return out;
    }

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

/* Bytes an immediate operand occupies after the instruction word, by operand
 * size. Table 2-3: a byte immediate still costs a whole word. */
static unsigned immediate_bytes(unsigned operand_size) {
  return (operand_size > 2u) ? 4u : 2u;
}

unsigned ap_m68030_instruction_length(const ap_m68030_decoded_t *decoded,
                                      uint16_t first_extension,
                                      uint16_t second_extension) {
  switch (decoded->kind) {
  case AP_M68030_DECODED_ILLEGAL:
  case AP_M68030_DECODED_COPROC:
    /* Coprocessor formats vary by coprocessor and are not modelled, so this
     * declines to guess rather than returning a plausible number. */
    return 0;

  case AP_M68030_DECODED_LINE_A:
  case AP_M68030_DECODED_MOVEQ:
    return 2;

  case AP_M68030_DECODED_BRANCH:
    return ap_m68030_branch_length(&decoded->as.branch);

  case AP_M68030_DECODED_MOVE: {
    /* The source's extensions come first, so the destination's extension word
     * is at an offset the caller had to compute -- hence two parameters. */
    const unsigned source = ap_m68030_ea_words(
        decoded->as.move.source.kind, first_extension, decoded->as.move.size);
    const unsigned destination =
        ap_m68030_ea_words(decoded->as.move.destination.kind, second_extension,
                           decoded->as.move.size);
    return 2u + (source + destination) * 2u;
  }

  case AP_M68030_DECODED_SINGLE:
    return 2u + ap_m68030_ea_words(decoded->as.single.ea.kind, first_extension,
                                   decoded->as.single.size) *
                    2u;

  case AP_M68030_DECODED_ARITH:
    return 2u + ap_m68030_ea_words(decoded->as.arith.ea.kind, first_extension,
                                   decoded->as.arith.size) *
                    2u;

  case AP_M68030_DECODED_QUICK:
    return ap_m68030_quick_length(&decoded->as.quick) +
           ap_m68030_ea_words(decoded->as.quick.ea.kind, first_extension,
                              decoded->as.quick.size) *
               2u;

  case AP_M68030_DECODED_CONTROL:
    return ap_m68030_control_length(&decoded->as.control) +
           ap_m68030_ea_words(decoded->as.control.ea.kind, first_extension, 4) *
               2u;

  case AP_M68030_DECODED_MISC:
    return ap_m68030_misc_length(&decoded->as.misc) +
           ap_m68030_ea_words(decoded->as.misc.ea.kind, first_extension,
                              decoded->as.misc.size ? decoded->as.misc.size : 4u) *
               2u;

  case AP_M68030_DECODED_SHIFT:
    return ap_m68030_shift_length(&decoded->as.shift) +
           ap_m68030_ea_words(decoded->as.shift.ea.kind, first_extension, 2) *
               2u;

  case AP_M68030_DECODED_BOUNDS: {
    /* The instruction word, its extension words, and whatever effective address
     * the operand needs. CAS2 names its operands in the extension words rather
     * than through an effective address, so it has none to size. */
    const ap_m68030_bounds_t *bounds = &decoded->as.bounds;
    const unsigned base = 2u + 2u * ap_m68030_bounds_length(bounds);
    if (bounds->kind == AP_M68030_BOUNDS_CAS2) {
      return base;
    }
    return base + ap_m68030_ea_words(bounds->ea.kind, first_extension,
                                     bounds->size) *
                      2u;
  }

  case AP_M68030_DECODED_IMMEDIATE: {
    const ap_m68030_immediate_t *imm = &decoded->as.immediate;
    switch (imm->kind) {
    /* MOVEP carries a 16-bit displacement and no effective address. */
    case AP_M68030_IMM_MOVEP:
      return 4;

    /* The CCR and SR forms take one immediate word whichever they are: the CCR
     * forms are byte operations, but Table 2-3 still gives them a whole word. */
    case AP_M68030_IMM_ORI_TO_CCR:
    case AP_M68030_IMM_ORI_TO_SR:
    case AP_M68030_IMM_ANDI_TO_CCR:
    case AP_M68030_IMM_ANDI_TO_SR:
    case AP_M68030_IMM_EORI_TO_CCR:
    case AP_M68030_IMM_EORI_TO_SR:
      return 4;

    case AP_M68030_IMM_BTST:
    case AP_M68030_IMM_BCHG:
    case AP_M68030_IMM_BCLR:
    case AP_M68030_IMM_BSET: {
      /* A static bit operation carries its bit number in a word of its own; a
       * dynamic one takes it from a register and carries nothing. */
      const unsigned bit_number = imm->dynamic ? 0u : 2u;
      return 2u + bit_number +
             ap_m68030_ea_words(imm->ea.kind, first_extension, 1) * 2u;
    }

    case AP_M68030_IMM_ORI:
    case AP_M68030_IMM_ANDI:
    case AP_M68030_IMM_SUBI:
    case AP_M68030_IMM_ADDI:
    case AP_M68030_IMM_EORI:
    case AP_M68030_IMM_CMPI:
      return 2u + immediate_bytes(imm->size) +
             ap_m68030_ea_words(imm->ea.kind, first_extension, imm->size) * 2u;

    case AP_M68030_IMM_MOVES:
      /* MOVES carries a register-and-direction extension word. */
      return 4u + ap_m68030_ea_words(imm->ea.kind, first_extension, imm->size) *
                      2u;

    case AP_M68030_IMM_INVALID:
      return 0;
    }
    return 0;
  }
  }
  return 0;
}

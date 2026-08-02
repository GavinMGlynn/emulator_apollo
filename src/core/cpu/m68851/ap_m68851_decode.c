/* MC68851 instruction decode. See ap_m68851_decode.h; Appendix A's bit rows
 * read from the page images, one page per instruction. */

#include "cpu/m68851/ap_m68851_decode.h"

ap_m68851_fc_spec_t ap_m68851_decode_fc(unsigned field) {
  ap_m68851_fc_spec_t spec = {.source = AP_M68851_FC_UNDEFINED};
  field &= 0x1Fu;

  /* A prefix code, tested longest-prefix first. */
  if ((field & 0x10u) != 0u) {
    spec.source = AP_M68851_FC_IMMEDIATE;
    spec.immediate = field & 0xFu;
  } else if ((field & 0x08u) != 0u) {
    spec.source = AP_M68851_FC_DATA_REGISTER;
    spec.data_register = field & 0x7u;
  } else if (field == 0x00u) {
    spec.source = AP_M68851_FC_SFC;
  } else if (field == 0x01u) {
    spec.source = AP_M68851_FC_DFC;
  }
  return spec;
}

bool ap_m68851_fc_reaches_dma(const ap_m68851_fc_spec_t *spec) {
  return spec->source == AP_M68851_FC_IMMEDIATE &&
         (spec->immediate & 0x8u) != 0u;
}

ap_m68851_operation_word_t ap_m68851_decode_operation(uint16_t word) {
  return (ap_m68851_operation_word_t){
      .is_coprocessor = (word & 0xF000u) == 0xF000u,
      .cpid = (unsigned)((word >> 9) & 0x7u),
      .type = (ap_m68851_type_t)((word >> 6) & 0x7u),
      .effective_address = (unsigned)(word & 0x3Fu),
  };
}

ap_m68851_condition_t ap_m68851_decode_condition(unsigned field) {
  const unsigned index = (field >> 1) & 0x7u;
  if (field > 0x0Fu) {
    /* Only sixteen conditions are defined, in one contiguous run. */
    return (ap_m68851_condition_t){.bit = AP_M68851_COND_UNDEFINED};
  }
  return (ap_m68851_condition_t){
      .bit = (ap_m68851_condition_bit_t)index,
      .test_clear = (field & 1u) != 0u,
  };
}

bool ap_m68851_condition_true(ap_m68851_condition_t condition, uint16_t psr) {
  /* The tested bits sit at PSR 15 down to 7, with `M` at bit 9 skipped -- so
   * the mapping is not a single shift and has to be spelled out. */
  uint16_t mask = 0;
  switch (condition.bit) {
  case AP_M68851_COND_BUS_ERROR:
    mask = 0x8000u;
    break;
  case AP_M68851_COND_LIMIT_VIOLATION:
    mask = 0x4000u;
    break;
  case AP_M68851_COND_SUPERVISOR_ONLY:
    mask = 0x2000u;
    break;
  case AP_M68851_COND_ACCESS_LEVEL_VIOLATION:
    mask = 0x1000u;
    break;
  case AP_M68851_COND_WRITE_PROTECTED:
    mask = 0x0800u;
    break;
  case AP_M68851_COND_INVALID:
    mask = 0x0400u;
    break;
  case AP_M68851_COND_GATE:
    /* Bit 8, skipping `M` at bit 9. */
    mask = 0x0100u;
    break;
  case AP_M68851_COND_GLOBALLY_SHARABLE:
    mask = 0x0080u;
    break;
  case AP_M68851_COND_UNDEFINED:
    return false;
  }
  const bool set = (psr & mask) != 0u;
  return condition.test_clear ? !set : set;
}

bool ap_m68851_is_mmu_operation_word(uint16_t word) {
  /* `1111 000 000` then six bits of effective address. */
  return (word & 0xFFC0u) == 0xF000u;
}

/* Opclass `001`: PLOAD, PFLUSH, PVALID, told apart by the mode field. */
static ap_m68851_instruction_t decode_class_001(uint16_t command) {
  ap_m68851_instruction_t out = {.opcode = AP_M68851_OP_UNDEFINED};
  const unsigned mode = (unsigned)((command >> 10) & 0x7u);
  const bool rw = (command & 0x0200u) != 0u;

  switch (mode) {
  case 0u:
    /* `001 | 000 | R/W | 0000 | FC`. PLOADR and PLOADW differ only in the R/W
     * bit: "PLOADR causes U bits in the translation tables to be updated as if
     * a read access had taken place. PLOADW causes U and M bits ... as if a
     * write access had taken place." */
    out.opcode = AP_M68851_OP_PLOAD;
    out.read_from_mmu = rw;
    out.fc = ap_m68851_decode_fc((unsigned)(command & 0x1Fu));
    break;

  case 1u:
  case 4u:
  case 5u:
  case 6u:
  case 7u:
    out.opcode = AP_M68851_OP_PFLUSH;
    out.pflush_mode = (ap_m68851_pflush_mode_t)mode;
    out.mask = (unsigned)((command >> 5) & 0xFu);
    out.fc = ap_m68851_decode_fc((unsigned)(command & 0x1Fu));
    break;

  case 2u:
    /* `001 | 010 | 0000000000`: tested against `VAL`. */
    out.opcode = AP_M68851_OP_PVALID;
    out.valid_against_register = false;
    break;

  case 3u:
    /* `001 | 011 | 0000000 | Reg`: tested against a main processor address
     * register instead. The two forms are one instruction to the assembler and
     * two encodings here. */
    out.opcode = AP_M68851_OP_PVALID;
    out.valid_against_register = true;
    out.valid_register = (unsigned)(command & 0x7u);
    break;

  default:
    break;
  }
  return out;
}

ap_m68851_instruction_t ap_m68851_decode_command(uint16_t command) {
  ap_m68851_instruction_t out = {.opcode = AP_M68851_OP_UNDEFINED,
                                 .preg = AP_M68851_PREG_UNDEFINED};
  const unsigned opclass = (unsigned)((command >> 13) & 0x7u);
  const unsigned preg = (unsigned)((command >> 10) & 0x7u);
  const bool rw = (command & 0x0200u) != 0u;

  switch (opclass) {
  case 1u:
    return decode_class_001(command);

  case 2u:
    /* `010 | PReg | R/W | 000000000`: the translation and protection
     * registers, whose `PReg` numbering runs straight from `TC` to `AC`. */
    out.opcode = AP_M68851_OP_PMOVE;
    out.read_from_mmu = rw;
    out.preg = (ap_m68851_preg_t)preg;
    break;

  case 3u:
    /* `011` holds two `PMOVE` formats, told apart by `PReg`: `000`/`001` are
     * the status registers and `100`/`101` the breakpoint registers. The
     * remaining four values name nothing. */
    out.opcode = AP_M68851_OP_PMOVE;
    out.read_from_mmu = rw;
    switch (preg) {
    case 0u:
      out.preg = AP_M68851_PREG_PSR;
      break;
    case 1u:
      out.preg = AP_M68851_PREG_PCSR;
      break;
    case 4u:
      out.preg = AP_M68851_PREG_BAD;
      out.breakpoint_number = (unsigned)((command >> 2) & 0x7u);
      break;
    case 5u:
      out.preg = AP_M68851_PREG_BAC;
      out.breakpoint_number = (unsigned)((command >> 2) & 0x7u);
      break;
    default:
      out.opcode = AP_M68851_OP_UNDEFINED;
      break;
    }
    break;

  case 4u:
    /* `100 | Level | R/W | AReg | FC`. */
    out.opcode = AP_M68851_OP_PTEST;
    out.level = preg; /* the same three bits, named Level here */
    out.read_from_mmu = rw;
    out.address_register = (unsigned)((command >> 5) & 0xFu);
    out.fc = ap_m68851_decode_fc((unsigned)(command & 0x1Fu));
    break;

  default:
    break;
  }
  return out;
}

bool ap_m68851_instruction_is_valid(
    const ap_m68851_instruction_t *instruction) {
  if (instruction->opcode == AP_M68851_OP_UNDEFINED) {
    return false;
  }
  if (instruction->opcode == AP_M68851_OP_PFLUSH &&
      instruction->pflush_mode == AP_M68851_PFLUSH_ALL) {
    /* "If mode = 001 (flush all entries), mask must be 0000" and "function code
     * must be 00000". `00000` decodes as the SFC form; for a flush-all it is
     * simply the required zero. */
    if (instruction->mask != 0u) {
      return false;
    }
    if (instruction->fc.source != AP_M68851_FC_SFC) {
      return false;
    }
  }
  return true;
}

bool ap_m68851_pflush_includes_shared(ap_m68851_pflush_mode_t mode) {
  return mode == AP_M68851_PFLUSH_FC_SHARED ||
         mode == AP_M68851_PFLUSH_FC_EA_SHARED;
}

bool ap_m68851_pflush_uses_address(ap_m68851_pflush_mode_t mode) {
  return mode == AP_M68851_PFLUSH_FC_EA ||
         mode == AP_M68851_PFLUSH_FC_EA_SHARED;
}

bool ap_m68851_pflush_matches_fc(unsigned mask, unsigned instruction_fc,
                                 unsigned entry_fc) {
  return (entry_fc & mask) == (instruction_fc & mask);
}

bool ap_m68851_preg_is_64_bit(ap_m68851_preg_t preg) {
  /* The three root pointers. `TC` is 32 bits and the rest are 16 or 8, so these
   * are the only registers a register-direct `PMOVE` cannot carry. */
  return preg == AP_M68851_PREG_CRP || preg == AP_M68851_PREG_SRP ||
         preg == AP_M68851_PREG_DRP;
}

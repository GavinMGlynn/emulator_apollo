/* MC68030 instruction step. See ap_m68030_step.h for why an unimplemented
 * instruction is reported rather than skipped. */

#include "cpu/m68030/ap_m68030_step.h"

#include "cpu/m68030/ap_m68030_branch.h"
#include "cpu/m68030/ap_m68030_control.h"
#include "cpu/m68030/ap_m68030_operand.h"

void ap_m68030_cpu_reset(ap_m68030_cpu_t *cpu, uint32_t pc) {
  cpu->regs.pc = pc;
  ap_m68030_fetch_reset(&cpu->fetch, pc);
  cpu->clocks = 0;
}

/* Fill the pipe until its decoded stage holds a word, reporting the clocks the
 * prefetches cost. The pipe is three stages deep, so a freshly reset pipe needs
 * three prefetches before anything is decoded. */
static bool fill_to_decoded(ap_m68030_cpu_t *cpu, uint32_t *clocks,
                            uint16_t *word, bool *abnormal) {
  for (unsigned attempt = 0; attempt < 4u; attempt++) {
    if (ap_m68030_pipe_decoded(&cpu->fetch.pipe, word, abnormal)) {
      return true;
    }
    const ap_m68030_fetch_result_t fetched =
        ap_m68030_fetch_prefetch(&cpu->fetch);
    *clocks += fetched.clocks;
    if (fetched.fault) {
      return false;
    }
    ap_m68030_pipe_advance(&cpu->fetch.pipe);
  }
  return ap_m68030_pipe_decoded(&cpu->fetch.pipe, word, abnormal);
}

/* MOVEQ: "The data in an 8-bit field within the operation word is sign-extended
 * to a long operand in the data register as it is transferred." X is not
 * affected, N and Z come from the result, V and C are always cleared. */
static void execute_moveq(ap_m68030_regs_t *regs,
                          const ap_m68030_moveq_t *moveq) {
  const int32_t value = moveq->data;
  regs->d[moveq->reg] = (uint32_t)value;

  uint16_t ccr = ap_m68030_read_ccr(regs);
  ccr &= (uint16_t)(1u << AP_M68030_SR_X_BIT); /* X survives, the rest do not */
  if (value < 0) {
    ccr |= (uint16_t)(1u << AP_M68030_SR_N_BIT);
  }
  if (value == 0) {
    ccr |= (uint16_t)(1u << AP_M68030_SR_Z_BIT);
  }
  ap_m68030_write_ccr(regs, ccr);
}


/* Take the next word of the instruction stream, which is where an extension
 * word comes from: the same prefetch path as the instruction word itself, not a
 * separate read. Advancing the pipe is what makes the word in stage C become
 * the decoded one. */
static bool next_word(ap_m68030_cpu_t *cpu, uint32_t *clocks, uint16_t *word) {
  ap_m68030_pipe_advance(&cpu->fetch.pipe);
  bool abnormal = false;
  if (!fill_to_decoded(cpu, clocks, word, &abnormal) || abnormal) {
    return false;
  }
  return true;
}

/* Gather the extension words an effective address needs, and turn them into the
 * calculation's inputs. Returns false for a mode this step cannot yet supply --
 * the full-format indexed forms with their own displacements, which need the
 * extension word decoded before the count is known. */
static bool gather_address_input(ap_m68030_cpu_t *cpu, ap_m68030_ea_kind_t kind,
                                 unsigned size, uint32_t *clocks,
                                 ap_m68030_address_input_t *input) {
  input->operand_size = size;

  switch (kind) {
  case AP_M68030_EA_DATA_REGISTER:
  case AP_M68030_EA_ADDRESS_REGISTER:
  case AP_M68030_EA_ADDRESS_INDIRECT:
  case AP_M68030_EA_POSTINCREMENT:
  case AP_M68030_EA_PREDECREMENT:
    return true;

  case AP_M68030_EA_DISPLACEMENT:
  case AP_M68030_EA_PC_DISPLACEMENT: {
    uint16_t word = 0;
    if (!next_word(cpu, clocks, &word)) {
      return false;
    }
    /* The displacement is signed, and the PC forms are relative to this very
     * word -- which is the address the pipe just delivered. */
    input->displacement = (int32_t)(int16_t)word;
    input->extension_address = cpu->regs.pc + 2u;
    return true;
  }

  case AP_M68030_EA_ABSOLUTE_SHORT: {
    uint16_t word = 0;
    if (!next_word(cpu, clocks, &word)) {
      return false;
    }
    /* "(xxx).W" is sign-extended, so $8000 addresses the top of memory rather
     * than the middle of it. */
    input->displacement = (int32_t)(int16_t)word;
    return true;
  }

  case AP_M68030_EA_ABSOLUTE_LONG: {
    uint16_t high = 0;
    uint16_t low = 0;
    if (!next_word(cpu, clocks, &high) || !next_word(cpu, clocks, &low)) {
      return false;
    }
    input->displacement =
        (int32_t)(((uint32_t)high << 16) | (uint32_t)low);
    return true;
  }

  case AP_M68030_EA_INDEXED:
  case AP_M68030_EA_PC_INDEXED: {
    uint16_t word = 0;
    if (!next_word(cpu, clocks, &word)) {
      return false;
    }
    input->extension_word = word;
    input->extension_address = cpu->regs.pc + 2u;
    /* Only the brief format is supplied here: the full format may declare base
     * and outer displacements of its own, and fetching those is its own item. */
    const ap_m68030_extension_t extension =
        ap_m68030_ea_decode_extension(word);
    return !extension.full_format;
  }

  case AP_M68030_EA_IMMEDIATE:
  case AP_M68030_EA_INVALID:
    return false;
  }
  return false;
}

/* MOVE: "N - Set if the result is negative; Z - Set if the result is zero;
 * V - Always cleared; C - Always cleared", X not affected. MOVEA affects none
 * of them, which is why it is worth distinguishing from MOVE at all. */
static void set_move_condition_codes(ap_m68030_regs_t *regs, uint32_t value,
                                     unsigned size) {
  const uint32_t extended = ap_m68030_sign_extend(value, size);
  uint16_t ccr = ap_m68030_read_ccr(regs);
  ccr &= (uint16_t)(1u << AP_M68030_SR_X_BIT);
  if ((int32_t)extended < 0) {
    ccr |= (uint16_t)(1u << AP_M68030_SR_N_BIT);
  }
  if (extended == 0u) {
    ccr |= (uint16_t)(1u << AP_M68030_SR_Z_BIT);
  }
  ap_m68030_write_ccr(regs, ccr);
}

/* MOVE and MOVEA in the modes reachable without an extension word. Returns
 * false when the instruction is outside that subset, leaving the caller to
 * report it unimplemented. */
static bool execute_move(ap_m68030_cpu_t *cpu, const ap_m68030_move_t *move,
                         uint32_t *clocks) {
  uint32_t value = 0;

  /* The source's extension words come first in the stream, then the
   * destination's -- which is the ordering ap_m68030_instruction_length's two
   * parameters exist to describe, now actually performed. */
  if (move->source.kind == AP_M68030_EA_IMMEDIATE) {
    /* An immediate is the operand itself rather than an address. */
    uint16_t high = 0;
    if (!next_word(cpu, clocks, &high)) {
      return false;
    }
    if (move->size == 4u) {
      uint16_t low = 0;
      if (!next_word(cpu, clocks, &low)) {
        return false;
      }
      value = ((uint32_t)high << 16) | (uint32_t)low;
    } else {
      /* Table 2-3: a byte immediate is the *low-order byte* of the word. */
      value = (move->size == 1u) ? (uint32_t)(high & 0xFFu) : (uint32_t)high;
    }
  } else {
    ap_m68030_address_input_t source_input = {0};
    if (!gather_address_input(cpu, move->source.kind, move->size, clocks,
                              &source_input)) {
      return false;
    }
    const ap_m68030_address_t source =
        ap_m68030_address_calculate(&cpu->regs, move->source, &source_input);
    const ap_m68030_operand_result_t read = ap_m68030_operand_read(
        &cpu->regs, cpu->data, &source, move->size, cpu->data_function_code);
    *clocks += read.clocks;
    if (!read.ok) {
      return false;
    }
    value = read.value;
  }

  ap_m68030_address_input_t destination_input = {0};
  if (!gather_address_input(cpu, move->destination.kind, move->size, clocks,
                            &destination_input)) {
    return false;
  }
  const ap_m68030_address_t destination = ap_m68030_address_calculate(
      &cpu->regs, move->destination, &destination_input);
  const ap_m68030_operand_result_t written =
      ap_m68030_operand_write(&cpu->regs, cpu->data, &destination, move->size,
                              value, cpu->data_function_code);
  *clocks += written.clocks;
  if (!written.ok) {
    return false;
  }
  const uint32_t read_value = value;

  if (ap_m68030_move_affects_condition_codes(move)) {
    set_move_condition_codes(&cpu->regs, read_value, move->size);
  }
  return true;
}

ap_m68030_step_result_t ap_m68030_step(ap_m68030_cpu_t *cpu) {
  ap_m68030_step_result_t out = {.status = AP_M68030_STEP_FAULT};
  uint16_t word = 0;
  bool abnormal = false;

  if (!fill_to_decoded(cpu, &out.clocks, &word, &abnormal) || abnormal) {
    cpu->clocks += out.clocks;
    return out;
  }

  out.instruction = word;
  const ap_m68030_decoded_t decoded = ap_m68030_decode(word);
  out.kind = decoded.kind;

  if (decoded.kind == AP_M68030_DECODED_ILLEGAL) {
    out.status = AP_M68030_STEP_ILLEGAL;
    cpu->clocks += out.clocks;
    return out;
  }

  const unsigned length = ap_m68030_instruction_length(&decoded, 0, 0);

  switch (decoded.kind) {
  case AP_M68030_DECODED_MOVEQ:
    execute_moveq(&cpu->regs, &decoded.as.moveq);
    break;

  case AP_M68030_DECODED_CONTROL:
    if (decoded.as.control.kind != AP_M68030_CTL_NOP) {
      out.status = AP_M68030_STEP_UNIMPLEMENTED;
      cpu->clocks += out.clocks;
      return out;
    }
    /* NOP "synchronizes the integer pipeline" and does nothing else. */
    break;

  case AP_M68030_DECODED_BRANCH: {
    const ap_m68030_branch_t *branch = &decoded.as.branch;
    /* Only the 8-bit form executes yet: the wider ones need their displacement
     * fetched, which is operand access this step does not do. */
    if (branch->size != AP_M68030_BRANCH_8BIT) {
      out.status = AP_M68030_STEP_UNIMPLEMENTED;
      cpu->clocks += out.clocks;
      return out;
    }
    if (branch->is_bsr) {
      out.status = AP_M68030_STEP_UNIMPLEMENTED; /* needs a stack write */
      cpu->clocks += out.clocks;
      return out;
    }

    const bool taken =
        branch->is_bra ||
        ap_m68030_condition(branch->condition, ap_m68030_read_ccr(&cpu->regs));
    out.branch_taken = taken;

    if (taken) {
      cpu->regs.pc =
          ap_m68030_branch_target(cpu->regs.pc, branch->displacement8);
      ap_m68030_fetch_reset(&cpu->fetch, cpu->regs.pc);
      out.status = AP_M68030_STEP_EXECUTED;
      cpu->clocks += out.clocks;
      return out;
    }
    break;
  }

  /* Every remaining kind is listed rather than defaulted. -Wswitch-enum is on,
   * so adding an instruction family to the decoder forces a decision here about
   * whether it executes -- which is exactly the decision that must not be made
   * silently. */
  case AP_M68030_DECODED_MOVE:
    if (!execute_move(cpu, &decoded.as.move, &out.clocks)) {
      out.status = AP_M68030_STEP_UNIMPLEMENTED;
      cpu->clocks += out.clocks;
      return out;
    }
    break;

  case AP_M68030_DECODED_IMMEDIATE:
  case AP_M68030_DECODED_MISC:
  case AP_M68030_DECODED_SINGLE:
  case AP_M68030_DECODED_QUICK:
  case AP_M68030_DECODED_ARITH:
  case AP_M68030_DECODED_SHIFT:
  case AP_M68030_DECODED_COPROC:
  case AP_M68030_DECODED_LINE_A:
  case AP_M68030_DECODED_ILLEGAL:
    out.status = AP_M68030_STEP_UNIMPLEMENTED;
    cpu->clocks += out.clocks;
    return out;
  }

  cpu->regs.pc += length;
  ap_m68030_pipe_advance(&cpu->fetch.pipe);
  out.status = AP_M68030_STEP_EXECUTED;
  cpu->clocks += out.clocks;
  return out;
}

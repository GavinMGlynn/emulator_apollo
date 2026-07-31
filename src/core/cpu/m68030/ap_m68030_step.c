/* MC68030 instruction step. See ap_m68030_step.h for why an unimplemented
 * instruction is reported rather than skipped. */

#include "cpu/m68030/ap_m68030_step.h"

#include "cpu/m68030/ap_m68030_branch.h"
#include "cpu/m68030/ap_m68030_control.h"
#include "cpu/m68030/ap_m68030_immediate.h"
#include "cpu/m68030/ap_m68030_quick.h"
#include "cpu/m68030/ap_m68030_shift.h"
#include "cpu/m68030/ap_m68030_single.h"

/* Forward declarations: the two below are defined after the dispatchers that
 * call them, so the file reads in the order the manual presents the
 * instructions rather than in dependency order. */
static bool execute_address_form(ap_m68030_cpu_t *cpu,
                                 const ap_m68030_arith_t *arith,
                                 uint32_t *clocks);
static bool execute_bit(ap_m68030_cpu_t *cpu, const ap_m68030_immediate_t *imm,
                        uint32_t *clocks);
static bool execute_extended(ap_m68030_cpu_t *cpu,
                             const ap_m68030_arith_t *arith, uint32_t *clocks);
#include "cpu/m68030/ap_m68030_alu.h"
#include "cpu/m68030/ap_m68030_exception.h"
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


/* The six arithmetic and logical operations that take a data register and an
 * effective address. The A-forms, the divides and multiplies, and the
 * register-to-register special forms are not here yet and report unimplemented.
 *
 * The direction bit decides which operand is the destination, and with it which
 * way round the subtraction goes -- "Destination - Source", where the
 * destination is the register in one direction and the effective address in the
 * other. Getting that backwards negates the result and inverts the carry, which
 * shows up only in a later conditional branch. */
static bool execute_arith(ap_m68030_cpu_t *cpu, const ap_m68030_arith_t *arith,
                          uint32_t *clocks) {
  switch (arith->kind) {
  case AP_M68030_ARITH_ADDA:
  case AP_M68030_ARITH_SUBA:
  case AP_M68030_ARITH_CMPA:
    return execute_address_form(cpu, arith, clocks);
  case AP_M68030_ARITH_MULU:
  case AP_M68030_ARITH_MULS:
  case AP_M68030_ARITH_DIVU:
  case AP_M68030_ARITH_DIVS:
  case AP_M68030_ARITH_ADDX:
  case AP_M68030_ARITH_SUBX:
  case AP_M68030_ARITH_EXG:
    return execute_extended(cpu, arith, clocks);
  case AP_M68030_ARITH_OR:
  case AP_M68030_ARITH_AND:
  case AP_M68030_ARITH_SUB:
  case AP_M68030_ARITH_ADD:
  case AP_M68030_ARITH_CMP:
  case AP_M68030_ARITH_EOR:
    break;
  case AP_M68030_ARITH_ABCD:
  case AP_M68030_ARITH_SBCD:
  case AP_M68030_ARITH_CMPM:
    return execute_extended(cpu, arith, clocks);
  case AP_M68030_ARITH_INVALID:
    return false;
  }

  ap_m68030_address_input_t input = {0};
  if (!gather_address_input(cpu, arith->ea.kind, arith->size, clocks, &input)) {
    /* An immediate source is legal for CMP and the rest through their *I*
     * forms, which live in family 0000 and are not this instruction. */
    return false;
  }

  const ap_m68030_address_t where =
      ap_m68030_address_calculate(&cpu->regs, arith->ea, &input);

  const ap_m68030_operand_result_t memory = ap_m68030_operand_read(
      &cpu->regs, cpu->data, &where, arith->size, cpu->data_function_code);
  *clocks += memory.clocks;
  if (!memory.ok) {
    return false;
  }

  const uint32_t mask = (arith->size == 1u)   ? 0xFFu
                        : (arith->size == 2u) ? 0xFFFFu
                                              : 0xFFFFFFFFu;
  const uint32_t register_value = cpu->regs.d[arith->reg] & mask;

  /* Which operand is the destination is the direction bit's whole meaning. */
  const uint32_t destination =
      arith->to_effective_address ? memory.value : register_value;
  const uint32_t source =
      arith->to_effective_address ? register_value : memory.value;

  ap_m68030_alu_result_t result;
  switch (arith->kind) {
  case AP_M68030_ARITH_ADD:
    result = ap_m68030_alu_add(destination, source, arith->size);
    break;
  case AP_M68030_ARITH_SUB:
    result = ap_m68030_alu_sub(destination, source, arith->size);
    break;
  case AP_M68030_ARITH_CMP:
    result = ap_m68030_alu_cmp(destination, source, arith->size);
    break;
  case AP_M68030_ARITH_AND:
    result = ap_m68030_alu_and(destination, source, arith->size);
    break;
  case AP_M68030_ARITH_OR:
    result = ap_m68030_alu_or(destination, source, arith->size);
    break;
  case AP_M68030_ARITH_EOR:
    result = ap_m68030_alu_eor(destination, source, arith->size);
    break;
  /* Handled above and returned before reaching here; listed so -Wswitch-enum
   * still forces a decision if one is ever added. */
  case AP_M68030_ARITH_SUBA:
  case AP_M68030_ARITH_ADDA:
  case AP_M68030_ARITH_CMPA:
  case AP_M68030_ARITH_DIVU:
  case AP_M68030_ARITH_DIVS:
  case AP_M68030_ARITH_MULU:
  case AP_M68030_ARITH_MULS:
  case AP_M68030_ARITH_SUBX:
  case AP_M68030_ARITH_ADDX:
  case AP_M68030_ARITH_ABCD:
  case AP_M68030_ARITH_SBCD:
  case AP_M68030_ARITH_CMPM:
  case AP_M68030_ARITH_EXG:
  case AP_M68030_ARITH_INVALID:
    return false;
  }

  ap_m68030_write_ccr(&cpu->regs,
                      ap_m68030_alu_apply(ap_m68030_read_ccr(&cpu->regs),
                                          &result));

  /* CMP writes nothing: it exists for its condition codes alone. */
  if (arith->kind == AP_M68030_ARITH_CMP) {
    return true;
  }

  if (arith->to_effective_address) {
    const ap_m68030_operand_result_t written = ap_m68030_operand_write(
        &cpu->regs, cpu->data, &where, arith->size, result.result,
        cpu->data_function_code);
    *clocks += written.clocks;
    return written.ok;
  }

  /* A data register destination keeps whatever the operand does not cover. */
  cpu->regs.d[arith->reg] =
      (cpu->regs.d[arith->reg] & ~mask) | (result.result & mask);
  return true;
}


/* Fetch an immediate operand from the instruction stream. Table 2-3: a byte
 * immediate is the low-order byte of a whole extension word, so byte and word
 * each cost one word and only a long costs two. */
static bool fetch_immediate(ap_m68030_cpu_t *cpu, unsigned size,
                            uint32_t *clocks, uint32_t *value) {
  uint16_t high = 0;
  if (!next_word(cpu, clocks, &high)) {
    return false;
  }
  if (size == 4u) {
    uint16_t low = 0;
    if (!next_word(cpu, clocks, &low)) {
      return false;
    }
    *value = ((uint32_t)high << 16) | (uint32_t)low;
    return true;
  }
  *value = (size == 1u) ? (uint32_t)(high & 0xFFu) : (uint32_t)high;
  return true;
}

/* ORI, ANDI, SUBI, ADDI, EORI and CMPI: the same six operations as the register
 * forms, with an immediate source. The immediate comes *before* the effective
 * address's own extension words in the instruction stream, which is why it is
 * fetched first. */
static bool execute_immediate(ap_m68030_cpu_t *cpu,
                              const ap_m68030_immediate_t *imm,
                              uint32_t *clocks) {
  switch (imm->kind) {
  case AP_M68030_IMM_ORI:
  case AP_M68030_IMM_ANDI:
  case AP_M68030_IMM_SUBI:
  case AP_M68030_IMM_ADDI:
  case AP_M68030_IMM_EORI:
  case AP_M68030_IMM_CMPI:
    break;
  case AP_M68030_IMM_MOVES:
  case AP_M68030_IMM_ORI_TO_CCR:
  case AP_M68030_IMM_ORI_TO_SR:
  case AP_M68030_IMM_ANDI_TO_CCR:
  case AP_M68030_IMM_ANDI_TO_SR:
  case AP_M68030_IMM_EORI_TO_CCR:
  case AP_M68030_IMM_EORI_TO_SR:
  case AP_M68030_IMM_MOVEP:
  case AP_M68030_IMM_INVALID:
    return false;
  case AP_M68030_IMM_BTST:
  case AP_M68030_IMM_BCHG:
  case AP_M68030_IMM_BCLR:
  case AP_M68030_IMM_BSET:
    return execute_bit(cpu, imm, clocks);
  }

  uint32_t immediate = 0;
  if (!fetch_immediate(cpu, imm->size, clocks, &immediate)) {
    return false;
  }

  ap_m68030_address_input_t input = {0};
  if (!gather_address_input(cpu, imm->ea.kind, imm->size, clocks, &input)) {
    return false;
  }
  const ap_m68030_address_t where =
      ap_m68030_address_calculate(&cpu->regs, imm->ea, &input);

  const ap_m68030_operand_result_t read = ap_m68030_operand_read(
      &cpu->regs, cpu->data, &where, imm->size, cpu->data_function_code);
  *clocks += read.clocks;
  if (!read.ok) {
    return false;
  }

  ap_m68030_alu_result_t result;
  switch (imm->kind) {
  case AP_M68030_IMM_ORI:
    result = ap_m68030_alu_or(read.value, immediate, imm->size);
    break;
  case AP_M68030_IMM_ANDI:
    result = ap_m68030_alu_and(read.value, immediate, imm->size);
    break;
  case AP_M68030_IMM_EORI:
    result = ap_m68030_alu_eor(read.value, immediate, imm->size);
    break;
  /* "Destination - Immediate Data" -- the destination is the effective address
   * here, not the immediate. */
  case AP_M68030_IMM_SUBI:
    result = ap_m68030_alu_sub(read.value, immediate, imm->size);
    break;
  case AP_M68030_IMM_ADDI:
    result = ap_m68030_alu_add(read.value, immediate, imm->size);
    break;
  case AP_M68030_IMM_CMPI:
    result = ap_m68030_alu_cmp(read.value, immediate, imm->size);
    break;
  case AP_M68030_IMM_MOVES:
  case AP_M68030_IMM_ORI_TO_CCR:
  case AP_M68030_IMM_ORI_TO_SR:
  case AP_M68030_IMM_ANDI_TO_CCR:
  case AP_M68030_IMM_ANDI_TO_SR:
  case AP_M68030_IMM_EORI_TO_CCR:
  case AP_M68030_IMM_EORI_TO_SR:
  case AP_M68030_IMM_BTST:
  case AP_M68030_IMM_BCHG:
  case AP_M68030_IMM_BCLR:
  case AP_M68030_IMM_BSET:
  case AP_M68030_IMM_MOVEP:
  case AP_M68030_IMM_INVALID:
    return false;
  }

  ap_m68030_write_ccr(&cpu->regs,
                      ap_m68030_alu_apply(ap_m68030_read_ccr(&cpu->regs),
                                          &result));

  if (imm->kind == AP_M68030_IMM_CMPI) {
    return true; /* compares, writes nothing */
  }

  const ap_m68030_operand_result_t written = ap_m68030_operand_write(
      &cpu->regs, cpu->data, &where, imm->size, result.result,
      cpu->data_function_code);
  *clocks += written.clocks;
  return written.ok;
}

/* CLR, NEG, NOT and TST: one operand, read-modify-write except for TST which
 * only reads and CLR which only writes. */
static bool execute_single(ap_m68030_cpu_t *cpu,
                           const ap_m68030_single_t *single, uint32_t *clocks) {
  switch (single->kind) {
  case AP_M68030_SINGLE_CLR:
  case AP_M68030_SINGLE_NEG:
  case AP_M68030_SINGLE_NOT:
  case AP_M68030_SINGLE_TST:
    break;
  case AP_M68030_SINGLE_NEGX:
  case AP_M68030_SINGLE_TAS:
  case AP_M68030_SINGLE_MOVE_FROM_SR:
  case AP_M68030_SINGLE_MOVE_FROM_CCR:
  case AP_M68030_SINGLE_MOVE_TO_CCR:
  case AP_M68030_SINGLE_MOVE_TO_SR:
  case AP_M68030_SINGLE_ILLEGAL:
  case AP_M68030_SINGLE_INVALID:
    return false;
  }

  ap_m68030_address_input_t input = {0};
  if (!gather_address_input(cpu, single->ea.kind, single->size, clocks,
                            &input)) {
    return false;
  }
  const ap_m68030_address_t where =
      ap_m68030_address_calculate(&cpu->regs, single->ea, &input);

  uint32_t value = 0;
  if (single->kind != AP_M68030_SINGLE_CLR) {
    const ap_m68030_operand_result_t read = ap_m68030_operand_read(
        &cpu->regs, cpu->data, &where, single->size, cpu->data_function_code);
    *clocks += read.clocks;
    if (!read.ok) {
      return false;
    }
    value = read.value;
  }

  ap_m68030_alu_result_t result;
  switch (single->kind) {
  case AP_M68030_SINGLE_CLR:
    result = ap_m68030_alu_test(0u, single->size);
    break;
  case AP_M68030_SINGLE_NEG:
    result = ap_m68030_alu_neg(value, single->size);
    break;
  case AP_M68030_SINGLE_NOT:
    result = ap_m68030_alu_not(value, single->size);
    break;
  case AP_M68030_SINGLE_TST:
    result = ap_m68030_alu_test(value, single->size);
    break;
  case AP_M68030_SINGLE_NEGX:
  case AP_M68030_SINGLE_TAS:
  case AP_M68030_SINGLE_MOVE_FROM_SR:
  case AP_M68030_SINGLE_MOVE_FROM_CCR:
  case AP_M68030_SINGLE_MOVE_TO_CCR:
  case AP_M68030_SINGLE_MOVE_TO_SR:
  case AP_M68030_SINGLE_ILLEGAL:
  case AP_M68030_SINGLE_INVALID:
    return false;
  }

  ap_m68030_write_ccr(&cpu->regs,
                      ap_m68030_alu_apply(ap_m68030_read_ccr(&cpu->regs),
                                          &result));

  if (single->kind == AP_M68030_SINGLE_TST) {
    return true; /* tests, writes nothing */
  }

  const ap_m68030_operand_result_t written = ap_m68030_operand_write(
      &cpu->regs, cpu->data, &where, single->size, result.result,
      cpu->data_function_code);
  *clocks += written.clocks;
  return written.ok;
}


/* ADDQ, SUBQ, Scc and DBcc.
 *
 * ADDQ and SUBQ have an address register special case that is easy to miss and
 * silent when missed: "When adding to address registers, the condition codes
 * are not altered, and the entire destination address register is used
 * regardless of the operation size." So ADDQ.W #1,A0 changes all 32 bits and
 * leaves the flags alone -- a pointer bumped in a loop must not clobber the
 * comparison the loop branches on. */
static bool execute_quick(ap_m68030_cpu_t *cpu, const ap_m68030_quick_t *quick,
                          uint32_t *clocks, bool *branch_taken) {
  switch (quick->kind) {
  case AP_M68030_QUICK_ADDQ:
  case AP_M68030_QUICK_SUBQ: {
    const bool to_address_register =
        quick->ea.kind == AP_M68030_EA_ADDRESS_REGISTER;
    /* "the entire destination address register is used regardless of the
     * operation size" -- so the operation widens to a long. */
    const unsigned size = to_address_register ? 4u : quick->size;

    ap_m68030_address_input_t input = {0};
    if (!gather_address_input(cpu, quick->ea.kind, size, clocks, &input)) {
      return false;
    }
    const ap_m68030_address_t where =
        ap_m68030_address_calculate(&cpu->regs, quick->ea, &input);

    const ap_m68030_operand_result_t read = ap_m68030_operand_read(
        &cpu->regs, cpu->data, &where, size, cpu->data_function_code);
    *clocks += read.clocks;
    if (!read.ok) {
      return false;
    }

    const ap_m68030_alu_result_t result =
        (quick->kind == AP_M68030_QUICK_ADDQ)
            ? ap_m68030_alu_add(read.value, quick->data, size)
            : ap_m68030_alu_sub(read.value, quick->data, size);

    /* "the condition codes are not altered" for an address register
     * destination -- which is what lets a pointer be bumped inside a loop
     * without disturbing the comparison the loop branches on. */
    if (!to_address_register) {
      ap_m68030_write_ccr(&cpu->regs,
                          ap_m68030_alu_apply(ap_m68030_read_ccr(&cpu->regs),
                                              &result));
    }

    const ap_m68030_operand_result_t written = ap_m68030_operand_write(
        &cpu->regs, cpu->data, &where, size, result.result,
        cpu->data_function_code);
    *clocks += written.clocks;
    return written.ok;
  }

  case AP_M68030_QUICK_SCC: {
    /* "if the condition is true, sets the byte specified by the effective
     * address to TRUE (all ones). Otherwise, sets that byte to [zero]" -- all
     * ones, not one, which is what makes the result usable as a mask. */
    ap_m68030_address_input_t input = {0};
    if (!gather_address_input(cpu, quick->ea.kind, 1u, clocks, &input)) {
      return false;
    }
    const ap_m68030_address_t where =
        ap_m68030_address_calculate(&cpu->regs, quick->ea, &input);

    const bool condition =
        ap_m68030_condition(quick->condition, ap_m68030_read_ccr(&cpu->regs));
    const ap_m68030_operand_result_t written = ap_m68030_operand_write(
        &cpu->regs, cpu->data, &where, 1u, condition ? 0xFFu : 0x00u,
        cpu->data_function_code);
    *clocks += written.clocks;
    return written.ok;
  }

  case AP_M68030_QUICK_DBCC: {
    /* "If Condition False Then (Dn - 1 -> Dn; If Dn != -1 Then PC + dn -> PC)".
     * The displacement word is consumed either way, since it is part of the
     * instruction whether or not the branch is taken. */
    uint16_t displacement_word = 0;
    if (!next_word(cpu, clocks, &displacement_word)) {
      return false;
    }

    const bool condition =
        ap_m68030_condition(quick->condition, ap_m68030_read_ccr(&cpu->regs));

    if (!condition) {
      /* Only the low *word* of the register counts down; the upper half is
       * left alone, so a loop counter cannot borrow into it. */
      const uint16_t counter =
          (uint16_t)((cpu->regs.d[quick->reg] & 0xFFFFu) - 1u);
      cpu->regs.d[quick->reg] =
          (cpu->regs.d[quick->reg] & 0xFFFF0000u) | counter;

      if (ap_m68030_dbcc_taken(false, counter)) {
        cpu->regs.pc = ap_m68030_branch_target(
            cpu->regs.pc, (int32_t)(int16_t)displacement_word);
        ap_m68030_fetch_reset(&cpu->fetch, cpu->regs.pc);
        *branch_taken = true;
        return true;
      }
    }
    return true;
  }

  case AP_M68030_QUICK_TRAPCC:
  case AP_M68030_QUICK_INVALID:
    return false;
  }
  return false;
}


/* ADDA, SUBA and CMPA: an effective address against an *address* register.
 *
 * "the source operand is sign-extended to a long operand and the operation is
 * performed on the address register using all 32 bits", so a word form is not a
 * word operation -- it is a long operation on a sign-extended operand. And
 * ADDA and SUBA alter no condition codes, while CMPA does, which is the whole
 * reason a compare against an address register is a separate instruction. */
static bool execute_address_form(ap_m68030_cpu_t *cpu,
                                 const ap_m68030_arith_t *arith,
                                 uint32_t *clocks) {
  uint32_t source = 0;

  /* An immediate is fetched rather than addressed, and must be handled *before*
   * gather_address_input, which has no address to gather for it. */
  if (arith->ea.kind == AP_M68030_EA_IMMEDIATE) {
    if (!fetch_immediate(cpu, arith->size, clocks, &source)) {
      return false;
    }
  } else {
    ap_m68030_address_input_t input = {0};
    if (!gather_address_input(cpu, arith->ea.kind, arith->size, clocks,
                              &input)) {
      return false;
    }
    const ap_m68030_address_t where =
        ap_m68030_address_calculate(&cpu->regs, arith->ea, &input);
    const ap_m68030_operand_result_t read = ap_m68030_operand_read(
        &cpu->regs, cpu->data, &where, arith->size, cpu->data_function_code);
    *clocks += read.clocks;
    if (!read.ok) {
      return false;
    }
    source = read.value;
  }

  /* The sign extension is what makes a word form reach the whole register. */
  const uint32_t extended = ap_m68030_sign_extend(source, arith->size);
  const uint32_t destination =
      ap_m68030_read_address_register(&cpu->regs, arith->reg);

  switch (arith->kind) {
  case AP_M68030_ARITH_ADDA:
    /* No condition codes: an address calculation must not disturb the flags a
     * following branch depends on. */
    ap_m68030_write_address_register(&cpu->regs, arith->reg,
                                     destination + extended);
    return true;
  case AP_M68030_ARITH_SUBA:
    ap_m68030_write_address_register(&cpu->regs, arith->reg,
                                     destination - extended);
    return true;
  case AP_M68030_ARITH_CMPA: {
    /* CMPA *does* set them, and always compares 32 bits. */
    const ap_m68030_alu_result_t result =
        ap_m68030_alu_cmp(destination, extended, 4u);
    ap_m68030_write_ccr(&cpu->regs,
                        ap_m68030_alu_apply(ap_m68030_read_ccr(&cpu->regs),
                                            &result));
    return true;
  }
  /* Only the three address-register forms reach this function. */
  case AP_M68030_ARITH_OR:
  case AP_M68030_ARITH_AND:
  case AP_M68030_ARITH_SUB:
  case AP_M68030_ARITH_ADD:
  case AP_M68030_ARITH_CMP:
  case AP_M68030_ARITH_EOR:
  case AP_M68030_ARITH_DIVU:
  case AP_M68030_ARITH_DIVS:
  case AP_M68030_ARITH_MULU:
  case AP_M68030_ARITH_MULS:
  case AP_M68030_ARITH_SUBX:
  case AP_M68030_ARITH_ADDX:
  case AP_M68030_ARITH_ABCD:
  case AP_M68030_ARITH_SBCD:
  case AP_M68030_ARITH_CMPM:
  case AP_M68030_ARITH_EXG:
  case AP_M68030_ARITH_INVALID:
    return false;
  }
  return false;
}

/* BTST, BCHG, BCLR and BSET.
 *
 * The operand size is decided by the *destination kind*, not by an encoding
 * field: "When a data register is the destination, any of the 32 bits can be
 * specified by a modulo 32-bit number. When a memory location is the
 * destination, the operation is a byte operation, and the bit number is modulo
 * 8." A model that picked one width would address the wrong bit for half of all
 * uses, silently.
 *
 * Z comes from the bit as it was *before* the operation -- "TEST (<bit number>
 * of Destination) -> Z; 1 -> <bit number> of Destination" -- so BSET on an
 * already-set bit sets Z clear, and testing after the write would invert it. */
static bool execute_bit(ap_m68030_cpu_t *cpu, const ap_m68030_immediate_t *imm,
                        uint32_t *clocks) {
  unsigned bit_number = 0;
  if (imm->dynamic) {
    bit_number = (unsigned)cpu->regs.d[imm->reg];
  } else {
    uint16_t word = 0;
    if (!next_word(cpu, clocks, &word)) {
      return false;
    }
    bit_number = word;
  }

  const bool to_register = imm->ea.kind == AP_M68030_EA_DATA_REGISTER;
  const unsigned size = to_register ? 4u : 1u;
  bit_number %= to_register ? 32u : 8u;

  ap_m68030_address_input_t input = {0};
  if (!gather_address_input(cpu, imm->ea.kind, size, clocks, &input)) {
    return false;
  }
  const ap_m68030_address_t where =
      ap_m68030_address_calculate(&cpu->regs, imm->ea, &input);

  const ap_m68030_operand_result_t read = ap_m68030_operand_read(
      &cpu->regs, cpu->data, &where, size, cpu->data_function_code);
  *clocks += read.clocks;
  if (!read.ok) {
    return false;
  }

  const uint32_t mask = UINT32_C(1) << bit_number;
  const bool was_set = (read.value & mask) != 0u;

  /* "Z = Dn" with the overbar restored by the operation description: Z is set
   * when the tested bit was *zero*. Only Z is affected at all. */
  uint16_t ccr = ap_m68030_read_ccr(&cpu->regs);
  ccr &= (uint16_t)~(1u << AP_M68030_SR_Z_BIT);
  if (!was_set) {
    ccr |= (uint16_t)(1u << AP_M68030_SR_Z_BIT);
  }
  ap_m68030_write_ccr(&cpu->regs, ccr);

  uint32_t updated = read.value;
  switch (imm->kind) {
  case AP_M68030_IMM_BTST:
    return true; /* tests only */
  case AP_M68030_IMM_BCHG:
    updated ^= mask;
    break;
  case AP_M68030_IMM_BCLR:
    updated &= ~mask;
    break;
  case AP_M68030_IMM_BSET:
    updated |= mask;
    break;
  /* Only the four bit operations reach this function. */
  case AP_M68030_IMM_ORI:
  case AP_M68030_IMM_ANDI:
  case AP_M68030_IMM_SUBI:
  case AP_M68030_IMM_ADDI:
  case AP_M68030_IMM_EORI:
  case AP_M68030_IMM_CMPI:
  case AP_M68030_IMM_MOVES:
  case AP_M68030_IMM_ORI_TO_CCR:
  case AP_M68030_IMM_ORI_TO_SR:
  case AP_M68030_IMM_ANDI_TO_CCR:
  case AP_M68030_IMM_ANDI_TO_SR:
  case AP_M68030_IMM_EORI_TO_CCR:
  case AP_M68030_IMM_EORI_TO_SR:
  case AP_M68030_IMM_MOVEP:
  case AP_M68030_IMM_INVALID:
    return false;
  }

  const ap_m68030_operand_result_t written = ap_m68030_operand_write(
      &cpu->regs, cpu->data, &where, size, updated, cpu->data_function_code);
  *clocks += written.clocks;
  return written.ok;
}


/* Shifts and rotates. The register form shifts a data register by a count; the
 * memory form shifts one word in memory by exactly one. */
static bool execute_shift(ap_m68030_cpu_t *cpu, const ap_m68030_shift_t *shift,
                          uint32_t *clocks) {
  const uint16_t ccr = ap_m68030_read_ccr(&cpu->regs);
  const bool x_in = ((ccr >> AP_M68030_SR_X_BIT) & 1u) != 0u;

  switch (shift->form) {
  case AP_M68030_SHIFT_REGISTER: {
    /* "the shift count is the value in the data register specified in
     * instruction modulo 64" -- so a register count of 64 is a no-op rather
     * than a full rotation, and one of 100 is a shift by 36. */
    const unsigned count =
        shift->count_in_register
            ? (unsigned)(cpu->regs.d[shift->count] % 64u)
            : shift->count;

    const uint32_t mask = (shift->size == 1u)   ? 0xFFu
                          : (shift->size == 2u) ? 0xFFFFu
                                                : 0xFFFFFFFFu;
    const ap_m68030_alu_result_t result = ap_m68030_alu_shift(
        shift->type, shift->left, cpu->regs.d[shift->reg] & mask, count,
        shift->size, x_in);

    ap_m68030_write_ccr(&cpu->regs, ap_m68030_alu_apply(ccr, &result));
    cpu->regs.d[shift->reg] =
        (cpu->regs.d[shift->reg] & ~mask) | (result.result & mask);
    return true;
  }

  case AP_M68030_SHIFT_MEMORY: {
    ap_m68030_address_input_t input = {0};
    if (!gather_address_input(cpu, shift->ea.kind, 2u, clocks, &input)) {
      return false;
    }
    const ap_m68030_address_t where =
        ap_m68030_address_calculate(&cpu->regs, shift->ea, &input);

    const ap_m68030_operand_result_t read = ap_m68030_operand_read(
        &cpu->regs, cpu->data, &where, 2u, cpu->data_function_code);
    *clocks += read.clocks;
    if (!read.ok) {
      return false;
    }

    /* "An operand in memory can be shifted one bit only, and the operand size
     * is restricted to a word." */
    const ap_m68030_alu_result_t result = ap_m68030_alu_shift(
        shift->type, shift->left, read.value, 1u, 2u, x_in);
    ap_m68030_write_ccr(&cpu->regs, ap_m68030_alu_apply(ccr, &result));

    const ap_m68030_operand_result_t written = ap_m68030_operand_write(
        &cpu->regs, cpu->data, &where, 2u, result.result,
        cpu->data_function_code);
    *clocks += written.clocks;
    return written.ok;
  }

  case AP_M68030_SHIFT_BITFIELD:
  case AP_M68030_SHIFT_INVALID:
    return false;
  }
  return false;
}


/* The register-to-register and wide forms: MULU, MULS, DIVU, DIVS, ADDX, SUBX,
 * CMPM and EXG. */
/* Read one operand through -(An), applying the decrement, and keep where it
 * came from so the same location can be written back. The predecrement step is
 * two rather than one for a byte on A7, which ap_m68030_address_calculate
 * already knows -- so this goes through it rather than adjusting by hand. */
static bool read_predecrement_at(ap_m68030_cpu_t *cpu, unsigned reg,
                                 unsigned size, uint32_t *clocks,
                                 uint32_t *value, ap_m68030_address_t *where) {
  const ap_m68030_ea_t ea = {.kind = AP_M68030_EA_PREDECREMENT, .reg = reg};
  const ap_m68030_address_input_t input = {.operand_size = size};
  *where = ap_m68030_address_calculate(&cpu->regs, ea, &input);

  const ap_m68030_operand_result_t read = ap_m68030_operand_read(
      &cpu->regs, cpu->data, where, size, cpu->data_function_code);
  *clocks += read.clocks;
  if (!read.ok) {
    return false;
  }
  *value = read.value;
  return true;
}

static bool read_predecrement(ap_m68030_cpu_t *cpu, unsigned reg, unsigned size,
                              uint32_t *clocks, uint32_t *value) {
  ap_m68030_address_t discarded = {0};
  return read_predecrement_at(cpu, reg, size, clocks, value, &discarded);
}

static bool read_postincrement(ap_m68030_cpu_t *cpu, unsigned reg,
                               unsigned size, uint32_t *clocks,
                               uint32_t *value) {
  const ap_m68030_ea_t ea = {.kind = AP_M68030_EA_POSTINCREMENT, .reg = reg};
  const ap_m68030_address_input_t input = {.operand_size = size};
  const ap_m68030_address_t where =
      ap_m68030_address_calculate(&cpu->regs, ea, &input);

  const ap_m68030_operand_result_t read = ap_m68030_operand_read(
      &cpu->regs, cpu->data, &where, size, cpu->data_function_code);
  *clocks += read.clocks;
  if (!read.ok) {
    return false;
  }
  *value = read.value;
  return true;
}

static bool execute_extended(ap_m68030_cpu_t *cpu,
                             const ap_m68030_arith_t *arith, uint32_t *clocks) {
  const uint16_t ccr = ap_m68030_read_ccr(&cpu->regs);
  const bool x_in = ((ccr >> AP_M68030_SR_X_BIT) & 1u) != 0u;
  const bool z_in = ((ccr >> AP_M68030_SR_Z_BIT) & 1u) != 0u;

  switch (arith->kind) {
  case AP_M68030_ARITH_EXG:
    /* "Exchanges the contents of two 32-bit registers", condition codes "Not
     * affected". There is no sized form -- "Size = (Long)" -- so this is always
     * the whole register, and an address register exchanged here is not
     * sign-extended from anything.
     *
     * A7 goes through the stack-pointer accessors rather than the array, since
     * A7 names whichever of USP/ISP/MSP is active. */
    switch (arith->exg) {
    case AP_M68030_EXG_DATA: {
      const uint32_t t = cpu->regs.d[arith->reg];
      cpu->regs.d[arith->reg] = cpu->regs.d[arith->source_reg];
      cpu->regs.d[arith->source_reg] = t;
      return true;
    }
    case AP_M68030_EXG_ADDRESS: {
      const uint32_t t = ap_m68030_read_address_register(&cpu->regs, arith->reg);
      ap_m68030_write_address_register(
          &cpu->regs, arith->reg,
          ap_m68030_read_address_register(&cpu->regs, arith->source_reg));
      ap_m68030_write_address_register(&cpu->regs, arith->source_reg, t);
      return true;
    }
    case AP_M68030_EXG_MIXED: {
      /* Rx "always specifies the data register" and Ry the address register,
       * whichever way round the assembler wrote them. */
      const uint32_t t = cpu->regs.d[arith->reg];
      cpu->regs.d[arith->reg] =
          ap_m68030_read_address_register(&cpu->regs, arith->source_reg);
      ap_m68030_write_address_register(&cpu->regs, arith->source_reg, t);
      return true;
    }
    case AP_M68030_EXG_NONE:
      break;
    }
    return false;

  case AP_M68030_ARITH_MULU:
  case AP_M68030_ARITH_MULS: {
    /* "The word form ... multiplies two word operands and produces a long
     * result", so the source is a word and the destination register's low word
     * is the other operand -- the whole register receives the product. */
    uint32_t source = 0;
    /* Immediate first: it is fetched, not addressed. */
    if (arith->ea.kind == AP_M68030_EA_IMMEDIATE) {
      if (!fetch_immediate(cpu, 2u, clocks, &source)) {
        return false;
      }
    } else {
      ap_m68030_address_input_t input = {0};
      if (!gather_address_input(cpu, arith->ea.kind, 2u, clocks, &input)) {
        return false;
      }
      const ap_m68030_address_t where =
          ap_m68030_address_calculate(&cpu->regs, arith->ea, &input);
      const ap_m68030_operand_result_t read = ap_m68030_operand_read(
          &cpu->regs, cpu->data, &where, 2u, cpu->data_function_code);
      *clocks += read.clocks;
      if (!read.ok) {
        return false;
      }
      source = read.value;
    }

    const uint32_t destination = cpu->regs.d[arith->reg] & 0xFFFFu;
    uint32_t product;
    if (arith->kind == AP_M68030_ARITH_MULU) {
      product = (uint32_t)((source & 0xFFFFu) * destination);
    } else {
      const int32_t a = (int32_t)(int16_t)(uint16_t)(source & 0xFFFFu);
      const int32_t b = (int32_t)(int16_t)(uint16_t)destination;
      product = (uint32_t)(a * b);
    }

    cpu->regs.d[arith->reg] = product;
    /* "V 0, C 0" for the multiplies at this width; N and Z from the long
     * result, which is why the product is formed before the flags are set. */
    const ap_m68030_alu_result_t flags = ap_m68030_alu_test(product, 4u);
    ap_m68030_write_ccr(&cpu->regs, ap_m68030_alu_apply(ccr, &flags));
    return true;
  }

  case AP_M68030_ARITH_DIVU:
  case AP_M68030_ARITH_DIVS: {
    uint32_t source = 0;
    /* Immediate first: it is fetched, not addressed. */
    if (arith->ea.kind == AP_M68030_EA_IMMEDIATE) {
      if (!fetch_immediate(cpu, 2u, clocks, &source)) {
        return false;
      }
    } else {
      ap_m68030_address_input_t input = {0};
      if (!gather_address_input(cpu, arith->ea.kind, 2u, clocks, &input)) {
        return false;
      }
      const ap_m68030_address_t where =
          ap_m68030_address_calculate(&cpu->regs, arith->ea, &input);
      const ap_m68030_operand_result_t read = ap_m68030_operand_read(
          &cpu->regs, cpu->data, &where, 2u, cpu->data_function_code);
      *clocks += read.clocks;
      if (!read.ok) {
        return false;
      }
      source = read.value;
    }

    const uint32_t divisor16 = source & 0xFFFFu;
    if (divisor16 == 0u) {
      /* "Attempted division by zero causes an exception." The step takes it,
       * because only the step knows the instruction's length and Table 8-6's
       * six-word frame wants both this instruction's address and the next
       * one's. The register is left alone. */
      cpu->pending_vector = AP_M68030_VECTOR_ZERO_DIVIDE;
      return true;
    }

    const uint32_t dividend = cpu->regs.d[arith->reg];
    uint32_t quotient;
    uint32_t remainder;
    bool overflow;

    if (arith->kind == AP_M68030_ARITH_DIVU) {
      quotient = dividend / divisor16;
      remainder = dividend % divisor16;
      overflow = quotient > 0xFFFFu;
    } else {
      const int32_t a = (int32_t)dividend;
      const int32_t b = (int32_t)(int16_t)(uint16_t)divisor16;
      const int32_t q = a / b;
      const int32_t r = a % b;
      quotient = (uint32_t)q;
      remainder = (uint32_t)r;
      overflow = q > 32767 || q < -32768;
    }

    if (overflow) {
      /* "If the quotient is larger than a 16-bit integer, the overflow
       * condition code is set and the operands are unchanged" -- so V is the
       * whole result, and the register must not be written. */
      uint16_t updated = ccr;
      updated |= (uint16_t)(1u << AP_M68030_SR_V_BIT);
      ap_m68030_write_ccr(&cpu->regs, updated);
      return true;
    }

    /* "a quotient in the lower word ... and a remainder in the upper word". */
    cpu->regs.d[arith->reg] =
        ((remainder & 0xFFFFu) << 16) | (quotient & 0xFFFFu);

    const ap_m68030_alu_result_t flags =
        ap_m68030_alu_test(quotient & 0xFFFFu, 2u);
    ap_m68030_write_ccr(&cpu->regs, ap_m68030_alu_apply(ccr, &flags));
    return true;
  }

  case AP_M68030_ARITH_ADDX:
  case AP_M68030_ARITH_SUBX:
  case AP_M68030_ARITH_ABCD:
  case AP_M68030_ARITH_SBCD: {
    const unsigned size = (arith->kind == AP_M68030_ARITH_ABCD ||
                           arith->kind == AP_M68030_ARITH_SBCD)
                              ? 1u
                              : arith->size;
    const uint32_t mask = (size == 1u) ? 0xFFu : (size == 2u) ? 0xFFFFu
                                                              : 0xFFFFFFFFu;

    uint32_t source = 0;
    uint32_t destination = 0;
    ap_m68030_address_t destination_where = {0};

    if (arith->memory_operands) {
      /* "The operands are addressed with the predecrement addressing mode using
       * the address registers specified in the instruction." Source first, so
       * that -(An),-(An) on the same register decrements twice in the order the
       * hardware does. */
      if (!read_predecrement(cpu, arith->source_reg, size, clocks, &source)) {
        return false;
      }
      if (!read_predecrement_at(cpu, arith->reg, size, clocks, &destination,
                                &destination_where)) {
        return false;
      }
    } else {
      destination = cpu->regs.d[arith->reg] & mask;
      source = cpu->regs.d[arith->source_reg] & mask;
    }

    /* Four kinds reach here and only four, so this is a chain rather than a
     * switch -- a switch would have to name every other arithmetic kind to
     * satisfy -Wswitch-enum, which would say nothing. */
    ap_m68030_alu_result_t result;
    if (arith->kind == AP_M68030_ARITH_ADDX) {
      result = ap_m68030_alu_addx(destination, source, size, x_in, z_in);
    } else if (arith->kind == AP_M68030_ARITH_SUBX) {
      result = ap_m68030_alu_subx(destination, source, size, x_in, z_in);
    } else if (arith->kind == AP_M68030_ARITH_ABCD) {
      result = ap_m68030_alu_abcd(destination, source, x_in, z_in);
    } else {
      result = ap_m68030_alu_sbcd(destination, source, x_in, z_in);
    }

    /* The flags are written before the store, so a faulting write leaves the
     * same visible state either way round; the store is what may fail. */
    if (arith->memory_operands) {
      const ap_m68030_operand_result_t wrote = ap_m68030_operand_write(
          &cpu->regs, cpu->data, &destination_where, size,
          result.result & mask, cpu->data_function_code);
      *clocks += wrote.clocks;
      if (!wrote.ok) {
        return false;
      }
    } else {
      cpu->regs.d[arith->reg] =
          (cpu->regs.d[arith->reg] & ~mask) | (result.result & mask);
    }
    ap_m68030_write_ccr(&cpu->regs, ap_m68030_alu_apply(ccr, &result));
    return true;
  }

  case AP_M68030_ARITH_CMPM: {
    /* "The operands are always addressed with the postincrement addressing
     * mode" -- Ay is always the source, Ax always the destination, and "the
     * destination location is not changed": this only sets condition codes. */
    uint32_t source = 0;
    uint32_t destination = 0;
    if (!read_postincrement(cpu, arith->source_reg, arith->size, clocks,
                            &source)) {
      return false;
    }
    if (!read_postincrement(cpu, arith->reg, arith->size, clocks,
                            &destination)) {
      return false;
    }

    const ap_m68030_alu_result_t result =
        ap_m68030_alu_cmp(destination, source, arith->size);
    ap_m68030_write_ccr(&cpu->regs, ap_m68030_alu_apply(ccr, &result));
    return true;
  }

  /* Only the forms routed here reach this function; the rest are listed so
   * -Wswitch-enum still forces a decision when one is added. */
  case AP_M68030_ARITH_OR:
  case AP_M68030_ARITH_AND:
  case AP_M68030_ARITH_SUB:
  case AP_M68030_ARITH_ADD:
  case AP_M68030_ARITH_CMP:
  case AP_M68030_ARITH_EOR:
  case AP_M68030_ARITH_SUBA:
  case AP_M68030_ARITH_ADDA:
  case AP_M68030_ARITH_CMPA:
  case AP_M68030_ARITH_INVALID:
    return false;
  }
  return false;
}

/* Write one operand to supervisor data space at an absolute address, which is
 * what building a stack frame is made of. */
static bool write_frame_field(ap_m68030_cpu_t *cpu, uint32_t address,
                              unsigned size, uint32_t value, uint32_t *clocks) {
  const ap_m68030_address_t where = {.address = address, .valid = true};
  const ap_m68030_operand_result_t wrote = ap_m68030_operand_write(
      &cpu->regs, cpu->data, &where, size, value, AP_M68030_FC_SUPERVISOR_DATA);
  *clocks += wrote.clocks;
  return wrote.ok;
}

ap_m68030_exception_result_t
ap_m68030_take_exception(ap_m68030_cpu_t *cpu, unsigned vector,
                         uint32_t stacked_pc, uint32_t instruction_address) {
  ap_m68030_exception_result_t out = {0};

  /* Reset stacks nothing -- "For all exceptions other than reset" -- and the
   * fault and coprocessor frames carry state this model does not have. Each is
   * declined rather than built wrong. */
  if (vector == AP_M68030_VECTOR_RESET_SP ||
      vector == AP_M68030_VECTOR_RESET_PC) {
    return out;
  }
  const ap_m68030_frame_format_t format = ap_m68030_frame_for_vector(vector);
  if (format != AP_M68030_FRAME_SHORT && format != AP_M68030_FRAME_SIX_WORD) {
    return out;
  }

  /* Step one. The copy is taken *before* the register is changed, and it is the
   * copy that gets stacked -- so RTE restores the privilege level the exception
   * interrupted, not the one the handler ran in. */
  const uint16_t saved_sr = cpu->regs.sr;

  uint16_t updated = saved_sr;
  updated |= (uint16_t)(1u << AP_M68030_SR_S_BIT);
  updated &= (uint16_t)~(1u << AP_M68030_SR_T1_BIT);
  updated &= (uint16_t)~(1u << AP_M68030_SR_T0_BIT);
  ap_m68030_write_sr(&cpu->regs, updated);

  /* Step three. "on the active supervisor stack" -- read after S is set, so a
   * user-state exception builds its frame on ISP or MSP and not on the USP it
   * came from. */
  const uint32_t bytes = ap_m68030_frame_words(format) * 2u;
  const uint32_t frame = ap_m68030_read_a7(&cpu->regs) - bytes;

  bool wrote = write_frame_field(cpu, frame + 0u, 2u, saved_sr, &out.clocks);
  wrote = wrote && write_frame_field(cpu, frame + 2u, 4u, stacked_pc,
                                     &out.clocks);
  wrote = wrote && write_frame_field(
                       cpu, frame + 6u, 2u,
                       ap_m68030_frame_format_word(format, vector), &out.clocks);
  if (format == AP_M68030_FRAME_SIX_WORD) {
    /* "INSTRUCTION ADDRESS is the address of the instruction that caused the
     * exception", which is not the stacked PC: that one points at the next. */
    wrote = wrote && write_frame_field(cpu, frame + 8u, 4u, instruction_address,
                                       &out.clocks);
  }
  if (!wrote) {
    /* A fault while stacking is a double fault on the real part, which halts
     * it. Modelling that needs the halted state, so this reports failure with
     * the SR already changed -- the caller sees ok false and must not treat the
     * processor as having taken the exception. */
    return out;
  }
  ap_m68030_write_a7(&cpu->regs, frame);

  /* Step four. "The processor multiplies the vector number by four to determine
   * the exception vector offset. It adds the offset to the value stored in the
   * vector base register". */
  out.vector_address = cpu->regs.vbr + ap_m68030_vector_offset(vector);
  const ap_m68030_address_t vector_where = {.address = out.vector_address,
                                            .valid = true};
  const ap_m68030_operand_result_t handler =
      ap_m68030_operand_read(&cpu->regs, cpu->data, &vector_where, 4u,
                             AP_M68030_FC_SUPERVISOR_DATA);
  out.clocks += handler.clocks;
  if (!handler.ok) {
    return out;
  }

  /* "After prefetching the first three words to fill the instruction pipe, the
   * processor resumes normal processing at the address in the program
   * counter" -- so the pipe is emptied here and refilled by the next step. */
  out.handler = handler.value;
  cpu->regs.pc = out.handler;
  ap_m68030_fetch_reset(&cpu->fetch, out.handler);

  out.frame_address = frame;
  out.ok = true;
  return out;
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
    /* "TRAP #<vector> ... 32 + <vector> -> Vector Number": the four-bit field
     * is an index into Table 8-1's trap range, not a vector number, so TRAP #0
     * is vector 32. The decoder reports the field; ap_m68030_trap_vector turns
     * it into the vector, which is what keeps this and the exception module
     * agreeing by construction rather than by two transcriptions matching. */
    if (decoded.as.control.kind == AP_M68030_CTL_TRAP) {
      cpu->pending_vector =
          ap_m68030_trap_vector(decoded.as.control.vector);
      break;
    }
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

  case AP_M68030_DECODED_ARITH:
    if (!execute_arith(cpu, &decoded.as.arith, &out.clocks)) {
      out.status = AP_M68030_STEP_UNIMPLEMENTED;
      cpu->clocks += out.clocks;
      return out;
    }
    break;

  case AP_M68030_DECODED_IMMEDIATE:
    if (!execute_immediate(cpu, &decoded.as.immediate, &out.clocks)) {
      out.status = AP_M68030_STEP_UNIMPLEMENTED;
      cpu->clocks += out.clocks;
      return out;
    }
    break;

  case AP_M68030_DECODED_SINGLE:
    if (!execute_single(cpu, &decoded.as.single, &out.clocks)) {
      out.status = AP_M68030_STEP_UNIMPLEMENTED;
      cpu->clocks += out.clocks;
      return out;
    }
    break;

  case AP_M68030_DECODED_QUICK: {
    bool taken = false;
    if (!execute_quick(cpu, &decoded.as.quick, &out.clocks, &taken)) {
      out.status = AP_M68030_STEP_UNIMPLEMENTED;
      cpu->clocks += out.clocks;
      return out;
    }
    if (taken) {
      out.branch_taken = true;
      out.status = AP_M68030_STEP_EXECUTED;
      cpu->clocks += out.clocks;
      return out;
    }
    break;
  }

  case AP_M68030_DECODED_SHIFT:
    if (!execute_shift(cpu, &decoded.as.shift, &out.clocks)) {
      out.status = AP_M68030_STEP_UNIMPLEMENTED;
      cpu->clocks += out.clocks;
      return out;
    }
    break;

  case AP_M68030_DECODED_MISC:
  case AP_M68030_DECODED_COPROC:
  case AP_M68030_DECODED_LINE_A:
  case AP_M68030_DECODED_ILLEGAL:
    out.status = AP_M68030_STEP_UNIMPLEMENTED;
    cpu->clocks += out.clocks;
    return out;
  }

  if (cpu->pending_vector != 0u) {
    /* Table 8-6's two addresses: the stacked PC "points to" the next
     * instruction for every exception raised this way, and the six-word frame's
     * INSTRUCTION ADDRESS is "the address of the instruction that caused the
     * exception" -- which is where the PC still is, since it has not advanced
     * past it yet. Advancing first and passing the same value twice is the easy
     * mistake, and it makes a handler's report of where the fault was point one
     * instruction too far on. */
    const unsigned vector = cpu->pending_vector;
    cpu->pending_vector = 0u;

    const ap_m68030_exception_result_t taken = ap_m68030_take_exception(
        cpu, vector, cpu->regs.pc + length, cpu->regs.pc);
    out.clocks += taken.clocks;
    if (!taken.ok) {
      /* A fault while stacking is a double fault, which halts the real part.
       * Reporting a memory fault is honest about not modelling the halt. */
      out.status = AP_M68030_STEP_FAULT;
      cpu->clocks += out.clocks;
      return out;
    }
    out.status = AP_M68030_STEP_EXCEPTION;
    cpu->clocks += out.clocks;
    return out;
  }

  cpu->regs.pc += length;
  ap_m68030_pipe_advance(&cpu->fetch.pipe);
  out.status = AP_M68030_STEP_EXECUTED;
  cpu->clocks += out.clocks;
  return out;
}

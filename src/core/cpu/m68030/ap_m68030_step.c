/* MC68030 instruction step. See ap_m68030_step.h for why an unimplemented
 * instruction is reported rather than skipped. */

#include <stddef.h>

#include "cpu/m68030/ap_m68030_step.h"

#include "cpu/m68030/ap_m68030_timing_table.h"

#include "cpu/m68030/ap_m68030_branch.h"
#include "cpu/m68030/ap_m68030_category.h"
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
#include "cpu/m68030/ap_m68030_mmusr.h"
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
  cpu->extension_words++;
  return true;
}

/* Read a base or outer displacement of the size the extension word declared.
 * A null displacement reads nothing and contributes zero -- "When omitting a
 * displacement or suppressing an element, its value is zero in the effective
 * address calculation" -- while the *reserved* encoding is not a null and is
 * refused, since accepting it would run an illegal instruction word. */
static bool read_displacement(ap_m68030_cpu_t *cpu, ap_m68030_bd_size_t size,
                              uint32_t *clocks, int32_t *out) {
  switch (size) {
  case AP_M68030_BD_NULL:
    *out = 0;
    return true;
  case AP_M68030_BD_WORD: {
    uint16_t word = 0;
    if (!next_word(cpu, clocks, &word)) {
      return false;
    }
    *out = (int32_t)(int16_t)word;
    return true;
  }
  case AP_M68030_BD_LONG: {
    uint16_t high = 0;
    uint16_t low = 0;
    if (!next_word(cpu, clocks, &high) || !next_word(cpu, clocks, &low)) {
      return false;
    }
    *out = (int32_t)(((uint32_t)high << 16) | low);
    return true;
  }
  case AP_M68030_BD_RESERVED:
    break;
  }
  return false;
}

/* Calculate an effective address, performing any memory indirection it asks
 * for. The calculation itself cannot: an indirect mode needs a bus read partway
 * through -- "The processor accesses a long word at this address" -- and the
 * bus belongs here.
 *
 * The long word read is the whole intermediate address, so this is one aligned
 * long-word read and not an operand-sized one however small the operand is. */
static ap_m68030_address_t resolve_address(ap_m68030_cpu_t *cpu,
                                           uint32_t *clocks, ap_m68030_ea_t ea,
                                           const ap_m68030_address_input_t *in) {
  ap_m68030_address_t where = ap_m68030_address_calculate(&cpu->regs, ea, in);
  if (!where.valid || !where.indirection_pending) {
    return where;
  }

  const ap_m68030_address_t intermediate = {.address = where.address,
                                            .valid = true};
  const ap_m68030_operand_result_t read =
      ap_m68030_operand_read(&cpu->regs, cpu->data, &intermediate, 4u,
                             cpu->data_function_code);
  *clocks += read.clocks;
  if (!read.ok) {
    where.valid = false;
    return where;
  }

  /* "adds the index operand ... and the outer displacement to yield the
   * effective address" -- whichever of those two the mode left outside. */
  where.address = read.value + (uint32_t)where.post_indirection;
  where.indirection_pending = false;
  where.post_indirection = 0;
  return where;
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

    const ap_m68030_extension_t extension =
        ap_m68030_ea_decode_extension(word);
    if (!extension.full_format) {
      return true; /* the brief format's displacement is in the word itself */
    }
    if (extension.reserved) {
      return false; /* an encoding the manual reserves is not an address */
    }

    /* The full format declares its own displacement sizes, so the number of
     * words to read is not known until the word naming them has been read.
     * "BD SIZE ... 01 = Null Displacement, 10 = Word Displacement, 11 = Long
     * Displacement", and the same three for the outer one -- and *null* is not
     * "reserved": collapsing the two would silently accept an illegal word. */
    if (!read_displacement(cpu, extension.base_displacement_size, clocks,
                           &input->base_displacement)) {
      return false;
    }
    /* The outer displacement exists only when there is a memory indirect
     * action to put it outside -- "no memory indirect action, so no outer
     * displacement". */
    if (extension.indirect != AP_M68030_INDIRECT_NONE) {
      ap_m68030_bd_size_t as_bd;
      switch (extension.outer_displacement_size) {
      case AP_M68030_OD_NULL:
        as_bd = AP_M68030_BD_NULL;
        break;
      case AP_M68030_OD_WORD:
        as_bd = AP_M68030_BD_WORD;
        break;
      case AP_M68030_OD_LONG:
        as_bd = AP_M68030_BD_LONG;
        break;
      case AP_M68030_OD_NONE:
        return false; /* an indirect action must name an outer size */
      }
      if (!read_displacement(cpu, as_bd, clocks, &input->outer_displacement)) {
        return false;
      }
    }
    return true;
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
        resolve_address(cpu, clocks, move->source, &source_input);
    const ap_m68030_operand_result_t read = ap_m68030_operand_read(
        &cpu->regs, cpu->data, &source, move->size, cpu->data_function_code);
    *clocks += read.clocks;
    if (!read.ok) {
      return false;
    }
    value = read.value;
  }

  /* "MOVE ... destination must be data alterable", and MOVEA's must be an
   * address register. Without the check, `MOVE.W D0,(d16,PC)` encodes and the
   * step would try to write through the program counter -- an instruction the
   * processor refuses, running here. */
  if (move->kind != AP_M68030_MOVE_TO_ADDRESS_REGISTER &&
      !ap_m68030_ea_is_data_alterable(move->destination.kind)) {
    return false;
  }

  ap_m68030_address_input_t destination_input = {0};
  if (!gather_address_input(cpu, move->destination.kind, move->size, clocks,
                            &destination_input)) {
    return false;
  }
  const ap_m68030_address_t destination =
      resolve_address(cpu, clocks, move->destination, &destination_input);
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
/* Defined below, beside the other immediate handling; forward declared because
 * the arithmetic forms take an immediate source too and sit above it. */
static bool fetch_immediate(ap_m68030_cpu_t *cpu, unsigned size,
                            uint32_t *clocks, uint32_t *value);

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

  /* "If the location specified is a source operand, all addressing modes can be
   * used" -- the immediate included. `ADD.W #$10,D0` in family 1101 is a real
   * instruction, distinct from the `ADDI` that assembles to the same thing, and
   * an immediate is fetched rather than addressed. The `100`-`110` opmodes
   * write to the effective address, so an immediate cannot be one there. */
  uint32_t source_operand = 0;
  if (arith->ea.kind == AP_M68030_EA_IMMEDIATE) {
    if (arith->to_effective_address) {
      return false;
    }
    if (!fetch_immediate(cpu, arith->size, clocks, &source_operand)) {
      return false;
    }
  }

  ap_m68030_address_t where = {0};
  if (arith->ea.kind != AP_M68030_EA_IMMEDIATE) {
    ap_m68030_address_input_t input = {0};
    if (!gather_address_input(cpu, arith->ea.kind, arith->size, clocks,
                              &input)) {
      return false;
    }
    where = resolve_address(cpu, clocks, arith->ea, &input);

    const ap_m68030_operand_result_t memory = ap_m68030_operand_read(
        &cpu->regs, cpu->data, &where, arith->size, cpu->data_function_code);
    *clocks += memory.clocks;
    if (!memory.ok) {
      return false;
    }
    source_operand = memory.value;
  }

  const uint32_t mask = (arith->size == 1u)   ? 0xFFu
                        : (arith->size == 2u) ? 0xFFFFu
                                              : 0xFFFFFFFFu;
  const uint32_t register_value = cpu->regs.d[arith->reg] & mask;
  const uint32_t operand = source_operand & mask;

  /* Which operand is the destination is the direction bit's whole meaning. */
  const uint32_t destination =
      arith->to_effective_address ? operand : register_value;
  const uint32_t source =
      arith->to_effective_address ? register_value : operand;

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
/* ORI, ANDI and EORI to the status register or the condition codes.
 *
 * A separate path from the ordinary immediate operations because the
 * destination is not an operand: there is no effective address, no size field to
 * honour -- the CCR forms are byte and the SR forms word by encoding, not by a
 * size bit -- and the SR forms are privileged where the CCR forms are not.
 *
 * The boot PROM's twenty-first instruction is `ORI #$0700,SR`, masking
 * interrupts before it touches hardware (`FINDINGS.md` C29). `MOVE to SR`
 * already worked here; this is a different encoding, and the whole group was
 * missing together. */
static bool execute_immediate_to_status(ap_m68030_cpu_t *cpu,
                                        const ap_m68030_immediate_t *imm,
                                        uint32_t *clocks) {
  bool to_sr = imm->kind == AP_M68030_IMM_ORI_TO_SR ||
               imm->kind == AP_M68030_IMM_ANDI_TO_SR ||
               imm->kind == AP_M68030_IMM_EORI_TO_SR;

  /* "If Supervisor State ... Else TRAP". The SR forms write the whole status
   * register including the privilege bits; the CCR forms reach only the
   * condition codes and are unprivileged. */
  if (to_sr && !ap_m68030_supervisor(&cpu->regs)) {
    cpu->pending_vector = AP_M68030_VECTOR_PRIVILEGE_VIOLATION;
    return true;
  }

  /* Both forms take a word of immediate data. The CCR forms then use only its
   * low byte -- the high byte is fetched and discarded, which is why the
   * instruction is a word long either way. */
  uint32_t immediate = 0;
  if (!fetch_immediate(cpu, AP_M68030_SIZE_WORD, clocks, &immediate)) {
    return false;
  }

  uint16_t operand = (uint16_t)(immediate & 0xFFFFu);
  uint16_t mask = to_sr ? 0xFFFFu : 0x00FFu;
  uint16_t current = cpu->regs.sr;
  uint16_t result;

  switch (imm->kind) {
  case AP_M68030_IMM_ORI_TO_CCR:
  case AP_M68030_IMM_ORI_TO_SR:
    result = (uint16_t)(current | (operand & mask));
    break;
  case AP_M68030_IMM_ANDI_TO_CCR:
  case AP_M68030_IMM_ANDI_TO_SR:
    /* AND must not clear what the instruction cannot reach: a CCR form ANDs
     * only the low byte, so the high byte of the status register is preserved
     * rather than ANDed against the discarded immediate half. */
    result = (uint16_t)(current & (operand | (uint16_t)~mask));
    break;
  case AP_M68030_IMM_EORI_TO_CCR:
  case AP_M68030_IMM_EORI_TO_SR:
    result = (uint16_t)(current ^ (operand & mask));
    break;
  case AP_M68030_IMM_ORI:
  case AP_M68030_IMM_ANDI:
  case AP_M68030_IMM_SUBI:
  case AP_M68030_IMM_ADDI:
  case AP_M68030_IMM_EORI:
  case AP_M68030_IMM_CMPI:
  case AP_M68030_IMM_MOVES:
  case AP_M68030_IMM_MOVEP:
  case AP_M68030_IMM_BTST:
  case AP_M68030_IMM_BCHG:
  case AP_M68030_IMM_BCLR:
  case AP_M68030_IMM_BSET:
  case AP_M68030_IMM_INVALID:
    /* Not reachable: the caller dispatches only the six status-register forms
     * here. Enumerated rather than defaulted so that adding an immediate kind
     * is a compile error in every switch that must consider it. */
    return false;
  }

  /* Through the register module rather than by assignment: writing the status
   * register can change which stack pointer A7 names, and a raw store would
   * leave the machine claiming a privilege level it had not switched into. */
  ap_m68030_write_sr(&cpu->regs, result);
  return true;
}

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
  case AP_M68030_IMM_ORI_TO_CCR:
  case AP_M68030_IMM_ORI_TO_SR:
  case AP_M68030_IMM_ANDI_TO_CCR:
  case AP_M68030_IMM_ANDI_TO_SR:
  case AP_M68030_IMM_EORI_TO_CCR:
  case AP_M68030_IMM_EORI_TO_SR:
    return execute_immediate_to_status(cpu, imm, clocks);
  case AP_M68030_IMM_MOVES:
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
      resolve_address(cpu, clocks, imm->ea, &input);

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
  /* "If Supervisor State ... Else TRAP". MOVE to SR is obviously privileged;
   * MOVE *from* SR became so on the 68010, and MOVE from CCR -- which the 68000
   * did not have -- is the unprivileged way to read the condition codes. */
  if (ap_m68030_single_privileged(single->kind) &&
      !ap_m68030_supervisor(&cpu->regs)) {
    cpu->pending_vector = AP_M68030_VECTOR_PRIVILEGE_VIOLATION;
    return true;
  }

  if (single->kind == AP_M68030_SINGLE_ILLEGAL) {
    /* "$4AFC ... takes the illegal instruction trap" -- an instruction whose
     * entire purpose is to raise vector 4, so it executes rather than being
     * rejected by the decoder. */
    cpu->pending_vector = AP_M68030_VECTOR_ILLEGAL_INSTRUCTION;
    return true;
  }

  switch (single->kind) {
  case AP_M68030_SINGLE_CLR:
  case AP_M68030_SINGLE_NEG:
  case AP_M68030_SINGLE_NOT:
  case AP_M68030_SINGLE_TST:
  case AP_M68030_SINGLE_NEGX:
  case AP_M68030_SINGLE_TAS:
  case AP_M68030_SINGLE_MOVE_FROM_SR:
  case AP_M68030_SINGLE_MOVE_FROM_CCR:
  case AP_M68030_SINGLE_MOVE_TO_CCR:
  case AP_M68030_SINGLE_MOVE_TO_SR:
    break;
  case AP_M68030_SINGLE_ILLEGAL:
  case AP_M68030_SINGLE_INVALID:
    return false;
  }

  /* `MOVE to SR` and `MOVE to CCR` take their operand as a *source*, and it is
   * almost always an immediate -- `MOVE #$2700,SR` is how every 68000-family
   * boot ROM sets up. An immediate is fetched rather than addressed, so it must
   * be taken before the address calculation, which rejects that mode. This is
   * the fourth place in the step that ordering has mattered. */
  const bool immediate_source =
      single->ea.kind == AP_M68030_EA_IMMEDIATE &&
      (single->kind == AP_M68030_SINGLE_MOVE_TO_SR ||
       single->kind == AP_M68030_SINGLE_MOVE_TO_CCR);

  /* `TST #<data>` is marked "MC68020, MC68030, MC68040, and CPU32" on the TST
   * page -- the 68000 had no such form, which is why a 68000-shaped model
   * refuses it. It only reads its operand, so an immediate is fetched and the
   * flags set from it with nothing written back. */
  if (single->kind == AP_M68030_SINGLE_TST &&
      single->ea.kind == AP_M68030_EA_IMMEDIATE) {
    uint32_t fetched = 0;
    if (!fetch_immediate(cpu, single->size, clocks, &fetched)) {
      return false;
    }
    const ap_m68030_alu_result_t flags =
        ap_m68030_alu_test(fetched, single->size);
    ap_m68030_write_ccr(&cpu->regs,
                        ap_m68030_alu_apply(ap_m68030_read_ccr(&cpu->regs),
                                            &flags));
    return true;
  }

  ap_m68030_address_t where = {0};
  if (immediate_source) {
    uint32_t fetched = 0;
    if (!fetch_immediate(cpu, single->size, clocks, &fetched)) {
      return false;
    }
    if (single->kind == AP_M68030_SINGLE_MOVE_TO_SR) {
      ap_m68030_write_sr(&cpu->regs, (uint16_t)fetched);
    } else {
      ap_m68030_write_ccr(&cpu->regs, (uint16_t)fetched);
    }
    return true;
  }

  ap_m68030_address_input_t input = {0};
  if (!gather_address_input(cpu, single->ea.kind, single->size, clocks,
                            &input)) {
    return false;
  }
  where = resolve_address(cpu, clocks, single->ea, &input);

  /* CLR writes without reading -- and on the 68020 and later that is literal:
   * it does not read the destination at all. The four register transfers take
   * their source from the status register instead of from the operand. */
  const bool reads_destination =
      single->kind != AP_M68030_SINGLE_CLR &&
      single->kind != AP_M68030_SINGLE_MOVE_FROM_SR &&
      single->kind != AP_M68030_SINGLE_MOVE_FROM_CCR;

  uint32_t value = 0;
  if (reads_destination) {
    const ap_m68030_operand_result_t read = ap_m68030_operand_read(
        &cpu->regs, cpu->data, &where, single->size, cpu->data_function_code);
    *clocks += read.clocks;
    if (!read.ok) {
      return false;
    }
    value = read.value;
  }

  /* The four status register transfers are moves, not arithmetic: "Condition
   * Codes: Not affected" for the two reads, and the two writes *are* the
   * condition codes. Handled before the ALU rather than inside it. */
  switch (single->kind) {
  case AP_M68030_SINGLE_MOVE_FROM_SR:
  case AP_M68030_SINGLE_MOVE_FROM_CCR: {
    /* "Unimplemented bits are read as zeros", which ap_m68030_write_sr already
     * guarantees of anything stored there. */
    const uint32_t source =
        (single->kind == AP_M68030_SINGLE_MOVE_FROM_SR)
            ? cpu->regs.sr
            : ap_m68030_read_ccr(&cpu->regs);
    const ap_m68030_operand_result_t wrote =
        ap_m68030_operand_write(&cpu->regs, cpu->data, &where, 2u, source,
                                cpu->data_function_code);
    *clocks += wrote.clocks;
    return wrote.ok;
  }

  case AP_M68030_SINGLE_MOVE_TO_SR:
    ap_m68030_write_sr(&cpu->regs, (uint16_t)value);
    return true;

  case AP_M68030_SINGLE_MOVE_TO_CCR:
    /* "the only portion of the status register (SR) available in the user
     * mode" -- so this cannot reach the system byte however it is written. */
    ap_m68030_write_ccr(&cpu->regs, (uint16_t)value);
    return true;

  case AP_M68030_SINGLE_TAS: {
    /* "Destination Tested -> Condition Codes; 1 -> Bit 7 of Destination",
     * over "a locked or read-modify-write transfer sequence" -- the flags come
     * from the value *before* the bit is set, which is the whole of what makes
     * it usable as a semaphore. */
    const ap_m68030_alu_result_t tested = ap_m68030_alu_test(value, 1u);
    ap_m68030_write_ccr(&cpu->regs,
                        ap_m68030_alu_apply(ap_m68030_read_ccr(&cpu->regs),
                                            &tested));
    const ap_m68030_operand_result_t wrote = ap_m68030_operand_write(
        &cpu->regs, cpu->data, &where, 1u, value | 0x80u,
        cpu->data_function_code);
    *clocks += wrote.clocks;
    return wrote.ok;
  }

  /* The arithmetic forms fall through to the ALU below. Listed rather than
   * defaulted so -Wswitch-enum still forces a decision on a new one. */
  case AP_M68030_SINGLE_NEGX:
  case AP_M68030_SINGLE_CLR:
  case AP_M68030_SINGLE_NEG:
  case AP_M68030_SINGLE_NOT:
  case AP_M68030_SINGLE_TST:
  case AP_M68030_SINGLE_ILLEGAL:
  case AP_M68030_SINGLE_INVALID:
    break;
  }

  const uint16_t ccr_in = ap_m68030_read_ccr(&cpu->regs);
  ap_m68030_alu_result_t result;
  switch (single->kind) {
  case AP_M68030_SINGLE_NEGX:
    /* "0 - Destination - X -> Destination", which is SUBX from zero, Z rule
     * included: "Cleared if the result is nonzero; unchanged otherwise". */
    result = ap_m68030_alu_subx(
        0u, value, single->size,
        ((ccr_in >> AP_M68030_SR_X_BIT) & 1u) != 0u,
        ((ccr_in >> AP_M68030_SR_Z_BIT) & 1u) != 0u);
    break;
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
        resolve_address(cpu, clocks, quick->ea, &input);

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
        resolve_address(cpu, clocks, quick->ea, &input);

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

    /* §11.6.15 publishes three DBcc cases, not two, and the step is the only
     * place that knows which occurred: condition true, condition false with
     * the counter still live, and condition false with it expired. The last is
     * the expensive one -- leaving a loop costs more than going round it. */
    cpu->dbcc_condition_true = condition;
    cpu->dbcc_count_expired = false;

    if (!condition) {
      /* Only the low *word* of the register counts down; the upper half is
       * left alone, so a loop counter cannot borrow into it. */
      const uint16_t counter =
          (uint16_t)((cpu->regs.d[quick->reg] & 0xFFFFu) - 1u);
      cpu->regs.d[quick->reg] =
          (cpu->regs.d[quick->reg] & 0xFFFF0000u) | counter;

      cpu->dbcc_count_expired = !ap_m68030_dbcc_taken(false, counter);
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
        resolve_address(cpu, clocks, arith->ea, &input);
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
      resolve_address(cpu, clocks, imm->ea, &input);

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
        resolve_address(cpu, clocks, shift->ea, &input);

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
  *where = resolve_address(cpu, clocks, ea, &input);

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
      resolve_address(cpu, clocks, ea, &input);

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
          resolve_address(cpu, clocks, arith->ea, &input);
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
          resolve_address(cpu, clocks, arith->ea, &input);
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

/* The body of taking an exception, with the stacked status register supplied
 * rather than read here. Every exception but an interrupt stacks the register as
 * it stood on entry; an interrupt raises the priority mask first and must stack
 * the copy taken *before* that, so the two cannot share a single read. */
static ap_m68030_exception_result_t take_exception_with(
    ap_m68030_cpu_t *cpu, unsigned vector, uint32_t stacked_pc,
    uint32_t instruction_address, uint16_t saved_sr) {
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

  /* Step one. The copy was taken *before* the register was changed, and it is
   * the copy that gets stacked -- so RTE restores the privilege level the
   * exception interrupted, not the one the handler ran in. */
  uint16_t updated = cpu->regs.sr;
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

ap_m68030_exception_result_t
ap_m68030_take_exception(ap_m68030_cpu_t *cpu, unsigned vector,
                         uint32_t stacked_pc, uint32_t instruction_address) {
  return take_exception_with(cpu, vector, stacked_pc, instruction_address,
                             cpu->regs.sr);
}

ap_m68030_exception_result_t ap_m68030_take_interrupt(ap_m68030_cpu_t *cpu) {
  ap_m68030_exception_result_t out = {0};

  const unsigned level = cpu->interrupt_level;
  if (!ap_m68030_interrupt_recognised(level, cpu->previous_interrupt_level,
                                      ap_m68030_interrupt_mask(&cpu->regs))) {
    cpu->previous_interrupt_level = level;
    return out;
  }
  cpu->previous_interrupt_level = level;

  /* "the processor first makes an internal copy of the status register" -- and
   * this copy is what both frames carry. Taken before the mask is raised, or
   * RTE would restore the handler's mask and the interrupted code would never
   * see another interrupt at its own level. */
  const uint16_t saved_sr = cpu->regs.sr;
  const bool master = ap_m68030_master(&cpu->regs);

  /* "sets the processor interrupt mask level to the level of the interrupt
   * being serviced", so the handler is not re-entered by its own device. */
  uint16_t raised = cpu->regs.sr;
  raised &= (uint16_t)~(AP_M68030_SR_INTERRUPT_MASK
                        << AP_M68030_SR_INTERRUPT_SHIFT);
  raised |= (uint16_t)((level & AP_M68030_SR_INTERRUPT_MASK)
                       << AP_M68030_SR_INTERRUPT_SHIFT);
  ap_m68030_write_sr(&cpu->regs, raised);

  /* "The processor attempts to obtain a vector number from the interrupting
   * device using an interrupt acknowledge bus cycle." */
  unsigned vector = AP_M68030_VECTOR_SPURIOUS_INTERRUPT;
  if (cpu->acknowledge != NULL) {
    const ap_m68030_iack_t answer =
        cpu->acknowledge(cpu->acknowledge_context, level);
    if (answer.bus_error) {
      /* "If external logic indicates a bus error during the interrupt
       * acknowledge cycle, the interrupt is considered spurious". */
      vector = AP_M68030_VECTOR_SPURIOUS_INTERRUPT;
    } else if (answer.autovector) {
      vector = ap_m68030_autovector(level);
    } else {
      vector = answer.vector;
    }
  }

  /* "The saved value of the program counter is the logical address of the
   * instruction that would have been executed had the interrupt not occurred"
   * -- so the PC as it stands, since an interrupt is taken between
   * instructions and nothing has been fetched for the next one. */
  const uint32_t resume = cpu->regs.pc;
  out = take_exception_with(cpu, vector, resume, resume, saved_sr);
  if (!out.ok) {
    return out;
  }

  if (!master) {
    return out;
  }

  /* "If the M bit of the status register is set, the processor clears the M bit
   * and creates a throwaway exception stack frame on top of the interrupt
   * stack." Clearing M first is what moves A7 from the master stack to the
   * interrupt stack, so the second frame lands on the other one -- which is the
   * whole point, and is why the order is not an implementation detail. */
  uint16_t without_master = cpu->regs.sr;
  without_master &= (uint16_t)~(1u << AP_M68030_SR_M_BIT);
  ap_m68030_write_sr(&cpu->regs, without_master);

  /* "This second frame contains the same program counter value and vector
   * offset as the frame created on top of the master stack, but has a format
   * number of 1", and "The copy of the status register saved on the throwaway
   * frame is exactly the same as that placed on the master stack except that
   * the S bit is set". */
  const uint16_t throwaway_sr =
      (uint16_t)(saved_sr | (uint16_t)(1u << AP_M68030_SR_S_BIT));
  const uint32_t frame = ap_m68030_read_a7(&cpu->regs) - 8u;

  bool wrote = write_frame_field(cpu, frame + 0u, 2u, throwaway_sr, &out.clocks);
  wrote = wrote && write_frame_field(cpu, frame + 2u, 4u, resume, &out.clocks);
  wrote = wrote && write_frame_field(
                       cpu, frame + 6u, 2u,
                       ap_m68030_frame_format_word(AP_M68030_FRAME_THROWAWAY,
                                                   vector),
                       &out.clocks);
  if (!wrote) {
    out.ok = false;
    return out;
  }
  ap_m68030_write_a7(&cpu->regs, frame);
  out.frame_address = frame;
  return out;
}

/* ---------------------------------------------------------------------------
 * The $4E control group's stack discipline.
 * ------------------------------------------------------------------------- */

/* Push a long word, which is what JSR, LINK and the frame builders all do. */
static bool push_long(ap_m68030_cpu_t *cpu, uint32_t value, uint32_t *clocks) {
  const uint32_t sp = ap_m68030_read_a7(&cpu->regs) - 4u;
  if (!write_frame_field(cpu, sp, 4u, value, clocks)) {
    return false;
  }
  ap_m68030_write_a7(&cpu->regs, sp);
  return true;
}

/* Read from the active stack without moving it; the caller decides the step,
 * because the frame instructions pop by fixed offsets rather than one at a
 * time. */
static bool read_stack(ap_m68030_cpu_t *cpu, uint32_t offset, unsigned size,
                       uint32_t *clocks, uint32_t *value) {
  const ap_m68030_address_t where = {
      .address = ap_m68030_read_a7(&cpu->regs) + offset, .valid = true};
  const ap_m68030_operand_result_t read =
      ap_m68030_operand_read(&cpu->regs, cpu->data, &where, size,
                             AP_M68030_FC_SUPERVISOR_DATA);
  *clocks += read.clocks;
  if (!read.ok) {
    return false;
  }
  *value = read.value;
  return true;
}

/* Land on a new PC, emptying the pipe -- every change of flow does this, and
 * forgetting it lets a prefetched word from the old path execute. */
static void jump_to(ap_m68030_cpu_t *cpu, uint32_t target) {
  cpu->regs.pc = target;
  ap_m68030_fetch_reset(&cpu->fetch, target);
}

/* RTE, `[030]` §8.1: "it examines the stack frame on top of the active
 * supervisor stack to determine if it is a valid frame and what type of context
 * restoration it requires."
 *
 * "For a normal four-word frame, the processor updates the status register and
 * program counter with the data read from the stack, increments the stack
 * pointer by eight"; the six-word frame is the same with 12. The throwaway
 * frame is the interesting one -- the processor reads only the SR, adds eight,
 * "and then begins RTE processing again", on whichever stack the restored S and
 * M bits now select. So it is a loop, not a special case, and the frame it
 * finds next "may be any format (even another throwaway frame)".
 *
 * An undefined format is a format error, vector 14. */
static bool execute_rte(ap_m68030_cpu_t *cpu, uint32_t *clocks) {
  /* A throwaway frame can chain, and the manual allows another throwaway
   * behind it. Bounded so a corrupt stack of throwaways cannot spin forever;
   * the bound is this model's, not the hardware's, and a run that hits it is
   * reported as a format error rather than silently stopping. */
  for (unsigned frames = 0; frames < 8u; frames++) {
    uint32_t format_word = 0;
    if (!read_stack(cpu, 6u, 2u, clocks, &format_word)) {
      return false;
    }
    if (!ap_m68030_frame_format_defined((uint16_t)format_word)) {
      cpu->pending_vector = AP_M68030_VECTOR_FORMAT_ERROR;
      return true;
    }
    const ap_m68030_frame_format_t format =
        ap_m68030_frame_format_of((uint16_t)format_word);

    uint32_t saved_sr = 0;
    if (!read_stack(cpu, 0u, 2u, clocks, &saved_sr)) {
      return false;
    }

    if (format == AP_M68030_FRAME_THROWAWAY) {
      /* Only the status register is restored, and the stack it selects may be
       * a different one -- which is exactly why the pointer is moved on the
       * *old* stack before the register is written. */
      ap_m68030_write_a7(&cpu->regs, ap_m68030_read_a7(&cpu->regs) + 8u);
      ap_m68030_write_sr(&cpu->regs, (uint16_t)saved_sr);
      continue;
    }

    if (format != AP_M68030_FRAME_SHORT && format != AP_M68030_FRAME_SIX_WORD) {
      /* The fault and coprocessor frames restore internal state this model does
       * not carry. Declined rather than half-restored. */
      return false;
    }

    uint32_t saved_pc = 0;
    if (!read_stack(cpu, 2u, 4u, clocks, &saved_pc)) {
      return false;
    }
    ap_m68030_write_a7(&cpu->regs,
                       ap_m68030_read_a7(&cpu->regs) +
                           ap_m68030_frame_words(format) * 2u);
    /* The status register goes back *whole*, system byte included -- that is
     * what returns a user program to user state. Writing only the CCR would
     * leave the handler's supervisor bit in place. */
    ap_m68030_write_sr(&cpu->regs, (uint16_t)saved_sr);
    jump_to(cpu, saved_pc);
    return true;
  }

  cpu->pending_vector = AP_M68030_VECTOR_FORMAT_ERROR;
  return true;
}

static bool execute_control(ap_m68030_cpu_t *cpu,
                            const ap_m68030_control_t *control,
                            uint32_t *clocks, bool *branched) {
  *branched = false;

  /* "If Supervisor State ... Else TRAP". Four instructions in this group are
   * privileged, and the failure mode is silent: a user program able to run
   * them could halt the processor or forge a return from exception. The
   * privilege violation's stacked PC is "First word of instruction causing
   * Privilege Violation", which is where the PC still is. */
  if (ap_m68030_control_privileged(control->kind) &&
      !ap_m68030_supervisor(&cpu->regs)) {
    cpu->pending_vector = AP_M68030_VECTOR_PRIVILEGE_VIOLATION;
    return true;
  }

  switch (control->kind) {
  case AP_M68030_CTL_NOP:
    return true;

  case AP_M68030_CTL_TRAP:
    cpu->pending_vector = ap_m68030_control_trap_vector(control);
    return true;

  case AP_M68030_CTL_TRAPV:
    /* "If V then TRAP" -- and when V is clear this is a no-op, not a branch. */
    if ((ap_m68030_read_ccr(&cpu->regs) & (1u << AP_M68030_SR_V_BIT)) != 0u) {
      cpu->pending_vector = AP_M68030_VECTOR_TRAPCC;
    }
    return true;

  case AP_M68030_CTL_RTS: {
    /* "(SP) -> PC; SP + 4 -> SP". */
    uint32_t target = 0;
    if (!read_stack(cpu, 0u, 4u, clocks, &target)) {
      return false;
    }
    ap_m68030_write_a7(&cpu->regs, ap_m68030_read_a7(&cpu->regs) + 4u);
    jump_to(cpu, target);
    *branched = true;
    return true;
  }

  case AP_M68030_CTL_RTR: {
    /* "(SP) -> CCR; SP + 2 -> SP; (SP) -> PC; SP + 4 -> SP", and "The
     * supervisor portion of the status register is unaffected" -- so this
     * writes the CCR, never the whole SR. RTR restoring the system byte would
     * be an unprivileged instruction that changes the privilege level. */
    uint32_t saved_ccr = 0;
    uint32_t target = 0;
    if (!read_stack(cpu, 0u, 2u, clocks, &saved_ccr) ||
        !read_stack(cpu, 2u, 4u, clocks, &target)) {
      return false;
    }
    ap_m68030_write_a7(&cpu->regs, ap_m68030_read_a7(&cpu->regs) + 6u);
    ap_m68030_write_ccr(&cpu->regs, (uint16_t)saved_ccr);
    jump_to(cpu, target);
    *branched = true;
    return true;
  }

  case AP_M68030_CTL_RTE:
    if (!execute_rte(cpu, clocks)) {
      return false;
    }
    *branched = cpu->pending_vector == 0u;
    return true;

  case AP_M68030_CTL_RTD: {
    /* "(SP) -> PC; SP + 4 + dn -> SP": the return address comes off first and
     * the displacement then releases the caller's arguments. */
    uint16_t displacement = 0;
    if (!next_word(cpu, clocks, &displacement)) {
      return false;
    }
    uint32_t target = 0;
    if (!read_stack(cpu, 0u, 4u, clocks, &target)) {
      return false;
    }
    ap_m68030_write_a7(&cpu->regs,
                       ap_m68030_read_a7(&cpu->regs) + 4u +
                           (uint32_t)(int32_t)(int16_t)displacement);
    jump_to(cpu, target);
    *branched = true;
    return true;
  }

  case AP_M68030_CTL_LINK: {
    /* "SP - 4 -> SP; An -> (SP); SP -> An; SP + dn -> SP". The order is the
     * instruction: the register is pushed, *then* takes the new stack pointer,
     * which is what makes LINK A6 build a frame chain. And "The user should
     * specify a negative displacement in order to allocate stack area" -- the
     * displacement is added, so allocation is a negative number. */
    uint16_t displacement = 0;
    if (!next_word(cpu, clocks, &displacement)) {
      return false;
    }
    const uint32_t saved =
        ap_m68030_read_address_register(&cpu->regs, control->reg);
    if (!push_long(cpu, saved, clocks)) {
      return false;
    }
    const uint32_t sp = ap_m68030_read_a7(&cpu->regs);
    ap_m68030_write_address_register(&cpu->regs, control->reg, sp);
    ap_m68030_write_a7(&cpu->regs,
                       sp + (uint32_t)(int32_t)(int16_t)displacement);
    return true;
  }

  case AP_M68030_CTL_UNLK: {
    /* "An -> SP; (SP) -> An; SP + 4 -> SP", and again the order is the whole
     * instruction: the stack pointer is loaded from the register *before* the
     * register is reloaded from the stack, which is what releases the frame
     * however far the callee moved the stack. */
    ap_m68030_write_a7(&cpu->regs,
                       ap_m68030_read_address_register(&cpu->regs,
                                                       control->reg));
    uint32_t saved = 0;
    if (!read_stack(cpu, 0u, 4u, clocks, &saved)) {
      return false;
    }
    ap_m68030_write_a7(&cpu->regs, ap_m68030_read_a7(&cpu->regs) + 4u);
    ap_m68030_write_address_register(&cpu->regs, control->reg, saved);
    return true;
  }

  case AP_M68030_CTL_JSR:
  case AP_M68030_CTL_JMP: {
    /* Both jump to the effective *address*, not to what is there: "JMP <ea>"
     * loads the address itself into the PC. So the address is calculated and
     * used, never read through. */
    ap_m68030_address_input_t input = {0};
    if (!gather_address_input(cpu, control->ea.kind, 4u, clocks, &input)) {
      return false;
    }
    const ap_m68030_address_t where =
        resolve_address(cpu, clocks, control->ea, &input);
    if (!where.valid || where.in_register || where.immediate ||
        where.indirection_pending) {
      return false;
    }

    if (control->kind == AP_M68030_CTL_JSR) {
      /* "SP - 4 -> SP; PC -> (SP); Destination Address -> PC" -- and the PC
       * pushed is the one *after* this instruction, extension words included,
       * which is why the count of words taken is read here rather than
       * predicted. */
      if (!push_long(cpu, cpu->regs.pc + 2u + 2u * cpu->extension_words,
                     clocks)) {
        return false;
      }
    }
    jump_to(cpu, where.address);
    *branched = true;
    return true;
  }

  case AP_M68030_CTL_MOVE_TO_USP:
    /* "MOVE An,USP": the *user* stack pointer, written directly rather than
     * through A7 -- this only executes in supervisor state, where A7 names the
     * ISP or MSP, so going through A7 would move the wrong stack and leave the
     * one being set up untouched. */
    cpu->regs.usp =
        ap_m68030_read_address_register(&cpu->regs, control->reg);
    return true;

  case AP_M68030_CTL_MOVE_FROM_USP:
    ap_m68030_write_address_register(&cpu->regs, control->reg, cpu->regs.usp);
    return true;

  case AP_M68030_CTL_RESET:
    /* "asserts the RSTO signal for 512 clock periods, resetting all external
     * devices. The processor state, other than the program counter, is
     * unaffected, and execution continues with the next instruction." So this
     * changes nothing inside the processor -- counting it is the whole of the
     * observable effect until there are devices to reset. */
    cpu->external_resets++;
    return true;

  case AP_M68030_CTL_STOP: {
    /* "Immediate Data -> SR; STOP". The status register is loaded *first* --
     * including its interrupt mask, which is the point: STOP is how a
     * supervisor waits for an interrupt at a chosen priority, and loading the
     * mask afterwards would leave a window at the old one. */
    uint16_t immediate = 0;
    if (!next_word(cpu, clocks, &immediate)) {
      return false;
    }
    ap_m68030_write_sr(&cpu->regs, immediate);
    cpu->stopped = true;
    return true;
  }

  case AP_M68030_CTL_MOVEC_FROM_CONTROL:
  case AP_M68030_CTL_MOVEC_TO_CONTROL: {
    /* "This is always a 32-bit transfer, even though the control register may
     * be implemented with fewer bits. Unimplemented bits are read as zeros." */
    uint16_t extension = 0;
    if (!next_word(cpu, clocks, &extension)) {
      return false;
    }
    const bool general_is_address = (extension & 0x8000u) != 0u;
    const unsigned general = (unsigned)((extension >> 12) & 0x7u);
    const unsigned which = (unsigned)(extension & 0x0FFFu);
    const bool to_control = control->kind == AP_M68030_CTL_MOVEC_TO_CONTROL;

    if (to_control) {
      const uint32_t value =
          general_is_address
              ? ap_m68030_read_address_register(&cpu->regs, general)
              : cpu->regs.d[general];
      switch (which) {
      case AP_M68030_CONTROL_SFC:
        /* Three bits wide; the rest read as zero, so they are not stored. */
        cpu->regs.sfc = (uint8_t)(value & 0x7u);
        return true;
      case AP_M68030_CONTROL_DFC:
        cpu->regs.dfc = (uint8_t)(value & 0x7u);
        return true;
      case AP_M68030_CONTROL_CACR:
        /* The clears happen "at the time a MOVEC instruction loads a one into"
         * the bit, so they are part of this write and use the CAAR index. */
        ap_m68030_cacr_write(&cpu->cacr, value, cpu->fetch.access->cache,
                             cpu->data->cache, cpu->caar);
        return true;
      case AP_M68030_CONTROL_USP:
        cpu->regs.usp = value;
        return true;
      case AP_M68030_CONTROL_VBR:
        cpu->regs.vbr = value;
        return true;
      case AP_M68030_CONTROL_CAAR:
        cpu->caar = value;
        return true;
      case AP_M68030_CONTROL_MSP:
        cpu->regs.msp = value;
        return true;
      case AP_M68030_CONTROL_ISP:
        cpu->regs.isp = value;
        return true;
      default:
        /* "If an attempt is made to access a control register that is not
         * defined ... an illegal instruction exception occurs." A code this
         * part does not implement is not a no-op. */
        cpu->pending_vector = AP_M68030_VECTOR_ILLEGAL_INSTRUCTION;
        return true;
      }
    }

    uint32_t value = 0;
    switch (which) {
    case AP_M68030_CONTROL_SFC:
      value = cpu->regs.sfc;
      break;
    case AP_M68030_CONTROL_DFC:
      value = cpu->regs.dfc;
      break;
    case AP_M68030_CONTROL_CACR:
      value = ap_m68030_cacr_pack(&cpu->cacr);
      break;
    case AP_M68030_CONTROL_USP:
      value = cpu->regs.usp;
      break;
    case AP_M68030_CONTROL_VBR:
      value = cpu->regs.vbr;
      break;
    case AP_M68030_CONTROL_CAAR:
      value = cpu->caar;
      break;
    case AP_M68030_CONTROL_MSP:
      value = cpu->regs.msp;
      break;
    case AP_M68030_CONTROL_ISP:
      value = cpu->regs.isp;
      break;
    default:
      cpu->pending_vector = AP_M68030_VECTOR_ILLEGAL_INSTRUCTION;
      return true;
    }

    if (general_is_address) {
      ap_m68030_write_address_register(&cpu->regs, general, value);
    } else {
      cpu->regs.d[general] = value;
    }
    return true;
  }

  case AP_M68030_CTL_INVALID:
    return false;
  }
  return false;
}

/* MOVEM's register list mask, `M68000PRM` MOVEM page.
 *
 * "The low-order bit corresponds to the first register to be transferred; the
 * high-order bit corresponds to the last register to be transferred. Thus, for
 * both control modes and postincrement mode addresses, the mask correspondence
 * is: [bit 15 A7 ... bit 0 D0]. For the predecrement mode addresses, the mask
 * correspondence is reversed: [bit 15 D0 ... bit 0 A7]."
 *
 * So there is one loop, bit 0 through bit 15, and only the naming changes --
 * which is also what makes the *order* right in both directions: control mode
 * transfers "from D0 to D7, then from A0 to A7", and predecrement stores "from
 * A7 to A0, then from D7 to D0".
 *
 * Reading the mask the same way round for both is the mistake, and it produces
 * a MOVEM that saves the right number of registers into the right amount of
 * space with every one in the wrong place. */
static unsigned movem_register(unsigned bit, bool predecrement) {
  return predecrement ? (15u - bit) : bit;
}

/* Registers 0-7 are D0-D7 and 8-15 are A0-A7. */
static uint32_t movem_read_register(const ap_m68030_regs_t *regs,
                                    unsigned index) {
  return (index < 8u) ? regs->d[index]
                      : ap_m68030_read_address_register(regs, index - 8u);
}

static void movem_write_register(ap_m68030_regs_t *regs, unsigned index,
                                 uint32_t value) {
  if (index < 8u) {
    regs->d[index] = value;
  } else {
    ap_m68030_write_address_register(regs, index - 8u, value);
  }
}

static bool execute_movem(ap_m68030_cpu_t *cpu, const ap_m68030_misc_t *misc,
                          uint32_t *clocks) {
  uint16_t mask = 0;
  if (!next_word(cpu, clocks, &mask)) {
    return false;
  }

  const bool to_memory = misc->kind == AP_M68030_MISC_MOVEM_TO_MEMORY;
  const bool predecrement = misc->ea.kind == AP_M68030_EA_PREDECREMENT;
  const bool postincrement = misc->ea.kind == AP_M68030_EA_POSTINCREMENT;

  /* "only the control modes, the predecrement mode, and the postincrement mode
   * are valid", and each direction takes only one of the two increment modes:
   * predecrement is register-to-memory only, postincrement memory-to-register
   * only. The other combinations are not this instruction. */
  if ((predecrement && !to_memory) || (postincrement && to_memory)) {
    return false;
  }

  uint32_t address = 0;
  if (predecrement || postincrement) {
    /* The increment modes walk the address themselves, register by register,
     * rather than through one calculation -- which is why
     * ap_m68030_address_step is exposed. Start from the register as it is. */
    address = ap_m68030_read_address_register(&cpu->regs, misc->ea.reg);
  } else {
    ap_m68030_address_input_t input = {0};
    if (!gather_address_input(cpu, misc->ea.kind, misc->size, clocks, &input)) {
      return false;
    }
    const ap_m68030_address_t where =
        resolve_address(cpu, clocks, misc->ea, &input);
    if (!where.valid || where.in_register || where.immediate ||
        where.indirection_pending) {
      return false;
    }
    address = where.address;
  }

  for (unsigned bit = 0; bit < 16u; bit++) {
    if ((mask & (1u << bit)) == 0u) {
      continue;
    }
    const unsigned index = movem_register(bit, predecrement);

    if (predecrement) {
      /* "The registers are stored starting at the specified address minus the
       * operand length", so the decrement comes first. */
      address -= misc->size;
    }

    const ap_m68030_address_t where = {.address = address, .valid = true};
    if (to_memory) {
      uint32_t value = movem_read_register(&cpu->regs, index);
      if (predecrement && index == misc->ea.reg + 8u) {
        /* "For the MC68020, MC68030, MC68040, and CPU32, if the addressing
         * register is also moved to memory, the value written is the initial
         * register value decremented by the size of the operation. The MC68000
         * and MC68010 write the initial register value (not decremented)." So
         * this is a part-specific difference, and this part is the later one. */
        value -= misc->size;
      }
      const ap_m68030_operand_result_t wrote = ap_m68030_operand_write(
          &cpu->regs, cpu->data, &where, misc->size, value,
          cpu->data_function_code);
      *clocks += wrote.clocks;
      if (!wrote.ok) {
        return false;
      }
    } else {
      const ap_m68030_operand_result_t read = ap_m68030_operand_read(
          &cpu->regs, cpu->data, &where, misc->size, cpu->data_function_code);
      *clocks += read.clocks;
      if (!read.ok) {
        return false;
      }
      /* "In the case of a word transfer to either address or data registers,
       * each word is sign-extended to 32 bits, and the resulting long word is
       * loaded into the associated register." A *data* register write that
       * replaces all 32 bits is unlike every other one, and a model that made
       * it partial would leave stale halves behind. */
      movem_write_register(&cpu->regs, index,
                           ap_m68030_sign_extend(read.value, misc->size));
    }

    if (!predecrement) {
      address += misc->size;
    }
  }

  if (predecrement || postincrement) {
    /* "When the instruction has completed, the decremented address register
     * contains the address of the last operand stored", and for postincrement
     * "the address of the last operand loaded plus the operand length" -- both
     * of which are simply where the walk stopped.
     *
     * Postincrement also has "If the addressing register is also loaded from
     * memory, the memory value is ignored and the register is written with the
     * postincremented effective address", which this ordering gives for free:
     * the walk's final write happens after the transfer loaded it. */
    ap_m68030_write_address_register(&cpu->regs, misc->ea.reg, address);
  }
  return true;
}

static bool execute_misc(ap_m68030_cpu_t *cpu, const ap_m68030_misc_t *misc,
                         uint32_t *clocks) {
  switch (misc->kind) {
  case AP_M68030_MISC_SWAP: {
    /* "Register 31-16 <-> Register 15-0", and N and Z come from the whole
     * 32-bit result rather than from either half. */
    const uint32_t value = cpu->regs.d[misc->reg];
    const uint32_t swapped = (value >> 16) | (value << 16);
    cpu->regs.d[misc->reg] = swapped;
    const ap_m68030_alu_result_t flags = ap_m68030_alu_test(swapped, 4u);
    ap_m68030_write_ccr(&cpu->regs,
                        ap_m68030_alu_apply(ap_m68030_read_ccr(&cpu->regs),
                                            &flags));
    return true;
  }

  case AP_M68030_MISC_EXT_WORD:
  case AP_M68030_MISC_EXT_LONG:
  case AP_M68030_MISC_EXTB_LONG: {
    /* "by replicating the sign bit to the left". EXT.W writes only the low
     * word, so the upper half survives; EXT.L and EXTB.L write all 32 bits.
     * The difference is what makes EXT.W after EXT.W not the same as EXT.L. */
    const uint32_t value = cpu->regs.d[misc->reg];
    uint32_t result;
    unsigned flag_size;
    if (misc->kind == AP_M68030_MISC_EXT_WORD) {
      const uint32_t extended = ap_m68030_sign_extend(value & 0xFFu, 1u);
      result = (value & 0xFFFF0000u) | (extended & 0xFFFFu);
      flag_size = 2u;
    } else if (misc->kind == AP_M68030_MISC_EXT_LONG) {
      result = ap_m68030_sign_extend(value & 0xFFFFu, 2u);
      flag_size = 4u;
    } else {
      result = ap_m68030_sign_extend(value & 0xFFu, 1u);
      flag_size = 4u;
    }
    cpu->regs.d[misc->reg] = result;
    const ap_m68030_alu_result_t flags = ap_m68030_alu_test(result, flag_size);
    ap_m68030_write_ccr(&cpu->regs,
                        ap_m68030_alu_apply(ap_m68030_read_ccr(&cpu->regs),
                                            &flags));
    return true;
  }

  case AP_M68030_MISC_MOVEM_TO_MEMORY:
  case AP_M68030_MISC_MOVEM_TO_REGISTERS:
    return execute_movem(cpu, misc, clocks);

  case AP_M68030_MISC_LEA:
  case AP_M68030_MISC_PEA:
    /* Both take *control* modes: there must be an address, and no size attached
     * to it, which is why `LEA (A0),A1` is legal and `LEA (A0)+,A1` is not.
     *
     * The check has to come before the address is calculated, not after: the
     * calculation applies the increment and decrement side effects, so a
     * refusal that happened afterwards would already have moved the register.
     * An instruction the processor refuses must leave no trace. */
    if (!ap_m68030_ea_is_control(misc->ea.kind)) {
      return false;
    }
    break;

  case AP_M68030_MISC_NBCD:
    /* "NBCD <ea>" writes its operand back, so its mode must be data
     * alterable. */
    if (!ap_m68030_ea_is_data_alterable(misc->ea.kind)) {
      return false;
    }
    break;

  case AP_M68030_MISC_CHK_WORD:
  case AP_M68030_MISC_CHK_LONG:
    /* "CHK <ea>,Dn" only reads its bound, so every data mode is legal --
     * including the immediate, which is how the bound is usually written. */
    if (!ap_m68030_ea_is_data(misc->ea.kind)) {
      return false;
    }
    break;

  case AP_M68030_MISC_BKPT:
  case AP_M68030_MISC_INVALID:
    /* BKPT runs a breakpoint acknowledge cycle in CPU space, which is a bus
     * transaction this step does not issue. Declined rather than treated as an
     * illegal instruction, which is what it becomes only if nothing answers. */
    return false;
  }

  /* Everything left takes an effective address. */
  const unsigned operand_size =
      (misc->kind == AP_M68030_MISC_CHK_LONG)   ? 4u
      : (misc->kind == AP_M68030_MISC_CHK_WORD) ? 2u
      : (misc->kind == AP_M68030_MISC_NBCD)     ? 1u
                                                : 4u;
  /* CHK's bound is commonly an immediate, and an immediate is *fetched* rather
   * than addressed -- gather_address_input has no address to gather for it. So
   * it is taken first, as it is on every other path that accepts one. LEA and
   * PEA cannot take an immediate at all: there is no address to load. */
  bool bound_is_immediate = false;
  uint32_t immediate_bound = 0;
  ap_m68030_address_t where = {0};

  if (misc->ea.kind == AP_M68030_EA_IMMEDIATE) {
    if (misc->kind != AP_M68030_MISC_CHK_WORD &&
        misc->kind != AP_M68030_MISC_CHK_LONG) {
      return false;
    }
    if (!fetch_immediate(cpu, operand_size, clocks, &immediate_bound)) {
      return false;
    }
    bound_is_immediate = true;
  } else {
    ap_m68030_address_input_t input = {0};
    if (!gather_address_input(cpu, misc->ea.kind, operand_size, clocks,
                              &input)) {
      return false;
    }
    where = resolve_address(cpu, clocks, misc->ea, &input);
    if (!where.valid || where.indirection_pending) {
      return false;
    }
  }

  switch (misc->kind) {
  case AP_M68030_MISC_LEA:
    /* "<ea> -> An": the address itself, not what is there -- which is the whole
     * difference between LEA and MOVEA. Condition codes are not affected. */
    ap_m68030_write_address_register(&cpu->regs, misc->reg, where.address);
    return true;

  case AP_M68030_MISC_PEA:
    /* "SP - 4 -> SP; <ea> -> (SP)" -- again the address, pushed. */
    return push_long(cpu, where.address, clocks);

  case AP_M68030_MISC_NBCD: {
    /* "0 - Destination10 - X -> Destination" -- the same decimal subtract as
     * SBCD, from zero, so it is the tens complement with X clear and the nines
     * complement with X set. */
    const ap_m68030_operand_result_t read = ap_m68030_operand_read(
        &cpu->regs, cpu->data, &where, 1u, cpu->data_function_code);
    *clocks += read.clocks;
    if (!read.ok) {
      return false;
    }
    const uint16_t ccr = ap_m68030_read_ccr(&cpu->regs);
    const ap_m68030_alu_result_t result = ap_m68030_alu_sbcd(
        0u, read.value, ((ccr >> AP_M68030_SR_X_BIT) & 1u) != 0u,
        ((ccr >> AP_M68030_SR_Z_BIT) & 1u) != 0u);
    const ap_m68030_operand_result_t wrote = ap_m68030_operand_write(
        &cpu->regs, cpu->data, &where, 1u, result.result,
        cpu->data_function_code);
    *clocks += wrote.clocks;
    if (!wrote.ok) {
      return false;
    }
    ap_m68030_write_ccr(&cpu->regs, ap_m68030_alu_apply(ccr, &result));
    return true;
  }

  case AP_M68030_MISC_CHK_WORD:
  case AP_M68030_MISC_CHK_LONG: {
    /* "If Dn < 0 or Dn > Source Then TRAP". Both comparisons are *signed* --
     * "The upper bound is a twos complement integer" -- so an unsigned compare
     * would let a negative register pass whenever the bound's top bit is
     * clear, which is almost always. */
    uint32_t raw_bound = immediate_bound;
    if (!bound_is_immediate) {
      const ap_m68030_operand_result_t read =
          ap_m68030_operand_read(&cpu->regs, cpu->data, &where, operand_size,
                                 cpu->data_function_code);
      *clocks += read.clocks;
      if (!read.ok) {
        return false;
      }
      raw_bound = read.value;
    }
    const int32_t value = (int32_t)ap_m68030_sign_extend(
        cpu->regs.d[misc->reg], operand_size);
    const int32_t bound =
        (int32_t)ap_m68030_sign_extend(raw_bound, operand_size);

    /* "N -- Set if Dn < 0; cleared if Dn > effective address operand;
     * undefined otherwise." Z, V and C are all undefined, so only N is set. */
    uint16_t ccr = ap_m68030_read_ccr(&cpu->regs);
    if (value < 0) {
      ccr |= (uint16_t)(1u << AP_M68030_SR_N_BIT);
    } else if (value > bound) {
      ccr &= (uint16_t)~(1u << AP_M68030_SR_N_BIT);
    }
    ap_m68030_write_ccr(&cpu->regs, ccr);

    if (value < 0 || value > bound) {
      cpu->pending_vector = AP_M68030_VECTOR_CHK;
    }
    return true;
  }

  case AP_M68030_MISC_SWAP:
  case AP_M68030_MISC_BKPT:
  case AP_M68030_MISC_EXT_WORD:
  case AP_M68030_MISC_EXT_LONG:
  case AP_M68030_MISC_EXTB_LONG:
  case AP_M68030_MISC_MOVEM_TO_MEMORY:
  case AP_M68030_MISC_MOVEM_TO_REGISTERS:
  case AP_M68030_MISC_INVALID:
    return false;
  }
  return false;
}

/* ---------------------------------------------------------------------------
 * PMOVE, `M68000PRM` "Move to/from MMU Registers (MC68030 only)".
 *
 * Three instruction formats, told apart by the extension word's top three bits:
 *
 *   010  SRP, CRP and TC     P-REGISTER 000 TC, 010 SRP, 011 CRP
 *   011  MMU status register P-REGISTER 000, and no FD bit
 *   000  TT0 and TT1         P-REGISTER 010 TT0, 011 TT1
 *
 * with R/W at bit 9 and FD at bit 8. Note that P-REGISTER `010` is the
 * supervisor root pointer under one prefix and TT0 under another: the prefix is
 * not decoration, and a decoder reading only the P-REGISTER field would write a
 * transparent translation register where a root pointer belongs.
 *
 * "The instruction is a quad-word (8 byte) operation for the CPU root pointer
 * and the supervisor root pointer. It is a long-word operation for the
 * translation control register and the transparent translation registers (TT0
 * and TT1). It is a word operation for the MMU status register."
 *
 * "Only control alterable addressing modes can be used", which is what the
 * 68030 means by "Reduced Instruction Set ... Only Control-Alterable Addressing
 * Modes Supported for MMU Instructions".
 * ------------------------------------------------------------------------- */

typedef enum {
  AP_PMOVE_TC,
  AP_PMOVE_SRP,
  AP_PMOVE_CRP,
  AP_PMOVE_TT0,
  AP_PMOVE_TT1,
  AP_PMOVE_MMUSR,
  AP_PMOVE_NONE,
} pmove_register_t;

static pmove_register_t pmove_register(uint16_t extension) {
  const unsigned prefix = (unsigned)((extension >> 13) & 0x7u);
  const unsigned which = (unsigned)((extension >> 10) & 0x7u);

  switch (prefix) {
  case 0x2u: /* 010: the root pointers and TC */
    switch (which) {
    case 0x0u:
      return AP_PMOVE_TC;
    case 0x2u:
      return AP_PMOVE_SRP;
    case 0x3u:
      return AP_PMOVE_CRP;
    default:
      return AP_PMOVE_NONE;
    }
  case 0x3u: /* 011: the status register, which has no FD bit */
    return (which == 0x0u) ? AP_PMOVE_MMUSR : AP_PMOVE_NONE;
  case 0x0u: /* 000: the transparent translation registers */
    switch (which) {
    case 0x2u:
      return AP_PMOVE_TT0;
    case 0x3u:
      return AP_PMOVE_TT1;
    default:
      return AP_PMOVE_NONE;
    }
  default:
    return AP_PMOVE_NONE;
  }
}

static unsigned pmove_size(pmove_register_t which) {
  switch (which) {
  case AP_PMOVE_SRP:
  case AP_PMOVE_CRP:
    return 8u;
  case AP_PMOVE_TC:
  case AP_PMOVE_TT0:
  case AP_PMOVE_TT1:
    return 4u;
  case AP_PMOVE_MMUSR:
    return 2u;
  case AP_PMOVE_NONE:
    break;
  }
  return 0u;
}

/* A root pointer register is a long-format descriptor, so it is unpacked by the
 * walk's own code rather than by a second transcription of the same bit
 * positions -- the two cannot then drift apart. */
static ap_m68030_root_t pmove_root_from(uint32_t upper, uint32_t lower,
                                        bool *invalid) {
  const ap_m68030_descriptor_t descriptor =
      ap_m68030_descriptor_unpack_long(upper, lower, false);

  /* "A descriptor-type code of $00 (invalid) is not allowed; an attempt to load
   * zero into the DT field of the CRP or SRP register results in an MMU
   * configuration exception." The move still happens -- the exception is taken
   * "after moving the operand" -- so this reports rather than refuses. */
  *invalid = descriptor.dt == AP_M68030_DT_INVALID;

  ap_m68030_root_t root = {0};

  /* The table address comes from the lower long word directly rather than from
   * the unpacked descriptor: the unpack stops early on an invalid DT, and this
   * register is written even then -- the exception is "after moving the
   * operand". Figure 9-35 puts the address in bits 31-4, and "Bits 3-0 of the
   * root pointer are not used and are ignored when written". */
  root.table_address = lower & UINT32_C(0xFFFFFFF0);
  root.long_format = descriptor.dt == AP_M68030_DT_VALID_8BYTE;
  root.limit = descriptor.limit;
  root.lower_limit = descriptor.lower_limit;
  root.has_limit = descriptor.has_limit;
  return root;
}

static bool execute_pmove(ap_m68030_cpu_t *cpu, const ap_m68030_coproc_t *coproc,
                          uint16_t extension, uint32_t *clocks) {
  const pmove_register_t which = pmove_register(extension);
  if (which == AP_PMOVE_NONE) {
    return false; /* a register this part does not have: F-line, not a no-op */
  }
  /* Bits 7-0 are shown as zero in all three formats. */
  if ((extension & 0x00FFu) != 0u) {
    return false;
  }

  const bool to_memory = (extension & (1u << 9)) != 0u;
  /* "If the FD bit equals one, the ATC is not flushed" -- and the status
   * register's format has no FD bit at all, so its bit 8 is simply zero and
   * flushing is not something a write to it does. */
  const bool flush_disabled = (extension & (1u << 8)) != 0u;
  const unsigned size = pmove_size(which);

  ap_m68030_address_input_t input = {0};
  if (!gather_address_input(cpu, coproc->ea.kind, size, clocks, &input)) {
    return false;
  }
  /* "Only control alterable addressing modes can be used" -- checked as the
   * *category*, not as "not a register and not an immediate", which would let
   * `(An)+`, `-(An)` and every PC-relative mode through. */
  if (!ap_m68030_ea_is_control_alterable(coproc->ea.kind)) {
    return false;
  }
  const ap_m68030_address_t where =
      resolve_address(cpu, clocks, coproc->ea, &input);
  if (!where.valid) {
    return false;
  }

  if (to_memory) {
    uint32_t high = 0;
    uint32_t low = 0;
    switch (which) {
    case AP_PMOVE_TC:
      low = ap_m68030_tc_encode(&cpu->tc);
      break;
    case AP_PMOVE_TT0:
      low = ap_m68030_tt_pack(&cpu->tt0);
      break;
    case AP_PMOVE_TT1:
      low = ap_m68030_tt_pack(&cpu->tt1);
      break;
    case AP_PMOVE_MMUSR:
      low = cpu->mmusr;
      break;
    case AP_PMOVE_SRP:
    case AP_PMOVE_CRP: {
      const ap_m68030_root_t *root =
          (which == AP_PMOVE_SRP) ? &cpu->srp : &cpu->crp;
      high = ap_m68030_root_pack_upper(root);
      low = root->table_address;
      break;
    }
    case AP_PMOVE_NONE:
      return false;
    }

    if (size == 8u) {
      if (!write_frame_field(cpu, where.address, 4u, high, clocks) ||
          !write_frame_field(cpu, where.address + 4u, 4u, low, clocks)) {
        return false;
      }
      return true;
    }
    return write_frame_field(cpu, where.address, size, low, clocks);
  }

  /* Memory to register. */
  uint32_t high = 0;
  uint32_t low = 0;
  if (size == 8u) {
    const ap_m68030_address_t upper_at = {.address = where.address,
                                          .valid = true};
    const ap_m68030_address_t lower_at = {.address = where.address + 4u,
                                          .valid = true};
    const ap_m68030_operand_result_t a = ap_m68030_operand_read(
        &cpu->regs, cpu->data, &upper_at, 4u, AP_M68030_FC_SUPERVISOR_DATA);
    const ap_m68030_operand_result_t b = ap_m68030_operand_read(
        &cpu->regs, cpu->data, &lower_at, 4u, AP_M68030_FC_SUPERVISOR_DATA);
    *clocks += a.clocks + b.clocks;
    if (!a.ok || !b.ok) {
      return false;
    }
    high = a.value;
    low = b.value;
  } else {
    const ap_m68030_operand_result_t read = ap_m68030_operand_read(
        &cpu->regs, cpu->data, &where, size, AP_M68030_FC_SUPERVISOR_DATA);
    *clocks += read.clocks;
    if (!read.ok) {
      return false;
    }
    low = read.value;
  }

  switch (which) {
  case AP_PMOVE_TC: {
    cpu->tc = ap_m68030_tc_decode(low);
    /* "If the E-bit = 1, consistency checks are performed on the PS and TIx
     * fields. If the checks fail, the instruction takes an MMU configuration
     * exception *after moving the operand* ... and the E-bit is cleared." So
     * the register is written either way, and only then does it fault. */
    if (cpu->tc.enable && !ap_m68030_tc_is_consistent(&cpu->tc, NULL)) {
      cpu->tc.enable = false;
      cpu->pending_vector = AP_M68030_VECTOR_MMU_CONFIGURATION;
    }
    break;
  }
  case AP_PMOVE_TT0:
    cpu->tt0 = ap_m68030_tt_unpack(low);
    break;
  case AP_PMOVE_TT1:
    cpu->tt1 = ap_m68030_tt_unpack(low);
    break;
  case AP_PMOVE_MMUSR:
    cpu->mmusr = (uint16_t)low;
    break;
  case AP_PMOVE_SRP:
  case AP_PMOVE_CRP: {
    bool invalid = false;
    const ap_m68030_root_t root = pmove_root_from(high, low, &invalid);
    if (which == AP_PMOVE_SRP) {
      cpu->srp = root;
    } else {
      cpu->crp = root;
    }
    if (invalid) {
      cpu->pending_vector = AP_M68030_VECTOR_MMU_CONFIGURATION;
    }
    break;
  }
  case AP_PMOVE_NONE:
    return false;
  }

  /* "When the FD-bit is zero, it flushes the address translation cache" -- for
   * every register but the status one, whose format carries no FD bit. */
  if (!flush_disabled && which != AP_PMOVE_MMUSR && cpu->data != NULL &&
      cpu->data->atc != NULL) {
    ap_m68030_atc_flush(cpu->data->atc);
  }
  return true;
}

/* ---------------------------------------------------------------------------
 * PFLUSH, PLOAD and PTEST.
 *
 * PFLUSH and PLOAD share the extension word prefix `001` and are told apart by
 * the MODE field below it -- PFLUSH's modes are 001, 100 and 110, and PLOAD is
 * 000. So the prefix alone does not identify the instruction, and a decoder
 * that stopped there would flush the ATC where it meant to load it.
 *
 * All three name a function code the same way, and the encoding is not a plain
 * number: "10XXX -- Function code is specified as bits XXX. 01DDD -- Function
 * code is specified as bits 2-0 of data register DDD. 00000 -- SFC register.
 * 00001 -- DFC register." Reading the low three bits as the code makes `01DDD`
 * name a function code that happens to be the register number.
 * ------------------------------------------------------------------------- */

static bool resolve_function_code(const ap_m68030_cpu_t *cpu, unsigned field,
                                  uint8_t *out) {
  if ((field & 0x18u) == 0x10u) { /* 10XXX */
    *out = (uint8_t)(field & 0x7u);
    return true;
  }
  if ((field & 0x18u) == 0x08u) { /* 01DDD */
    *out = (uint8_t)(cpu->regs.d[field & 0x7u] & 0x7u);
    return true;
  }
  if (field == 0x00u) {
    *out = cpu->regs.sfc;
    return true;
  }
  if (field == 0x01u) {
    *out = cpu->regs.dfc;
    return true;
  }
  return false; /* an encoding the field does not define */
}

/* The page size the MMU is configured for, which every ATC operation is
 * expressed in. */
static uint8_t mmu_page_size_bits(const ap_m68030_cpu_t *cpu) {
  return cpu->tc.page_size_bits;
}

/* Which root pointer a search uses. "SRE ... supervisor root pointer enable":
 * with it set, a supervisor access searches the SRP tree and everything else
 * the CRP's. With it clear there is one tree, and it is the CRP's. */
static const ap_m68030_root_t *root_for(const ap_m68030_cpu_t *cpu,
                                        uint8_t function_code) {
  const bool supervisor = (function_code & 0x4u) != 0u;
  return (cpu->tc.supervisor_root && supervisor) ? &cpu->srp : &cpu->crp;
}

static bool execute_pflush_or_pload(ap_m68030_cpu_t *cpu,
                                    const ap_m68030_coproc_t *coproc,
                                    uint16_t extension, uint32_t *clocks) {
  const unsigned mode = (unsigned)((extension >> 10) & 0x7u);
  const unsigned mask = (unsigned)((extension >> 5) & 0x7u);
  const unsigned fc_field = (unsigned)(extension & 0x1Fu);

  ap_m68030_atc_t *atc = (cpu->data != NULL) ? cpu->data->atc : NULL;
  if (atc == NULL) {
    return false;
  }

  /* "001 -- Flush all entries", and with it "mask must be 000" and the FC field
   * "must be 00000". A word that sets them is not this instruction. */
  if (mode == 0x1u) {
    if (mask != 0u || fc_field != 0u) {
      return false;
    }
    ap_m68030_atc_flush(atc);
    return true;
  }

  uint8_t function_code = 0;
  if (!resolve_function_code(cpu, fc_field, &function_code)) {
    return false;
  }

  if (mode == 0x4u) {
    /* "100 -- Flush by function code only." */
    ap_m68030_atc_flush_function_codes(atc, function_code, (uint8_t)mask);
    return true;
  }

  /* The remaining two both need the effective address. "The address field must
   * provide the memory management unit with the effective address to be flushed
   * ... not the effective address describing where the PFLUSH operand is
   * located" -- so the calculated address *is* the operand, and is never read
   * through. */
  ap_m68030_address_input_t input = {0};
  if (!gather_address_input(cpu, coproc->ea.kind, 4u, clocks, &input)) {
    return false;
  }
  if (!ap_m68030_ea_is_control_alterable(coproc->ea.kind)) {
    return false;
  }
  const ap_m68030_address_t where =
      resolve_address(cpu, clocks, coproc->ea, &input);
  if (!where.valid) {
    return false;
  }

  if (mode == 0x6u) {
    /* "110 -- Flush by function code and effective address." */
    ap_m68030_atc_flush_entry(atc, function_code, where.address,
                              mmu_page_size_bits(cpu));
    return true;
  }

  if (mode != 0x0u) {
    return false;
  }

  /* PLOAD. "It also searches the translation table for the descriptor
   * corresponding to the specified effective address. It creates a new entry as
   * if the MC68030 had attempted to access that address. Sets the used and
   * modified bits appropriately as part of the search. The instruction executes
   * despite the value of the E-bit in the translation control register" -- so
   * unlike an ordinary access, translation being disabled does not skip it.
   *
   * "Any existing entry in the ATC that translates the specified address is
   * flushed" before the new one is made. */
  const bool read = (extension & (1u << 9)) != 0u;
  const ap_m68030_search_access_t access = {
      .write = !read,
      .read_modify_write = false,
      .supervisor = (function_code & 0x4u) != 0u,
  };

  ap_m68030_atc_flush_entry(atc, function_code, where.address,
                            mmu_page_size_bits(cpu));

  const ap_m68030_walk_result_t result = ap_m68030_walk(
      &cpu->tc, root_for(cpu, function_code), where.address, &access,
      cpu->data->table_fetch, cpu->data->table_update, cpu->data->context);
  (void)ap_m68030_walk_fill_atc(atc, &result, &access, function_code,
                                where.address, mmu_page_size_bits(cpu));
  /* "The PLOAD instruction does not alter the MMUSR." */
  return true;
}

static bool execute_ptest(ap_m68030_cpu_t *cpu,
                          const ap_m68030_coproc_t *coproc, uint16_t extension,
                          uint32_t *clocks) {
  const unsigned level = (unsigned)((extension >> 10) & 0x7u);
  const bool want_address = (extension & (1u << 8)) != 0u;
  const unsigned address_register = (unsigned)((extension >> 5) & 0x7u);
  const unsigned fc_field = (unsigned)(extension & 0x1Fu);

  /* "When this field contains 0, the A field and the register field must also
   * be 0. The instruction takes an F-line exception when the level field is 0
   * and the A field is not 0." An ATC probe has no descriptor address to
   * return, because it never fetched one. */
  if (level == 0u && (want_address || address_register != 0u)) {
    return false;
  }

  uint8_t function_code = 0;
  if (!resolve_function_code(cpu, fc_field, &function_code)) {
    return false;
  }

  ap_m68030_address_input_t input = {0};
  if (!gather_address_input(cpu, coproc->ea.kind, 4u, clocks, &input)) {
    return false;
  }
  if (!ap_m68030_ea_is_control_alterable(coproc->ea.kind)) {
    return false;
  }
  const ap_m68030_address_t where =
      resolve_address(cpu, clocks, coproc->ea, &input);
  if (!where.valid) {
    return false;
  }

  ap_m68030_atc_t *atc = (cpu->data != NULL) ? cpu->data->atc : NULL;
  if (atc == NULL) {
    return false;
  }

  if (level == 0u) {
    /* "PTEST, Level 0" searches the ATC and nothing else. The T bit reports a
     * transparent translation match, which the caller evaluates -- the TTx
     * registers are consulted before the ATC on a real access, so a
     * transparently translated address is one the ATC never sees. */
    const ap_m68030_access_t probe = {
        .address = where.address,
        .function_code = function_code,
        .read = true,
        .read_modify_write = false,
    };
    const ap_m68030_tt_result_t transparent =
        ap_m68030_tt_translate(&cpu->tt0, &cpu->tt1, &probe);
    const ap_m68030_mmusr_t status = ap_m68030_mmusr_probe_atc(
        atc, function_code, where.address, mmu_page_size_bits(cpu),
        transparent.transparent);
    cpu->mmusr = ap_m68030_mmusr_pack(&status);
    return true;
  }

  /* Levels 1-7 perform a table search. "The PTEST instruction does not alter
   * the ATC", and the update callback is NULL so it does not disturb the
   * tree's history bits either -- which is exactly what ap_m68030_walk's
   * nullable `update` exists for. */
  const ap_m68030_search_access_t access = {
      .write = (extension & (1u << 9)) == 0u,
      .read_modify_write = false,
      .supervisor = (function_code & 0x4u) != 0u,
  };
  const ap_m68030_walk_result_t result =
      ap_m68030_walk(&cpu->tc, root_for(cpu, function_code), where.address,
                     &access, cpu->data->table_fetch, NULL, cpu->data->context);

  const ap_m68030_mmusr_t status =
      ap_m68030_mmusr_from_search(&result, function_code);
  cpu->mmusr = ap_m68030_mmusr_pack(&status);

  if (want_address) {
    /* "The physical address of the last descriptor fetched can be returned in
     * an address register." */
    ap_m68030_write_address_register(&cpu->regs, address_register,
                                     result.last_descriptor_address);
  }
  return true;
}

/* The MMU instruction dispatcher. The extension word's top three bits choose,
 * and the four instructions do not partition it evenly: `010`, `011` and `000`
 * are all PMOVE (three formats, three register groups), `001` is PFLUSH *and*
 * PLOAD sharing a prefix, and `100` is PTEST. */
static bool execute_mmu(ap_m68030_cpu_t *cpu, const ap_m68030_coproc_t *coproc,
                        uint32_t *clocks) {
  uint16_t extension = 0;
  if (!next_word(cpu, clocks, &extension)) {
    return false;
  }

  switch ((unsigned)((extension >> 13) & 0x7u)) {
  case 0x0u: /* PMOVE, the transparent translation registers */
  case 0x2u: /* PMOVE, the root pointers and TC */
  case 0x3u: /* PMOVE, the status register */
    return execute_pmove(cpu, coproc, extension, clocks);
  case 0x1u: /* PFLUSH and PLOAD, told apart by the MODE field */
    return execute_pflush_or_pload(cpu, coproc, extension, clocks);
  case 0x4u: /* PTEST */
    return execute_ptest(cpu, coproc, extension, clocks);
  default:
    break;
  }
  return false;
}

/* Whether an instruction "forces a change of flow", which is what the T1=0,
 * T0=1 trace mode watches. `[030]` §8.1.7: "Instructions that are traced in this
 * mode include all branches, jumps, instruction traps, returns, and coprocessor
 * instructions that modify the program counter flow. This mode also includes
 * status register manipulations, because the processor must re-prefetch
 * instruction words to fill the pipe again any time an instruction that can
 * modify the status register is executed."
 *
 * That last clause is the surprising one, and it is a *hardware* reason rather
 * than a logical one: the pipe is refilled, so as far as the trace logic is
 * concerned the flow changed. A model that traced only actual branches would
 * silently skip every `MOVE to SR` and `ANDI to SR` a debugger asked to see. */
static bool changes_flow(const ap_m68030_decoded_t *decoded, bool branch_taken,
                         bool raised_exception) {
  if (branch_taken || raised_exception) {
    return true;
  }

  /* Chains rather than switches: each asks one question with one answer, and a
   * switch would have to name every other enumerator to satisfy -Wswitch-enum
   * while saying nothing. */
  if (decoded->kind == AP_M68030_DECODED_SINGLE) {
    return decoded->as.single.kind == AP_M68030_SINGLE_MOVE_TO_SR ||
           decoded->as.single.kind == AP_M68030_SINGLE_MOVE_TO_CCR;
  }
  if (decoded->kind == AP_M68030_DECODED_IMMEDIATE) {
    const ap_m68030_immediate_kind_t kind = decoded->as.immediate.kind;
    return kind == AP_M68030_IMM_ORI_TO_SR ||
           kind == AP_M68030_IMM_ANDI_TO_SR ||
           kind == AP_M68030_IMM_EORI_TO_SR ||
           kind == AP_M68030_IMM_ORI_TO_CCR ||
           kind == AP_M68030_IMM_ANDI_TO_CCR ||
           kind == AP_M68030_IMM_EORI_TO_CCR;
  }
  if (decoded->kind == AP_M68030_DECODED_CONTROL) {
    /* The returns and jumps already reported a taken branch; what is left that
     * touches the status register is STOP. */
    return decoded->as.control.kind == AP_M68030_CTL_STOP;
  }
  return false;
}

ap_m68030_step_result_t ap_m68030_step(ap_m68030_cpu_t *cpu) {
  ap_m68030_step_result_t out = {.status = AP_M68030_STEP_FAULT};
  uint16_t word = 0;
  bool abnormal = false;

  /* Interrupts are group 4.2, "Exception processing begins when current
   * instruction or previous exception processing is completed" -- so they are
   * recognised *between* instructions, which is here and not in the middle of
   * one. */
  const ap_m68030_exception_result_t interrupt = ap_m68030_take_interrupt(cpu);
  if (interrupt.ok) {
    /* An interrupt is what a stopped processor is waiting for, so taking one
     * also ends the stop. */
    cpu->stopped = false;
    out.clocks = interrupt.clocks;
    out.status = AP_M68030_STEP_EXCEPTION;

    cpu->clocks += out.clocks;
    return out;
  }

  /* "The processor stops fetching and executing instructions" -- so a stopped
   * processor does not even prefetch, and this returns before touching the
   * pipe. Only an interrupt or a reset clears it, neither of which is an
   * instruction, so nothing here can. */
  if (cpu->stopped) {
    out.status = AP_M68030_STEP_STOPPED;
    return out;
  }

  cpu->extension_words = 0;

  /* "The state of these bits when an instruction begins execution determines
   * whether the instruction generates a trace exception after the instruction
   * completes" -- so the mode is captured *here*, not after. An instruction
   * that turns tracing off still traces, which is what lets a debugger single
   * step through the instruction that disables it. */
  const ap_m68030_trace_mode_t trace = ap_m68030_trace_mode(&cpu->regs);
  const uint32_t instruction_address = cpu->regs.pc;

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

  /* The length is *not* predicted from the instruction word: a full-format
   * indexed mode declares its own displacement sizes in an extension word that
   * has not been read yet, so no function of the instruction word can know it.
   * Instead the PC advances by the instruction word plus however many extension
   * words this step actually took, which makes the fetch and the PC agree by
   * construction rather than by two calculations matching.
   *
   * ap_m68030_instruction_length remains the decoder-level answer to "how long
   * is this", asked by anything disassembling rather than executing. */
#define CONSUMED_LENGTH (2u + 2u * cpu->extension_words)

  switch (decoded.kind) {
  case AP_M68030_DECODED_MOVEQ:
    execute_moveq(&cpu->regs, &decoded.as.moveq);
    break;

  case AP_M68030_DECODED_CONTROL: {
    bool branched = false;
    if (!execute_control(cpu, &decoded.as.control, &out.clocks,
                         &branched)) {
      out.status = AP_M68030_STEP_UNIMPLEMENTED;
      cpu->clocks += out.clocks;
      return out;
    }
    out.branch_taken = branched;
    break;
  }

  case AP_M68030_DECODED_BRANCH: {
    const ap_m68030_branch_t *branch = &decoded.as.branch;

    /* "PC + dn -> PC", and the base is the same for all three sizes: the
     * instruction address plus two, which is where the displacement word sits.
     * The 16- and 32-bit forms take that displacement from the instruction
     * stream, so they must be read *before* the branch is decided -- the PC has
     * to advance past them even when the condition is false. Deciding first and
     * skipping the read for an untaken branch desynchronises the pipe. */
    int32_t displacement = branch->displacement8;
    if (branch->size != AP_M68030_BRANCH_8BIT) {
      uint16_t high = 0;
      if (!next_word(cpu, &out.clocks, &high)) {
        cpu->clocks += out.clocks;
        return out;
      }
      if (branch->size == AP_M68030_BRANCH_16BIT) {
        displacement = (int32_t)(int16_t)high;
      } else {
        uint16_t low = 0;
        if (!next_word(cpu, &out.clocks, &low)) {
          cpu->clocks += out.clocks;
          return out;
        }
        displacement = (int32_t)(((uint32_t)high << 16) | low);
      }
    }

    /* BSR is unconditional, and its condition *field* is `F` -- the encoding
     * that means "never" for a Bcc. Testing the condition without excluding it
     * pushes a return address and then falls through to the next instruction,
     * so every subroutine call becomes a leaked stack word. */
    const bool taken =
        branch->is_bra || branch->is_bsr ||
        ap_m68030_condition(branch->condition, ap_m68030_read_ccr(&cpu->regs));
    out.branch_taken = taken;

    if (branch->is_bsr) {
      /* "SP - 4 -> SP; PC -> (SP); PC + dn -> PC" -- the pushed PC is the one
       * after this instruction, displacement words included. */
      if (!push_long(cpu, cpu->regs.pc + CONSUMED_LENGTH, &out.clocks)) {
        out.status = AP_M68030_STEP_FAULT;
        cpu->clocks += out.clocks;
        return out;
      }
    }

    if (taken) {
      cpu->regs.pc = ap_m68030_branch_target(cpu->regs.pc, displacement);
      ap_m68030_fetch_reset(&cpu->fetch, cpu->regs.pc);
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
    out.branch_taken = taken;
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
    if (!execute_misc(cpu, &decoded.as.misc, &out.clocks)) {
      out.status = AP_M68030_STEP_UNIMPLEMENTED;
      cpu->clocks += out.clocks;
      return out;
    }
    break;

  case AP_M68030_DECODED_COPROC: {
    const ap_m68030_coproc_t *coproc = &decoded.as.coproc;

    /* "The MMU instructions use the same opcodes and coprocessor
     * identification (CpID) as the corresponding instructions of the
     * MC68851", so the 68030's own MMU sits at cpID 0 in family 1111. */
    if (coproc->is_mmu && coproc->type == AP_M68030_CP_GENERAL) {
      /* Every MMU instruction is privileged, and the vector an *unsupported*
       * one takes depends on the privilege state: F-line from supervisor,
       * privilege violation from user. Reporting F-line in both would let a
       * user program distinguish "unimplemented" from "not allowed". */
      if (!ap_m68030_supervisor(&cpu->regs)) {
        cpu->pending_vector = AP_M68030_VECTOR_PRIVILEGE_VIOLATION;
        break;
      }
      if (execute_mmu(cpu, coproc, &out.clocks)) {
        break;
      }
    }
    out.status = AP_M68030_STEP_UNIMPLEMENTED;
    cpu->clocks += out.clocks;
    return out;
  }

  case AP_M68030_DECODED_BOUNDS:
    /* CMP2/CHK2/CAS/CAS2 decode but have no semantics here yet. CAS and CAS2
     * are the ones that need more than arithmetic: their read and write are
     * indivisible, so executing them honestly means the bus asserting RMC for
     * the pair, and that is the bus module's item rather than this one. */
  case AP_M68030_DECODED_LINE_A:
  case AP_M68030_DECODED_ILLEGAL:
    out.status = AP_M68030_STEP_UNIMPLEMENTED;
    cpu->clocks += out.clocks;
    return out;
  }

  /* "When the processor is in the trace mode and attempts to execute an illegal
   * or unimplemented instruction, that instruction does not cause a trace
   * exception since it is not executed" -- every path above that returns
   * ILLEGAL, UNIMPLEMENTED or FAULT has already done so, so reaching here means
   * the instruction executed and the trace is owed. */
  const bool traced =
      trace == AP_M68030_TRACE_ANY_INSTRUCTION ||
      (trace == AP_M68030_TRACE_ON_CHANGE_OF_FLOW &&
       changes_flow(&decoded, out.branch_taken, cpu->pending_vector != 0u));

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

    /* Table 8-6 decides which address the frame's PC field gets, and it is not
     * always the next instruction: a privilege violation stacks "First word of
     * instruction causing Privilege Violation" and a format error stacks the
     * RTE that found the bad frame. Defaulting to the next one would have a
     * handler return past the very instruction it was called to diagnose. */
    const uint32_t stacked =
        ap_m68030_stacks_next_instruction(vector)
            ? cpu->regs.pc + CONSUMED_LENGTH
            : cpu->regs.pc;

    const ap_m68030_exception_result_t taken =
        ap_m68030_take_exception(cpu, vector, stacked, cpu->regs.pc);
    out.clocks += taken.clocks;
    if (!taken.ok) {
      /* A fault while stacking is a double fault, which halts the real part.
       * Reporting a memory fault is honest about not modelling the halt. */
      out.status = AP_M68030_STEP_FAULT;
      cpu->clocks += out.clocks;
      return out;
    }
    out.status = AP_M68030_STEP_EXCEPTION;

    if (traced) {
      /* "If an instruction forces an exception as part of its normal
       * execution, the forced exception processing occurs before the trace
       * exception is processed" -- both happen, in that order, so the trace
       * frame sits on top and its handler runs first and returns into the
       * forced one. That is the general rule stated backwards in §8.1:
       * "the lower the priority of an exception, the sooner the handler
       * routine for that exception executes". */
      const ap_m68030_exception_result_t traced_after = ap_m68030_take_exception(
          cpu, AP_M68030_VECTOR_TRACE, cpu->regs.pc, instruction_address);
      out.clocks += traced_after.clocks;
      if (!traced_after.ok) {
        out.status = AP_M68030_STEP_FAULT;
      }
    }
    cpu->clocks += out.clocks;
    return out;
  }

  /* The microsequencer and the bus controller run concurrently, so the
   * instruction's cost is the two *scheduled* rather than summed -- see
   * ap_m68030_overlap.h, where the tables' own CC and NCC columns are what
   * establish that. `out.clocks` holds the bus time this step actually
   * incurred; the published figure is the microcode.
   *
   * Only the transcribed forms are scheduled. Everything else keeps bus time
   * alone, which is visibly a lower bound rather than a plausible guess, and
   * `--time-instructions` shows which is which. */
  /* A branch's cost is not a function of its opcode: §11.6.15 gives a taken
   * `Bcc` 6 clocks and an untaken byte `Bcc` 4, and only the run this step just
   * performed knows which happened. DBcc has three such cases. Everything else
   * is answered by the instruction word alone. */
  const ap_m68030_table_entry_t *published = nullptr;
  if (decoded.kind == AP_M68030_DECODED_BRANCH) {
    published = ap_m68030_timing_for_branch(out.instruction, out.branch_taken);
  } else if (decoded.kind == AP_M68030_DECODED_QUICK &&
             decoded.as.quick.kind == AP_M68030_QUICK_DBCC) {
    published = ap_m68030_timing_for_dbcc(cpu->dbcc_condition_true,
                                          cpu->dbcc_count_expired);
  } else {
    published = ap_m68030_timing_for_word(out.instruction);
  }
  /* A row footnoted "Add Fetch Effective Address Time" publishes a *component*,
   * not a total: `ADD Dn,EA` is 3, and the effective address it reads through
   * is another 3 or 4 from §11.6.1. Until those tables are composed in, such a
   * row is declined rather than applied -- `FINDINGS.md` C9 measured the gap at
   * 7 clocks against our 4 for `ADD.B D0,(A0)`.
   *
   * Declining leaves the instruction at bus time alone, which `--time-instructions`
   * shows as an *alternating* figure rather than a steady one -- visibly a lower
   * bound, exactly as every instruction with no published figure at all reads.
   * Reporting the component instead would produce a steady number that looks
   * like a measurement and is short by a whole memory access, which is the one
   * outcome this core's conventions rule out everywhere else. */
  if (published != nullptr && !published->needs_effective_address_time) {
    out.clocks = ap_m68030_schedule(published->timing.cache_case, out.clocks);
  }

  /* A taken branch, jump or return has already set the PC and emptied the pipe;
   * advancing again would step past the target's first instruction. */
  if (!out.branch_taken) {
    cpu->regs.pc += CONSUMED_LENGTH;
    ap_m68030_pipe_advance(&cpu->fetch.pipe);
  }
  out.status = AP_M68030_STEP_EXECUTED;

  if (traced) {
    /* Trace uses the six-word frame, whose INSTRUCTION ADDRESS is the traced
     * instruction and whose PC is where execution would have resumed -- which
     * for a taken branch is the target, and that is the point of the mode. */
    const ap_m68030_exception_result_t taken_trace = ap_m68030_take_exception(
        cpu, AP_M68030_VECTOR_TRACE, cpu->regs.pc, instruction_address);
    out.clocks += taken_trace.clocks;
    out.status = taken_trace.ok ? AP_M68030_STEP_EXCEPTION
                                : AP_M68030_STEP_FAULT;
  }

  cpu->clocks += out.clocks;
  return out;
}
#undef CONSUMED_LENGTH

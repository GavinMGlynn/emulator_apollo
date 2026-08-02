/* MC68030 instruction step. See ap_m68030_step.h for why an unimplemented
 * instruction is reported rather than skipped. */

#include <stddef.h>

#include "cpu/m68030/ap_m68030_step.h"

#include "cpu/m68030/ap_m68030_ea_timing.h"
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
#include "cpu/m68030/ap_m68030_ssw.h"

/* The two operand entry points, wrapped so that a fault is *recorded* on the
 * CPU before the result reaches a caller that will reduce it to a bool.
 *
 * Every executor signals failure by returning `false`, and by then the access
 * result is gone. Recording the fault at the only place that still knows is
 * what lets the step distinguish "the bus said no" from "this model has no
 * semantics for that instruction" -- two failures that are indistinguishable at
 * the return, and that a reader of the status must never have to guess between.
 *
 * These take the same arguments as the functions they wrap, plus the CPU, so
 * that a call site converted to use them reads identically and no argument can
 * be silently dropped in the conversion. */
static ap_m68030_operand_result_t
step_operand_read(ap_m68030_cpu_t *cpu, ap_m68030_regs_t *regs,
                  ap_m68030_access_ctx_t *access,
                  const ap_m68030_address_t *where, unsigned size,
                  uint8_t function_code) {
  const ap_m68030_operand_result_t result =
      ap_m68030_operand_read(regs, access, where, size, function_code);
  if (result.fault) {
    cpu->access_faulted = true;
    cpu->fault_address = where->address;
    cpu->fault_size = size;
    cpu->fault_read = true;
    cpu->fault_function_code = function_code;
    cpu->fault_instruction_stream = false;
    cpu->fault_data_output = 0u;
  }
  return result;
}

static ap_m68030_operand_result_t
step_operand_write(ap_m68030_cpu_t *cpu, ap_m68030_regs_t *regs,
                   ap_m68030_access_ctx_t *access,
                   const ap_m68030_address_t *where, unsigned size,
                   uint32_t value, uint8_t function_code) {
  const ap_m68030_operand_result_t result =
      ap_m68030_operand_write(regs, access, where, size, value, function_code);
  if (result.fault) {
    cpu->access_faulted = true;
    cpu->fault_address = where->address;
    cpu->fault_size = size;
    cpu->fault_read = false;
    cpu->fault_function_code = function_code;
    cpu->fault_instruction_stream = false;
    /* "For data write faults, the handler must transfer the properly sized data
     * from the data output buffer (DOB) on the stack frame to the location
     * indicated by the data fault address" -- so the value the write was
     * carrying is not incidental, it is the only copy the handler will get. */
    cpu->fault_data_output = value;
  }
  return result;
}

bool ap_m68030_take_reset(ap_m68030_cpu_t *cpu) {
  /* Steps 1-3: trace off, supervisor *interrupt* mode -- S set and M clear --
   * and the mask at 7. */
  uint16_t sr = cpu->regs.sr;
  sr &= (uint16_t)~((1u << AP_M68030_SR_T1_BIT) | (1u << AP_M68030_SR_T0_BIT));
  sr |= (uint16_t)(1u << AP_M68030_SR_S_BIT);
  sr &= (uint16_t)~(1u << AP_M68030_SR_M_BIT);
  sr |= (uint16_t)(AP_M68030_SR_INTERRUPT_MASK << AP_M68030_SR_INTERRUPT_SHIFT);
  ap_m68030_write_sr(&cpu->regs, sr);

  /* Step 4. */
  cpu->regs.vbr = 0u;

  /* Step 5: both caches disabled, unfrozen, not bursting, and the data cache's
   * write allocation off. */
  cpu->cacr = (ap_m68030_cacr_t){0};

  /* Step 6: "Invalidates all entries in the instruction and data caches." Only
   * the caches -- step 7's own text and the closing paragraph are explicit that
   * the ATC is *not* flushed. */
  if (cpu->fetch.access != nullptr && cpu->fetch.access->cache != nullptr) {
    ap_m68030_cache_clear(cpu->fetch.access->cache);
  }
  if (cpu->data != nullptr && cpu->data->cache != nullptr) {
    ap_m68030_cache_clear(cpu->data->cache);
  }

  /* Step 7. */
  cpu->tc.enable = false;
  cpu->tt0.enabled = false;
  cpu->tt1.enabled = false;

  /* Steps 8-10: the two long words at offset zero, in supervisor *program*
   * space. Read through the ordinary path, so a machine with nothing at zero
   * reports the failure rather than starting from whatever was in the
   * registers. */
  const ap_m68030_access_result_t stack = ap_m68030_access_read(
      cpu->fetch.access, 0u, AP_M68030_FC_SUPERVISOR_PROGRAM);
  const ap_m68030_access_result_t start = ap_m68030_access_read(
      cpu->fetch.access, 4u, AP_M68030_FC_SUPERVISOR_PROGRAM);
  if (!stack.ok || !start.ok) {
    return false;
  }

  cpu->regs.isp = stack.value;
  cpu->stopped = false;
  cpu->pending_vector = 0;
  cpu->interrupt_level = 0;
  cpu->previous_interrupt_level = 0;
  ap_m68030_fetch_reset(&cpu->fetch, start.value);
  cpu->regs.pc = start.value;
  return true;
}

void ap_m68030_cpu_reset(ap_m68030_cpu_t *cpu, uint32_t pc) {
  cpu->regs.pc = pc;
  ap_m68030_fetch_reset(&cpu->fetch, pc);
  cpu->clocks = 0;
  /* Both counters begin here, and only here. `ap_m68030_fetch_reset` is also a
   * branch's pipe flush, which must not un-spend clocks already spent. */
  cpu->fetch.bus_clocks = 0;
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
    /* An extension word that did not arrive is a fault of the instruction
     * stream, not a gap in this model: the instruction was decoded, and what
     * failed was reading the rest of it. Both arms are faults -- one is a
     * prefetch that never returned, the other a word the pipe marked abnormal
     * because its fetch had. */
    cpu->access_faulted = true;
    cpu->fault_instruction_stream = true;
    cpu->fault_address = cpu->regs.pc;
    cpu->fault_size = 2u;
    cpu->fault_read = true;
    cpu->fault_function_code = cpu->fetch.function_code;
    cpu->fault_data_output = 0u;
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
      step_operand_read(cpu, &cpu->regs, cpu->data, &intermediate, 4u,
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
    const ap_m68030_operand_result_t read = step_operand_read(
        cpu, &cpu->regs, cpu->data, &source, move->size, cpu->data_function_code);
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
      step_operand_write(cpu, &cpu->regs, cpu->data, &destination, move->size,
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

    const ap_m68030_operand_result_t memory = step_operand_read(
        cpu, &cpu->regs, cpu->data, &where, arith->size, cpu->data_function_code);
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
    const ap_m68030_operand_result_t written = step_operand_write(
        cpu, &cpu->regs, cpu->data, &where, arith->size, result.result,
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

  const ap_m68030_operand_result_t read = step_operand_read(
      cpu, &cpu->regs, cpu->data, &where, imm->size, cpu->data_function_code);
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

  const ap_m68030_operand_result_t written = step_operand_write(
      cpu, &cpu->regs, cpu->data, &where, imm->size, result.result,
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
    const ap_m68030_operand_result_t read = step_operand_read(
        cpu, &cpu->regs, cpu->data, &where, single->size, cpu->data_function_code);
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
        step_operand_write(cpu, &cpu->regs, cpu->data, &where, 2u, source,
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
    const ap_m68030_operand_result_t wrote = step_operand_write(
        cpu, &cpu->regs, cpu->data, &where, 1u, value | 0x80u,
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

  const ap_m68030_operand_result_t written = step_operand_write(
      cpu, &cpu->regs, cpu->data, &where, single->size, result.result,
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

    const ap_m68030_operand_result_t read = step_operand_read(
        cpu, &cpu->regs, cpu->data, &where, size, cpu->data_function_code);
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

    const ap_m68030_operand_result_t written = step_operand_write(
        cpu, &cpu->regs, cpu->data, &where, size, result.result,
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
    const ap_m68030_operand_result_t written = step_operand_write(
        cpu, &cpu->regs, cpu->data, &where, 1u, condition ? 0xFFu : 0x00u,
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
    const ap_m68030_operand_result_t read = step_operand_read(
        cpu, &cpu->regs, cpu->data, &where, arith->size, cpu->data_function_code);
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

  const ap_m68030_operand_result_t read = step_operand_read(
      cpu, &cpu->regs, cpu->data, &where, size, cpu->data_function_code);
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

  const ap_m68030_operand_result_t written = step_operand_write(
      cpu, &cpu->regs, cpu->data, &where, size, updated, cpu->data_function_code);
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

    const ap_m68030_operand_result_t read = step_operand_read(
        cpu, &cpu->regs, cpu->data, &where, 2u, cpu->data_function_code);
    *clocks += read.clocks;
    if (!read.ok) {
      return false;
    }

    /* "An operand in memory can be shifted one bit only, and the operand size
     * is restricted to a word." */
    const ap_m68030_alu_result_t result = ap_m68030_alu_shift(
        shift->type, shift->left, read.value, 1u, 2u, x_in);
    ap_m68030_write_ccr(&cpu->regs, ap_m68030_alu_apply(ccr, &result));

    const ap_m68030_operand_result_t written = step_operand_write(
        cpu, &cpu->regs, cpu->data, &where, 2u, result.result,
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

  const ap_m68030_operand_result_t read = step_operand_read(
      cpu, &cpu->regs, cpu->data, where, size, cpu->data_function_code);
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

  const ap_m68030_operand_result_t read = step_operand_read(
      cpu, &cpu->regs, cpu->data, &where, size, cpu->data_function_code);
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
      const ap_m68030_operand_result_t read = step_operand_read(
          cpu, &cpu->regs, cpu->data, &where, 2u, cpu->data_function_code);
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
      const ap_m68030_operand_result_t read = step_operand_read(
          cpu, &cpu->regs, cpu->data, &where, 2u, cpu->data_function_code);
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
      const ap_m68030_operand_result_t wrote = step_operand_write(
          cpu, &cpu->regs, cpu->data, &destination_where, size,
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
  const ap_m68030_operand_result_t wrote = step_operand_write(
      cpu, &cpu->regs, cpu->data, &where, size, value, AP_M68030_FC_SUPERVISOR_DATA);
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
   * two bus fault frames are built by take_bus_fault_with instead, which has
   * the special status word this one has no way to supply. */
  if (vector == AP_M68030_VECTOR_RESET_SP ||
      vector == AP_M68030_VECTOR_RESET_PC) {
    return out;
  }
  const ap_m68030_frame_format_t format = ap_m68030_frame_for_vector(vector);
  if (format != AP_M68030_FRAME_SHORT && format != AP_M68030_FRAME_SIX_WORD &&
      format != AP_M68030_FRAME_COPROCESSOR_MID) {
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
  if (format == AP_M68030_FRAME_SIX_WORD ||
      format == AP_M68030_FRAME_COPROCESSOR_MID) {
    /* "INSTRUCTION ADDRESS is the address of the instruction that caused the
     * exception", which is not the stacked PC: that one points at the next.
     * Both frames carry the field at the same offset. */
    wrote = wrote && write_frame_field(cpu, frame + 8u, 4u, instruction_address,
                                       &out.clocks);
  }
  if (format == AP_M68030_FRAME_COPROCESSOR_MID) {
    /* Format $9's remaining four words are "INTERNAL REGISTERS", written as
     * zero. `PROVISIONAL`, and the same deliberate approximation the bus fault
     * frames make for the same reason: this model has no microsequencer state
     * to save. Written rather than skipped -- a frame that left them holding
     * whatever the stack already had would give a handler the previous
     * program's data under a documented field name.
     *
     * What that costs is stated where it is paid: an RTE from this frame is
     * declined, because resuming needs the coprocessor dialog these words
     * describe and this model does not carry it. A handler that diagnoses and
     * does not resume -- which is what a protocol violation calls for -- works
     * from the fields above, and those are real. */
    wrote = wrote && write_frame_field(cpu, frame + 12u, 4u, 0u, &out.clocks);
    wrote = wrote && write_frame_field(cpu, frame + 16u, 4u, 0u, &out.clocks);
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
      step_operand_read(cpu, &cpu->regs, cpu->data, &vector_where, 4u,
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

/* The special status word describing the fault the CPU last recorded. */
static ap_m68030_ssw_t fault_ssw(const ap_m68030_cpu_t *cpu) {
  ap_m68030_ssw_t ssw = {0};
  if (cpu->fault_instruction_stream) {
    /* "The fault bits (FB and FC) indicate that the processor attempted to use
     * a stage (B or C) and found it to be marked invalid due to a bus error on
     * the prefetch for that stage."
     *
     * B and C are the two stages *ahead* of the decoded one, which is why the
     * frame carries images of exactly those: stage D's word has already been
     * used, while B and C hold words the faulted prefetch never delivered and
     * which a handler must supply before execution can resume. Both are
     * reported, because words advance B to C to D and a prefetch that failed to
     * fill the pipe left both stages invalid. The encoder adds the rerun
     * bits. */
    ssw.stage_c_fault = true;
    ssw.stage_b_fault = true;
    return ssw;
  }
  ssw.data_fault = true;
  ssw.read = cpu->fault_read;
  ssw.size = ap_m68030_ssw_size_for(cpu->fault_size);
  ssw.function_code = cpu->fault_function_code;
  return ssw;
}

/* The body of both fault exceptions. The special status word, the address and
 * the data output are supplied rather than read here, because an address error
 * has no faulted access to read them from: it is "internally initiated" and "a
 * bus cycle is not executed". */
static ap_m68030_exception_result_t
take_bus_fault_with(ap_m68030_cpu_t *cpu, unsigned vector,
                    uint32_t instruction_address, ap_m68030_ssw_t ssw,
                    uint32_t fault_address, uint32_t data_output) {
  ap_m68030_exception_result_t out = {0};
  const ap_m68030_frame_format_t format = ap_m68030_bus_fault_frame(&ssw);

  /* Table 8-6 gives the two frames different PC meanings, and it is not a
   * detail: the short frame is "Execution Unit at Instruction Boundary" and
   * stacks the *next* instruction, while the long frame is "Instruction
   * Execution in Progress" and stacks "the address of the instruction in
   * execution when the fault occurred".
   *
   * Every data fault this model detects is the second case -- the operand
   * access failed partway through an instruction that has not completed -- so
   * the instruction's own address is stacked and the handler can retry it. */
  const uint32_t stacked_pc = (format == AP_M68030_FRAME_LONG_BUS_FAULT)
                                  ? instruction_address
                                  : cpu->regs.pc;

  const uint16_t saved_sr = cpu->regs.sr;
  uint16_t updated = cpu->regs.sr;
  updated |= (uint16_t)(1u << AP_M68030_SR_S_BIT);
  updated &= (uint16_t)~(1u << AP_M68030_SR_T1_BIT);
  updated &= (uint16_t)~(1u << AP_M68030_SR_T0_BIT);
  ap_m68030_write_sr(&cpu->regs, updated);

  const uint32_t bytes = ap_m68030_frame_words(format) * 2u;
  const uint32_t frame = ap_m68030_read_a7(&cpu->regs) - bytes;

  /* Every word of the frame is written, including the ones Table 8-6 labels
   * INTERNAL REGISTER, which are written as zero.
   *
   * That is a deliberate approximation, and `PROVISIONAL`: this model has no
   * microsequencer state to save. What matters here is that they are *written*
   * rather than skipped -- a frame that only filled its named fields would
   * leave whatever the stack already held in the gaps, and a handler reading
   * those would act on the previous program's data. Zero is a stated value; a
   * skipped word is an unstated one. */
  bool wrote = true;
  for (uint32_t offset = 0; offset < bytes && wrote; offset += 2u) {
    wrote = write_frame_field(cpu, frame + offset, 2u, 0u, &out.clocks);
  }

  wrote = wrote && write_frame_field(cpu, frame + 0u, 2u, saved_sr, &out.clocks);
  wrote = wrote && write_frame_field(cpu, frame + 2u, 4u, stacked_pc,
                                     &out.clocks);
  wrote = wrote && write_frame_field(
                       cpu, frame + 6u, 2u,
                       ap_m68030_frame_format_word(format, vector), &out.clocks);
  wrote = wrote && write_frame_field(cpu, frame + AP_M68030_BUS_FAULT_SSW, 2u,
                                     ap_m68030_ssw_encode(&ssw), &out.clocks);
  /* The pipe images, so a handler can repair the instruction stream. These are
   * the words the pipe actually holds, not a reconstruction. */
  wrote = wrote && write_frame_field(cpu, frame + AP_M68030_BUS_FAULT_STAGE_C,
                                     2u, cpu->fetch.pipe.c.word, &out.clocks);
  wrote = wrote && write_frame_field(cpu, frame + AP_M68030_BUS_FAULT_STAGE_B,
                                     2u, cpu->fetch.pipe.b.word, &out.clocks);
  wrote = wrote && write_frame_field(cpu, frame + AP_M68030_BUS_FAULT_ADDRESS,
                                     4u, fault_address, &out.clocks);
  wrote = wrote && write_frame_field(cpu,
                                     frame + AP_M68030_BUS_FAULT_DATA_OUTPUT,
                                     4u, data_output, &out.clocks);
  if (!wrote) {
    /* A fault while stacking is a double fault, which halts the real part. As
     * elsewhere, this reports failure with the status register already changed
     * and the caller must not treat the exception as taken. */
    return out;
  }
  ap_m68030_write_a7(&cpu->regs, frame);

  out.vector_address = cpu->regs.vbr + ap_m68030_vector_offset(vector);
  const ap_m68030_address_t vector_where = {.address = out.vector_address,
                                            .valid = true};
  const ap_m68030_operand_result_t handler =
      step_operand_read(cpu, &cpu->regs, cpu->data, &vector_where, 4u,
                        AP_M68030_FC_SUPERVISOR_DATA);
  out.clocks += handler.clocks;
  if (!handler.ok) {
    return out;
  }

  out.handler = handler.value;
  cpu->regs.pc = out.handler;
  ap_m68030_fetch_reset(&cpu->fetch, out.handler);

  out.frame_address = frame;
  out.ok = true;
  return out;
}

ap_m68030_exception_result_t
ap_m68030_take_bus_fault(ap_m68030_cpu_t *cpu, unsigned vector,
                         uint32_t instruction_address) {
  if (!cpu->access_faulted) {
    /* Nothing faulted, so there is no frame to describe. Declining is the only
     * honest answer: a frame built from cleared fault state would tell a
     * handler to repair address zero. */
    const ap_m68030_exception_result_t declined = {0};
    return declined;
  }

  /* Captured before anything else runs. Building the frame is itself a series
   * of writes through the same path that records these, so a fault while
   * stacking -- a double fault -- would otherwise overwrite the fault being
   * reported with the fault that happened trying to report it. */
  return take_bus_fault_with(cpu, vector, instruction_address, fault_ssw(cpu),
                             cpu->fault_address, cpu->fault_data_output);
}

ap_m68030_exception_result_t
ap_m68030_take_address_error(ap_m68030_cpu_t *cpu) {
  /* §8.2.1: "If an address error exception occurs, the fault bits written to
   * the stack frame are not set (they are only set due to a bus error, as
   * previously described), and the rerun bits alone show the cause of the
   * exception. Depending on the state of the pipeline, either RB and RC are
   * both set, or RC alone is set."
   *
   * Both, here: an odd program counter invalidates the whole pipe, so neither
   * stage holds a word worth keeping and both need refilling. The absence of
   * the fault bits is what tells a handler this was an address error rather
   * than a bus error, so the encoder's rerun-implies-fault rule must not be
   * inverted -- and it is not: it adds rerun bits to faults, never the
   * reverse. */
  const ap_m68030_ssw_t ssw = {.stage_c_rerun = true, .stage_b_rerun = true};

  /* "A bus cycle is not executed, and the processor begins exception
   * processing immediately" -- so nothing is counted as a bus error, and the
   * address stacked is the odd one the processor declined to fetch from. That
   * is the address a handler has to correct, and it is also where the program
   * counter still stands. */
  return take_bus_fault_with(cpu, AP_M68030_VECTOR_ADDRESS_ERROR, cpu->regs.pc,
                             ssw, cpu->regs.pc, 0u);
}

/* What an executor's `false` meant, now that the two causes are distinguished.
 *
 * A fault is not merely reported: the processor *takes* it, which is what the
 * real part does and what firmware depends on. An undecoded read is how a
 * probe asks whether a card is present, and a handler that answers "no" is the
 * normal, expected path -- so stopping the machine there would model a
 * question as a crash.
 *
 * `FAULT` remains for the case that cannot be taken: a second fault while
 * building the frame, which halts the real part. */
static ap_m68030_step_status_t fault_or_unimplemented(
    ap_m68030_cpu_t *cpu, ap_m68030_step_result_t *out,
    uint32_t instruction_address) {
  if (!cpu->access_faulted) {
    return AP_M68030_STEP_UNIMPLEMENTED;
  }
  const ap_m68030_exception_result_t taken = ap_m68030_take_bus_fault(
      cpu, AP_M68030_VECTOR_BUS_ERROR, instruction_address);
  out->clocks += taken.clocks;
  return taken.ok ? AP_M68030_STEP_EXCEPTION : AP_M68030_STEP_FAULT;
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
      step_operand_read(cpu, &cpu->regs, cpu->data, &where, size,
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

    if (format == AP_M68030_FRAME_COPROCESSOR_MID) {
      /* The coprocessor frame restores a coprocessor's mid-instruction state,
       * which this model does not carry at all. Declined rather than
       * half-restored. */
      return false;
    }

    /* The two fault frames are returned from the same way: status register and
     * program counter off the stack, the pointer advanced by the frame's own
     * size. What they add is the rerun, and that is where this model makes a
     * deliberate approximation.
     *
     * §8.2.2: "If the DF bit is set when the processor reads the stack frame,
     * it reruns the faulted data access". The real part resumes *mid*
     * instruction, using the internal registers it saved. This model has none,
     * so it does the only other thing that can be correct: the long frame
     * stacks the address of the instruction that was executing, so returning
     * there re-executes that instruction from the start.
     *
     * `PROVISIONAL`. That is exact for an instruction whose faulted access
     * happens before any register or memory is changed -- which is every instruction the boot PROM
     * faults on, since a fault on the *first* operand access is the common case
     * and a compare writes nothing. It is wrong for an instruction that had
     * already committed a side effect, which is the cost of the approximation
     * and the reason it is recorded rather than assumed harmless. Closing it
     * needs the internal state, which needs a microsequencer model. */

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
      const ap_m68030_operand_result_t wrote = step_operand_write(
          cpu, &cpu->regs, cpu->data, &where, misc->size, value,
          cpu->data_function_code);
      *clocks += wrote.clocks;
      if (!wrote.ok) {
        return false;
      }
    } else {
      const ap_m68030_operand_result_t read = step_operand_read(
          cpu, &cpu->regs, cpu->data, &where, misc->size, cpu->data_function_code);
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
    /* BKPT takes no effective address at all -- its operand is a breakpoint
     * *number* carried in the instruction word, which the acknowledge cycle
     * puts on A2-A4. So it skips the address resolution below and is handled in
     * the execution switch. */
    break;

  case AP_M68030_MISC_INVALID:
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
    const ap_m68030_operand_result_t read = step_operand_read(
        cpu, &cpu->regs, cpu->data, &where, 1u, cpu->data_function_code);
    *clocks += read.clocks;
    if (!read.ok) {
      return false;
    }
    const uint16_t ccr = ap_m68030_read_ccr(&cpu->regs);
    const ap_m68030_alu_result_t result = ap_m68030_alu_sbcd(
        0u, read.value, ((ccr >> AP_M68030_SR_X_BIT) & 1u) != 0u,
        ((ccr >> AP_M68030_SR_Z_BIT) & 1u) != 0u);
    const ap_m68030_operand_result_t wrote = step_operand_write(
        cpu, &cpu->regs, cpu->data, &where, 1u, result.result,
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
          step_operand_read(cpu, &cpu->regs, cpu->data, &where, operand_size,
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
     * undefined otherwise." */
    uint16_t ccr = ap_m68030_read_ccr(&cpu->regs);
    if (value < 0) {
      ccr |= (uint16_t)(1u << AP_M68030_SR_N_BIT);
    } else if (value > bound) {
      ccr &= (uint16_t)~(1u << AP_M68030_SR_N_BIT);
    }

    /* Z, V and C are documented undefined, and the part sets them definitely:
     * "Z is set if the register operand (the second operand; not the effective
     * address operand) is 0", and V and C are "always cleared". Same class of
     * finding as `ABCD`'s undefined flags, from the same body of hardware
     * testing -- and the parenthesis is the load-bearing part, since `Z` from
     * the *bound* would be the plausible wrong reading.
     *
     * `PROVISIONAL` for the same reason: measured on a 68000, applied to a
     * 68030 as the best evidence there is. A reference core has to be
     * deterministic either way, so the choice is between a cited rule and an
     * invented one. */
    if (value == 0) {
      ccr |= (uint16_t)(1u << AP_M68030_SR_Z_BIT);
    } else {
      ccr &= (uint16_t)~(1u << AP_M68030_SR_Z_BIT);
    }
    ccr &= (uint16_t)~((1u << AP_M68030_SR_V_BIT) | (1u << AP_M68030_SR_C_BIT));
    ap_m68030_write_ccr(&cpu->regs, ccr);

    if (value < 0 || value > bound) {
      cpu->pending_vector = AP_M68030_VECTOR_CHK;
    }
    return true;
  }

  case AP_M68030_MISC_BKPT: {
    /* "The breakpoint acknowledge cycle is generated by the execution of a
     * breakpoint instruction (BKPT) ... This cycle accesses the CPU space with
     * a type field of zero and provides the breakpoint number specified by the
     * instruction on address lines A2-A4." (`[030]` §7.4.2.)
     *
     * So the address is not an address at all: CPU space is selected by the
     * function code, the type field picks breakpoint acknowledge out of the
     * other CPU-space cycles, and the breakpoint number rides on three address
     * lines. Putting the number anywhere else -- A0-A2 is the obvious slip --
     * would acknowledge a different breakpoint, and external hardware would
     * answer with the wrong instruction word rather than fault. */
    const uint32_t address = (uint32_t)((misc->reg & 7u) << 2);

    const ap_m68030_access_result_t acknowledge = ap_m68030_access_read(
        cpu->data, address, AP_M68030_FC_CPU_SPACE);
    *clocks += acknowledge.clocks;

    if (!acknowledge.ok) {
      /* "If the external logic terminates the breakpoint acknowledge cycle with
       * BERR (i.e., no instruction word available), the processor takes an
       * illegal instruction exception."
       *
       * That is the DN3500's case and not an error path: no breakpoint hardware
       * is fitted, so nothing answers, so every BKPT is an illegal instruction.
       * A model that declined the instruction instead would report our gap
       * where the machine has a behaviour. */
      cpu->access_faulted = false;
      cpu->pending_vector = AP_M68030_VECTOR_ILLEGAL_INSTRUCTION;
      return true;
    }

    /* "the data on the bus (an instruction word) is inserted into the
     * instruction pipe, replacing the breakpoint opcode, and is executed after
     * the breakpoint acknowledge cycle completes."
     *
     * Filled at the BKPT's own address, so the replacement occupies the word
     * the breakpoint did and the following instruction is unmoved. The pipe is
     * not advanced here: the step advances it after execution as it does for
     * any instruction, and the replacement is decoded on the next one. */
    ap_m68030_pipe_fill(&cpu->fetch.pipe, cpu->regs.pc,
                        (uint16_t)acknowledge.value, false);
    return true;
  }

  case AP_M68030_MISC_SWAP:
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
    const ap_m68030_operand_result_t a = step_operand_read(
        cpu, &cpu->regs, cpu->data, &upper_at, 4u, AP_M68030_FC_SUPERVISOR_DATA);
    const ap_m68030_operand_result_t b = step_operand_read(
        cpu, &cpu->regs, cpu->data, &lower_at, 4u, AP_M68030_FC_SUPERVISOR_DATA);
    *clocks += a.clocks + b.clocks;
    if (!a.ok || !b.ok) {
      return false;
    }
    high = a.value;
    low = b.value;
  } else {
    const ap_m68030_operand_result_t read = step_operand_read(
        cpu, &cpu->regs, cpu->data, &where, size, AP_M68030_FC_SUPERVISOR_DATA);
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

/* ---------------------------------------------------------------------------
 * Fetching a floating-point source operand
 *
 * `[68881]` §4.8.3: "a one in this field indicates that the source operand is
 * external to the FPCP" -- and moving it is the *main processor's* job.
 * `[030]` §10.4.9 is that job stated as hardware: the coprocessor answers with
 * an evaluate effective address and transfer data primitive, "the processor
 * calculates the effective address using the appropriate effective address
 * extension words at the current scanPC", and transfers the named number of
 * bytes. The primitive exchange itself is not modelled -- see
 * ap_m68882_source_transfer -- but the work it asks for is exactly this.
 */
typedef enum {
  FP_SOURCE_FETCHED,
  /* §10.4.9 names this failure specifically, and it is a *trap* rather than a
   * gap: "all other lengths (zero, for example) cause the main processor to
   * initiate protocol violation exception processing". */
  FP_SOURCE_PROTOCOL_VIOLATION,
  /* A bus fault, or an addressing mode this step cannot yet supply. */
  FP_SOURCE_FAILED,
} fp_source_result_t;

/* Read `size` bytes from `where` into `bytes`, most significant first.
 *
 * §10.4.9: the transfer uses "long-word transfers whenever possible", so the
 * twelve-byte extended operand is three long words and not some other division
 * -- which is the bus traffic an observer would see, and the reason this loops
 * over long words rather than reading byte by byte. */
static bool read_operand_bytes(ap_m68030_cpu_t *cpu, uint32_t *clocks,
                               const ap_m68030_address_t *where, unsigned size,
                               uint8_t *bytes) {
  unsigned done = 0;
  while (done < size) {
    const unsigned chunk = (size - done >= 4u) ? 4u : (size - done);
    const ap_m68030_address_t at = {.address = where->address + done,
                                    .valid = true};
    const ap_m68030_operand_result_t read = step_operand_read(
        cpu, &cpu->regs, cpu->data, &at, chunk, cpu->data_function_code);
    *clocks += read.clocks;
    if (!read.ok) {
      return false;
    }
    for (unsigned i = 0; i < chunk; i++) {
      bytes[done + i] = (uint8_t)(read.value >> (8u * (chunk - 1u - i)));
    }
    done += chunk;
  }
  return true;
}

static fp_source_result_t fetch_fp_source(ap_m68030_cpu_t *cpu,
                                          const ap_m68030_coproc_t *coproc,
                                          ap_m68882_format_t format,
                                          uint32_t *clocks,
                                          ap_m68882_extended_t *source) {
  const unsigned size = ap_m68882_format_size(format);
  uint8_t bytes[12] = {0};

  /* The FADD page's table, and every other arithmetic page's: the source is a
   * *data* addressing mode. Address register direct is the one mode with no
   * encoding shown at all -- its row carries dashes -- because an address
   * register cannot hold a floating-point operand. Checked as the category so
   * that the whole family of modes is covered by the one rule. */
  if (!ap_m68030_ea_is_data(coproc->ea.kind)) {
    return FP_SOURCE_PROTOCOL_VIOLATION;
  }

  if (coproc->ea.kind == AP_M68030_EA_DATA_REGISTER) {
    /* The footnote under that table: "Only if <fmt> is Byte, Word, Long, or
     * Single". §10.4.9 gives the same restriction from the other side, as
     * lengths rather than formats -- "If the effective address is a main
     * processor register (register direct mode), only operand lengths of one,
     * two, or four bytes are valid" -- and names what a longer one does. */
    if (size > 4u) {
      return FP_SOURCE_PROTOCOL_VIOLATION;
    }
    const uint32_t value = cpu->regs.d[coproc->ea.reg];
    for (unsigned i = 0; i < size; i++) {
      bytes[i] = (uint8_t)(value >> (8u * (size - 1u - i)));
    }
  } else if (coproc->ea.kind == AP_M68030_EA_IMMEDIATE) {
    /* An immediate operand is in the instruction stream rather than at an
     * address, and §4.7 counts it in *words*: "the longest case is for an
     * immediate operand of six words - the X or P format", the stream itself
     * being "1-6 words". So a byte operand still occupies a whole word, and
     * Table 2-3's rule for the main processor applies to it -- the byte is the
     * low-order half. Advancing by one byte instead would leave the program
     * counter odd and fault the next instruction fetch. */
    for (unsigned done = 0; done < size; done += 2u) {
      uint16_t word = 0;
      if (!next_word(cpu, clocks, &word)) {
        return FP_SOURCE_FAILED;
      }
      if (size == 1u) {
        bytes[0] = (uint8_t)(word & 0xFFu);
        break;
      }
      bytes[done] = (uint8_t)(word >> 8);
      bytes[done + 1u] = (uint8_t)(word & 0xFFu);
    }
  } else {
    /* A memory mode. The operand's *size* is an input to the address and not
     * just to the read that follows it: a postincrement steps by the operand's
     * length, so the format has to be known before the address is calculated
     * and not after. */
    ap_m68030_address_input_t input = {0};
    if (!gather_address_input(cpu, coproc->ea.kind, size, clocks, &input)) {
      return FP_SOURCE_FAILED;
    }
    const ap_m68030_address_t where =
        resolve_address(cpu, clocks, coproc->ea, &input);
    if (!where.valid) {
      return FP_SOURCE_FAILED;
    }
    if (!read_operand_bytes(cpu, clocks, &where, size, bytes)) {
      return FP_SOURCE_FAILED;
    }
  }

  /* Packed decimal declines here, which is this model's gap and not a trap. */
  return ap_m68882_operand_decode(format, bytes, source) ? FP_SOURCE_FETCHED
                                                         : FP_SOURCE_FAILED;
}

/* Write `size` bytes of `bytes` at `where`, most significant first -- the
 * mirror of read_operand_bytes, in long words for the same reason. */
static bool write_operand_bytes(ap_m68030_cpu_t *cpu, uint32_t *clocks,
                                const ap_m68030_address_t *where, unsigned size,
                                const uint8_t *bytes) {
  unsigned done = 0;
  while (done < size) {
    const unsigned chunk = (size - done >= 4u) ? 4u : (size - done);
    uint32_t value = 0;
    for (unsigned i = 0; i < chunk; i++) {
      value = (value << 8) | bytes[done + i];
    }
    const ap_m68030_address_t at = {.address = where->address + done,
                                    .valid = true};
    const ap_m68030_operand_result_t wrote = step_operand_write(
        cpu, &cpu->regs, cpu->data, &at, chunk, value, cpu->data_function_code);
    *clocks += wrote.clocks;
    if (!wrote.ok) {
      return false;
    }
    done += chunk;
  }
  return true;
}

/* The store direction. Same shape as fetch_fp_source and the same two refusals,
 * with one addition that only applies here: §10.4.9's "the MC68030 initiates
 * protocol violation exception processing if the primitive requests a write to
 * a nonalterable effective address". */
static fp_source_result_t store_fp_destination(
    ap_m68030_cpu_t *cpu, const ap_m68030_coproc_t *coproc,
    const ap_m68882_store_t *result, uint32_t *clocks) {
  /* Data *alterable*: the write rules out the PC-relative modes and the
   * immediate, which a read allows, as well as the address registers a read
   * already ruled out. Checked as the category so the whole family is covered
   * by one rule. */
  if (!ap_m68030_ea_is_data_alterable(coproc->ea.kind)) {
    return FP_SOURCE_PROTOCOL_VIOLATION;
  }

  if (coproc->ea.kind == AP_M68030_EA_DATA_REGISTER) {
    if (result->size > 4u) {
      return FP_SOURCE_PROTOCOL_VIOLATION; /* the same one-two-or-four rule */
    }
    uint32_t value = 0;
    for (unsigned i = 0; i < result->size; i++) {
      value = (value << 8) | result->bytes[i];
    }
    /* Only the operand's own bytes are replaced. A byte store leaves the upper
     * 24 bits of the register alone, as every other byte operation on this
     * family does. */
    const uint32_t mask = (result->size >= 4u)
                              ? UINT32_MAX
                              : ((UINT32_C(1) << (result->size * 8u)) - 1u);
    cpu->regs.d[coproc->ea.reg] =
        (cpu->regs.d[coproc->ea.reg] & ~mask) | (value & mask);
    return FP_SOURCE_FETCHED;
  }

  ap_m68030_address_input_t input = {0};
  if (!gather_address_input(cpu, coproc->ea.kind, result->size, clocks,
                            &input)) {
    return FP_SOURCE_FAILED;
  }
  const ap_m68030_address_t where =
      resolve_address(cpu, clocks, coproc->ea, &input);
  if (!where.valid) {
    return FP_SOURCE_FAILED;
  }
  return write_operand_bytes(cpu, clocks, &where, result->size, result->bytes)
             ? FP_SOURCE_FETCHED
             : FP_SOURCE_FAILED;
}

/* FMOVEM, which is a *list* of transfers and not one seen eight times.
 *
 * Three things differ from the single-operand path and each is its own rule:
 *
 * - **The addressing modes are the union of a category and one mode.** Reading,
 *   the table allows the control modes and `(An)+`; writing, the control
 *   alterable modes and `-(An)`. So `(An)+` is legal in one direction only and
 *   `-(An)` in the other, which no category expresses -- the manual states it
 *   as "If the effective address is the predecrement mode, only a register to
 *   memory operation is allowed."
 * - **The address register steps per register**, twelve bytes at a time, and
 *   the step happens *before* the store in the predecrement case and *after*
 *   the load in the postincrement one. Handing this to `ap_m68030_address_
 *   calculate` would step it once for the whole instruction.
 * - **The mask's bit order reverses with the mode.** Handled entirely by
 *   `ap_m68882_movem_register`, which is why the loop below is one loop.
 */
static fp_source_result_t execute_fmovem(ap_m68030_cpu_t *cpu,
                                         const ap_m68030_coproc_t *coproc,
                                         const ap_m68882_movem_t *movem,
                                         uint32_t *clocks) {
  const ap_m68030_ea_kind_t kind = coproc->ea.kind;
  const bool control = ap_m68030_ea_is_control(kind);

  if (movem->to_memory) {
    if (!(ap_m68030_ea_is_control_alterable(kind) ||
          kind == AP_M68030_EA_PREDECREMENT)) {
      return FP_SOURCE_PROTOCOL_VIOLATION;
    }
  } else {
    if (!(control || kind == AP_M68030_EA_POSTINCREMENT)) {
      return FP_SOURCE_PROTOCOL_VIOLATION;
    }
  }
  /* The mode field and the effective address have to agree. MODE says
   * predecrement or "postincrement or control", and an instruction whose two
   * halves disagree names no transfer at all. */
  if (movem->predecrement != (kind == AP_M68030_EA_PREDECREMENT)) {
    return FP_SOURCE_PROTOCOL_VIOLATION;
  }

  /* "a dynamic value in the least significant 8-bits of a main processor data
   * register (the remaining bits of the register are ignored)". */
  const unsigned mask =
      movem->dynamic ? (unsigned)(cpu->regs.d[movem->dynamic_register] & 0xFFu)
                     : movem->mask;

  /* A control mode resolves once, and every register sits at a fixed offset
   * above it: "the registers are transferred between the FPCP and memory
   * starting at the specified address and up through higher addresses". */
  uint32_t base = 0;
  if (control) {
    ap_m68030_address_input_t input = {0};
    if (!gather_address_input(cpu, kind, 12u, clocks, &input)) {
      return FP_SOURCE_FAILED;
    }
    const ap_m68030_address_t where =
        resolve_address(cpu, clocks, coproc->ea, &input);
    if (!where.valid) {
      return FP_SOURCE_FAILED;
    }
    base = where.address;
  }

  /* Bit 7 first in every mode -- see ap_m68882_movem_register. What changes is
   * which register that bit names and which way memory runs. */
  unsigned transferred = 0;
  for (int bit = 7; bit >= 0; bit--) {
    if ((mask & (1u << (unsigned)bit)) == 0u) {
      continue;
    }
    const unsigned reg =
        ap_m68882_movem_register(movem->predecrement, (unsigned)bit);

    uint32_t address = 0;
    if (control) {
      address = base + 12u * transferred;
    } else if (movem->predecrement) {
      /* "Before each register is stored, the address register is decremented
       * by 12 ... and the floating-point data register is then stored at the
       * resultant address." So the register is left pointing at the image it
       * stored last, not one slot past it. */
      cpu->regs.a[coproc->ea.reg] -= 12u;
      address = cpu->regs.a[coproc->ea.reg];
    } else {
      address = cpu->regs.a[coproc->ea.reg];
    }

    const ap_m68030_address_t where = {.address = address, .valid = true};
    uint8_t bytes[12] = {0};
    if (movem->to_memory) {
      ap_m68882_movem_read(cpu->fpu, reg, bytes);
      if (!write_operand_bytes(cpu, clocks, &where, 12u, bytes)) {
        return FP_SOURCE_FAILED;
      }
    } else {
      if (!read_operand_bytes(cpu, clocks, &where, 12u, bytes)) {
        return FP_SOURCE_FAILED;
      }
      ap_m68882_movem_write(cpu->fpu, reg, bytes);
    }

    if (!control && !movem->predecrement) {
      /* "After each register is loaded, the address register is incremented by
       * 12 ... the address register points to the byte immediately following
       * the image of the last floating-point data register loaded." */
      cpu->regs.a[coproc->ea.reg] += 12u;
    }
    transferred++;
  }
  return FP_SOURCE_FETCHED;
}

/* The system control registers: `FMOVE` of one and `FMOVEM` of several, which
 * are one encoding and so one routine.
 *
 * **The address register steps once, not per register**, and that is the rule
 * that differs from the data-register FMOVEM directly above: "If the addressing
 * mode is predecrement, the address register is first decremented by the total
 * size of the register images to be moved (i.e., 4 times the number of
 * registers) and then the registers are transferred starting at the resultant
 * address. For the postincrement addressing mode, the selected registers are
 * transferred to or from the specified address, and then the address register is
 * incremented by the total size." So both increment modes run *upwards* through
 * memory here, where the data registers' predecrement runs downwards.
 *
 * The register direct modes are allowed only for a single register, and the
 * address registers only for the FPIAR -- which is the one control register that
 * holds an address. */
static fp_source_result_t transfer_fp_control(ap_m68030_cpu_t *cpu,
                                              const ap_m68030_coproc_t *coproc,
                                              const ap_m68882_control_t *control,
                                              uint32_t *clocks) {
  const ap_m68030_ea_kind_t kind = coproc->ea.kind;
  const unsigned count = ap_m68882_control_count(control->select);
  const unsigned bytes_total = count * 4u;

  if (count == 0u) {
    /* A select of zero names nothing. Nothing is transferred and no address is
     * evaluated, which is what "if a register is to be moved, the corresponding
     * bit in the list is set" leaves when none is. */
    return FP_SOURCE_FETCHED;
  }

  const bool single_register_direct =
      kind == AP_M68030_EA_DATA_REGISTER && count == 1u;
  /* "Only if the FPIAR is the single register selected" -- the footnote is on
   * the address register row in both directions. */
  const bool fpiar_only =
      kind == AP_M68030_EA_ADDRESS_REGISTER &&
      control->select == (1u << AP_M68882_CONTROL_FPIAR);

  if (!single_register_direct && !fpiar_only) {
    /* Everything else must be a memory mode -- alterable too, when the transfer
     * is outward. */
    const bool allowed = control->to_memory
                             ? ap_m68030_ea_is_memory_alterable(kind)
                             : ap_m68030_ea_is_memory(kind);
    if (!allowed) {
      return FP_SOURCE_PROTOCOL_VIOLATION;
    }
  }

  /* A register direct destination or source, which by the rules above is
   * exactly one register. */
  if (single_register_direct || fpiar_only) {
    unsigned bit = AP_M68882_CONTROL_FPCR;
    while ((control->select & (1u << bit)) == 0u) {
      bit--;
    }
    uint32_t *const reg = (kind == AP_M68030_EA_DATA_REGISTER)
                              ? &cpu->regs.d[coproc->ea.reg]
                              : &cpu->regs.a[coproc->ea.reg];
    if (control->to_memory) {
      *reg = ap_m68882_control_read(cpu->fpu, bit);
    } else {
      ap_m68882_control_write(cpu->fpu, bit, *reg);
    }
    return FP_SOURCE_FETCHED;
  }

  /* An immediate source is in the instruction stream: four bytes per register,
   * and only ever inward, since the register-to-memory table has no `#<data>`
   * row at all. */
  uint32_t base = 0;
  if (kind == AP_M68030_EA_IMMEDIATE) {
    if (control->to_memory) {
      return FP_SOURCE_PROTOCOL_VIOLATION;
    }
    for (unsigned bit = AP_M68882_CONTROL_FPCR;
         bit + 1u > AP_M68882_CONTROL_FPIAR; bit--) {
      if ((control->select & (1u << bit)) == 0u) {
        continue;
      }
      uint16_t high = 0;
      uint16_t low = 0;
      if (!next_word(cpu, clocks, &high) || !next_word(cpu, clocks, &low)) {
        return FP_SOURCE_FAILED;
      }
      ap_m68882_control_write(cpu->fpu, bit,
                              ((uint32_t)high << 16) | (uint32_t)low);
    }
    return FP_SOURCE_FETCHED;
  }

  if (kind == AP_M68030_EA_PREDECREMENT) {
    cpu->regs.a[coproc->ea.reg] -= bytes_total;
    base = cpu->regs.a[coproc->ea.reg];
  } else if (kind == AP_M68030_EA_POSTINCREMENT) {
    base = cpu->regs.a[coproc->ea.reg];
    cpu->regs.a[coproc->ea.reg] += bytes_total;
  } else {
    ap_m68030_address_input_t input = {0};
    if (!gather_address_input(cpu, kind, bytes_total, clocks, &input)) {
      return FP_SOURCE_FAILED;
    }
    const ap_m68030_address_t where =
        resolve_address(cpu, clocks, coproc->ea, &input);
    if (!where.valid) {
      return FP_SOURCE_FAILED;
    }
    base = where.address;
  }

  /* "The registers are always moved in the same order, regardless of the
   * addressing mode used; with the FPCR moved first, followed by the FPSR, and
   * the FPIAR moved last", at successively higher addresses. Bit 12 down to bit
   * 10 is that order, so the walk is the same shape as FMOVEM's and without its
   * reversal. */
  unsigned transferred = 0;
  for (unsigned bit = AP_M68882_CONTROL_FPCR;
       bit + 1u > AP_M68882_CONTROL_FPIAR; bit--) {
    if ((control->select & (1u << bit)) == 0u) {
      continue;
    }
    const ap_m68030_address_t where = {.address = base + 4u * transferred,
                                       .valid = true};
    if (control->to_memory) {
      const ap_m68030_operand_result_t wrote = step_operand_write(
          cpu, &cpu->regs, cpu->data, &where, 4u,
          ap_m68882_control_read(cpu->fpu, bit), cpu->data_function_code);
      *clocks += wrote.clocks;
      if (!wrote.ok) {
        return FP_SOURCE_FAILED;
      }
    } else {
      const ap_m68030_operand_result_t read = step_operand_read(
          cpu, &cpu->regs, cpu->data, &where, 4u, cpu->data_function_code);
      *clocks += read.clocks;
      if (!read.ok) {
        return FP_SOURCE_FAILED;
      }
      ap_m68882_control_write(cpu->fpu, bit, read.value);
    }
    transferred++;
  }
  return FP_SOURCE_FETCHED;
}

/* `FDBcc`, `FScc` and `FTRAPcc`: one instruction type (`001`), one command word
 * format, and Table 4-19's instruction-specific field to tell them apart.
 *
 * §4.7.2: "For these instruction types, the MPU writes a conditional predicate
 * to the FPCP condition CIR for evaluation ... The true or false result is
 * returned to the main processor with the null primitive." So the coprocessor's
 * whole half is `ap_m68882_condition`, and everything below -- decrementing a
 * register, writing a byte, taking a trap -- is the main processor's.
 *
 * **Table 4-19 has a defect at `111 000` and `111 001`.** It marks both
 * "(Undefined, reserved)", which by its own Note 3 would take an F-line trap.
 * Two other statements disagree, and they are the per-instruction ones: `FScc`'s
 * page lists `(xxx).W` at `111 000` and `(xxx).L` at `111 001` in its addressing
 * mode table, and the `M68000 Family Programmer's Reference Manual` says of the
 * same instruction "Only data alterable addressing modes can be used" and lists
 * both. Absolute addressing *is* data alterable. Two sources against one
 * summary table, so absolute addressing is accepted here and Table 4-19 is
 * recorded as the suspect entry. */
static fp_source_result_t execute_fp_conditional(
    ap_m68030_cpu_t *cpu, const ap_m68030_coproc_t *coproc,
    uint16_t operation_word, uint32_t *clocks, bool *branch_taken) {
  const unsigned mode = (unsigned)((operation_word >> 3) & 0x7u);
  const unsigned reg = (unsigned)(operation_word & 0x7u);

  /* The command word carries the predicate in bits 5-0; §4.7.2 shows bits 15-6
   * as zero. */
  uint16_t command = 0;
  if (!next_word(cpu, clocks, &command)) {
    return FP_SOURCE_FAILED;
  }
  const unsigned predicate = (unsigned)(command & 0x3Fu);

  if (mode == 0x1u) {
    /* FDBcc. Note 2: "If the condition is true, the MPU proceeds to the next
     * instruction. Otherwise, the counter register Dn.W ... is decremented, and
     * the new value is compared with -1. If it is equal to -1, the MPU proceeds
     * to the next instruction; otherwise, the 16-bit displacement is sign
     * extended and added to the PC."
     *
     * The displacement is fetched either way, so an untaken loop still lands
     * past it. */
    uint16_t displacement = 0;
    const uint32_t displacement_address = cpu->regs.pc + 2u + 2u * cpu->extension_words;
    if (!next_word(cpu, clocks, &displacement)) {
      return FP_SOURCE_FAILED;
    }
    if (ap_m68882_condition(cpu->fpu, predicate)) {
      return FP_SOURCE_FETCHED; /* the loop's termination condition was met */
    }
    /* "the low order 16-bits of the counter register are decremented by one",
     * so the upper half is left alone -- a full-width decrement would be right
     * for every count that never borrows and wrong for the one that does. */
    const uint16_t counter =
        (uint16_t)((cpu->regs.d[reg] & 0xFFFFu) - 1u);
    cpu->regs.d[reg] = (cpu->regs.d[reg] & 0xFFFF0000u) | counter;
    if (counter == 0xFFFFu) {
      return FP_SOURCE_FETCHED; /* the count is exhausted */
    }
    /* "The value of the PC used in the branch address calculation is the
     * address of the displacement word" -- *not* the operation word plus two,
     * which is `FBcc`'s rule two pages earlier. The two instructions differ by
     * the predicate word that sits between. */
    cpu->regs.pc = displacement_address + (uint32_t)(int32_t)(int16_t)displacement;
    ap_m68030_fetch_reset(&cpu->fetch, cpu->regs.pc);
    *branch_taken = true;
    return FP_SOURCE_FETCHED;
  }

  if (mode == 0x7u && (reg == 0x2u || reg == 0x3u || reg == 0x4u)) {
    /* FTRAPcc, with a word operand, a long one, or none. Note 4: "If the
     * condition is true, then the cpTRAPcc exception is taken. Otherwise, the
     * MPU proceeds to the next instruction, **discarding the optional immediate
     * operand**" -- discarded, but still consumed, or the operand would decode
     * as the next instruction. */
    const unsigned words = (reg == 0x2u) ? 1u : (reg == 0x3u) ? 2u : 0u;
    for (unsigned i = 0; i < words; i++) {
      uint16_t ignored = 0;
      if (!next_word(cpu, clocks, &ignored)) {
        return FP_SOURCE_FAILED;
      }
    }
    if (ap_m68882_condition(cpu->fpu, predicate)) {
      cpu->pending_vector = AP_M68030_VECTOR_TRAPCC;
    }
    return FP_SOURCE_FETCHED;
  }

  if (mode == 0x7u && reg > 0x1u) {
    /* Table 4-19's genuinely reserved rows, `111 101` through `111 111`. Note
     * 3: "The MPU takes an F-line emulation trap" -- the machine's behaviour
     * and not our gap. */
    cpu->pending_vector = AP_M68030_VECTOR_LINE_F;
    return FP_SOURCE_FETCHED;
  }

  /* FScc. "If the specified floating-point condition is true, sets the byte
   * integer operand at the destination to TRUE (all ones), otherwise sets the
   * byte to FALSE (all zeroes)." */
  if (!ap_m68030_ea_is_data_alterable(coproc->ea.kind)) {
    return FP_SOURCE_PROTOCOL_VIOLATION;
  }
  const uint8_t value = ap_m68882_condition(cpu->fpu, predicate) ? 0xFFu : 0x00u;

  if (coproc->ea.kind == AP_M68030_EA_DATA_REGISTER) {
    cpu->regs.d[coproc->ea.reg] =
        (cpu->regs.d[coproc->ea.reg] & 0xFFFFFF00u) | value;
    return FP_SOURCE_FETCHED;
  }

  ap_m68030_address_input_t input = {0};
  if (!gather_address_input(cpu, coproc->ea.kind, 1u, clocks, &input)) {
    return FP_SOURCE_FAILED;
  }
  const ap_m68030_address_t where =
      resolve_address(cpu, clocks, coproc->ea, &input);
  if (!where.valid) {
    return FP_SOURCE_FAILED;
  }
  const ap_m68030_operand_result_t wrote =
      step_operand_write(cpu, &cpu->regs, cpu->data, &where, 1u, value,
                         cpu->data_function_code);
  *clocks += wrote.clocks;
  return wrote.ok ? FP_SOURCE_FETCHED : FP_SOURCE_FAILED;
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


/* CAS2, `M68000 Family PRM` 4-67.
 *
 * "CAS2 compares memory operand 1 (Rn1) to compare operand 1 (Dc1). If the
 * operands are equal, the instruction compares memory operand 2 (Rn2) to
 * compare operand 2 (Dc2). If these operands are also equal, the instruction
 * writes the update operands (Du1 and Du2) to the memory operands (Rn1 and
 * Rn2). If either comparison fails, the instruction writes the memory operands
 * (Rn1 and Rn2) to the compare operands (Dc1 and Dc2)."
 *
 * ## The addresses are registers, not effective addresses
 *
 * This had been declined as "a two-address atomic this operand path cannot
 * express", and that was wrong: the instruction's own format gives each operand
 * an `Rn` field and nothing else -- "Rn1, Rn2 fields: Specify the numbers of
 * the registers that contain the addresses of the first and second memory
 * operands". There is no addressing mode to evaluate, so what looked like a
 * missing capability was a misreading of the encoding.
 *
 * The `<ea>` in the *operation* word is `111100`, the immediate mode, which CAS
 * cannot legally take -- so the pattern is free and CAS2 uses it purely as an
 * escape. Reading it as an address is what makes this instruction look harder
 * than it is.
 *
 * ## One lock across four accesses
 *
 * Both comparisons and both writes happen under a single `RMC`, which is the
 * point of the instruction: it is what lets two ends of a linked list be
 * swapped atomically. Two separate CAS operations would be a different
 * instruction with the same mnemonic.
 *
 * ## The failure case writes to a register, and may write only one
 *
 * "If Dc1 and Dc2 specify the same data register and the comparison fails,
 * memory operand 1 is stored in the data register." So the two register writes
 * happen in order and the first wins when they collide -- a model writing the
 * second last would leave the wrong value in a case the manual calls out
 * explicitly. */
static bool execute_cas2(ap_m68030_cpu_t *cpu, const ap_m68030_bounds_t *bounds,
                         uint16_t first_extension, uint32_t *clocks) {
  uint16_t second_extension = 0;
  if (!next_word(cpu, clocks, &second_extension)) {
    return false;
  }

  const uint16_t extensions[2] = {first_extension, second_extension};
  ap_m68030_address_t where[2] = {0};
  uint32_t memory[2] = {0};
  unsigned compare_reg[2] = {0};
  unsigned update_reg[2] = {0};

  cpu->data->rmc = true;

  for (unsigned i = 0; i < 2u; i++) {
    const uint16_t word = extensions[i];
    const bool address_register = (word & 0x8000u) != 0u;
    const unsigned reg = (unsigned)((word >> 12) & 7u);
    update_reg[i] = (unsigned)((word >> 6) & 7u);
    compare_reg[i] = (unsigned)(word & 7u);

    where[i].valid = true;
    where[i].address = address_register
                           ? (reg == 7u ? ap_m68030_read_a7(&cpu->regs)
                                        : cpu->regs.a[reg])
                           : cpu->regs.d[reg];

    const ap_m68030_operand_result_t read = step_operand_read(
        cpu, &cpu->regs, cpu->data, &where[i], bounds->size,
        cpu->data_function_code);
    *clocks += read.clocks;
    if (!read.ok) {
      cpu->data->rmc = false;
      return false;
    }
    memory[i] = read.value;
  }

  /* Both comparisons before either write: the instruction is all-or-nothing,
   * and comparing one at a time with a write between them would leave memory
   * half updated if the second failed. */
  const uint16_t ccr = ap_m68030_read_ccr(&cpu->regs);
  const ap_m68030_alu_result_t first = ap_m68030_alu_sub(
      memory[0], cpu->regs.d[compare_reg[0]], bounds->size);
  bool matched = first.z;
  ap_m68030_alu_result_t codes = first;

  if (matched) {
    const ap_m68030_alu_result_t second = ap_m68030_alu_sub(
        memory[1], cpu->regs.d[compare_reg[1]], bounds->size);
    matched = second.z;
    codes = second;
  }
  ap_m68030_write_ccr(&cpu->regs, ap_m68030_alu_apply(ccr, &codes));

  bool ok = true;
  if (matched) {
    for (unsigned i = 0; i < 2u && ok; i++) {
      const ap_m68030_operand_result_t wrote = step_operand_write(
          cpu, &cpu->regs, cpu->data, &where[i], bounds->size,
          cpu->regs.d[update_reg[i]], cpu->data_function_code);
      *clocks += wrote.clocks;
      ok = wrote.ok;
    }
  } else {
    /* **Second operand first.** "If Dc1 and Dc2 specify the same data register
     * and the comparison fails, memory operand 1 is stored in the data
     * register" -- so operand 1 must be the one that *remains*, which means it
     * is written last. Writing them in the obvious order leaves operand 2
     * there, and only in the colliding case, which no ordinary test reaches. */
    for (unsigned i = 2u; i-- > 0u && ok;) {
      ap_m68030_address_t into_register = {0};
      into_register.valid = true;
      into_register.in_register = true;
      into_register.reg = compare_reg[i];
      const ap_m68030_operand_result_t back = step_operand_write(
          cpu, &cpu->regs, cpu->data, &into_register, bounds->size, memory[i],
          cpu->data_function_code);
      *clocks += back.clocks;
      ok = back.ok;
    }
  }

  cpu->data->rmc = false;
  return ok;
}

/* CAS, `M68000 Family PRM` 4-65.
 *
 * "CAS compares the effective address operand to the compare operand (Dc). If
 * the operands are equal, the instruction writes the update operand (Du) to the
 * effective address operand; otherwise, the instruction writes the effective
 * address operand to the compare operand (Dc)."
 *
 * ## The lock is the instruction
 *
 * "Both operations access memory using locked or read-modify-write transfer
 * sequences, providing a means of synchronizing several processors." So the
 * read and the write are one indivisible operation, and `RMC` is what says so
 * to the rest of the machine: `ap_m68030_arb` refuses a bus grant while it is
 * asserted, which is what stops a DMA controller taking the bus between the two
 * halves. Running the read and the write without it would produce the right
 * bytes and the wrong instruction -- a compare-and-swap that is not atomic is
 * not a compare-and-swap, and the failure only appears under contention.
 *
 * ## The write happens on *failure* too, and it is a different write
 *
 * On a match the update register goes to memory. On a mismatch the *memory*
 * goes to the compare register -- a register write, not a memory one. A model
 * that simply skipped the store on mismatch would leave `Dc` holding the value
 * the caller expected rather than the one that was actually there, which is
 * precisely the value the caller needs in order to retry. */
static bool execute_cas(ap_m68030_cpu_t *cpu, const ap_m68030_bounds_t *bounds,
                        uint16_t extension, uint32_t *clocks) {
  ap_m68030_address_input_t input = {0};
  if (!gather_address_input(cpu, bounds->ea.kind, bounds->size, clocks,
                            &input)) {
    return false;
  }
  ap_m68030_address_t where = resolve_address(cpu, clocks, bounds->ea, &input);
  if (!where.valid || where.in_register || where.immediate) {
    /* "Memory alterable" only: the operand is compared and conditionally
     * written back, so a register or an immediate is not one of its modes. */
    return false;
  }

  const unsigned compare_reg = ap_m68030_cas_compare_register(extension);
  const unsigned update_reg = ap_m68030_cas_update_register(extension);

  /* RMC spans both halves. Asserted before the read and negated after the
   * write, exactly as §7.3.5's flowchart has it. */
  cpu->data->rmc = true;

  const ap_m68030_operand_result_t read = step_operand_read(
      cpu, &cpu->regs, cpu->data, &where, bounds->size,
      cpu->data_function_code);
  *clocks += read.clocks;
  if (!read.ok) {
    cpu->data->rmc = false;
    return false;
  }

  /* The comparison is an ordinary subtract and sets all four codes -- "N set if
   * the result is negative", V and C included -- unlike CMP2, whose N and V are
   * undefined. */
  const uint16_t ccr = ap_m68030_read_ccr(&cpu->regs);
  const ap_m68030_alu_result_t compared = ap_m68030_alu_sub(
      read.value, cpu->regs.d[compare_reg], bounds->size);
  ap_m68030_write_ccr(&cpu->regs, ap_m68030_alu_apply(ccr, &compared));

  bool ok = true;
  if (compared.z) {
    const ap_m68030_operand_result_t wrote = step_operand_write(
        cpu, &cpu->regs, cpu->data, &where, bounds->size,
        cpu->regs.d[update_reg], cpu->data_function_code);
    *clocks += wrote.clocks;
    ok = wrote.ok;
  } else {
    /* "otherwise, the instruction writes the effective address operand to the
     * compare operand" -- into the register, at the operand's width, leaving
     * the rest of Dc alone as any partial data-register write does. */
    ap_m68030_address_t into_register = {0};
    into_register.valid = true;
    into_register.in_register = true;
    into_register.reg = compare_reg;
    const ap_m68030_operand_result_t back = step_operand_write(
        cpu, &cpu->regs, cpu->data, &into_register, bounds->size, read.value,
        cpu->data_function_code);
    *clocks += back.clocks;
    ok = back.ok;
  }

  cpu->data->rmc = false;
  return ok;
}

/* CMP2 and CHK2, `M68000 Family PRM` pages 4-70 and 4-81.
 *
 * "Compares the value in Rn to each bound. The effective address contains the
 * bounds pair: the upper bound following the lower bound."
 *
 * ## One comparison serves both signed and unsigned bounds
 *
 * The manual does not give the processor a signedness mode; it tells the
 * *programmer* which ordering to use -- "For signed comparisons, the
 * arithmetically smaller value should be used as the lower bound. For unsigned
 * comparisons, the logically smaller value should be the lower bound." So the
 * instruction cannot be looking at the sign of anything: it must be a test that
 * is correct under both readings given the stated ordering.
 *
 * The range check that satisfies exactly that is one unsigned comparison of the
 * offsets from the lower bound:
 *
 *     out of bounds  <=>  (unsigned)(Rn - LB) > (unsigned)(UB - LB)
 *
 * Signed bounds -5..5 with Rn = -10 gives an offset of -5, which as an unsigned
 * value is enormous and exceeds the span of 10 -- out. Unsigned bounds 10..20
 * with Rn = 5 gives the same, and is out for the same reason. A model that
 * chose signed or unsigned by inspecting the operands would have to invent the
 * rule for choosing, and would differ from this on the wrapped cases that are
 * precisely the ones the idiom exists to get right.
 *
 * ## The address register case reaches further than the operand size
 *
 * "If Rn is a data register and the operation size is byte or word, only the
 * appropriate low-order part of Rn is checked. If Rn is an address register and
 * the operation size is byte or word, the bounds operands are sign-extended to
 * 32 bits, and the resultant operands are compared to the full 32 bits of An."
 *
 * So the same instruction compares 8 bits of a data register and 32 bits of an
 * address register, and a model that masked both to the operand size would let
 * a large negative An pass every byte-sized check. */
static bool execute_bounds(ap_m68030_cpu_t *cpu,
                           const ap_m68030_bounds_t *bounds, uint32_t *clocks) {
  uint16_t extension = 0;
  if (!next_word(cpu, clocks, &extension)) {
    return false;
  }

  const ap_m68030_bounds_kind_t kind =
      ap_m68030_bounds_kind(bounds, extension);

  if (kind == AP_M68030_BOUNDS_CAS) {
    return execute_cas(cpu, bounds, extension, clocks);
  }
  if (kind == AP_M68030_BOUNDS_CAS2) {
    return execute_cas2(cpu, bounds, extension, clocks);
  }
  if (kind != AP_M68030_BOUNDS_CMP2 && kind != AP_M68030_BOUNDS_CHK2) {
    return false;
  }

  ap_m68030_address_input_t input = {0};
  if (!gather_address_input(cpu, bounds->ea.kind, bounds->size, clocks,
                            &input)) {
    return false;
  }
  ap_m68030_address_t where = resolve_address(cpu, clocks, bounds->ea, &input);
  if (!where.valid || where.in_register || where.immediate) {
    /* Both pages give a control addressing mode only: the bounds are a *pair*
     * in memory, so a register operand has nowhere to hold the second. */
    return false;
  }

  const ap_m68030_operand_result_t lower_read = step_operand_read(
      cpu, &cpu->regs, cpu->data, &where, bounds->size,
      cpu->data_function_code);
  *clocks += lower_read.clocks;
  if (!lower_read.ok) {
    return false;
  }

  /* "the upper bound following the lower bound" -- one operand's width along,
   * not one byte. */
  ap_m68030_address_t upper_where = where;
  upper_where.address = where.address + bounds->size;
  const ap_m68030_operand_result_t upper_read = step_operand_read(
      cpu, &cpu->regs, cpu->data, &upper_where, bounds->size,
      cpu->data_function_code);
  *clocks += upper_read.clocks;
  if (!upper_read.ok) {
    return false;
  }

  const bool is_address = ap_m68030_bounds_register_is_address(extension);
  const unsigned reg = ap_m68030_bounds_register(extension);

  uint32_t value = 0;
  uint32_t lower = lower_read.value;
  uint32_t upper = upper_read.value;
  if (is_address) {
    /* The full 32 bits of An against sign-extended bounds. */
    value = cpu->regs.a[reg & 7u];
    if (reg == 7u) {
      value = ap_m68030_read_a7(&cpu->regs);
    }
    lower = ap_m68030_sign_extend(lower, bounds->size);
    upper = ap_m68030_sign_extend(upper, bounds->size);
  } else {
    /* "only the appropriate low-order part of Rn is checked", so the register
     * and the bounds are all taken at the operand's width. */
    const uint32_t mask = (bounds->size >= 4u)
                              ? UINT32_C(0xFFFFFFFF)
                              : ((UINT32_C(1) << (bounds->size * 8u)) - 1u);
    value = cpu->regs.d[reg] & mask;
    lower &= mask;
    upper &= mask;
  }

  const bool out_of_bounds = (value - lower) > (upper - lower);

  uint16_t ccr = ap_m68030_read_ccr(&cpu->regs);
  /* "Z -- Set if Rn is equal to either bound; cleared otherwise." Either, not
   * both, and not "within": a value in the middle of a wide range clears Z. */
  if (value == lower || value == upper) {
    ccr |= (uint16_t)(1u << AP_M68030_SR_Z_BIT);
  } else {
    ccr &= (uint16_t)~(1u << AP_M68030_SR_Z_BIT);
  }
  if (out_of_bounds) {
    ccr |= (uint16_t)(1u << AP_M68030_SR_C_BIT);
  } else {
    ccr &= (uint16_t)~(1u << AP_M68030_SR_C_BIT);
  }
  /* N and V are undefined and X is unaffected, so neither is written. A model
   * that cleared N and V would be inventing a guarantee software could come to
   * depend on. */
  ap_m68030_write_ccr(&cpu->regs, ccr);

  if (kind == AP_M68030_BOUNDS_CHK2 && out_of_bounds) {
    /* "a CHK instruction exception (vector number 6) occurs" -- the same vector
     * CHK uses, which is why the two share a handler. CMP2 sets the codes and
     * returns, which is the only difference between them. */
    cpu->pending_vector = AP_M68030_VECTOR_CHK;
  }
  return true;
}

ap_m68030_step_result_t ap_m68030_step(ap_m68030_cpu_t *cpu) {
  ap_m68030_step_result_t out = {.status = AP_M68030_STEP_FAULT};
  uint16_t word = 0;
  bool abnormal = false;

  /* Where prefetching stood before this instruction, so what it spends can be
   * told from what its operands spend. §11.6's model prices the two
   * differently -- an operand access is waited on by the microcode that
   * consumes it and a prefetch is not -- and `out.clocks` alone cannot
   * distinguish them. */
  const uint64_t instruction_bus_before = cpu->fetch.bus_clocks;

  /* This describes the instruction about to run, so it starts clear. Leaving a
   * previous instruction's fault standing would make the *next* unimplemented
   * instruction report as a fault -- the same conflation this flag exists to
   * end, merely pointing the other way. */
  cpu->access_faulted = false;

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

  /* §8.1.3: "An address error exception occurs when the processor attempts to
   * prefetch an instruction from an odd address."
   *
   * Only a prefetch. Misaligned *data* is legal on this part -- §7.2.1's
   * long-word transfer to an odd address simply costs three bus cycles -- which
   * is the difference from the 68000 and the reason this check is on the
   * program counter alone. Applying it to operands would fault programs the
   * hardware runs.
   *
   * Checked before the pipe is touched, because "a bus cycle is not executed,
   * and the processor begins exception processing immediately": the fault is
   * internally initiated, so no bus error is counted and no prefetch is
   * attempted. */
  if ((cpu->regs.pc & 1u) != 0u) {
    const ap_m68030_exception_result_t taken = ap_m68030_take_address_error(cpu);
    out.clocks += taken.clocks;
    out.status = taken.ok ? AP_M68030_STEP_EXCEPTION : AP_M68030_STEP_FAULT;
    cpu->clocks += out.clocks;
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
      out.status = fault_or_unimplemented(cpu, &out, instruction_address);
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
      out.status = fault_or_unimplemented(cpu, &out, instruction_address);
      cpu->clocks += out.clocks;
      return out;
    }
    break;

  case AP_M68030_DECODED_ARITH:
    if (!execute_arith(cpu, &decoded.as.arith, &out.clocks)) {
      out.status = fault_or_unimplemented(cpu, &out, instruction_address);
      cpu->clocks += out.clocks;
      return out;
    }
    break;

  case AP_M68030_DECODED_IMMEDIATE:
    if (!execute_immediate(cpu, &decoded.as.immediate, &out.clocks)) {
      out.status = fault_or_unimplemented(cpu, &out, instruction_address);
      cpu->clocks += out.clocks;
      return out;
    }
    break;

  case AP_M68030_DECODED_SINGLE:
    if (!execute_single(cpu, &decoded.as.single, &out.clocks)) {
      out.status = fault_or_unimplemented(cpu, &out, instruction_address);
      cpu->clocks += out.clocks;
      return out;
    }
    break;

  case AP_M68030_DECODED_QUICK: {
    bool taken = false;
    if (!execute_quick(cpu, &decoded.as.quick, &out.clocks, &taken)) {
      out.status = fault_or_unimplemented(cpu, &out, instruction_address);
      cpu->clocks += out.clocks;
      return out;
    }
    out.branch_taken = taken;
    break;
  }

  case AP_M68030_DECODED_SHIFT:
    if (!execute_shift(cpu, &decoded.as.shift, &out.clocks)) {
      out.status = fault_or_unimplemented(cpu, &out, instruction_address);
      cpu->clocks += out.clocks;
      return out;
    }
    break;

  case AP_M68030_DECODED_MISC:
    if (!execute_misc(cpu, &decoded.as.misc, &out.clocks)) {
      out.status = fault_or_unimplemented(cpu, &out, instruction_address);
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
      /* An MMU instruction this model has not got to. Reported as
       * unimplemented and *not* as F-line, even though F-line is what the
       * comment above says an unsupported one takes -- because the MMU is
       * fitted here. The real 68030 would execute it. Raising F-line would
       * dress our own gap up as correct hardware behaviour, and it would do so
       * convincingly: the firmware would take a plausible exception and carry
       * on, and the gap would stop being visible. */
      out.status = fault_or_unimplemented(cpu, &out, instruction_address);
      cpu->clocks += out.clocks;
      return out;
    }

    /* A floating-point coprocessor, if one is fitted. The DN3500 has a 68882
     * and a DN3000 does not, and that difference is the whole of what software
     * can see here -- so the part is attached to the CPU rather than compiled
     * into it, and a machine without one keeps exactly the trap it had.
     *
     * The general type needs its *command word*, which is the word after the
     * operation word: the operation word gets as far as "a coprocessor
     * instruction for this cpID" and cannot tell `FADD` from `FSIN`. */
    if (cpu->fpu != nullptr && coproc->cpid == cpu->fpu->cpid) {
      /* `FBcc`, which is its own instruction *type* rather than an opclass and
       * so never reaches the general path below. The operation word carries the
       * conditional predicate in bits 5-0 and the size in bit 6; the
       * displacement follows.
       *
       * §9's dialog for this one has the main processor write the predicate to
       * the condition CIR and read the answer, and everything after -- fetching
       * a displacement and moving the program counter -- is the MPU's. The
       * coprocessor's whole half is `ap_m68882_condition`. */
      if (coproc->type == AP_M68030_CP_CONDITIONAL) {
        bool branched = false;
        switch (execute_fp_conditional(cpu, coproc, out.instruction,
                                       &out.clocks, &branched)) {
        case FP_SOURCE_FETCHED:
          out.branch_taken = branched;
          break;
        case FP_SOURCE_PROTOCOL_VIOLATION:
          cpu->pending_vector = AP_M68030_VECTOR_COPROCESSOR_PROTOCOL;
          break;
        case FP_SOURCE_FAILED:
          out.status = fault_or_unimplemented(cpu, &out, instruction_address);
          cpu->clocks += out.clocks;
          return out;
        }
        break;
      }

      if (coproc->type == AP_M68030_CP_BRANCH_WORD ||
          coproc->type == AP_M68030_CP_BRANCH_LONG) {
        /* "The value of the PC used to calculate the destination address is the
         * address of the branch instruction plus two" -- the operation word's
         * own address, not where the displacement sits and not where the next
         * instruction starts. */
        const uint32_t base = instruction_address + 2u;

        uint16_t high = 0;
        if (!next_word(cpu, &out.clocks, &high)) {
          out.status = fault_or_unimplemented(cpu, &out, instruction_address);
          cpu->clocks += out.clocks;
          return out;
        }
        uint32_t displacement = (uint32_t)(int32_t)(int16_t)high;
        if (coproc->type == AP_M68030_CP_BRANCH_LONG) {
          uint16_t low = 0;
          if (!next_word(cpu, &out.clocks, &low)) {
            out.status = fault_or_unimplemented(cpu, &out, instruction_address);
            cpu->clocks += out.clocks;
            return out;
          }
          displacement = ((uint32_t)high << 16) | (uint32_t)low;
        }

        /* The predicate is evaluated *after* the displacement is fetched, so
         * that an untaken branch has still consumed its extension words and the
         * program counter lands past them.
         *
         * Bits 5-0 are the conditional predicate. They are *not* an effective
         * address here, however much they look like one -- reading
         * `coproc->ea` would decode a predicate as an addressing mode. */
        const unsigned predicate = (unsigned)(out.instruction & 0x3Fu);
        if (ap_m68882_condition(cpu->fpu, predicate)) {
          cpu->regs.pc = base + displacement;
          ap_m68030_fetch_reset(&cpu->fetch, cpu->regs.pc);
          /* The tail leaves the program counter alone when this is set: "A
           * taken branch ... has already set the PC and emptied the pipe". */
          out.branch_taken = true;
        }
        break;
      }

      uint16_t command = 0;
      if (coproc->type == AP_M68030_CP_GENERAL &&
          !next_word(cpu, &out.clocks, &command)) {
        out.status = fault_or_unimplemented(cpu, &out, instruction_address);
        cpu->clocks += out.clocks;
        return out;
      }

      /* §2.4's "before the instruction is executed", which is the only time it
       * is any use to a handler trying to find the instruction. The part
       * decides whether this one records at all -- the transfers do not. */
      ap_m68882_note_instruction(cpu->fpu, out.instruction, command,
                                 instruction_address);

      /* Ask the part what it needs before executing anything. For opclass
       * `010` the answer is a data format, and the main processor fetches the
       * operand -- which is the whole of §10.4.9's division of labour, and the
       * reason an `FADD.S (a0),FP0` runs at all. */
      bool needs_source = false;
      ap_m68882_format_t format = AP_M68882_FORMAT_LONG;
      ap_m68882_status_t executed = ap_m68882_source_transfer(
          cpu->fpu, out.instruction, command, &needs_source, &format);

      if (executed == AP_M68882_EXECUTED && needs_source) {
        ap_m68882_extended_t source = {0};
        bool violated = false;
        switch (fetch_fp_source(cpu, coproc, format, &out.clocks, &source)) {
        case FP_SOURCE_FETCHED:
          executed = ap_m68882_execute_source(cpu->fpu, out.instruction,
                                              command, &source);
          break;
        case FP_SOURCE_PROTOCOL_VIOLATION:
          /* §10.4.9's own failure, so it is the machine's trap and not our
           * gap -- an addressing mode the instruction may not name, caught by
           * the main processor before the coprocessor ever sees an operand.
           *
           * Left as a *pending vector* rather than returned from here: the
           * tail below is what turns one into a taken exception, and a step
           * that returned early would report the fault status this result was
           * initialised with and never stack a frame. */
          cpu->pending_vector = AP_M68030_VECTOR_COPROCESSOR_PROTOCOL;
          violated = true;
          break;
        case FP_SOURCE_FAILED:
          out.status = fault_or_unimplemented(cpu, &out, instruction_address);
          cpu->clocks += out.clocks;
          return out;
        }
        if (violated) {
          break;
        }
      } else if (executed == AP_M68882_EXECUTED) {
        /* Nothing to fetch. Either a result has to go the other way, or both
         * operands are already in the part. */
        bool needs_store = false;
        executed = ap_m68882_destination_transfer(
            cpu->fpu, out.instruction, command, &needs_store, &format);

        if (executed == AP_M68882_EXECUTED && needs_store) {
          ap_m68882_store_t result = {0};
          executed = ap_m68882_execute_store(cpu->fpu, out.instruction, command,
                                             &result);
          if (executed == AP_M68882_EXECUTED) {
            bool violated = false;
            switch (store_fp_destination(cpu, coproc, &result, &out.clocks)) {
            case FP_SOURCE_FETCHED:
              break;
            case FP_SOURCE_PROTOCOL_VIOLATION:
              cpu->pending_vector = AP_M68030_VECTOR_COPROCESSOR_PROTOCOL;
              violated = true;
              break;
            case FP_SOURCE_FAILED:
              out.status =
                  fault_or_unimplemented(cpu, &out, instruction_address);
              cpu->clocks += out.clocks;
              return out;
            }
            if (violated) {
              break;
            }
          }
        } else if (executed == AP_M68882_EXECUTED) {
          bool is_movem = false;
          ap_m68882_movem_t movem = {0};
          executed = ap_m68882_movem_transfer(cpu->fpu, out.instruction, command,
                                              &is_movem, &movem);

          if (executed == AP_M68882_EXECUTED && is_movem) {
            bool violated = false;
            switch (execute_fmovem(cpu, coproc, &movem, &out.clocks)) {
            case FP_SOURCE_FETCHED:
              break;
            case FP_SOURCE_PROTOCOL_VIOLATION:
              cpu->pending_vector = AP_M68030_VECTOR_COPROCESSOR_PROTOCOL;
              violated = true;
              break;
            case FP_SOURCE_FAILED:
              out.status =
                  fault_or_unimplemented(cpu, &out, instruction_address);
              cpu->clocks += out.clocks;
              return out;
            }
            if (violated) {
              break;
            }
          } else if (executed == AP_M68882_EXECUTED) {
            bool is_control = false;
            ap_m68882_control_t control = {0};
            executed = ap_m68882_control_transfer(
                cpu->fpu, out.instruction, command, &is_control, &control);

            if (executed == AP_M68882_EXECUTED && is_control) {
              bool violated = false;
              switch (
                  transfer_fp_control(cpu, coproc, &control, &out.clocks)) {
              case FP_SOURCE_FETCHED:
                break;
              case FP_SOURCE_PROTOCOL_VIOLATION:
                cpu->pending_vector = AP_M68030_VECTOR_COPROCESSOR_PROTOCOL;
                violated = true;
                break;
              case FP_SOURCE_FAILED:
                out.status =
                    fault_or_unimplemented(cpu, &out, instruction_address);
                cpu->clocks += out.clocks;
                return out;
              }
              if (violated) {
                break;
              }
            } else if (executed == AP_M68882_EXECUTED) {
              /* Both operands already in the part: opclass `000`. */
              executed = ap_m68882_execute(cpu->fpu, out.instruction, command);
            }
          }
        }
      }

      if (executed == AP_M68882_EXECUTED) {
        break;
      }
      if (executed == AP_M68882_UNIMPLEMENTED) {
        /* A form the part executes and this model has not got to. Reported as
         * *our* gap and not as the machine's trap -- raising F-line here would
         * be indistinguishable from a correct unfitted machine, and the gap
         * would stop being visible. */
        out.status = fault_or_unimplemented(cpu, &out, instruction_address);
        cpu->clocks += out.clocks;
        return out;
      }
      /* AP_M68882_TAKE_LINE_F falls through to the trap below, which is where
       * an undefined encoding belongs: Table 4-13's footnote 2 has the FPCP
       * itself ask the MPU for an F-line trap, so the vector is the same one an
       * unfitted machine takes and arrives for a different reason. */
    }

    /* No coprocessor answered. `[030]` §8.1: an F-line word takes the line 1111
     * emulator exception when no coprocessor responds -- which is every cpID
     * but the MMU's on a machine with none fitted.
     *
     * This is correct hardware behaviour rather than a stand-in for missing
     * work, and the distinction from the MMU case above is the whole point: one
     * is the machine doing what it does, the other is us not having finished.
     * They must not report the same thing. */
    cpu->pending_vector = AP_M68030_VECTOR_LINE_F;
    break;
  }

  case AP_M68030_DECODED_BOUNDS:
    /* CMP2 and CHK2 execute. CAS and CAS2 still decline: their read and write
     * are indivisible, so executing them honestly means the bus asserting RMC
     * for the pair, and that is the bus module's item rather than this one --
     * `execute_bounds` refuses them rather than running them without it. */
    if (execute_bounds(cpu, &decoded.as.bounds, &out.clocks)) {
      break;
    }
    out.status = fault_or_unimplemented(cpu, &out, instruction_address);
    cpu->clocks += out.clocks;
    return out;

  case AP_M68030_DECODED_ILLEGAL:
    out.status = fault_or_unimplemented(cpu, &out, instruction_address);
    cpu->clocks += out.clocks;
    return out;

  case AP_M68030_DECODED_LINE_A:
    /* `[030]` Table 8-1, vector 10: the line 1010 emulator. Unconditional --
     * no word with bits 15-12 = 1010 is an instruction on any member of the
     * family, so there is nothing to implement here and never will be. The
     * whole `A000-AFFF` range exists to be trapped and emulated in software.
     *
     * Reporting these unimplemented was wrong in a way that mattered: it said
     * the gap was ours, when in fact taking the trap *is* the complete and
     * correct behaviour. */
    cpu->pending_vector = AP_M68030_VECTOR_LINE_A;
    break;
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
  /* A row footnoted `*` publishes a *component*: `ADD Dn,EA` is 3 clocks, and
   * the effective address it reads through is another 3 or 4 from §11.6.1. Both
   * halves are now transcribed, so such a row is composed through Equation
   * (11-2) rather than declined -- which is what `FINDINGS.md` C9 asked for
   * after measuring 7 clocks against our 4 for `ADD.B D0,(A0)`.
   *
   * The effective address is bits 5-0 of the instruction word for every row
   * this applies to: the arithmetic forms' operand, and `MOVE`'s *source* --
   * §11.6.6's own figures already include the destination address, which is why
   * only the source is added separately.
   *
   * `**` rows are still declined. That footnote names §11.6.2, Fetch Immediate
   * Effective Address, which is a different table and not transcribed; pricing
   * one off §11.6.1 would produce a plausible number from the wrong page. */
  const ap_m68030_ea_timing_t *ea_timing = nullptr;
  if (published != nullptr) {
    const ap_m68030_ea_t ea =
        ap_m68030_ea_decode((out.instruction >> 3) & 7u, out.instruction & 7u);
    switch (published->effective_address_time) {
    case AP_M68030_EA_TIME_FETCH:
      /* The size is read only for §11.6.1's immediate rows. No `*` row can
       * *take* an immediate -- every one of them writes its effective address
       * back -- so the figure passed here is never the one used, and a long is
       * the safe reading if that ever changes: it is the larger of the two. */
      ea_timing = ap_m68030_ea_fetch_timing(ea.kind, 4u);
      break;
    case AP_M68030_EA_TIME_FETCH_IMMEDIATE:
      /* §11.6.2, whose entry covers the immediate *and* the destination
       * together -- which is why a `**` row cannot be priced off §11.6.1, and
       * why these declined until that table was transcribed.
       *
       * The immediate's size is the instruction's operand size, since the
       * source is the operand: `ADDI.L` carries a long and `ADDI.W` a word.
       * Table 2-3's byte case occupies a whole extension word and is therefore
       * the word row, which is the same rule §11.6.1's immediate rows follow.
       *
       * The size comes from bits 7-6, which is where family `0000`'s immediate
       * rows carry it -- `00` byte, `01` word, `10` long. Every `**` row in the
       * transcription is one of those, so this is read where the manual puts it
       * rather than inferred. */
      ea_timing = ap_m68030_ea_fetch_immediate_timing(
          ea.kind, ((out.instruction >> 6) & 3u) == 2u);
      break;
    case AP_M68030_EA_TIME_NONE:
      break;
    }
  }

  const bool priceable =
      published != nullptr &&
      (published->effective_address_time == AP_M68030_EA_TIME_NONE ||
       ea_timing != nullptr);

  if (priceable) {
    /* The published cache case, composed if it has two components, then split
     * into the microcode and the operand bus cycles it contains. §11.6 states
     * both halves of that split at the head of every table: the `(r/p/w)`
     * counts "are included in the total clock cycle number", and "all timing
     * data assumes two-clock reads and writes".
     *
     * Only the microcode is taken from the manual. The operand bus time is
     * whatever this core just measured, so a wait state, a cache hit or a slow
     * device still moves the answer -- which is the difference between this and
     * a cycle-table model, and the whole reason the figures were decomposed
     * rather than used whole. */
    ap_m68030_overlap_state_t composed = ap_m68030_overlap_begin();
    ap_m68030_ea_timing_compose(&composed, ea_timing, &published->timing);

    unsigned published_bus =
        (published->timing.reads + published->timing.writes) * 2u;
    if (ea_timing != nullptr) {
      published_bus += (ea_timing->timing.reads + ea_timing->timing.writes) * 2u;
    }
    const uint64_t total = ap_m68030_overlap_total(&composed);
    const uint32_t microcode =
        total > published_bus ? (uint32_t)(total - published_bus) : 0u;

    /* What this instruction spent on its own prefetches, told apart from what
     * it spent on operands -- the two are priced differently and `out.clocks`
     * cannot distinguish them. */
    const uint32_t instruction_bus =
        (uint32_t)(cpu->fetch.bus_clocks - instruction_bus_before);
    const uint32_t operand_bus =
        out.clocks > instruction_bus ? out.clocks - instruction_bus : 0u;

    /* And a prefetch costs what the published pair says it costs *for this
     * instruction*, not what it costs on the bus. §11.3.3's no-cache figure is
     * the average of the two alignment cases, and for the rows this applies to
     * the odd-aligned case runs no fetch at all -- so the even-aligned case is
     * twice the published difference, and comes to 0 or 2. Zero is a prefetch
     * that ran entirely under the microcode; two is one that hid behind
     * nothing. `ap_m68030_prefetch_exposure` and its test carry the reasoning
     * and the rows it cannot apply to. */
    /* ...and only for *one* prefetch, which is what a row's published
     * difference is about: the cycle that keeps a full pipe full. An
     * instruction that ran more than one is refilling a pipe some change of
     * flow emptied, and §11.6 charges that refill to the branch -- `Bcc`
     * taken is 6 clocks against an untaken byte branch's 4 for exactly that
     * reason. This core cannot move the cost to the branch, so it charges the
     * refill where it happens, at what it measured. That is the same
     * convention as declining an unknown: report the measurement rather than
     * substitute a figure derived for something else.
     *
     * Without this the refill would vanish entirely -- the target instruction
     * would discard it in favour of its own exposure, and the branch that
     * caused it is a change of flow, whose class is UNKNOWN and declines. */
    const uint32_t one_bus_cycle = 2u;
    uint32_t prefetch_cost = instruction_bus;
    if (published->prefetch_class == AP_M68030_PREFETCH_ODD_WORDS) {
      /* Three words: both alignments run a fetch, and it is the *count* that
       * differs -- two when aligned, one when not. So the published average is
       * undone by charging the larger case and nothing for the smaller, which
       * is why this cannot go through the "did a prefetch happen" test below. */
      prefetch_cost = instruction_bus > one_bus_cycle
                          ? ap_m68030_prefetch_exposure(
                                &published->timing, published->prefetch_class)
                          : 0u;
    } else if (instruction_bus <= one_bus_cycle) {
      prefetch_cost =
          instruction_bus > 0u
              ? ap_m68030_prefetch_exposure(&published->timing,
                                            published->prefetch_class)
              : 0u;
    }

    out.clocks = microcode + operand_bus + prefetch_cost;
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

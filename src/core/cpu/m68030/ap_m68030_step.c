/* MC68030 instruction step. See ap_m68030_step.h for why an unimplemented
 * instruction is reported rather than skipped. */

#include "cpu/m68030/ap_m68030_step.h"

#include "cpu/m68030/ap_m68030_branch.h"
#include "cpu/m68030/ap_m68030_control.h"

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
  case AP_M68030_DECODED_IMMEDIATE:
  case AP_M68030_DECODED_MOVE:
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

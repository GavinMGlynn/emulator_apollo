#include "cpu/m68030/ap_m68030_ssw.h"

#include "cpu/m68030/ap_m68030_regs.h"

static uint16_t bit(bool set, unsigned position) {
  return set ? (uint16_t)(1u << position) : (uint16_t)0u;
}

uint16_t ap_m68030_ssw_encode(const ap_m68030_ssw_t *ssw) {
  /* "A rerun bit is always set when the corresponding fault bit is set."
   * Applied here rather than asked of the caller: a fault bit is what a bus
   * error sets, and forgetting its rerun bit produces a word claiming the stage
   * is invalid but needs no prefetch -- which leaves a stale instruction word
   * in the pipe when the handler returns. */
  const bool stage_c_rerun = ssw->stage_c_rerun || ssw->stage_c_fault;
  const bool stage_b_rerun = ssw->stage_b_rerun || ssw->stage_b_fault;

  uint16_t word = 0;
  word |= bit(ssw->stage_c_fault, AP_M68030_SSW_FC_BIT);
  word |= bit(ssw->stage_b_fault, AP_M68030_SSW_FB_BIT);
  word |= bit(stage_c_rerun, AP_M68030_SSW_RC_BIT);
  word |= bit(stage_b_rerun, AP_M68030_SSW_RB_BIT);
  word |= bit(ssw->data_fault, AP_M68030_SSW_DF_BIT);
  word |= bit(ssw->read_modify_write, AP_M68030_SSW_RM_BIT);
  word |= bit(ssw->read, AP_M68030_SSW_RW_BIT);
  word |= (uint16_t)(((unsigned)ssw->size & AP_M68030_SSW_SIZE_MASK)
                     << AP_M68030_SSW_SIZE_SHIFT);
  word |= (uint16_t)(ssw->function_code & AP_M68030_SSW_FUNCTION_CODE_MASK);
  return word;
}

ap_m68030_ssw_t ap_m68030_ssw_decode(uint16_t word) {
  ap_m68030_ssw_t out = {
      .stage_c_fault = (word & (1u << AP_M68030_SSW_FC_BIT)) != 0u,
      .stage_b_fault = (word & (1u << AP_M68030_SSW_FB_BIT)) != 0u,
      .stage_c_rerun = (word & (1u << AP_M68030_SSW_RC_BIT)) != 0u,
      .stage_b_rerun = (word & (1u << AP_M68030_SSW_RB_BIT)) != 0u,
      .data_fault = (word & (1u << AP_M68030_SSW_DF_BIT)) != 0u,
      .read_modify_write = (word & (1u << AP_M68030_SSW_RM_BIT)) != 0u,
      .read = (word & (1u << AP_M68030_SSW_RW_BIT)) != 0u,
      .size = (ap_m68030_ssw_size_t)((word >> AP_M68030_SSW_SIZE_SHIFT) &
                                     AP_M68030_SSW_SIZE_MASK),
      .function_code =
          (uint8_t)(word & AP_M68030_SSW_FUNCTION_CODE_MASK),
  };
  return out;
}

ap_m68030_ssw_size_t ap_m68030_ssw_size_for(unsigned bytes) {
  switch (bytes) {
  case 1u: return AP_M68030_SSW_SIZE_BYTE;
  case 2u: return AP_M68030_SSW_SIZE_WORD;
  case 3u: return AP_M68030_SSW_SIZE_THREE_BYTE;
  default: break;
  }
  /* "Indicates the number of bytes remaining to be transferred": a long word is
   * zero because four bytes do not fit in two bits, and every operand this
   * processor moves is one, two, three or four bytes. Anything else is a caller
   * error, and long is the only encoding left to report it as -- so it is
   * reported as long rather than as a fifth value the field cannot hold. */
  return AP_M68030_SSW_SIZE_LONG;
}

unsigned ap_m68030_ssw_size_bytes(ap_m68030_ssw_size_t size) {
  switch (size) {
  case AP_M68030_SSW_SIZE_BYTE: return 1u;
  case AP_M68030_SSW_SIZE_WORD: return 2u;
  case AP_M68030_SSW_SIZE_THREE_BYTE: return 3u;
  case AP_M68030_SSW_SIZE_LONG: return 4u;
  }
  return 4u;
}

ap_m68030_frame_format_t
ap_m68030_bus_fault_frame(const ap_m68030_ssw_t *ssw,
                          bool at_instruction_boundary) {
  /* "Data read faults only generate the long bus fault frame ... the handler
   * must transfer properly sized data from the location indicated by the fault
   * address and address space to the image of the data input buffer (DIB) at
   * location SP + $2C of the long format stack frame."
   *
   * The short frame has no data input buffer. A read fault given that frame is
   * not merely stacked in less detail -- there is nowhere for the handler to
   * put the value the read was supposed to return, so the fault becomes
   * unrepairable and demand paging cannot work. */
  /* **"Data read faults", and an instruction prefetch is not one.** The rule
   * above is about the data input buffer: a faulted data read has to be
   * *repaired* by the handler writing the value into the frame, so it needs the
   * long frame that has somewhere to write it. A faulted instruction read has
   * no such image and needs none -- the handler makes the page resident and the
   * pipe fetches the word again -- so the short frame is complete.
   *
   * The function code is what tells them apart, and it is already in the word:
   * 2 and 6 are the program spaces, 1 and 5 the data spaces.
   *
   * Measured: the oracle stacks format `A` -- the short frame -- for the
   * instruction fetch fault at `3B5AC3FE`, with `DF` and `RW` both set. Giving
   * that fault the long frame is one of the five encodings this project tried
   * and rejected, and it failed for this reason. */
  const bool program_space =
      ssw->function_code == AP_M68030_FC_USER_PROGRAM ||
      ssw->function_code == AP_M68030_FC_SUPERVISOR_PROGRAM;
  if (ssw->data_fault && ssw->read && !program_space) {
    return AP_M68030_FRAME_LONG_BUS_FAULT;
  }
  /* An instruction-stream read that is *not* at an instruction boundary is
   * Table 8-6's format `B` case: the extension word of an instruction already
   * in execution. Measured on the oracle at `0081CBFE`, which stacks `B008`
   * with the fault address `0081CC00` -- `PC+2` -- in the frame. */
  if (ssw->data_fault && ssw->read && program_space &&
      !at_instruction_boundary) {
    return AP_M68030_FRAME_LONG_BUS_FAULT;
  }
  return AP_M68030_FRAME_SHORT_BUS_FAULT;
}

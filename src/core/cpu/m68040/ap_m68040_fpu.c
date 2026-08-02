/* MC68040 floating-point unit: Table 9-10's unimplemented list, and the
 * three-way classification of an F-line word. See the header for why refusing
 * these instructions is the hardware's behaviour rather than a gap in ours. */

#include "cpu/m68040/ap_m68040_fpu.h"

bool ap_m68040_fpu_is_unimplemented(ap_m68882_operation_t op) {
  switch (op) {
  /* Table 9-10, monadic. Every transcendental, and -- less obviously -- the
   * exactly-specified extractions and integer parts this core computes
   * bit-exactly for the 68882. */
  case AP_M68882_OP_FACOS:
  case AP_M68882_OP_FASIN:
  case AP_M68882_OP_FATAN:
  case AP_M68882_OP_FATANH:
  case AP_M68882_OP_FCOS:
  case AP_M68882_OP_FCOSH:
  case AP_M68882_OP_FETOX:
  case AP_M68882_OP_FETOXM1:
  case AP_M68882_OP_FGETEXP:
  case AP_M68882_OP_FGETMAN:
  case AP_M68882_OP_FINT:
  case AP_M68882_OP_FINTRZ:
  case AP_M68882_OP_FLOG10:
  case AP_M68882_OP_FLOGN:
  case AP_M68882_OP_FLOGNP1:
  /* `FLOG2` is **absent from Table 9-10**, and it belongs here. See the header:
   * Appendix E's Table E-2 lists it among the instructions the `M68040FPSP`
   * emulates, so the omission is a defect in Table 9-10 rather than a
   * statement that log base 2 runs in silicon. */
  case AP_M68882_OP_FLOG2:
  case AP_M68882_OP_FSIN:
  case AP_M68882_OP_FSINCOS:
  case AP_M68882_OP_FSINH:
  case AP_M68882_OP_FTAN:
  case AP_M68882_OP_FTANH:
  case AP_M68882_OP_FTENTOX:
  case AP_M68882_OP_FTWOTOX:
  /* Table 9-10, dyadic. */
  case AP_M68882_OP_FMOD:
  case AP_M68882_OP_FREM:
  case AP_M68882_OP_FSCALE:
    return true;

  /* Everything else executes in hardware. `FSQRT` is the notable survivor:
   * IEEE specifies it exactly, and unlike the extractions above it stayed in
   * silicon. */
  case AP_M68882_OP_FMOVE_TO_FPN:
  case AP_M68882_OP_FSQRT:
  case AP_M68882_OP_FABS:
  case AP_M68882_OP_FNEG:
  case AP_M68882_OP_FDIV:
  case AP_M68882_OP_FADD:
  case AP_M68882_OP_FMUL:
  case AP_M68882_OP_FSGLDIV:
  case AP_M68882_OP_FSGLMUL:
  case AP_M68882_OP_FSUB:
  case AP_M68882_OP_FCMP:
  case AP_M68882_OP_FTST:
    return false;
  }
  return false;
}

ap_m68040_fpu_outcome_t ap_m68040_fpu_classify(uint16_t command_word) {
  const ap_m68882_command_word_t command =
      ap_m68882_decode_command(command_word);

  switch (command.extension_class) {
  case AP_M68882_EXTENSION_UNDEFINED:
    /* Not a floating-point pattern the part recognises: "the processor takes an
     * F-line illegal exception". */
    return AP_M68040_FPU_F_LINE_ILLEGAL;

  case AP_M68882_EXTENSION_REDUNDANT:
    /* The 68882's footnote 3 encodings, which "do not cause an F-line exception
     * if executed" on that part. The 68040 recognises the same patterns, so
     * they are not illegal here either -- and since they are redundant with
     * defined instructions they are what those instructions are. Reported as
     * implemented, because nothing in Table 9-10 names them. */
    return AP_M68040_FPU_IMPLEMENTED;

  case AP_M68882_EXTENSION_DEFINED:
    break;
  }

  return ap_m68040_fpu_is_unimplemented(command.operation)
             ? AP_M68040_FPU_UNIMPLEMENTED_INSTRUCTION
             : AP_M68040_FPU_IMPLEMENTED;
}

unsigned ap_m68040_fpu_frame_format(ap_m68040_fpu_outcome_t outcome) {
  switch (outcome) {
  case AP_M68040_FPU_UNIMPLEMENTED_INSTRUCTION:
    return AP_M68040_FPU_FRAME_UNIMPLEMENTED;
  case AP_M68040_FPU_F_LINE_ILLEGAL:
    return AP_M68040_FPU_FRAME_F_LINE_ILLEGAL;
  case AP_M68040_FPU_IMPLEMENTED:
    break;
  }
  /* No frame: the instruction completed. */
  return 0u;
}

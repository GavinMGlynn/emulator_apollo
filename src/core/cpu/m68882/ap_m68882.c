/* The MC68882 as a fitted part. See ap_m68882.h for why "fitted" is a machine
 * property and why an unimplemented form must not report the F-line trap. */

#include "cpu/m68882/ap_m68882.h"

void ap_m68882_reset(ap_m68882_t *fpu) {
  ap_m68882_regs_reset(&fpu->regs);
  fpu->cpid = AP_M68882_DEFAULT_CPID;
}

/* Set the condition codes from a result, Table 2-1. Every arithmetic
 * instruction ends here -- "four condition code bits that are set at the end of
 * all arithmetic instructions involving the floating-point data registers" --
 * and the moves and control transfers deliberately do not. */
static void set_condition_from(ap_m68882_regs_t *regs,
                               const ap_m68882_extended_t *value) {
  ap_m68882_result_t kind = AP_M68882_RESULT_NORMAL;
  switch (ap_m68882_classify(value)) {
  case AP_M68882_TYPE_ZERO:
    kind = AP_M68882_RESULT_ZERO;
    break;
  case AP_M68882_TYPE_INFINITY:
    kind = AP_M68882_RESULT_INFINITY;
    break;
  case AP_M68882_TYPE_NAN:
    kind = AP_M68882_RESULT_NAN;
    break;
  case AP_M68882_TYPE_NORMALIZED:
  case AP_M68882_TYPE_DENORMALIZED:
    break;
  }
  ap_m68882_set_condition(regs, kind, value->sign);
}

/* Fold an operation's exceptions into the FPSR and accrue them.
 *
 * The exception byte is cleared *first*: "this byte is cleared by the FPCP at
 * the start of most operations", so it describes the instruction that just ran
 * and not the history. The accrued byte is what keeps the history, and
 * `ap_m68882_accrue` is deliberately called after -- its `AEXC(UNFL)` is an AND
 * of two bits and cannot be evaluated one exception at a time. */
static void apply_exceptions(ap_m68882_regs_t *regs, uint32_t exceptions) {
  regs->fpsr &= ~(UINT32_C(0xFF) << 8);
  regs->fpsr |= exceptions;
  ap_m68882_accrue(regs);
}

static ap_m68882_status_t execute_register_to_register(
    ap_m68882_t *fpu, const ap_m68882_command_word_t *command) {
  const ap_m68882_rounding_t mode = ap_m68882_rounding_mode(&fpu->regs);
  const ap_m68882_precision_t precision =
      ap_m68882_rounding_precision(&fpu->regs);

  const ap_m68882_extended_t source = fpu->regs.fp[command->rx];
  const ap_m68882_extended_t destination = fpu->regs.fp[command->ry];

  ap_m68882_op_t result = {0};
  bool writes_destination = true;

  switch (command->operation) {
  case AP_M68882_OP_FADD:
    /* "FPn + Source -> FPn", so the destination is the left operand. It makes
     * no difference to an add and every difference to the subtract below. */
    result = ap_m68882_add(&destination, &source, mode, precision);
    break;
  case AP_M68882_OP_FSUB:
    /* "FPn - Source -> FPn". The other order gives the right magnitude and the
     * wrong sign, which is the kind of error that survives a test using equal
     * operands. */
    result = ap_m68882_sub(&destination, &source, mode, precision);
    break;
  case AP_M68882_OP_FMUL:
    result = ap_m68882_mul(&destination, &source, mode, precision);
    break;
  case AP_M68882_OP_FDIV:
    result = ap_m68882_div(&destination, &source, mode, precision);
    break;

  case AP_M68882_OP_FMOVE_TO_FPN:
    result.value = source;
    break;
  case AP_M68882_OP_FABS:
    result.value = source;
    result.value.sign = false;
    break;
  case AP_M68882_OP_FNEG:
    result.value = source;
    result.value.sign = !source.sign;
    break;

  case AP_M68882_OP_FTST:
    /* "FTST" sets the condition codes from the source and writes nothing --
     * which is what makes it a test rather than a move. */
    result.value = source;
    writes_destination = false;
    break;

  case AP_M68882_OP_FCMP: {
    /* A compare sets the codes from the *difference* and writes nothing. The
     * codes come from the subtraction's result rather than from a three-way
     * answer, which is what makes an unordered compare set NAN. */
    const ap_m68882_compare_t comparison =
        ap_m68882_compare(&destination, &source);
    if (comparison.unordered) {
      ap_m68882_set_condition(&fpu->regs, AP_M68882_RESULT_NAN, false);
    } else if (comparison.equal) {
      ap_m68882_set_condition(&fpu->regs, AP_M68882_RESULT_ZERO, false);
    } else {
      ap_m68882_set_condition(&fpu->regs, AP_M68882_RESULT_NORMAL,
                              comparison.less);
    }
    apply_exceptions(&fpu->regs, comparison.exceptions);
    return AP_M68882_EXECUTED;
  }

  /* Every other defined operation is a transcendental, a rounding form or one
   * of the remainder forms, which this model has not got to. Listed
   * individually rather than caught by a `default`, because `-Wswitch-enum` is
   * what will force a decision here when one of them lands -- the same
   * discipline the 68030's step uses for its own families.
   *
   * Reported as unimplemented and **not** as F-line: the hardware executes
   * these, and dressing our gap up as the machine's behaviour would make it
   * invisible. */
  case AP_M68882_OP_FINT:
  case AP_M68882_OP_FSINH:
  case AP_M68882_OP_FINTRZ:
  case AP_M68882_OP_FSQRT:
  case AP_M68882_OP_FLOGNP1:
  case AP_M68882_OP_FETOXM1:
  case AP_M68882_OP_FTANH:
  case AP_M68882_OP_FATAN:
  case AP_M68882_OP_FASIN:
  case AP_M68882_OP_FATANH:
  case AP_M68882_OP_FSIN:
  case AP_M68882_OP_FTAN:
  case AP_M68882_OP_FETOX:
  case AP_M68882_OP_FTWOTOX:
  case AP_M68882_OP_FTENTOX:
  case AP_M68882_OP_FLOGN:
  case AP_M68882_OP_FLOG10:
  case AP_M68882_OP_FLOG2:
  case AP_M68882_OP_FCOSH:
  case AP_M68882_OP_FACOS:
  case AP_M68882_OP_FCOS:
  case AP_M68882_OP_FGETEXP:
  case AP_M68882_OP_FGETMAN:
  case AP_M68882_OP_FMOD:
  case AP_M68882_OP_FSGLDIV:
  case AP_M68882_OP_FREM:
  case AP_M68882_OP_FSCALE:
  case AP_M68882_OP_FSGLMUL:
  case AP_M68882_OP_FSINCOS:
    return AP_M68882_UNIMPLEMENTED;
  }

  set_condition_from(&fpu->regs, &result.value);
  apply_exceptions(&fpu->regs, result.exceptions);
  if (writes_destination) {
    fpu->regs.fp[command->ry] = result.value;
  }
  return AP_M68882_EXECUTED;
}

ap_m68882_status_t ap_m68882_execute(ap_m68882_t *fpu, uint16_t operation_word,
                                     uint16_t command_word) {
  const ap_m68882_operation_word_t operation =
      ap_m68882_decode_operation(operation_word);

  if (!operation.is_coprocessor || operation.cpid != fpu->cpid) {
    /* Not this part's instruction. Another coprocessor may answer it; on a
     * machine where none does, the 68030 takes the F-line trap -- which is what
     * the caller does with this. */
    return AP_M68882_TAKE_LINE_F;
  }

  if (operation.type != AP_M68882_TYPE_GENERAL) {
    /* The branches, FSAVE, FRESTORE and the conditionals are their own
     * instruction dialogs. Not implemented, and not F-line either. */
    return AP_M68882_UNIMPLEMENTED;
  }

  const ap_m68882_command_word_t command =
      ap_m68882_decode_command(command_word);

  if (command.extension_class == AP_M68882_EXTENSION_UNDEFINED) {
    /* Table 4-13 footnote 2: "the FPCP issues the take pre-instruction
     * exception primitive with a vector number of 11 to instruct the MPU to
     * take an F-line emulator trap". Hardware behaviour, so it is the trap and
     * not our own gap. */
    return AP_M68882_TAKE_LINE_F;
  }
  if (command.extension_class == AP_M68882_EXTENSION_REDUNDANT) {
    /* Footnote 3: redundant with a defined instruction and explicitly *not* an
     * F-line exception. Which defined instruction is not stated, so this
     * declines rather than picking one -- guessing would run an instruction the
     * program did not write. */
    return AP_M68882_UNIMPLEMENTED;
  }

  if (command.opclass != AP_M68882_OPCLASS_REGISTER) {
    /* Every other opclass needs the main processor to fetch or store an
     * operand, which is a dialog through the coprocessor interface. */
    return AP_M68882_UNIMPLEMENTED;
  }

  return execute_register_to_register(fpu, &command);
}

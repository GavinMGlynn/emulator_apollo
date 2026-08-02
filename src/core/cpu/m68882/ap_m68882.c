/* The MC68882 as a fitted part. See ap_m68882.h for why "fitted" is a machine
 * property and why an unimplemented form must not report the F-line trap. */

#include "cpu/m68882/ap_m68882.h"

#include "cpu/m68882/ap_m68882_transcendental.h"

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

bool ap_m68882_condition(ap_m68882_t *fpu, unsigned predicate) {
  const ap_m68882_condition_t evaluated =
      ap_m68882_evaluate_condition(&fpu->regs, predicate);
  /* The exception byte describes the instruction that just ran, so a
   * conditional clears it like any other operation and then sets `BSUN` if the
   * predicate earned it. Going through `apply_exceptions` rather than setting
   * the bit directly is what keeps `AEXC(IOP)` accruing -- §6.1.10 folds `BSUN`
   * into it alongside `SNAN` and `OPERR`, and a conditional that set the
   * exception byte without accruing would lose the history the accrued byte
   * exists to keep. */
  apply_exceptions(&fpu->regs,
                   evaluated.bsun ? (UINT32_C(1) << AP_M68882_EXC_BSUN) : 0u);
  return evaluated.taken;
}

/* The general type's arithmetic, once the source operand is in hand.
 *
 * `source` is passed rather than read from `command->rx` because for opclass
 * `010` that field is a data *format* and not a register number -- using it as
 * an index there would read a floating-point register the instruction never
 * names. Both callers below supply the operand their opclass says to. */
static ap_m68882_status_t execute_general(
    ap_m68882_t *fpu, const ap_m68882_command_word_t *command,
    const ap_m68882_extended_t *supplied_source) {
  const ap_m68882_rounding_t mode = ap_m68882_rounding_mode(&fpu->regs);
  const ap_m68882_precision_t precision =
      ap_m68882_rounding_precision(&fpu->regs);

  const ap_m68882_extended_t source = *supplied_source;
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

  case AP_M68882_OP_FSQRT:
    result = ap_m68882_sqrt(&source, mode, precision);
    break;

  /* §4.3.2's exponential family. Computed to within the published error bound
   * rather than reported unimplemented -- the reasoning is in the
   * transcendental module's header, and the short form is that raising an
   * unimplemented-instruction exception where the part returns an answer is a
   * larger divergence than being sixty-four units in the last place from it. */
  case AP_M68882_OP_FETOX:
    result = ap_m68882_etox(&source, mode, precision);
    break;
  case AP_M68882_OP_FETOXM1:
    result = ap_m68882_etoxm1(&source, mode, precision);
    break;
  case AP_M68882_OP_FTWOTOX:
    result = ap_m68882_twotox(&source, mode, precision);
    break;
  case AP_M68882_OP_FTENTOX:
    result = ap_m68882_tentox(&source, mode, precision);
    break;

  /* §4.3.2's logarithms. `FLOG2` and `FLOGNP1` are not compositions of
   * `FLOGN` -- see the transcendental module's header for why each is its own
   * reduction. */
  case AP_M68882_OP_FLOGN:
    result = ap_m68882_logn(&source, mode, precision);
    break;
  case AP_M68882_OP_FLOGNP1:
    result = ap_m68882_lognp1(&source, mode, precision);
    break;
  case AP_M68882_OP_FLOG2:
    result = ap_m68882_log2(&source, mode, precision);
    break;
  case AP_M68882_OP_FLOG10:
    result = ap_m68882_log10(&source, mode, precision);
    break;

  /* §4.3.2's trigonometric functions. */
  case AP_M68882_OP_FSIN:
    result = ap_m68882_sin(&source, mode, precision);
    break;
  case AP_M68882_OP_FCOS:
    result = ap_m68882_cos(&source, mode, precision);
    break;
  case AP_M68882_OP_FTAN:
    result = ap_m68882_tan(&source, mode, precision);
    break;

  /* §4.3.2's inverse trigonometric functions. */
  case AP_M68882_OP_FATAN:
    result = ap_m68882_atan(&source, mode, precision);
    break;
  case AP_M68882_OP_FASIN:
    result = ap_m68882_asin(&source, mode, precision);
    break;
  case AP_M68882_OP_FACOS:
    result = ap_m68882_acos(&source, mode, precision);
    break;

  /* §4.3.2's hyperbolic functions, and the last of the nineteen. */
  case AP_M68882_OP_FSINH:
    result = ap_m68882_sinh(&source, mode, precision);
    break;
  case AP_M68882_OP_FCOSH:
    result = ap_m68882_cosh(&source, mode, precision);
    break;
  case AP_M68882_OP_FTANH:
    result = ap_m68882_tanh(&source, mode, precision);
    break;
  case AP_M68882_OP_FATANH:
    result = ap_m68882_atanh(&source, mode, precision);
    break;
  case AP_M68882_OP_FSINCOS: {
    /* Two destinations. Page 4-101: bits 9-7 are "DESTINATION REGISTER, FPs.
     * The sine result is stored in this register", and bits 2-0 are FPc, which
     * takes the cosine -- which is why `$30-$37` are eight encodings of one
     * instruction.
     *
     * The cosine is written here and the sine by the shared tail below, and
     * that order is the specification rather than a convenience: "if FPc and
     * FPs specify the same floating-point data register, the sine result is
     * stored in the register, and the cosine result is discarded". Writing the
     * cosine last would silently invert that. */
    ap_m68882_op_t sine, cosine;
    ap_m68882_sincos(&source, mode, precision, &sine, &cosine);
    fpu->regs.fp[command->extension & 7u] = cosine.value;
    result = sine;
    result.exceptions |= cosine.exceptions;
    break;
  }
  case AP_M68882_OP_FGETEXP:
    result = ap_m68882_getexp(&source);
    break;
  case AP_M68882_OP_FGETMAN:
    result = ap_m68882_getman(&source);
    break;
  case AP_M68882_OP_FINT:
    result = ap_m68882_int(&source, mode);
    break;
  case AP_M68882_OP_FINTRZ:
    result = ap_m68882_intrz(&source);
    break;
  case AP_M68882_OP_FSGLMUL:
    /* The rounding *precision* is ignored and single is used regardless -- but
     * the rounding *mode* still comes from the FPCR, which is why the mode is
     * passed and the precision is not. */
    result = ap_m68882_single_mul(&destination, &source, mode);
    break;
  case AP_M68882_OP_FSGLDIV:
    result = ap_m68882_single_div(&destination, &source, mode);
    break;

  case AP_M68882_OP_FMOD:
  case AP_M68882_OP_FREM: {
    /* The IEEE remainder and the modulo differ only in how the implied
     * quotient is rounded -- to nearest for `FREM`, to zero for `FMOD` -- and
     * the manual is explicit that this makes `FMOD`'s answer "different from
     * the remainder required by the IEEE Specification". */
    const ap_m68882_remainder_t r = ap_m68882_remainder(
        &destination, &source, command->operation == AP_M68882_OP_FREM);
    result.value = r.value;
    result.exceptions = r.exceptions;
    /* §2.3.2: the quotient byte is "set at the completion of the modulo (FMOD)
     * or IEEE remainder (FREM) instructions", and only those two. It is not
     * cleared at the start of an operation the way the exception byte is --
     * "the quotient bits remain set until they are cleared by the user, or
     * until another FMOD or FREM instruction is executed" -- so it is written
     * here rather than in the shared tail. */
    fpu->regs.fpsr &= ~((UINT32_C(1) << AP_M68882_QUOTIENT_SIGN) |
                        ((uint32_t)AP_M68882_QUOTIENT_MASK
                         << AP_M68882_QUOTIENT_SHIFT));
    fpu->regs.fpsr |= (uint32_t)r.quotient << AP_M68882_QUOTIENT_SHIFT;
    if (r.quotient_sign) {
      fpu->regs.fpsr |= UINT32_C(1) << AP_M68882_QUOTIENT_SIGN;
    }
    break;
  }

  case AP_M68882_OP_FSCALE:
    /* Dyadic: "FPn x INT(2^Source) -> FPn", so the destination is scaled by the
     * source and not the other way round. */
    result = ap_m68882_scale(&destination, &source);
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

  /* What is left is the rounding and remainder forms -- `FMOD`, `FREM`,
   * `FSGLDIV`, `FSGLMUL`. Every transcendental is now computed. Listed
   * individually rather than caught by a `default`, because `-Wswitch-enum` is
   * what will force a decision here when one of them lands -- the same
   * discipline the 68030's step uses for its own families.
   *
   * Reported as unimplemented and **not** as F-line: the hardware executes
   * these, and dressing our gap up as the machine's behaviour would make it
   * invisible. */
    return AP_M68882_UNIMPLEMENTED;
  }

  set_condition_from(&fpu->regs, &result.value);
  apply_exceptions(&fpu->regs, result.exceptions);
  if (writes_destination) {
    fpu->regs.fp[command->ry] = result.value;
  }
  return AP_M68882_EXECUTED;
}

/* Decode and validate a general-type instruction, stopping short of executing
 * it. Shared by every entry point below so that the F-line and unimplemented
 * decisions are made once: an encoding that traps when its operands are in
 * registers must trap identically when one is in memory, and two copies of
 * Table 4-13's footnotes would not stay that way. */
static ap_m68882_status_t decode_general(const ap_m68882_t *fpu,
                                         uint16_t operation_word,
                                         uint16_t command_word,
                                         ap_m68882_command_word_t *out) {
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
     * instruction dialogs. Not implemented here, and not F-line either.
     *
     * For the conditionals the *coprocessor's* half is implemented and reachable
     * as `ap_m68882_condition`: §9's protocol has the MPU write the predicate to
     * the condition CIR and read the answer, and the branching, decrementing,
     * trapping or byte-writing that follows is the 68030's. What is missing is
     * that dialog, not the condition. */
    return AP_M68882_UNIMPLEMENTED;
  }

  const ap_m68882_command_word_t command =
      ap_m68882_decode_command(command_word);
  *out = command;

  /* Table 4-13 tabulates the extension field *as an operation*, which it is
   * only for the two arithmetic opclasses -- `000` and `010`, the forms that
   * name a source and a destination register. Elsewhere those seven bits are a
   * different field entirely: a k-factor for a packed decimal store, a register
   * select for the control registers, a register list for FMOVEM. Reading them
   * against Table 4-13 there would raise F-line on a k-factor that happened to
   * fall in one of the table's gaps -- a trap for an instruction the hardware
   * executes, on a value that is not an opcode at all.
   *
   * FMOVECR is the same exception from inside opclass `010`: RX = 7 makes the
   * extension a ROM offset rather than an operation, which is why the test is
   * not simply on the opclass. */
  const bool extension_is_an_operation =
      command.opclass == AP_M68882_OPCLASS_REGISTER ||
      (command.opclass == AP_M68882_OPCLASS_MEMORY_TO_REGISTER &&
       command.rx != 7u);
  if (!extension_is_an_operation) {
    return AP_M68882_EXECUTED;
  }

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

  return AP_M68882_EXECUTED;
}

/* ---------------------------------------------------------------------------
 * The constant ROM
 *
 * Twenty-two published offsets. The values are computed rather than
 * transcribed, because there is nothing to transcribe -- see the header.
 */
typedef struct {
  unsigned offset;
  ap_m68882_extended_t value;
} rom_entry_t;

static const rom_entry_t constant_rom[] = {
    {0x00u, {false, 0x4000u, UINT64_C(0xC90FDAA22168C235)}}, /* pi */
    {0x0Bu, {false, 0x3FFDu, UINT64_C(0x9A209A84FBCFF799)}}, /* Log10(2) */
    {0x0Cu, {false, 0x4000u, UINT64_C(0xADF85458A2BB4A9B)}}, /* e */
    {0x0Du, {false, 0x3FFFu, UINT64_C(0xB8AA3B295C17F0BC)}}, /* Log2(e) */
    {0x0Eu, {false, 0x3FFDu, UINT64_C(0xDE5BD8A937287195)}}, /* Log10(e) */
    {0x0Fu, {false, 0x0000u, UINT64_C(0x0000000000000000)}}, /* 0.0 */
    {0x30u, {false, 0x3FFEu, UINT64_C(0xB17217F7D1CF79AC)}}, /* ln(2) */
    {0x31u, {false, 0x4000u, UINT64_C(0x935D8DDDAAA8AC17)}}, /* ln(10) */
    /* The powers of ten. `10^0` through `10^16` are exact -- 5^16 fits a 64-bit
     * significand with room to spare -- and everything above is rounded. */
    {0x32u, {false, 0x3FFFu, UINT64_C(0x8000000000000000)}}, /* 10^0 */
    {0x33u, {false, 0x4002u, UINT64_C(0xA000000000000000)}}, /* 10^1 */
    {0x34u, {false, 0x4005u, UINT64_C(0xC800000000000000)}}, /* 10^2 */
    {0x35u, {false, 0x400Cu, UINT64_C(0x9C40000000000000)}}, /* 10^4 */
    {0x36u, {false, 0x4019u, UINT64_C(0xBEBC200000000000)}}, /* 10^8 */
    {0x37u, {false, 0x4034u, UINT64_C(0x8E1BC9BF04000000)}}, /* 10^16 */
    {0x38u, {false, 0x4069u, UINT64_C(0x9DC5ADA82B70B59E)}}, /* 10^32 */
    {0x39u, {false, 0x40D3u, UINT64_C(0xC2781F49FFCFA6D5)}}, /* 10^64 */
    {0x3Au, {false, 0x41A8u, UINT64_C(0x93BA47C980E98CE0)}}, /* 10^128 */
    {0x3Bu, {false, 0x4351u, UINT64_C(0xAA7EEBFB9DF9DE8E)}}, /* 10^256 */
    {0x3Cu, {false, 0x46A3u, UINT64_C(0xE319A0AEA60E91C7)}}, /* 10^512 */
    {0x3Du, {false, 0x4D48u, UINT64_C(0xC976758681750C17)}}, /* 10^1024 */
    {0x3Eu, {false, 0x5A92u, UINT64_C(0x9E8B3B5DC53D5DE5)}}, /* 10^2048 */
    {0x3Fu, {false, 0x7525u, UINT64_C(0xC46052028A20979B)}}, /* 10^4096 */
};

bool ap_m68882_constant(unsigned offset, ap_m68882_extended_t *out) {
  for (unsigned i = 0; i < sizeof constant_rom / sizeof constant_rom[0]; i++) {
    if (constant_rom[i].offset == (offset & 0x7Fu)) {
      *out = constant_rom[i].value;
      return true;
    }
  }
  /* Reserved to Motorola and mask-set dependent, so there is no value to be
   * right about. The PRM's convention, stated rather than left to chance. */
  *out = (ap_m68882_extended_t){0};
  return false;
}

/* FMOVECR, which is opclass `010` with RX = 7 and reads no memory at all.
 *
 * "Fetches an extended precision constant from the FPCP on-chip ROM, **rounds
 * it to the precision specified in the FPCR mode control byte**, and stores it
 * in the destination floating-point data register" -- the mirror of the store
 * rule, where the destination format overrides PREC and PREC is ignored. Here
 * there is no destination format, so PREC is the whole of it.
 *
 * The exception byte is emphatic: everything Cleared except "INEX2: Refer to
 * 6.1.7 Inexact Result". So rounding pi to single precision is inexact and
 * nothing else, and even a constant far outside single's *range* raises no
 * overflow -- OVFL is listed as Cleared, so the range is not checked. */
static ap_m68882_status_t execute_move_constant(
    ap_m68882_t *fpu, const ap_m68882_command_word_t *command) {
  ap_m68882_extended_t value = {0};
  (void)ap_m68882_constant(command->extension, &value);

  const ap_m68882_round_result_t rounded = ap_m68882_round(
      value, false, false, false, ap_m68882_rounding_mode(&fpu->regs),
      ap_m68882_rounding_precision(&fpu->regs));

  fpu->regs.fp[command->ry] = rounded.value;
  set_condition_from(&fpu->regs, &rounded.value);
  apply_exceptions(&fpu->regs, rounded.inexact
                                  ? (UINT32_C(1) << AP_M68882_EXC_INEX2)
                                  : 0u);
  return AP_M68882_EXECUTED;
}

ap_m68882_status_t ap_m68882_execute(ap_m68882_t *fpu, uint16_t operation_word,
                                     uint16_t command_word) {
  ap_m68882_command_word_t command = {0};
  const ap_m68882_status_t decoded =
      decode_general(fpu, operation_word, command_word, &command);
  if (decoded != AP_M68882_EXECUTED) {
    return decoded;
  }

  if (command.opclass == AP_M68882_OPCLASS_MEMORY_TO_REGISTER &&
      command.rx == 7u) {
    /* FMOVECR. It lives in the memory-to-register opclass and touches no
     * memory, which is why it arrives here rather than through a transfer. */
    return execute_move_constant(fpu, &command);
  }

  if (command.opclass != AP_M68882_OPCLASS_REGISTER) {
    /* This entry point is the register-to-register one. Every other opclass
     * needs an operand the main processor has to move, so it belongs to
     * `ap_m68882_source_transfer` and its caller -- answering it here would
     * execute against whatever `rx` happened to select. */
    return AP_M68882_UNIMPLEMENTED;
  }

  /* Opclass `000`: RX is a register number, and the source is that register. */
  const ap_m68882_extended_t source = fpu->regs.fp[command.rx];
  return execute_general(fpu, &command, &source);
}

ap_m68882_status_t ap_m68882_source_transfer(const ap_m68882_t *fpu,
                                             uint16_t operation_word,
                                             uint16_t command_word,
                                             bool *needs_source,
                                             ap_m68882_format_t *format) {
  *needs_source = false;

  ap_m68882_command_word_t command = {0};
  const ap_m68882_status_t decoded =
      decode_general(fpu, operation_word, command_word, &command);
  if (decoded != AP_M68882_EXECUTED) {
    return decoded;
  }

  if (command.opclass != AP_M68882_OPCLASS_MEMORY_TO_REGISTER ||
      !ap_m68882_command_uses_memory(&command)) {
    /* Not a source fetch: the store direction, the control registers, FMOVEM,
     * register-to-register -- and FMOVECR, which is opclass `010` with RX = 7
     * and reads the part's own ROM rather than memory.
     *
     * Reported as "nothing to fetch" and **not** as unimplemented, which is a
     * distinction the caller depends on: it goes on to ask about the store
     * direction and then about register-to-register, and each of those answers
     * for itself. Declining here would make this call the one that decides what
     * the other two can do. */
    return AP_M68882_EXECUTED;
  }

  /* §4.8.4: "If R/M = 1, it specifies the source operand data format." */
  *format = (ap_m68882_format_t)command.rx;
  *needs_source = true;
  return AP_M68882_EXECUTED;
}

ap_m68882_status_t ap_m68882_execute_source(
    ap_m68882_t *fpu, uint16_t operation_word, uint16_t command_word,
    const ap_m68882_extended_t *source) {
  ap_m68882_command_word_t command = {0};
  const ap_m68882_status_t decoded =
      decode_general(fpu, operation_word, command_word, &command);
  if (decoded != AP_M68882_EXECUTED) {
    return decoded;
  }
  if (command.opclass != AP_M68882_OPCLASS_MEMORY_TO_REGISTER) {
    return AP_M68882_UNIMPLEMENTED;
  }
  return execute_general(fpu, &command, source);
}

void ap_m68882_note_instruction(ap_m68882_t *fpu, uint16_t operation_word,
                                uint16_t command_word, uint32_t address) {
  ap_m68882_command_word_t command = {0};
  if (decode_general(fpu, operation_word, command_word, &command) !=
      AP_M68882_EXECUTED) {
    return;
  }

  switch (command.opclass) {
  case AP_M68882_OPCLASS_REGISTER:
  case AP_M68882_OPCLASS_MEMORY_TO_REGISTER:
  case AP_M68882_OPCLASS_REGISTER_TO_MEMORY:
    /* The arithmetic opclasses, which includes the store: narrowing to a
     * destination format raises `OPERR`, `OVFL`, `UNFL` and `INEX2`, so a store
     * is one of the instructions a handler may have to locate. */
    break;
  case AP_M68882_OPCLASS_RESERVED_1:
  case AP_M68882_OPCLASS_MOVE_TO_CONTROL:
  case AP_M68882_OPCLASS_MOVE_FROM_CONTROL:
  case AP_M68882_OPCLASS_MOVEM_TO_REGISTERS:
  case AP_M68882_OPCLASS_MOVEM_FROM_REGISTERS:
    /* "these instructions do not modify the FPIAR" -- which is what lets a trap
     * handler read it. */
    return;
  }

  /* "unless all arithmetic exceptions are disabled". The enable byte is FPCR
   * bits 15-8; `BSUN` at bit 15 is excluded because it is not an arithmetic
   * exception. See the header. */
  const uint32_t arithmetic_enables =
      fpu->regs.fpcr & ((UINT32_C(1) << AP_M68882_EXC_SNAN) |
                        (UINT32_C(1) << AP_M68882_EXC_OPERR) |
                        (UINT32_C(1) << AP_M68882_EXC_OVFL) |
                        (UINT32_C(1) << AP_M68882_EXC_UNFL) |
                        (UINT32_C(1) << AP_M68882_EXC_DZ) |
                        (UINT32_C(1) << AP_M68882_EXC_INEX2) |
                        (UINT32_C(1) << AP_M68882_EXC_INEX1));
  if (arithmetic_enables == 0u) {
    return;
  }
  fpu->regs.fpiar = address;
}

ap_m68882_status_t ap_m68882_control_transfer(const ap_m68882_t *fpu,
                                              uint16_t operation_word,
                                              uint16_t command_word,
                                              bool *is_control,
                                              ap_m68882_control_t *control) {
  *is_control = false;

  ap_m68882_command_word_t command = {0};
  const ap_m68882_status_t decoded =
      decode_general(fpu, operation_word, command_word, &command);
  if (decoded != AP_M68882_EXECUTED) {
    return decoded;
  }

  if (command.opclass != AP_M68882_OPCLASS_MOVE_TO_CONTROL &&
      command.opclass != AP_M68882_OPCLASS_MOVE_FROM_CONTROL) {
    return AP_M68882_EXECUTED; /* not this instruction */
  }

  control->to_memory = command.opclass == AP_M68882_OPCLASS_MOVE_FROM_CONTROL;
  control->select = (unsigned)(command_word & 0x1C00u);
  *is_control = true;
  return AP_M68882_EXECUTED;
}

unsigned ap_m68882_control_count(unsigned select) {
  unsigned count = 0;
  for (unsigned bit = AP_M68882_CONTROL_FPIAR; bit <= AP_M68882_CONTROL_FPCR;
       bit++) {
    if ((select & (1u << bit)) != 0u) {
      count++;
    }
  }
  return count;
}

uint32_t ap_m68882_control_read(const ap_m68882_t *fpu, unsigned bit) {
  switch (bit) {
  case AP_M68882_CONTROL_FPCR:
    return fpu->regs.fpcr & AP_M68882_FPCR_IMPLEMENTED;
  case AP_M68882_CONTROL_FPSR:
    return fpu->regs.fpsr & AP_M68882_FPSR_IMPLEMENTED;
  case AP_M68882_CONTROL_FPIAR:
    return fpu->regs.fpiar;
  default:
    break;
  }
  return 0u;
}

void ap_m68882_control_write(ap_m68882_t *fpu, unsigned bit, uint32_t value) {
  switch (bit) {
  case AP_M68882_CONTROL_FPCR:
    /* Masked on the way in as well as out: "ignored during writes". A model
     * that stored the whole word would hand the extra bits back on the next
     * read and contradict "read as zeros". */
    fpu->regs.fpcr = value & AP_M68882_FPCR_IMPLEMENTED;
    break;
  case AP_M68882_CONTROL_FPSR:
    /* "Changed only if the destination is the FPSR; in which case **all bits
     * are modified** to reflect the value of the source operand." So this is a
     * replacement and not a merge -- the condition codes included, which is the
     * one way a control move touches them. */
    fpu->regs.fpsr = value & AP_M68882_FPSR_IMPLEMENTED;
    break;
  case AP_M68882_CONTROL_FPIAR:
    fpu->regs.fpiar = value;
    break;
  default:
    break;
  }
  /* No `apply_exceptions`. "a write to the FPCR exception enable byte or the
   * FPSR exception status byte cannot generate a new exception, regardless of
   * the value written" -- so enabling a trap whose exception bit is already set
   * does not fire it here. */
}

ap_m68882_status_t ap_m68882_movem_transfer(const ap_m68882_t *fpu,
                                            uint16_t operation_word,
                                            uint16_t command_word,
                                            bool *is_movem,
                                            ap_m68882_movem_t *movem) {
  *is_movem = false;

  ap_m68882_command_word_t command = {0};
  const ap_m68882_status_t decoded =
      decode_general(fpu, operation_word, command_word, &command);
  if (decoded != AP_M68882_EXECUTED) {
    return decoded;
  }

  if (command.opclass != AP_M68882_OPCLASS_MOVEM_TO_REGISTERS &&
      command.opclass != AP_M68882_OPCLASS_MOVEM_FROM_REGISTERS) {
    return AP_M68882_EXECUTED; /* not this instruction; some other path is */
  }

  *movem = ap_m68882_decode_movem(command_word);
  *is_movem = true;
  return AP_M68882_EXECUTED;
}

void ap_m68882_movem_read(const ap_m68882_t *fpu, unsigned reg,
                          uint8_t *bytes) {
  uint32_t high = 0;
  uint64_t mantissa = 0;
  ap_m68882_to_extended(&fpu->regs.fp[reg & 7u], &high, &mantissa);
  for (unsigned i = 0; i < 4u; i++) {
    bytes[i] = (uint8_t)(high >> (8u * (3u - i)));
  }
  for (unsigned i = 0; i < 8u; i++) {
    bytes[4u + i] = (uint8_t)(mantissa >> (8u * (7u - i)));
  }
}

void ap_m68882_movem_write(ap_m68882_t *fpu, unsigned reg,
                           const uint8_t *bytes) {
  uint32_t high = 0;
  uint64_t mantissa = 0;
  for (unsigned i = 0; i < 4u; i++) {
    high = (high << 8) | bytes[i];
  }
  for (unsigned i = 0; i < 8u; i++) {
    mantissa = (mantissa << 8) | bytes[4u + i];
  }
  /* Straight into the register file. Not through `apply_exceptions` and not
   * through `set_condition_from`: "the FPSR is not affected by the
   * instruction", which is what makes FMOVEM the only way to move a signalling
   * NAN without turning it into a quiet one. */
  fpu->regs.fp[reg & 7u] = ap_m68882_from_extended(high, mantissa);
}

ap_m68882_status_t ap_m68882_destination_transfer(const ap_m68882_t *fpu,
                                                  uint16_t operation_word,
                                                  uint16_t command_word,
                                                  bool *needs_store,
                                                  ap_m68882_format_t *format) {
  *needs_store = false;

  ap_m68882_command_word_t command = {0};
  const ap_m68882_status_t decoded =
      decode_general(fpu, operation_word, command_word, &command);
  if (decoded != AP_M68882_EXECUTED) {
    return decoded;
  }

  if (command.opclass != AP_M68882_OPCLASS_REGISTER_TO_MEMORY) {
    return AP_M68882_EXECUTED; /* nothing to store; some other path applies */
  }

  /* Bits 12-10 are the DESTINATION FORMAT here, where in opclass `010` the same
   * field is the source format. Same encoding, opposite direction. */
  *format = (ap_m68882_format_t)command.rx;
  if (*format == AP_M68882_FORMAT_PACKED ||
      *format == AP_M68882_FORMAT_PACKED_DYNAMIC) {
    /* The k-factor in the extension field is the other half of what packed
     * decimal needs, and neither half is implemented. */
    return AP_M68882_UNIMPLEMENTED;
  }
  *needs_store = true;
  return AP_M68882_EXECUTED;
}

ap_m68882_status_t ap_m68882_execute_store(ap_m68882_t *fpu,
                                           uint16_t operation_word,
                                           uint16_t command_word,
                                           ap_m68882_store_t *out) {
  ap_m68882_command_word_t command = {0};
  const ap_m68882_status_t decoded =
      decode_general(fpu, operation_word, command_word, &command);
  if (decoded != AP_M68882_EXECUTED) {
    return decoded;
  }
  if (command.opclass != AP_M68882_OPCLASS_REGISTER_TO_MEMORY) {
    return AP_M68882_UNIMPLEMENTED;
  }

  /* Bits 9-7 are the SOURCE REGISTER, which is `ry` -- the same field that
   * names the *destination* everywhere else. Reading it as a destination here
   * would store whichever register the instruction was writing towards. */
  const ap_m68882_extended_t *source = &fpu->regs.fp[command.ry];

  if (!ap_m68882_store_encode((ap_m68882_format_t)command.rx, source,
                              ap_m68882_rounding_mode(&fpu->regs), out)) {
    return AP_M68882_UNIMPLEMENTED;
  }

  /* The conversion's exceptions are accrued; the condition codes are not
   * touched. "Condition Codes: Not affected", "Quotient Byte: Not affected" --
   * which is why this does not go through the common result path. */
  apply_exceptions(&fpu->regs, out->exceptions);
  return AP_M68882_EXECUTED;
}

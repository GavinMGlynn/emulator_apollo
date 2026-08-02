/* MC68882 §4.3.2's classification. Semantics in the header. */

#include "cpu/m68882/ap_m68882_accuracy.h"

bool ap_m68882_is_transcendental(ap_m68882_operation_t operation) {
  /* Every enumerator is listed and there is no `default`, so adding an
   * operation to the enum fails the build here rather than defaulting to "not
   * transcendental" -- which is the answer that would silently let an
   * approximated function pass as an exact one. */
  switch (operation) {
  /* Trigonometric. */
  case AP_M68882_OP_FSIN:
  case AP_M68882_OP_FCOS:
  case AP_M68882_OP_FTAN:
  case AP_M68882_OP_FASIN:
  case AP_M68882_OP_FACOS:
  case AP_M68882_OP_FATAN:
  case AP_M68882_OP_FSINCOS:
  /* Hyperbolic. */
  case AP_M68882_OP_FSINH:
  case AP_M68882_OP_FCOSH:
  case AP_M68882_OP_FTANH:
  case AP_M68882_OP_FATANH:
  /* Logarithmic. `FLOGNP1` is log(1+x), a separate instruction because the
   * accuracy near zero is what it exists for -- and transcendental all the
   * same. */
  case AP_M68882_OP_FLOGN:
  case AP_M68882_OP_FLOGNP1:
  case AP_M68882_OP_FLOG10:
  case AP_M68882_OP_FLOG2:
  /* Exponential. `FETOXM1` is e^x - 1, the mirror of `FLOGNP1`. */
  case AP_M68882_OP_FETOX:
  case AP_M68882_OP_FETOXM1:
  case AP_M68882_OP_FTWOTOX:
  case AP_M68882_OP_FTENTOX:
    return true;

  /* The IEEE-specified monadics. `FSQRT` is excluded by §4.3.2's own
   * parenthesis; the rest have one right answer and are not even approximate. */
  case AP_M68882_OP_FSQRT:
  case AP_M68882_OP_FGETEXP:
  case AP_M68882_OP_FGETMAN:
  case AP_M68882_OP_FINT:
  case AP_M68882_OP_FINTRZ:
  case AP_M68882_OP_FSCALE:
  case AP_M68882_OP_FABS:
  case AP_M68882_OP_FNEG:
  case AP_M68882_OP_FMOVE_TO_FPN:
  /* The arithmetic, which §4.3.1 holds to the IEEE bound. */
  case AP_M68882_OP_FADD:
  case AP_M68882_OP_FSUB:
  case AP_M68882_OP_FMUL:
  case AP_M68882_OP_FDIV:
  case AP_M68882_OP_FSGLMUL:
  case AP_M68882_OP_FSGLDIV:
  case AP_M68882_OP_FREM:
  case AP_M68882_OP_FMOD:
  case AP_M68882_OP_FCMP:
  case AP_M68882_OP_FTST:
    break;
  }
  /* Also the encodings Table 4-13 leaves undefined, which reach here as
   * out-of-enum values and are not transcendental either. */
  return false;
}

unsigned ap_m68882_transcendental_count(void) { return 19u; }

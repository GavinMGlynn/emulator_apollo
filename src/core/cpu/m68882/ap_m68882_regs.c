/* MC68882 programming model. See ap_m68882_regs.h for the figures each field
 * is transcribed from and for why the two exception bytes share a layout. */

#include "cpu/m68882/ap_m68882_regs.h"

/* A non-signalling NAN in extended precision: exponent all ones, and a mantissa
 * whose most significant fraction bit is set. The integer bit is explicit in
 * this format, so a NAN has it set too. */
static const ap_m68882_extended_t NON_SIGNALLING_NAN = {
    .sign = false,
    .exponent = 0x7FFFu,
    .mantissa = UINT64_C(0xFFFFFFFFFFFFFFFF),
};

void ap_m68882_regs_reset(ap_m68882_regs_t *regs) {
  for (unsigned i = 0; i < AP_M68882_DATA_REGISTERS; i++) {
    /* Not zero. A zeroed register reads as +0, which is a perfectly good
     * operand -- so an uninitialised program would appear to work and produce
     * plausible answers. A NAN propagates instead, which is what the part
     * leaves and what makes the mistake visible at the first use. */
    regs->fp[i] = NON_SIGNALLING_NAN;
  }
  regs->fpcr = 0u;
  regs->fpsr = 0u;
  regs->fpiar = 0u;
}

ap_m68882_rounding_t ap_m68882_rounding_mode(const ap_m68882_regs_t *regs) {
  return (ap_m68882_rounding_t)((regs->fpcr >> 4) & 3u);
}

ap_m68882_precision_t
ap_m68882_rounding_precision(const ap_m68882_regs_t *regs) {
  return (ap_m68882_precision_t)((regs->fpcr >> 6) & 3u);
}

bool ap_m68882_exception_enabled(const ap_m68882_regs_t *regs,
                                 unsigned exception_bit) {
  /* One AND of the two registers at the same bit position, which is the whole
   * reason the manual puts the enable and status bytes in the same places. */
  return ((regs->fpcr >> exception_bit) & 1u) != 0u &&
         ((regs->fpsr >> exception_bit) & 1u) != 0u;
}

bool ap_m68882_inexact_trap(const ap_m68882_regs_t *regs) {
  const uint32_t exc = regs->fpsr;
  const uint32_t enable = regs->fpcr;
  const bool overflow = ((exc >> AP_M68882_EXC_OVFL) & 1u) != 0u;
  const bool inexact2 = ((exc >> AP_M68882_EXC_INEX2) & 1u) != 0u;
  const bool inexact1 = ((exc >> AP_M68882_EXC_INEX1) & 1u) != 0u;
  const bool enable2 = ((enable >> AP_M68882_EXC_INEX2) & 1u) != 0u;
  const bool enable1 = ((enable >> AP_M68882_EXC_INEX1) & 1u) != 0u;
  /* §6.1.10, transcribed: the overflow term is the part that a plain
   * bit-against-bit test would lose. */
  return ((overflow || inexact2) && enable2) || (inexact1 && enable1);
}

void ap_m68882_set_condition(ap_m68882_regs_t *regs, ap_m68882_result_t kind,
                             bool negative) {
  uint32_t fpsr = regs->fpsr;
  fpsr &= ~((UINT32_C(1) << AP_M68882_FPCC_N) |
            (UINT32_C(1) << AP_M68882_FPCC_Z) |
            (UINT32_C(1) << AP_M68882_FPCC_I) |
            (UINT32_C(1) << AP_M68882_FPCC_NAN));

  /* Table 2-1. The sign bit is set for every negative row including -0 and
   * -NAN, which is why it is a separate argument rather than being folded into
   * the kind: a zero still has a sign on this part. */
  if (negative) {
    fpsr |= UINT32_C(1) << AP_M68882_FPCC_N;
  }
  switch (kind) {
  case AP_M68882_RESULT_NORMAL:
    break;
  case AP_M68882_RESULT_ZERO:
    fpsr |= UINT32_C(1) << AP_M68882_FPCC_Z;
    break;
  case AP_M68882_RESULT_INFINITY:
    fpsr |= UINT32_C(1) << AP_M68882_FPCC_I;
    break;
  case AP_M68882_RESULT_NAN:
    fpsr |= UINT32_C(1) << AP_M68882_FPCC_NAN;
    break;
  }
  regs->fpsr = fpsr;
}

static bool condition_bit(const ap_m68882_regs_t *regs, unsigned bit) {
  return ((regs->fpsr >> bit) & 1u) != 0u;
}

bool ap_m68882_condition_equal(const ap_m68882_regs_t *regs) {
  return condition_bit(regs, AP_M68882_FPCC_Z);
}

bool ap_m68882_condition_greater(const ap_m68882_regs_t *regs) {
  return !(condition_bit(regs, AP_M68882_FPCC_NAN) ||
           condition_bit(regs, AP_M68882_FPCC_Z) ||
           condition_bit(regs, AP_M68882_FPCC_N));
}

bool ap_m68882_condition_less(const ap_m68882_regs_t *regs) {
  return condition_bit(regs, AP_M68882_FPCC_N) &&
         !(condition_bit(regs, AP_M68882_FPCC_NAN) ||
           condition_bit(regs, AP_M68882_FPCC_Z));
}

bool ap_m68882_condition_unordered(const ap_m68882_regs_t *regs) {
  return condition_bit(regs, AP_M68882_FPCC_NAN);
}

void ap_m68882_raise_exception(ap_m68882_regs_t *regs, unsigned exception_bit) {
  /* "When a floating-point exception is detected by the FPCP, the corresponding
   * bit in the EXC byte is set, **even if the trap for that exception class is
   * disabled**." The enable byte decides whether a trap is taken, never whether
   * the bit is recorded. */
  regs->fpsr |= UINT32_C(1) << exception_bit;
}

void ap_m68882_accrue(ap_m68882_regs_t *regs) {
  const uint32_t exc = regs->fpsr;
  const bool bsun = ((exc >> AP_M68882_EXC_BSUN) & 1u) != 0u;
  const bool snan = ((exc >> AP_M68882_EXC_SNAN) & 1u) != 0u;
  const bool operr = ((exc >> AP_M68882_EXC_OPERR) & 1u) != 0u;
  const bool ovfl = ((exc >> AP_M68882_EXC_OVFL) & 1u) != 0u;
  const bool unfl = ((exc >> AP_M68882_EXC_UNFL) & 1u) != 0u;
  const bool dz = ((exc >> AP_M68882_EXC_DZ) & 1u) != 0u;
  const bool inex2 = ((exc >> AP_M68882_EXC_INEX2) & 1u) != 0u;
  const bool inex1 = ((exc >> AP_M68882_EXC_INEX1) & 1u) != 0u;

  uint32_t accrued = regs->fpsr;

  /* "AEXC(IOP) = AEXC(IOP) v EXC(BSUN v SNAN v OPERR)" -- three exception
   * classes accruing into one bit, which is what "invalid operation" means to
   * the IEEE standard. */
  if (bsun || snan || operr) {
    accrued |= UINT32_C(1) << AP_M68882_AEXC_IOP;
  }
  if (ovfl) {
    accrued |= UINT32_C(1) << AP_M68882_AEXC_OVFL;
  }
  /* "AEXC(UNFL) = AEXC(UNFL) v EXC(UNFL ^ INEX2)" -- an **AND**, where every
   * other equation is an OR. An underflow that was exact does not accrue, and
   * writing this as the obvious OR produces a sticky bit that is set far too
   * often and never faults. */
  if (unfl && inex2) {
    accrued |= UINT32_C(1) << AP_M68882_AEXC_UNFL;
  }
  if (dz) {
    accrued |= UINT32_C(1) << AP_M68882_AEXC_DZ;
  }
  /* "AEXC(INEX) = AEXC(INEX) v EXC(INEX1 v INEX2 v OVFL)" -- an overflow
   * accrues inexactness as well as overflow, since an overflowed result is by
   * definition not the exact one. */
  if (inex1 || inex2 || ovfl) {
    accrued |= UINT32_C(1) << AP_M68882_AEXC_INEX;
  }

  /* Accumulated, never cleared here: "the AEXC byte contains the history of all
   * floating-point exceptions that have occurred since the user last cleared
   * the AEXC byte ... cleared by the FPCP only by a reset or a restore
   * operation of the null state". */
  regs->fpsr = accrued;
}

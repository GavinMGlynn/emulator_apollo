/* MC68030 conditional tests. See ap_m68030_cond.h for why the lost overbars are
 * recovered from the encoding's pairwise structure rather than guessed. */

#include "cpu/m68030/ap_m68030_cond.h"

#include "cpu/m68030/ap_m68030_regs.h"

bool ap_m68030_condition(ap_m68030_cond_t condition, uint16_t ccr) {
  const bool n = ((ccr >> AP_M68030_SR_N_BIT) & 1u) != 0u;
  const bool z = ((ccr >> AP_M68030_SR_Z_BIT) & 1u) != 0u;
  const bool v = ((ccr >> AP_M68030_SR_V_BIT) & 1u) != 0u;
  const bool c = ((ccr >> AP_M68030_SR_C_BIT) & 1u) != 0u;

  switch (condition) {
  case AP_M68030_COND_T:
    return true;
  case AP_M68030_COND_F:
    return false;
  case AP_M68030_COND_HI:
    return !c && !z;
  case AP_M68030_COND_LS:
    return c || z;
  case AP_M68030_COND_CC:
    return !c;
  case AP_M68030_COND_CS:
    return c;
  case AP_M68030_COND_NE:
    return !z;
  case AP_M68030_COND_EQ:
    return z;
  case AP_M68030_COND_VC:
    return !v;
  case AP_M68030_COND_VS:
    return v;
  case AP_M68030_COND_PL:
    return !n;
  case AP_M68030_COND_MI:
    return n;
  /* "N^V V N^V" with the bars restored: (N and V) or (not-N and not-V), which
   * is N equal to V. */
  case AP_M68030_COND_GE:
    return n == v;
  case AP_M68030_COND_LT:
    return n != v;
  /* GE with Z additionally clear, and its complement. */
  case AP_M68030_COND_GT:
    return (n == v) && !z;
  case AP_M68030_COND_LE:
    return z || (n != v);
  }
  return false;
}

bool ap_m68030_condition_available_to_bcc(ap_m68030_cond_t condition) {
  /* Table 3-19 marks T and F with "*Not available for the Bcc instruction":
   * those two encodings are BRA and BSR. */
  return condition != AP_M68030_COND_T && condition != AP_M68030_COND_F;
}

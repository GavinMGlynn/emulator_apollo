/* MC68030 integer ALU. See ap_m68030_alu.h for why these formulas are verified
 * exhaustively rather than transcribed from Table 3-18. */

#include "cpu/m68030/ap_m68030_alu.h"

#include "cpu/m68030/ap_m68030_regs.h"

static uint32_t width_mask(unsigned size) {
  switch (size) {
  case 1:
    return 0xFFu;
  case 2:
    return 0xFFFFu;
  default:
    return 0xFFFFFFFFu;
  }
}

static uint32_t sign_bit(unsigned size) {
  switch (size) {
  case 1:
    return 0x80u;
  case 2:
    return 0x8000u;
  default:
    return 0x80000000u;
  }
}

static void set_nz(ap_m68030_alu_result_t *out, unsigned size) {
  out->n = (out->result & sign_bit(size)) != 0u;
  out->z = out->result == 0u;
}

ap_m68030_alu_result_t ap_m68030_alu_add(uint32_t destination, uint32_t source,
                                         unsigned size) {
  const uint32_t mask = width_mask(size);
  const uint32_t d = destination & mask;
  const uint32_t s = source & mask;
  const uint64_t wide = (uint64_t)d + (uint64_t)s;

  ap_m68030_alu_result_t out = {0};
  out.result = (uint32_t)(wide & mask);
  set_nz(&out, size);

  /* Carry out of the operand's most significant bit. */
  out.c = (wide & ~(uint64_t)mask) != 0u;
  /* Overflow: the operands agreed in sign and the result did not. */
  out.v = ((~(d ^ s) & (d ^ out.result)) & sign_bit(size)) != 0u;
  out.x = out.c;
  out.sets_x = true;
  return out;
}

ap_m68030_alu_result_t ap_m68030_alu_sub(uint32_t destination, uint32_t source,
                                         unsigned size) {
  const uint32_t mask = width_mask(size);
  const uint32_t d = destination & mask;
  const uint32_t s = source & mask;

  ap_m68030_alu_result_t out = {0};
  /* "Destination - Source" -- in that order. */
  out.result = (d - s) & mask;
  set_nz(&out, size);

  /* Borrow. */
  out.c = d < s;
  /* Overflow: the operands differed in sign and the result took the source's. */
  out.v = (((d ^ s) & (d ^ out.result)) & sign_bit(size)) != 0u;
  out.x = out.c;
  out.sets_x = true;
  return out;
}

ap_m68030_alu_result_t ap_m68030_alu_cmp(uint32_t destination, uint32_t source,
                                         unsigned size) {
  ap_m68030_alu_result_t out = ap_m68030_alu_sub(destination, source, size);
  /* Table 3-18 gives CMP an em dash under X: the subtraction is identical, and
   * only the effect on X differs. */
  out.sets_x = false;
  return out;
}

static ap_m68030_alu_result_t logic(uint32_t value, unsigned size) {
  ap_m68030_alu_result_t out = {0};
  out.result = value & width_mask(size);
  set_nz(&out, size);
  /* "V 0, C 0", and X is not affected. */
  out.v = false;
  out.c = false;
  out.sets_x = false;
  return out;
}

ap_m68030_alu_result_t ap_m68030_alu_and(uint32_t destination, uint32_t source,
                                         unsigned size) {
  return logic(destination & source, size);
}

ap_m68030_alu_result_t ap_m68030_alu_or(uint32_t destination, uint32_t source,
                                        unsigned size) {
  return logic(destination | source, size);
}

ap_m68030_alu_result_t ap_m68030_alu_eor(uint32_t destination, uint32_t source,
                                         unsigned size) {
  return logic(destination ^ source, size);
}

ap_m68030_alu_result_t ap_m68030_alu_neg(uint32_t destination, unsigned size) {
  /* Subtracting from zero, rather than a separate formula that could drift
   * from the subtraction's. */
  return ap_m68030_alu_sub(0u, destination, size);
}

ap_m68030_alu_result_t ap_m68030_alu_not(uint32_t destination, unsigned size) {
  return logic(~destination, size);
}

ap_m68030_alu_result_t ap_m68030_alu_test(uint32_t value, unsigned size) {
  return logic(value, size);
}

uint16_t ap_m68030_alu_apply(uint16_t ccr, const ap_m68030_alu_result_t *r) {
  uint16_t out = 0;
  if (r->sets_x ? r->x : ((ccr >> AP_M68030_SR_X_BIT) & 1u) != 0u) {
    out |= (uint16_t)(1u << AP_M68030_SR_X_BIT);
  }
  if (r->n) {
    out |= (uint16_t)(1u << AP_M68030_SR_N_BIT);
  }
  if (r->z) {
    out |= (uint16_t)(1u << AP_M68030_SR_Z_BIT);
  }
  if (r->v) {
    out |= (uint16_t)(1u << AP_M68030_SR_V_BIT);
  }
  if (r->c) {
    out |= (uint16_t)(1u << AP_M68030_SR_C_BIT);
  }
  return out;
}

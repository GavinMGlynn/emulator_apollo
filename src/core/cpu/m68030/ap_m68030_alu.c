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

ap_m68030_alu_result_t ap_m68030_alu_addx(uint32_t destination,
                                          uint32_t source, unsigned size,
                                          bool x_in, bool z_in) {
  const uint32_t mask = width_mask(size);
  const uint32_t d = destination & mask;
  const uint32_t s = source & mask;
  const uint64_t wide = (uint64_t)d + (uint64_t)s + (x_in ? 1u : 0u);

  ap_m68030_alu_result_t out = {0};
  out.result = (uint32_t)(wide & mask);
  out.n = (out.result & sign_bit(size)) != 0u;
  /* "Cleared if the result is nonzero; unchanged otherwise" -- never set. */
  out.z = z_in && (out.result == 0u);
  out.c = (wide & ~(uint64_t)mask) != 0u;
  out.v = ((~(d ^ s) & (d ^ out.result)) & sign_bit(size)) != 0u;
  out.x = out.c;
  out.sets_x = true;
  return out;
}

ap_m68030_alu_result_t ap_m68030_alu_subx(uint32_t destination,
                                          uint32_t source, unsigned size,
                                          bool x_in, bool z_in) {
  const uint32_t mask = width_mask(size);
  const uint32_t d = destination & mask;
  const uint32_t s = source & mask;
  const uint64_t borrow = (uint64_t)s + (x_in ? 1u : 0u);

  ap_m68030_alu_result_t out = {0};
  out.result = (uint32_t)((d - (uint32_t)borrow) & mask);
  out.n = (out.result & sign_bit(size)) != 0u;
  out.z = z_in && (out.result == 0u);
  out.c = (uint64_t)d < borrow;
  out.v = (((d ^ s) & (d ^ out.result)) & sign_bit(size)) != 0u;
  out.x = out.c;
  out.sets_x = true;
  return out;
}

ap_m68030_alu_result_t ap_m68030_alu_shift(ap_m68030_shift_type_t type,
                                           bool left, uint32_t value,
                                           unsigned count, unsigned size,
                                           bool x_in) {
  const uint32_t mask = width_mask(size);
  const unsigned width = size * 8u;
  uint32_t v = value & mask;

  ap_m68030_alu_result_t out = {0};
  out.sets_x = false; /* raised below only where the table shows an asterisk */

  if (count == 0u) {
    /* Not a no-op: X is left alone and V and C are cleared -- except for the
     * rotate-with-extend forms, where the table gives "C ?  X=C". */
    out.result = v;
    set_nz(&out, size);
    out.v = false;
    out.c = (type == AP_M68030_SHIFT_ROTATE_EXTEND) ? x_in : false;
    return out;
  }

  bool carry = false;
  bool x = x_in;
  bool msb_changed = false;
  const uint32_t sign = sign_bit(size);
  const bool original_sign = (v & sign) != 0u;

  for (unsigned i = 0; i < count; i++) {
    if (left) {
      carry = (v & sign) != 0u;
      v = (v << 1) & mask;
      switch (type) {
      case AP_M68030_SHIFT_ROTATE:
        /* ROL brings the bit that left round to the bottom. */
        v |= carry ? 1u : 0u;
        break;
      case AP_M68030_SHIFT_ROTATE_EXTEND:
        /* ROXL rotates *through* X: the old X enters at the bottom and the bit
         * that left becomes the new X. */
        v |= x ? 1u : 0u;
        x = carry;
        break;
      case AP_M68030_SHIFT_ARITHMETIC:
      case AP_M68030_SHIFT_LOGICAL:
        x = carry;
        break;
      }
      if (((v & sign) != 0u) != original_sign) {
        msb_changed = true;
      }
    } else {
      carry = (v & 1u) != 0u;
      const bool keep_sign = (v & sign) != 0u;
      v = (v >> 1) & mask;
      switch (type) {
      case AP_M68030_SHIFT_ARITHMETIC:
        /* ASR replicates the sign rather than shifting in zero. */
        if (keep_sign) {
          v |= sign;
        }
        x = carry;
        break;
      case AP_M68030_SHIFT_LOGICAL:
        x = carry;
        break;
      case AP_M68030_SHIFT_ROTATE:
        v |= carry ? (sign) : 0u;
        break;
      case AP_M68030_SHIFT_ROTATE_EXTEND:
        v |= x ? sign : 0u;
        x = carry;
        break;
      }
    }
  }
  (void)width;

  out.result = v;
  set_nz(&out, size);
  out.c = carry;

  /* "V is set if the most significant bit is changed at any time during the
   * shift operation" -- and only for the arithmetic left shift. Every other
   * entry in the table has a plain zero there. */
  out.v = (type == AP_M68030_SHIFT_ARITHMETIC && left) ? msb_changed : false;

  /* X is affected by the shifts and by the extend rotates, and left alone by
   * ROL and ROR -- the em dash against the asterisk. */
  if (type != AP_M68030_SHIFT_ROTATE) {
    out.sets_x = true;
    out.x = x;
  }
  return out;
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

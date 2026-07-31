/* MC68030 operand access. See ap_m68030_operand.h for the two register write
 * rules and why confusing them fails silently. */

#include "cpu/m68030/ap_m68030_operand.h"

uint32_t ap_m68030_sign_extend(uint32_t value, unsigned size) {
  switch (size) {
  case 1:
    return (uint32_t)(int32_t)(int8_t)(uint8_t)(value & 0xFFu);
  case 2:
    return (uint32_t)(int32_t)(int16_t)(uint16_t)(value & 0xFFFFu);
  default:
    return value;
  }
}

static uint32_t size_mask(unsigned size) {
  switch (size) {
  case 1:
    return 0xFFu;
  case 2:
    return 0xFFFFu;
  default:
    return 0xFFFFFFFFu;
  }
}

ap_m68030_operand_result_t
ap_m68030_operand_read(ap_m68030_regs_t *regs, ap_m68030_access_ctx_t *access,
                       const ap_m68030_address_t *where, unsigned size,
                       uint8_t function_code) {
  ap_m68030_operand_result_t out = {0};

  if (!where->valid || where->indirection_pending) {
    out.fault = true;
    return out;
  }

  if (where->in_register) {
    const uint32_t whole =
        where->address_register
            ? ap_m68030_read_address_register(regs, where->reg)
            : regs->d[where->reg];
    out.value = whole & size_mask(size);
    out.ok = true;
    return out;
  }

  if (where->immediate) {
    /* The instruction unit supplies an immediate itself; there is nothing to
     * read here, and pretending otherwise would return a zero that looks real. */
    out.fault = true;
    return out;
  }

  const ap_m68030_access_result_t read =
      ap_m68030_access_read(access, where->address, function_code);
  out.clocks = read.clocks;
  out.fault = read.fault;
  out.ok = read.ok;
  /* The access path deals in long words; take the operand from it by size. */
  out.value = read.value & size_mask(size);
  return out;
}

ap_m68030_operand_result_t
ap_m68030_operand_write(ap_m68030_regs_t *regs, ap_m68030_access_ctx_t *access,
                        const ap_m68030_address_t *where, unsigned size,
                        uint32_t value, uint8_t function_code) {
  ap_m68030_operand_result_t out = {0};

  if (!where->valid || where->indirection_pending || where->immediate) {
    out.fault = true;
    return out;
  }

  if (where->in_register) {
    if (where->address_register) {
      /* "the source operand is sign-extended to a long operand and the
       * operation is performed on the address register using all 32 bits" --
       * an address register write is never partial. */
      ap_m68030_write_address_register(regs, where->reg,
                                       ap_m68030_sign_extend(value, size));
    } else {
      /* A data register keeps whatever the operand does not cover. */
      const uint32_t mask = size_mask(size);
      regs->d[where->reg] = (regs->d[where->reg] & ~mask) | (value & mask);
    }
    out.ok = true;
    return out;
  }

  const ap_m68030_access_result_t written = ap_m68030_access_write(
      access, where->address, function_code, value, size == 4u);
  out.clocks = written.clocks;
  out.fault = written.fault;
  out.ok = written.ok;
  return out;
}

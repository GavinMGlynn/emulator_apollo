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

  /* The access path deals in one bus cycle at a time, and a cycle carries at
   * most the long word containing its address. So an operand is selected out of
   * that long word by *position* -- the 68030 is big endian, so the byte at
   * address A sits in bits 31-24 when A & 3 is zero and moves down a byte for
   * each step of A & 3 -- and an operand crossing the boundary takes more than
   * one cycle.
   *
   * Masking the low bits instead, the shape this first had, returns the long
   * word's last byte for every address: right exactly when A & 3 is 3, silently
   * wrong the other three times in four, and never faulting.
   *
   * Misalignment is not an address error on this part, unlike the 68000 -- and
   * it is not a rare case either. Every exception frame puts its long-word PC
   * at SP + 2, so RTE and RTR read a straddling long *every time*. A model that
   * declined them would decline returning from every exception. */
  uint32_t address = where->address;
  unsigned remaining = size;
  uint32_t value = 0;

  while (remaining > 0u) {
    const unsigned offset = address & 3u;
    unsigned chunk = 4u - offset;
    if (chunk > remaining) {
      chunk = remaining;
    }

    /* The width the *program* asked for, so a device register is read once and
     * at its own address. Memory ignores it and is read a long word at a time
     * as the cache needs. */
    const ap_m68030_access_result_t read =
        ap_m68030_access_read_sized(access, address, function_code, chunk);
    out.clocks += read.clocks;
    if (!read.ok) {
      out.fault = read.fault;
      out.ok = false;
      return out;
    }

    const unsigned shift = (4u - offset - chunk) * 8u;
    const uint32_t piece = (read.value >> shift) & size_mask(chunk);
    value = (value << (chunk * 8u)) | piece;

    address += chunk;
    remaining -= chunk;
  }

  out.value = value;
  out.ok = true;
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

  /* The same split as the read, in the same order: the operand's most
   * significant bytes go to the lower address, because the part is big endian
   * and a straddling write must land the same way round the read takes it. */
  uint32_t address = where->address;
  unsigned remaining = size;

  while (remaining > 0u) {
    const unsigned offset = address & 3u;
    unsigned chunk = 4u - offset;
    if (chunk > remaining) {
      chunk = remaining;
    }

    const unsigned piece_shift = (remaining - chunk) * 8u;
    const uint32_t piece = (value >> piece_shift) & size_mask(chunk);

    const ap_m68030_access_result_t written =
        ap_m68030_access_write(access, address, function_code, piece, chunk);
    out.clocks += written.clocks;
    if (!written.ok) {
      out.fault = written.fault;
      out.ok = false;
      return out;
    }

    address += chunk;
    remaining -= chunk;
  }

  out.ok = true;
  return out;
}

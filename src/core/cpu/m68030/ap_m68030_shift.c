/* MC68030 family 1110. See ap_m68030_shift.h for the three shapes and for the
 * type field moving between the two shift forms. */

#include "cpu/m68030/ap_m68030_shift.h"

ap_m68030_shift_t ap_m68030_shift_decode(uint16_t instruction) {
  ap_m68030_shift_t out = {.form = AP_M68030_SHIFT_INVALID};

  if ((instruction & 0xF000u) != 0xE000u) {
    return out;
  }

  const unsigned size_field = (unsigned)((instruction >> 6) & 0x3u);
  const unsigned mode = (unsigned)((instruction >> 3) & 0x7u);
  const unsigned reg = (unsigned)(instruction & 0x7u);
  out.left = ((instruction >> 8) & 1u) != 0u;

  if (size_field != 0x3u) {
    /* Register shift. The type is at bits 4-3 here, *not* at 11-9. */
    out.form = AP_M68030_SHIFT_REGISTER;
    out.type = (ap_m68030_shift_type_t)((instruction >> 3) & 0x3u);
    out.size = 1u << size_field;
    out.reg = reg;
    out.count_in_register = ((instruction >> 5) & 1u) != 0u;

    const unsigned count_field = (unsigned)((instruction >> 9) & 0x7u);
    if (out.count_in_register) {
      out.count = count_field; /* a register number */
    } else {
      /* "1-7 represent counts of 1-7; 0 represents a count of 8" -- the same
       * quirk as ADDQ's quick data, resolved here so no caller can forget. */
      out.count = (count_field == 0u) ? 8u : count_field;
    }
    return out;
  }

  /* Size field 11 is not a size. Bit 11 chooses between the other two shapes. */
  if ((instruction & 0x0800u) != 0u) {
    /* Bit field, the 68020's addition. Bits 11-8 name the instruction. */
    out.form = AP_M68030_SHIFT_BITFIELD;
    out.bitfield = (ap_m68030_bitfield_t)((instruction >> 8) & 0xFu);
    out.ea = ap_m68030_ea_decode(mode, reg);
    if (out.ea.kind == AP_M68030_EA_INVALID) {
      out.form = AP_M68030_SHIFT_INVALID;
    }
    return out;
  }

  /* Memory shift: one word in memory, by exactly one. The type is at bits 11-9
   * here, where the register form keeps its count. */
  /* Only bits 10-9 are read: the third bit of that field is bit 11, which is
   * already known clear here, so the type cannot exceed 3. Masking three bits
   * and then rejecting values above 3 would be unreachable code pretending to
   * be a check. */
  out.type = (ap_m68030_shift_type_t)((instruction >> 9) & 0x3u);
  out.form = AP_M68030_SHIFT_MEMORY;
  out.size = 2;   /* "shifts a word in memory" */
  out.count = 1;  /* "by one bit position" */
  out.ea = ap_m68030_ea_decode(mode, reg);
  if (out.ea.kind == AP_M68030_EA_INVALID) {
    out.form = AP_M68030_SHIFT_INVALID;
  }
  return out;
}

unsigned ap_m68030_shift_length(const ap_m68030_shift_t *shift) {
  switch (shift->form) {
  case AP_M68030_SHIFT_BITFIELD:
    return 4; /* the offset-and-width extension word */
  case AP_M68030_SHIFT_REGISTER:
  case AP_M68030_SHIFT_MEMORY:
  case AP_M68030_SHIFT_INVALID:
    return 2;
  }
  return 2;
}

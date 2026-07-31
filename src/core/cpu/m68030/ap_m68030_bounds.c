/* MC68030 family 0000's size-11 escape: CMP2, CHK2, CAS and CAS2. See
 * ap_m68030_bounds.h for why the two halves count their sizes differently. */

#include "cpu/m68030/ap_m68030_bounds.h"

#include "cpu/m68030/ap_m68030_category.h"

/* The escape is family 0000 with bits 8-6 reading 011 -- which is the immediate
 * instructions' size field at its unassigned value. */
bool ap_m68030_bounds_matches(uint16_t instruction) {
  return ((instruction >> 12) & 0xFu) == 0x0u &&
         ((instruction >> 6) & 0x7u) == 0x3u;
}

ap_m68030_bounds_t ap_m68030_bounds_decode(uint16_t instruction) {
  ap_m68030_bounds_t out = {.kind = AP_M68030_BOUNDS_INVALID};
  if (!ap_m68030_bounds_matches(instruction)) {
    return out;
  }

  const unsigned size_field = (unsigned)((instruction >> 9) & 0x3u);
  const unsigned mode = (unsigned)((instruction >> 3) & 0x7u);
  const unsigned reg = (unsigned)(instruction & 0x7u);
  const bool compare_and_swap = ((instruction >> 11) & 1u) != 0u;

  if (!compare_and_swap) {
    /* CMP2 and CHK2: "00 -- Byte, 01 -- Word, 10 -- Long", and 11 unassigned. */
    if (size_field == 0x3u) {
      return out;
    }
    out.size = 1u << size_field;
    out.ea = ap_m68030_ea_decode(mode, reg);
    /* "Only control addressing modes can be used" -- the bounds pair is read,
     * never written, so control rather than control alterable. */
    if (!ap_m68030_ea_is_control(out.ea.kind)) {
      return out;
    }
    /* The extension word decides which of the two this is; until it has been
     * read, CMP2 stands for the pair. */
    out.kind = AP_M68030_BOUNDS_CMP2;
    return out;
  }

  /* CAS and CAS2: "01 -- Byte, 10 -- Word, 11 -- Long", one higher than the
   * family's ordinary encoding throughout, with 00 unassigned rather than 11.
   * So the width is 1 << (field - 1) here and 1 << field in the other half --
   * which is the whole reason the two are decoded apart rather than sharing a
   * size lookup. */
  if (size_field == 0x0u) {
    return out;
  }
  const unsigned swap_size = 1u << (size_field - 1u);

  /* CAS2 takes the pattern an immediate operand would have used, which CAS
   * cannot: its operand must be memory alterable. */
  if (mode == 0x7u && reg == 0x4u) {
    /* "10 -- Word operation, 11 -- Long operation": CAS2 has no byte form, so
     * the size CAS reads as a byte is not a CAS2 at all. */
    if (size_field == 0x1u) {
      return out;
    }
    out.size = swap_size;
    out.kind = AP_M68030_BOUNDS_CAS2;
    return out;
  }

  out.size = swap_size;
  out.ea = ap_m68030_ea_decode(mode, reg);
  /* "Only memory alterable addressing modes can be used": the operand is
   * read *and* written, and CAS's whole purpose is that the pair is
   * indivisible, so a register operand would be meaningless. */
  if (!ap_m68030_ea_is_memory_alterable(out.ea.kind)) {
    return out;
  }
  out.kind = AP_M68030_BOUNDS_CAS;
  return out;
}

ap_m68030_bounds_kind_t ap_m68030_bounds_kind(const ap_m68030_bounds_t *bounds,
                                              uint16_t extension) {
  if (bounds->kind != AP_M68030_BOUNDS_CMP2) {
    return bounds->kind;
  }
  return ((extension >> 11) & 1u) != 0u ? AP_M68030_BOUNDS_CHK2
                                        : AP_M68030_BOUNDS_CMP2;
}

bool ap_m68030_bounds_register_is_address(uint16_t extension) {
  return ((extension >> 15) & 1u) != 0u;
}

unsigned ap_m68030_bounds_register(uint16_t extension) {
  return (unsigned)((extension >> 12) & 0x7u);
}

unsigned ap_m68030_cas_update_register(uint16_t extension) {
  return (unsigned)((extension >> 6) & 0x7u);
}

unsigned ap_m68030_cas_compare_register(uint16_t extension) {
  return (unsigned)(extension & 0x7u);
}

unsigned ap_m68030_bounds_length(const ap_m68030_bounds_t *bounds) {
  switch (bounds->kind) {
  case AP_M68030_BOUNDS_CMP2:
  case AP_M68030_BOUNDS_CHK2:
  case AP_M68030_BOUNDS_CAS:
    return 1u;
  case AP_M68030_BOUNDS_CAS2:
    /* Two extension words, one per operand -- which is what lets CAS2 swap
     * both ends of a linked list in one indivisible operation. */
    return 2u;
  case AP_M68030_BOUNDS_INVALID:
    break;
  }
  return 0u;
}

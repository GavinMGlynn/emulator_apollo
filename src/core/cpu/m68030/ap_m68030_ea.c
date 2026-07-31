/* MC68030 effective address decode. See ap_m68030_ea.h for the citations, and
 * for why mode 7's register field is folded into the kind. */

#include "cpu/m68030/ap_m68030_ea.h"

ap_m68030_ea_t ap_m68030_ea_decode(unsigned mode, unsigned reg) {
  switch (mode & 0x7u) {
  case 0:
    return (ap_m68030_ea_t){AP_M68030_EA_DATA_REGISTER, reg};
  case 1:
    return (ap_m68030_ea_t){AP_M68030_EA_ADDRESS_REGISTER, reg};
  case 2:
    return (ap_m68030_ea_t){AP_M68030_EA_ADDRESS_INDIRECT, reg};
  case 3:
    return (ap_m68030_ea_t){AP_M68030_EA_POSTINCREMENT, reg};
  case 4:
    return (ap_m68030_ea_t){AP_M68030_EA_PREDECREMENT, reg};
  case 5:
    return (ap_m68030_ea_t){AP_M68030_EA_DISPLACEMENT, reg};
  case 6:
    return (ap_m68030_ea_t){AP_M68030_EA_INDEXED, reg};
  default:
    break;
  }

  /* Mode 111: the register field is a sub-opcode, not a register. Table 2-4
   * assigns 000 through 100 and nothing above. */
  switch (reg & 0x7u) {
  case 0:
    return (ap_m68030_ea_t){AP_M68030_EA_ABSOLUTE_SHORT, 0};
  case 1:
    return (ap_m68030_ea_t){AP_M68030_EA_ABSOLUTE_LONG, 0};
  case 2:
    return (ap_m68030_ea_t){AP_M68030_EA_PC_DISPLACEMENT, 0};
  case 3:
    return (ap_m68030_ea_t){AP_M68030_EA_PC_INDEXED, 0};
  case 4:
    return (ap_m68030_ea_t){AP_M68030_EA_IMMEDIATE, 0};
  default:
    return (ap_m68030_ea_t){AP_M68030_EA_INVALID, 0};
  }
}

bool ap_m68030_ea_uses_extension(ap_m68030_ea_kind_t kind) {
  /* Every kind is listed rather than defaulted: -Wswitch-enum is on, so adding
   * an addressing mode without deciding this question fails the build. */
  switch (kind) {
  case AP_M68030_EA_INDEXED:
  case AP_M68030_EA_PC_INDEXED:
    return true;
  case AP_M68030_EA_DATA_REGISTER:
  case AP_M68030_EA_ADDRESS_REGISTER:
  case AP_M68030_EA_ADDRESS_INDIRECT:
  case AP_M68030_EA_POSTINCREMENT:
  case AP_M68030_EA_PREDECREMENT:
  case AP_M68030_EA_DISPLACEMENT:
  case AP_M68030_EA_ABSOLUTE_SHORT:
  case AP_M68030_EA_ABSOLUTE_LONG:
  case AP_M68030_EA_PC_DISPLACEMENT:
  case AP_M68030_EA_IMMEDIATE:
  case AP_M68030_EA_INVALID:
    return false;
  }
  return false;
}

/* Table 2-2 read as a whole: the same I/IS value means different things
 * depending on IS, so the pair is decoded together rather than separately. */
static void decode_indirect(ap_m68030_extension_t *out, bool index_suppressed,
                            unsigned i_is) {
  if (!index_suppressed) {
    if (i_is == 0) {
      out->indirect = AP_M68030_INDIRECT_NONE;
      out->outer_displacement_size = AP_M68030_OD_NONE;
      return;
    }
    if (i_is == 4) {
      /* "0  100  Reserved" */
      out->indirect = AP_M68030_INDIRECT_RESERVED;
      out->reserved = true;
      return;
    }
    /* 001-011 preindexed, 101-111 postindexed, each with null, word or long
     * outer displacement in that order. */
    out->indirect = (i_is < 4) ? AP_M68030_INDIRECT_PREINDEXED
                               : AP_M68030_INDIRECT_POSTINDEXED;
    out->outer_displacement_size =
        (ap_m68030_od_size_t)(((i_is - 1u) % 4u) + AP_M68030_OD_NULL);
    return;
  }

  /* IS = 1. */
  if (i_is == 0) {
    out->indirect = AP_M68030_INDIRECT_NONE;
    out->outer_displacement_size = AP_M68030_OD_NONE;
    return;
  }
  if (i_is >= 4) {
    /* "1  100-111  Reserved" */
    out->indirect = AP_M68030_INDIRECT_RESERVED;
    out->reserved = true;
    return;
  }
  out->indirect = AP_M68030_INDIRECT_MEMORY;
  out->outer_displacement_size =
      (ap_m68030_od_size_t)((i_is - 1u) + AP_M68030_OD_NULL);
}

ap_m68030_extension_t ap_m68030_ea_decode_extension(uint16_t word) {
  ap_m68030_extension_t out = {0};

  out.index_is_address_register = ((word >> 15) & 1u) != 0u;
  out.index_register = (unsigned)((word >> 12) & 0x7u);
  out.index_long = ((word >> 11) & 1u) != 0u;
  /* "00 = 1, 01 = 2, 10 = 4, 11 = 8" -- stored as the factor rather than the
   * encoding, so no caller repeats the shift. */
  out.scale = 1u << ((word >> 9) & 0x3u);
  out.full_format = ((word >> 8) & 1u) != 0u;

  if (!out.full_format) {
    /* The brief format's low byte is a signed 8-bit displacement. */
    out.displacement = (int8_t)(word & 0xFFu);
    out.outer_displacement_size = AP_M68030_OD_NONE;
    return out;
  }

  out.base_suppressed = ((word >> 7) & 1u) != 0u;
  out.index_suppressed = ((word >> 6) & 1u) != 0u;
  out.base_displacement_size = (ap_m68030_bd_size_t)((word >> 4) & 0x3u);
  if (out.base_displacement_size == AP_M68030_BD_RESERVED) {
    out.reserved = true;
  }

  decode_indirect(&out, out.index_suppressed, (unsigned)(word & 0x7u));
  return out;
}

static unsigned displacement_words(unsigned size) {
  switch (size) {
  case AP_M68030_BD_WORD:
    return 1;
  case AP_M68030_BD_LONG:
    return 2;
  default:
    return 0;
  }
}

unsigned ap_m68030_ea_extension_words(const ap_m68030_extension_t *extension) {
  if (!extension->full_format) {
    return 0; /* the brief format carries its displacement in its own word */
  }

  unsigned words = displacement_words((unsigned)extension->base_displacement_size);

  switch (extension->outer_displacement_size) {
  case AP_M68030_OD_WORD:
    words += 1;
    break;
  case AP_M68030_OD_LONG:
    words += 2;
    break;
  case AP_M68030_OD_NONE:
  case AP_M68030_OD_NULL:
    break; /* neither adds a word */
  }
  return words;
}

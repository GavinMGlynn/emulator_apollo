/* MC68040 MMU registers. See the header for the three rules where this part
 * contradicts the 68851. */

#include "cpu/m68040/ap_m68040_regs.h"

uint32_t ap_m68040_root_pointer(uint32_t value) {
  return value & AP_M68040_ROOT_POINTER_MASK;
}

bool ap_m68040_root_pointer_is_aligned(uint32_t value) {
  return (value & ~AP_M68040_ROOT_POINTER_MASK) == 0u;
}

ap_m68040_tcr_t ap_m68040_tcr_decode(uint16_t value) {
  return (ap_m68040_tcr_t){
      .enable = (value & 0x8000u) != 0u,
      .page_size = (value & 0x4000u) ? AP_M68040_PAGE_8K : AP_M68040_PAGE_4K,
  };
}

uint16_t ap_m68040_tcr_encode(const ap_m68040_tcr_t *tcr) {
  uint16_t value = 0;
  if (tcr->enable) {
    value |= 0x8000u;
  }
  if (tcr->page_size == AP_M68040_PAGE_8K) {
    value |= 0x4000u;
  }
  return value;
}

ap_m68040_ttr_t ap_m68040_ttr_decode(uint32_t value) {
  ap_m68040_ttr_t out = {
      .logical_base = (unsigned)((value >> 24) & 0xFFu),
      .logical_mask = (unsigned)((value >> 16) & 0xFFu),
      .enable = (value & 0x8000u) != 0u,
      .user_attribute_1 = (value & 0x0200u) != 0u,
      .user_attribute_0 = (value & 0x0100u) != 0u,
      .cache_mode = (ap_m68040_cache_mode_t)((value >> 5) & 0x3u),
      .write_protect = (value & 0x0004u) != 0u,
  };

  /* "00 = Match only if FC2 = 0; 01 = Match only if FC2 = 1; 1X = Ignore FC2."
   * The low bit is a don't-care only when the high bit is set, so this cannot
   * be a plain two-bit value. */
  const unsigned s = (unsigned)((value >> 13) & 0x3u);
  out.supervisor_mode = (s & 0x2u) ? AP_M68040_TT_ANY
                                   : (ap_m68040_tt_mode_t)s;
  return out;
}

uint32_t ap_m68040_ttr_encode(const ap_m68040_ttr_t *ttr) {
  uint32_t value = ((uint32_t)(ttr->logical_base & 0xFFu) << 24) |
                   ((uint32_t)(ttr->logical_mask & 0xFFu) << 16);
  if (ttr->enable) {
    value |= 0x8000u;
  }
  /* `AP_M68040_TT_ANY` re-encodes as `10`, the canonical member of `1X`. */
  value |= ((uint32_t)ttr->supervisor_mode & 0x3u) << 13;
  if (ttr->user_attribute_1) {
    value |= 0x0200u;
  }
  if (ttr->user_attribute_0) {
    value |= 0x0100u;
  }
  value |= ((uint32_t)ttr->cache_mode & 0x3u) << 5;
  if (ttr->write_protect) {
    value |= 0x0004u;
  }
  return value;
}

bool ap_m68040_ttr_matches(const ap_m68040_ttr_t *ttr, uint32_t address,
                           unsigned function_code) {
  if (!ttr->enable) {
    return false;
  }

  /* "This field specifies the way FC2 is used in matching an address." */
  const bool supervisor = (function_code & 0x4u) != 0u;
  switch (ttr->supervisor_mode) {
  case AP_M68040_TT_USER_ONLY:
    if (supervisor) {
      return false;
    }
    break;
  case AP_M68040_TT_SUPERVISOR_ONLY:
    if (!supervisor) {
      return false;
    }
    break;
  case AP_M68040_TT_ANY:
    break;
  }

  /* "This 8-bit field is compared with address bits A31-A24" and "setting a bit
   * in [the mask] causes the corresponding bit in the Logical Address Base
   * field to be ignored" -- so the mask widens the block rather than narrowing
   * it, which is the opposite of what "mask" usually suggests. */
  const unsigned high = (unsigned)((address >> 24) & 0xFFu);
  const unsigned compared = ~ttr->logical_mask & 0xFFu;
  return (high & compared) == (ttr->logical_base & compared);
}

ap_m68040_mmusr_t ap_m68040_mmusr_decode(uint32_t value) {
  return (ap_m68040_mmusr_t){
      .physical_address = value & 0xFFFFF000u,
      .bus_error = (value & 0x0800u) != 0u,
      .global = (value & 0x0400u) != 0u,
      .user_attribute_1 = (value & 0x0200u) != 0u,
      .user_attribute_0 = (value & 0x0100u) != 0u,
      .supervisor = (value & 0x0080u) != 0u,
      .cache_mode = (ap_m68040_cache_mode_t)((value >> 5) & 0x3u),
      .modified = (value & 0x0010u) != 0u,
      .write_protect = (value & 0x0004u) != 0u,
      .transparent = (value & 0x0002u) != 0u,
      .resident = (value & 0x0001u) != 0u,
  };
}

uint32_t ap_m68040_mmusr_encode(const ap_m68040_mmusr_t *mmusr) {
  uint32_t value = mmusr->physical_address & 0xFFFFF000u;
  if (mmusr->bus_error) {
    value |= 0x0800u;
  }
  if (mmusr->global) {
    value |= 0x0400u;
  }
  if (mmusr->user_attribute_1) {
    value |= 0x0200u;
  }
  if (mmusr->user_attribute_0) {
    value |= 0x0100u;
  }
  if (mmusr->supervisor) {
    value |= 0x0080u;
  }
  value |= ((uint32_t)mmusr->cache_mode & 0x3u) << 5;
  if (mmusr->modified) {
    value |= 0x0010u;
  }
  if (mmusr->write_protect) {
    value |= 0x0004u;
  }
  if (mmusr->transparent) {
    value |= 0x0002u;
  }
  if (mmusr->resident) {
    value |= 0x0001u;
  }
  /* Bit 3 stays clear. */
  return value;
}

ap_m68040_mmusr_t ap_m68040_mmusr_bus_error(void) {
  /* "If the B-bit is set, all other bits are zero." Built rather than assembled
   * field by field, so a caller cannot accidentally report a physical address
   * alongside a transfer error that prevented one being found. */
  return (ap_m68040_mmusr_t){.bus_error = true};
}

ap_m68040_mmusr_t ap_m68040_mmusr_transparent(void) {
  /* "If the T-bit is set, then the PTEST address matches an instruction or data
   * TTR, the R-bit is set, and all other bits are zero." Both bits, and nothing
   * else -- a transparent block has no page descriptor to report attributes
   * from. */
  return (ap_m68040_mmusr_t){.transparent = true, .resident = true};
}

/* MC68851 instruction decode. See ap_m68851_decode.h; Appendix A's bit rows
 * read from the page images. */

#include "cpu/m68851/ap_m68851_decode.h"

ap_m68851_fc_spec_t ap_m68851_decode_fc(unsigned field) {
  ap_m68851_fc_spec_t spec = {.source = AP_M68851_FC_UNDEFINED};
  field &= 0x1Fu;

  /* A prefix code, tested longest-prefix first. `00000` and `00001` are two
   * meanings inside what a careless widening of `01RRR` would swallow. */
  if ((field & 0x10u) != 0u) {
    spec.source = AP_M68851_FC_IMMEDIATE;
    spec.immediate = field & 0xFu;
  } else if ((field & 0x08u) != 0u) {
    spec.source = AP_M68851_FC_DATA_REGISTER;
    spec.data_register = field & 0x7u;
  } else if (field == 0x00u) {
    spec.source = AP_M68851_FC_SFC;
  } else if (field == 0x01u) {
    spec.source = AP_M68851_FC_DFC;
  }
  /* `00010` through `00111` fall through as undefined: the manual lists four
   * forms and no others. */
  return spec;
}

bool ap_m68851_fc_reaches_dma(const ap_m68851_fc_spec_t *spec) {
  /* Only the immediate form carries all four bits in the instruction. "Since
   * the SFC of the MC68020 has only three implemented bits, only function
   * codes $0 through $7 can be specified in this manner." */
  return spec->source == AP_M68851_FC_IMMEDIATE &&
         (spec->immediate & 0x8u) != 0u;
}

ap_m68851_pflush_t ap_m68851_decode_pflush(uint16_t command) {
  const unsigned mode = (unsigned)((command >> 10) & 0x7u);
  ap_m68851_pflush_t out = {
      .mode = AP_M68851_PFLUSH_UNDEFINED,
      .mask = (unsigned)((command >> 5) & 0xFu),
      .fc = ap_m68851_decode_fc((unsigned)(command & 0x1Fu)),
  };

  switch (mode) {
  case 1u:
  case 4u:
  case 5u:
  case 6u:
  case 7u:
    out.mode = (ap_m68851_pflush_mode_t)mode;
    break;
  default:
    /* `000`, `010` and `011` are listed nowhere. */
    break;
  }
  return out;
}

bool ap_m68851_pflush_is_valid(const ap_m68851_pflush_t *pflush) {
  if (pflush->mode == AP_M68851_PFLUSH_UNDEFINED) {
    return false;
  }
  if (pflush->mode == AP_M68851_PFLUSH_ALL) {
    /* "If mode = 001 (flush all entries), mask must be 0000" and "function code
     * must be 00000". A flush-all that names a function code contradicts
     * itself, so the encoding is forbidden rather than the fields ignored. */
    if (pflush->mask != 0u) {
      return false;
    }
    if (pflush->fc.source != AP_M68851_FC_SFC) {
      /* `00000` decodes as the SFC form; for a flush-all it is simply the
       * required zero. */
      return false;
    }
  }
  return true;
}

bool ap_m68851_pflush_includes_shared(ap_m68851_pflush_mode_t mode) {
  return mode == AP_M68851_PFLUSH_FC_SHARED ||
         mode == AP_M68851_PFLUSH_FC_EA_SHARED;
}

bool ap_m68851_pflush_uses_address(ap_m68851_pflush_mode_t mode) {
  return mode == AP_M68851_PFLUSH_FC_EA ||
         mode == AP_M68851_PFLUSH_FC_EA_SHARED;
}

bool ap_m68851_pflush_matches_fc(unsigned mask, unsigned instruction_fc,
                                 unsigned entry_fc) {
  /* "(ATC function code bits and <mask>) = (<fc> and <mask>)". */
  return (entry_fc & mask) == (instruction_fc & mask);
}

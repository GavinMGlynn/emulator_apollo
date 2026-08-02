/* MC68020 module call and return. See ap_m68020_module.h; formats read from the
 * page images of Figures D-1, D-2 and D-3 rather than from extracted text,
 * which had dropped a column from the entry word. */

#include "cpu/m68020/ap_m68020_module.h"

ap_m68020_module_decode_t ap_m68020_module_decode(uint16_t word) {
  ap_m68020_module_decode_t out = {
      .opcode = AP_M68020_MODULE_NOT_A_MODULE_INSTRUCTION,
  };

  /* Both instructions are `0000 0110 11...`, so ten bits of prefix. */
  if ((word & 0xFFC0u) != AP_M68020_CALLM_BASE) {
    return out;
  }

  const unsigned mode = (word >> 3) & 7u;
  const unsigned reg = word & 7u;

  /* Modes 000 and 001 are `Dn` and `An`, which CALLM's table marks with a dash:
   * "only control addressing modes can be used". RTM lives there. */
  if (mode <= 1u) {
    out.opcode = AP_M68020_MODULE_RTM;
    /* "D/A field -- specifies whether the module data pointer is in a data or
     * an address register": bit 3, which is the low bit of the mode field. */
    out.rtm_address_register = (mode == 1u);
    out.rtm_register = reg;
    return out;
  }

  /* The remaining modes must be *control* modes. From CALLM's table: `(An)`,
   * `(d16,An)`, the `(An,Xn)` family, `(xxx).W`, `(xxx).L` and the PC-relative
   * pair -- so mode 011 (postincrement), mode 100 (predecrement) and the
   * immediate encoding 111/100 are excluded. A word in that space is an illegal
   * instruction and never reads a descriptor, so it is not a format error. */
  const bool control_mode =
      (mode == 2u) ||                       /* (An) */
      (mode == 5u) ||                       /* (d16,An) */
      (mode == 6u) ||                       /* (d8,An,Xn) and the full formats */
      (mode == 7u && reg <= 3u);            /* (xxx).W/.L, (d16,PC), (d8,PC,Xn) */
  if (!control_mode) {
    return out;
  }

  out.opcode = AP_M68020_MODULE_CALLM;
  out.mode = mode;
  out.reg = reg;
  return out;
}

ap_m68020_module_control_t ap_m68020_module_control(uint32_t first_long_word) {
  /* Figure D-1: Opt 31-29, Type 28-24, Access Level 23-16, the low half
   * "(Reserved, Must be Zero)". */
  return (ap_m68020_module_control_t){
      .opt = (unsigned)((first_long_word >> 29) & 0x7u),
      .type = (unsigned)((first_long_word >> 24) & 0x1Fu),
      .access_level = (unsigned)((first_long_word >> 16) & 0xFFu),
  };
}

ap_m68020_module_status_t
ap_m68020_module_validate(const ap_m68020_module_control_t *control) {
  /* "All module descriptor types $10-$1F are reserved for user definition and
   * cause a format error exception. This provides the user with a means of
   * disabling any module by setting a single bit in its descriptor" -- bit 4 of
   * the type. Checked before the general type test so the disable case keeps
   * its own name; both take the same vector. */
  if ((control->type & 0x10u) != 0u) {
    return AP_M68020_MODULE_DISABLED;
  }

  /* "The MC68020 only recognizes descriptors of type $00 and $01, all others
   * cause a format exception." */
  if (control->type != AP_M68020_MODULE_TYPE_NO_ACCESS_CHANGE &&
      control->type != AP_M68020_MODULE_TYPE_ACCESS_CHANGE) {
    return AP_M68020_MODULE_BAD_TYPE;
  }

  /* "The MC68020 recognizes only the options of 000 and 100, all others cause a
   * format exception." */
  if (control->opt != AP_M68020_MODULE_OPT_ON_STACK &&
      control->opt != AP_M68020_MODULE_OPT_INDIRECT) {
    return AP_M68020_MODULE_BAD_OPT;
  }

  return AP_M68020_MODULE_OK;
}

bool ap_m68020_module_changes_access(
    const ap_m68020_module_control_t *control) {
  return control->type == AP_M68020_MODULE_TYPE_ACCESS_CHANGE;
}

bool ap_m68020_module_copies_arguments(
    const ap_m68020_module_control_t *control, bool stack_pointer_changes) {
  /* Both conditions, not either: option 100's arguments are reached through an
   * indirect pointer and are never copied however the stack moves, and option
   * 000's are already in the right place when the stack does not move. */
  return control->opt == AP_M68020_MODULE_OPT_ON_STACK && stack_pointer_changes;
}

ap_m68020_module_entry_t ap_m68020_module_entry(uint16_t word) {
  /* Figure D-2, read from the page image: D/A at bit 15, Register at 14-12,
   * bits 11-0 all zero, and the module's first instruction in the next word. */
  return (ap_m68020_module_entry_t){
      .address_register = (word & 0x8000u) != 0u,
      .reg = (unsigned)((word >> 12) & 0x7u),
  };
}

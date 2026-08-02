/* MC68882 coprocessor interface registers. See ap_m68882_cir.h for why the
 * select field has don't-care bits and why two registers in the map do not
 * exist on this part. */

#include "cpu/m68882/ap_m68882_cir.h"

ap_m68882_cir_t ap_m68882_cir_select(uint32_t address) {
  const uint32_t select = address & 0x1Fu;

  /* Table 7-2's patterns, widest first. `100xx`, `110xx` and `111xx` are the
   * three 32-bit registers, each spanning four byte addresses -- matching them
   * before the narrower patterns is what keeps `$12` inside the operand CIR
   * rather than falling through to a reserved value. */
  if ((select & 0x1Cu) == 0x10u) {
    return AP_M68882_CIR_OPERAND; /* 100xx */
  }
  if ((select & 0x1Cu) == 0x18u) {
    return AP_M68882_CIR_INSTRUCTION_ADDRESS; /* 110xx */
  }
  if ((select & 0x1Cu) == 0x1Cu) {
    return AP_M68882_CIR_OPERAND_ADDRESS; /* 111xx */
  }

  /* The 16-bit registers, whose patterns end in a don't-care: `0000x`, `0001x`
   * and so on. A0 is that don't-care, which is what puts each register at both
   * of its byte addresses. */
  switch (select >> 1) {
  case 0x0u: return AP_M68882_CIR_RESPONSE;        /* $00 */
  case 0x1u: return AP_M68882_CIR_CONTROL;         /* $02 */
  case 0x2u: return AP_M68882_CIR_SAVE;            /* $04 */
  case 0x3u: return AP_M68882_CIR_RESTORE;         /* $06 */
  case 0x4u: return AP_M68882_CIR_OPERATION_WORD;  /* $08 */
  case 0x5u: return AP_M68882_CIR_COMMAND;         /* $0A */
  case 0x6u: return AP_M68882_CIR_RESERVED;        /* $0C */
  case 0x7u: return AP_M68882_CIR_CONDITION;       /* $0E */
  case 0xAu: return AP_M68882_CIR_REGISTER_SELECT; /* $14 */
  case 0xBu: return AP_M68882_CIR_RESERVED;        /* $16 */
  default: break;
  }
  return AP_M68882_CIR_NONE;
}

bool ap_m68882_cir_selected(uint8_t function_code, uint32_t address,
                            unsigned cpid) {
  /* All three, and each on its own matches something else. CPU space alone is
   * shared with the breakpoint acknowledge this core already runs; the type
   * field alone is shared with every other coprocessor on the bus; and the cpID
   * alone means nothing outside CPU space. */
  if (function_code != 7u) {
    return false;
  }
  if (((address >> 16) & 0xFu) != AP_M68882_CPU_SPACE_TYPE) {
    return false;
  }
  return ((address >> 13) & 7u) == cpid;
}

unsigned ap_m68882_cir_width(ap_m68882_cir_t cir) {
  switch (cir) {
  case AP_M68882_CIR_OPERAND:
  case AP_M68882_CIR_INSTRUCTION_ADDRESS:
  case AP_M68882_CIR_OPERAND_ADDRESS:
    return 32u;
  case AP_M68882_CIR_RESPONSE:
  case AP_M68882_CIR_CONTROL:
  case AP_M68882_CIR_SAVE:
  case AP_M68882_CIR_RESTORE:
  case AP_M68882_CIR_OPERATION_WORD:
  case AP_M68882_CIR_COMMAND:
  case AP_M68882_CIR_CONDITION:
  case AP_M68882_CIR_REGISTER_SELECT:
  case AP_M68882_CIR_RESERVED:
    return 16u;
  case AP_M68882_CIR_NONE:
    break;
  }
  return 0u;
}

bool ap_m68882_cir_readable(ap_m68882_cir_t cir) {
  switch (cir) {
  case AP_M68882_CIR_RESPONSE:
  case AP_M68882_CIR_SAVE:
  case AP_M68882_CIR_RESTORE:
  case AP_M68882_CIR_OPERAND:
  case AP_M68882_CIR_REGISTER_SELECT:
    return true;
  /* Write-only, and their reads return all ones rather than faulting. */
  case AP_M68882_CIR_CONTROL:
  case AP_M68882_CIR_COMMAND:
  case AP_M68882_CIR_CONDITION:
  case AP_M68882_CIR_INSTRUCTION_ADDRESS:
  /* Absent from this part, and reserved. */
  case AP_M68882_CIR_OPERATION_WORD:
  case AP_M68882_CIR_OPERAND_ADDRESS:
  case AP_M68882_CIR_RESERVED:
  case AP_M68882_CIR_NONE:
    break;
  }
  return false;
}

bool ap_m68882_cir_writable(ap_m68882_cir_t cir) {
  switch (cir) {
  case AP_M68882_CIR_CONTROL:
  case AP_M68882_CIR_RESTORE:
  case AP_M68882_CIR_COMMAND:
  case AP_M68882_CIR_CONDITION:
  case AP_M68882_CIR_OPERAND:
  case AP_M68882_CIR_INSTRUCTION_ADDRESS:
    return true;
  /* Read-only, and their writes are ignored rather than faulting. */
  case AP_M68882_CIR_RESPONSE:
  case AP_M68882_CIR_SAVE:
  case AP_M68882_CIR_REGISTER_SELECT:
  case AP_M68882_CIR_OPERATION_WORD:
  case AP_M68882_CIR_OPERAND_ADDRESS:
  case AP_M68882_CIR_RESERVED:
  case AP_M68882_CIR_NONE:
    break;
  }
  return false;
}

bool ap_m68882_cir_implemented(ap_m68882_cir_t cir) {
  /* Table 7-2's footnote: "these CIRs are optionally implemented by a
   * coprocessor only if they are needed; since they are not used by the
   * MC68881, they are not implemented." They are in Figure 7-2's map and absent
   * from the silicon, which is what a map transcribed without its footnote gets
   * wrong. */
  return cir != AP_M68882_CIR_OPERATION_WORD &&
         cir != AP_M68882_CIR_OPERAND_ADDRESS &&
         cir != AP_M68882_CIR_RESERVED && cir != AP_M68882_CIR_NONE;
}

uint32_t ap_m68882_cir_unreadable_value(ap_m68882_cir_t cir) {
  /* All ones at the register's own width. Never zero: zero is a legal value for
   * most of these, so a driver could not tell "nothing answered" from data. */
  return ap_m68882_cir_width(cir) == 32u ? UINT32_C(0xFFFFFFFF)
                                         : UINT32_C(0x0000FFFF);
}

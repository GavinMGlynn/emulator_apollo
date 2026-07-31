/* MC68030 family 0100, the $4E control group. See ap_m68030_control.h for the
 * subtree's shape and for which four instructions are privileged. */

#include "cpu/m68030/ap_m68030_control.h"

#include "cpu/m68030/ap_m68030_exception.h"

bool ap_m68030_control_matches(uint16_t instruction) {
  return (instruction & 0xFF00u) == 0x4E00u;
}

ap_m68030_control_t ap_m68030_control_decode(uint16_t instruction) {
  ap_m68030_control_t control = {.kind = AP_M68030_CTL_INVALID};
  if (!ap_m68030_control_matches(instruction)) {
    return control;
  }

  const unsigned low = (unsigned)(instruction & 0xFFu);
  const unsigned mode = (unsigned)((instruction >> 3) & 0x7u);
  const unsigned reg = (unsigned)(instruction & 0x7u);

  /* Bits 7-6: 10 is JSR, 11 is JMP, each with a six-bit effective address. */
  if ((instruction & 0x00C0u) == 0x0080u) {
    control.kind = AP_M68030_CTL_JSR;
    control.ea = ap_m68030_ea_decode((unsigned)((instruction >> 3) & 0x7u), reg);
    return control;
  }
  if ((instruction & 0x00C0u) == 0x00C0u) {
    control.kind = AP_M68030_CTL_JMP;
    control.ea = ap_m68030_ea_decode((unsigned)((instruction >> 3) & 0x7u), reg);
    return control;
  }

  /* $4E40-$4E4F: TRAP #vector, the vector being the low four bits. */
  if (low >= 0x40u && low <= 0x4Fu) {
    control.kind = AP_M68030_CTL_TRAP;
    control.vector = (unsigned)(instruction & 0xFu);
    return control;
  }

  /* $4E50-$4E5F: LINK below, UNLK above, split by bit 3. */
  if (low >= 0x50u && low <= 0x5Fu) {
    control.kind = (mode == 0x2u) ? AP_M68030_CTL_LINK : AP_M68030_CTL_UNLK;
    control.reg = reg;
    return control;
  }

  /* $4E60-$4E6F: MOVE USP, with bit 3 the direction. */
  if (low >= 0x60u && low <= 0x6Fu) {
    control.kind = (mode == 0x4u) ? AP_M68030_CTL_MOVE_TO_USP
                                  : AP_M68030_CTL_MOVE_FROM_USP;
    control.reg = reg;
    return control;
  }

  /* $4E70-$4E77: fully decoded singles. */
  switch (low) {
  case 0x70u:
    control.kind = AP_M68030_CTL_RESET;
    return control;
  case 0x71u:
    control.kind = AP_M68030_CTL_NOP;
    return control;
  case 0x72u:
    control.kind = AP_M68030_CTL_STOP;
    return control;
  case 0x73u:
    control.kind = AP_M68030_CTL_RTE;
    return control;
  case 0x74u:
    control.kind = AP_M68030_CTL_RTD;
    return control;
  case 0x75u:
    control.kind = AP_M68030_CTL_RTS;
    return control;
  case 0x76u:
    control.kind = AP_M68030_CTL_TRAPV;
    return control;
  case 0x77u:
    control.kind = AP_M68030_CTL_RTR;
    return control;
  default:
    break;
  }

  return control; /* still AP_M68030_CTL_INVALID */
}

unsigned ap_m68030_control_length(const ap_m68030_control_t *control) {
  switch (control->kind) {
  case AP_M68030_CTL_LINK: /* "WORD" displacement */
  case AP_M68030_CTL_RTD:  /* "16-BIT DISPLACEMENT" */
  case AP_M68030_CTL_STOP: /* "IMMEDIATE DATA" */
    return 4;
  case AP_M68030_CTL_TRAP:
  case AP_M68030_CTL_UNLK:
  case AP_M68030_CTL_MOVE_TO_USP:
  case AP_M68030_CTL_MOVE_FROM_USP:
  case AP_M68030_CTL_RESET:
  case AP_M68030_CTL_NOP:
  case AP_M68030_CTL_RTE:
  case AP_M68030_CTL_RTS:
  case AP_M68030_CTL_TRAPV:
  case AP_M68030_CTL_RTR:
  case AP_M68030_CTL_JSR:
  case AP_M68030_CTL_JMP:
  case AP_M68030_CTL_INVALID:
    return 2;
  }
  return 2;
}

bool ap_m68030_control_privileged(ap_m68030_control_kind_t kind) {
  switch (kind) {
  case AP_M68030_CTL_RESET:
  case AP_M68030_CTL_STOP:
  case AP_M68030_CTL_RTE:
  case AP_M68030_CTL_MOVE_TO_USP:
  case AP_M68030_CTL_MOVE_FROM_USP:
    return true;
  case AP_M68030_CTL_TRAP:
  case AP_M68030_CTL_LINK:
  case AP_M68030_CTL_UNLK:
  case AP_M68030_CTL_NOP:
  case AP_M68030_CTL_RTD:
  case AP_M68030_CTL_RTS:
  case AP_M68030_CTL_TRAPV:
  case AP_M68030_CTL_RTR:
  case AP_M68030_CTL_JSR:
  case AP_M68030_CTL_JMP:
  case AP_M68030_CTL_INVALID:
    return false;
  }
  return false;
}

unsigned ap_m68030_control_trap_vector(const ap_m68030_control_t *control) {
  /* Table 8-1 puts TRAP #0-15 at vectors 32-47, so the instruction's four-bit
   * field is an index into that range and not a vector number itself. */
  return ap_m68030_trap_vector(control->vector);
}

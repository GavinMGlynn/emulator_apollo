/* MC68030 family 0100 single-operand group. See ap_m68030_single.h for the
 * size-field escape and for which of these are privileged. */

#include "cpu/m68030/ap_m68030_single.h"

ap_m68030_single_t ap_m68030_single_decode(uint16_t instruction) {
  ap_m68030_single_t single = {.kind = AP_M68030_SINGLE_INVALID};

  if ((instruction & 0xF000u) != 0x4000u) {
    return single;
  }

  /* "ILLEGAL" is a specific word rather than an absence of one: the manual
   * defines $4AFC as an instruction whose purpose is to take the illegal
   * instruction exception. It sits inside TAS's range and must be recognised
   * first. */
  if (instruction == 0x4AFCu) {
    single.kind = AP_M68030_SINGLE_ILLEGAL;
    return single;
  }

  const unsigned row = (unsigned)((instruction >> 9) & 0x7u);
  const unsigned size_field = (unsigned)((instruction >> 6) & 0x3u);
  const unsigned mode = (unsigned)((instruction >> 3) & 0x7u);
  const unsigned reg = (unsigned)(instruction & 0x7u);

  /* Bit 8 set is not this group -- those are LEA, CHK and the $48/$4C forms. */
  if ((instruction & 0x0100u) != 0u) {
    return single;
  }

  if (size_field == 0x3u) {
    /* The illegal size is the escape, and what it selects depends on the row. */
    switch (row) {
    case 0x0u:
      single.kind = AP_M68030_SINGLE_MOVE_FROM_SR;
      break;
    case 0x1u:
      single.kind = AP_M68030_SINGLE_MOVE_FROM_CCR;
      break;
    case 0x2u:
      single.kind = AP_M68030_SINGLE_MOVE_TO_CCR;
      break;
    case 0x3u:
      single.kind = AP_M68030_SINGLE_MOVE_TO_SR;
      break;
    case 0x5u:
      single.kind = AP_M68030_SINGLE_TAS;
      break;
    default:
      return single; /* invalid */
    }
    single.ea = ap_m68030_ea_decode(mode, reg);
    if (single.ea.kind == AP_M68030_EA_INVALID) {
      single.kind = AP_M68030_SINGLE_INVALID;
    }
    return single;
  }

  switch (row) {
  case 0x0u:
    single.kind = AP_M68030_SINGLE_NEGX;
    break;
  case 0x1u:
    single.kind = AP_M68030_SINGLE_CLR;
    break;
  case 0x2u:
    single.kind = AP_M68030_SINGLE_NEG;
    break;
  case 0x3u:
    single.kind = AP_M68030_SINGLE_NOT;
    break;
  case 0x5u:
    single.kind = AP_M68030_SINGLE_TST;
    break;
  default:
    return single; /* invalid */
  }

  /* "00 - Byte, 01 - Word, 10 - Long." */
  single.size = 1u << size_field;
  single.ea = ap_m68030_ea_decode(mode, reg);
  if (single.ea.kind == AP_M68030_EA_INVALID) {
    single.kind = AP_M68030_SINGLE_INVALID;
  }
  return single;
}

bool ap_m68030_single_privileged(ap_m68030_single_kind_t kind) {
  switch (kind) {
  /* Writing SR sets the S bit; reading it became privileged on the 68010. */
  case AP_M68030_SINGLE_MOVE_TO_SR:
  case AP_M68030_SINGLE_MOVE_FROM_SR:
    return true;
  /* The CCR forms touch only the condition codes. */
  case AP_M68030_SINGLE_MOVE_TO_CCR:
  case AP_M68030_SINGLE_MOVE_FROM_CCR:
  case AP_M68030_SINGLE_NEGX:
  case AP_M68030_SINGLE_CLR:
  case AP_M68030_SINGLE_NEG:
  case AP_M68030_SINGLE_NOT:
  case AP_M68030_SINGLE_TST:
  case AP_M68030_SINGLE_TAS:
  case AP_M68030_SINGLE_ILLEGAL:
  case AP_M68030_SINGLE_INVALID:
    return false;
  }
  return false;
}

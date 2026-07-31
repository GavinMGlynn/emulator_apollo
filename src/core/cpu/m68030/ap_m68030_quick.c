/* MC68030 family 0101. See ap_m68030_quick.h for how the five instructions are
 * told apart and for the quick data field's zero. */

#include "cpu/m68030/ap_m68030_quick.h"

ap_m68030_quick_t ap_m68030_quick_decode(uint16_t instruction) {
  ap_m68030_quick_t quick = {0};

  const unsigned size_field = (unsigned)((instruction >> 6) & 0x3u);
  const unsigned mode = (unsigned)((instruction >> 3) & 0x7u);
  const unsigned reg = (unsigned)(instruction & 0x7u);

  if (size_field != 0x3u) {
    /* ADDQ or SUBQ: bit 8 is the direction, bits 11-9 the quick data. */
    quick.kind = ((instruction >> 8) & 1u) ? AP_M68030_QUICK_SUBQ
                                           : AP_M68030_QUICK_ADDQ;

    const unsigned data = (unsigned)((instruction >> 9) & 0x7u);
    /* "zero represents eight" -- resolved here so no caller can forget. */
    quick.data = (data == 0u) ? 8u : data;

    /* "00 - Byte operation, 01 - Word operation, 10 - Long operation." */
    quick.size = 1u << size_field;
    quick.ea = ap_m68030_ea_decode(mode, reg);
    return quick;
  }

  /* Size field 11 is not a legal ADDQ/SUBQ size, and that spare encoding is
   * the conditional group. Bit 8 now belongs to the condition. */
  quick.condition = (ap_m68030_cond_t)((instruction >> 8) & 0xFu);

  if (mode == 0x1u) {
    /* Address register direct, which Scc could never use: DBcc lives here. */
    quick.kind = AP_M68030_QUICK_DBCC;
    quick.reg = reg;
    return quick;
  }

  if (mode == 0x7u && (reg == 0x2u || reg == 0x3u || reg == 0x4u)) {
    /* Mode 111 with the low bits read as an opmode rather than a sub-opcode. */
    quick.kind = AP_M68030_QUICK_TRAPCC;
    quick.form = (ap_m68030_trapcc_form_t)reg;
    return quick;
  }

  quick.ea = ap_m68030_ea_decode(mode, reg);
  if (quick.ea.kind == AP_M68030_EA_INVALID) {
    quick.kind = AP_M68030_QUICK_INVALID;
    return quick;
  }
  quick.kind = AP_M68030_QUICK_SCC;
  return quick;
}

unsigned ap_m68030_quick_length(const ap_m68030_quick_t *quick) {
  switch (quick->kind) {
  case AP_M68030_QUICK_DBCC:
    /* "16-BIT DISPLACEMENT" always follows. */
    return 4;
  case AP_M68030_QUICK_TRAPCC:
    switch (quick->form) {
    case AP_M68030_TRAPCC_WORD:
      return 4;
    case AP_M68030_TRAPCC_LONG:
      return 6;
    case AP_M68030_TRAPCC_NONE:
      return 2;
    }
    return 2;
  case AP_M68030_QUICK_ADDQ:
  case AP_M68030_QUICK_SUBQ:
  case AP_M68030_QUICK_SCC:
  case AP_M68030_QUICK_INVALID:
    return 2;
  }
  return 2;
}

bool ap_m68030_dbcc_taken(bool condition_true,
                          uint16_t counter_after_decrement) {
  /* "If Condition False Then (Dn - 1 -> Dn; If Dn != -1 Then PC + dn -> PC)".
   * The condition being *true* exits the loop without touching the counter. */
  if (condition_true) {
    return false;
  }
  /* The decrement has already happened; the branch is taken unless it reached
   * -1. Testing the post-decrement value against $FFFF is what makes a starting
   * count of zero run once and stop, rather than wrapping to 65535. */
  return counter_after_decrement != 0xFFFFu;
}

/* MC68030 family 0000. See ap_m68030_immediate.h for how bit 8 splits the
 * family and why MOVEP lives inside the dynamic bit operations. */

#include "cpu/m68030/ap_m68030_immediate.h"

/* The CCR and SR forms of ORI, ANDI and EORI, indexed by size field. */
static ap_m68030_immediate_kind_t special_form(unsigned row, unsigned size_field) {
  if (size_field == 0u) { /* byte: the CCR form */
    switch (row) {
    case 0x0u: return AP_M68030_IMM_ORI_TO_CCR;
    case 0x1u: return AP_M68030_IMM_ANDI_TO_CCR;
    case 0x5u: return AP_M68030_IMM_EORI_TO_CCR;
    default: return AP_M68030_IMM_INVALID;
    }
  }
  if (size_field == 1u) { /* word: the SR form */
    switch (row) {
    case 0x0u: return AP_M68030_IMM_ORI_TO_SR;
    case 0x1u: return AP_M68030_IMM_ANDI_TO_SR;
    case 0x5u: return AP_M68030_IMM_EORI_TO_SR;
    default: return AP_M68030_IMM_INVALID;
    }
  }
  return AP_M68030_IMM_INVALID;
}

ap_m68030_immediate_t ap_m68030_immediate_decode(uint16_t instruction) {
  ap_m68030_immediate_t out = {.kind = AP_M68030_IMM_INVALID};

  if ((instruction & 0xF000u) != 0x0000u) {
    return out;
  }

  const unsigned row = (unsigned)((instruction >> 9) & 0x7u);
  const unsigned opmode = (unsigned)((instruction >> 6) & 0x7u);
  const unsigned size_field = (unsigned)((instruction >> 6) & 0x3u);
  const unsigned mode = (unsigned)((instruction >> 3) & 0x7u);
  const unsigned reg = (unsigned)(instruction & 0x7u);

  if ((instruction & 0x0100u) != 0u) {
    /* Bit 8 set: a dynamic bit operation, or MOVEP sharing its space. */
    if (mode == 0x1u) {
      /* A bit operation cannot address an address register, so this is MOVEP.
       * Opmodes 100 and 101 move memory to the register, 110 and 111 the
       * register to memory; the low bit of the opmode is the size. */
      out.kind = AP_M68030_IMM_MOVEP;
      out.reg = row;
      out.address_register = reg;
      out.movep_to_memory = (opmode >= 0x6u);
      out.size = (opmode & 1u) ? 4u : 2u;
      return out;
    }

    out.dynamic = true;
    out.reg = row;
    switch (opmode) {
    case 0x4u: out.kind = AP_M68030_IMM_BTST; break;
    case 0x5u: out.kind = AP_M68030_IMM_BCHG; break;
    case 0x6u: out.kind = AP_M68030_IMM_BCLR; break;
    case 0x7u: out.kind = AP_M68030_IMM_BSET; break;
    default: return out; /* invalid */
    }
    out.ea = ap_m68030_ea_decode(mode, reg);
    if (out.ea.kind == AP_M68030_EA_INVALID) {
      out.kind = AP_M68030_IMM_INVALID;
    }
    return out;
  }

  /* Bit 8 clear. Row 100 is the static bit operations, whose size field is
   * instead the operation selector. */
  if (row == 0x4u) {
    switch (size_field) {
    case 0x0u: out.kind = AP_M68030_IMM_BTST; break;
    case 0x1u: out.kind = AP_M68030_IMM_BCHG; break;
    case 0x2u: out.kind = AP_M68030_IMM_BCLR; break;
    case 0x3u: out.kind = AP_M68030_IMM_BSET; break;
    default: break;
    }
    out.ea = ap_m68030_ea_decode(mode, reg);
    if (out.ea.kind == AP_M68030_EA_INVALID) {
      out.kind = AP_M68030_IMM_INVALID;
    }
    return out;
  }

  /* An immediate destination is meaningless, so that encoding carries the CCR
   * and SR forms instead -- with the size field choosing between them. */
  if (mode == 0x7u && reg == 0x4u) {
    out.kind = special_form(row, size_field);
    return out;
  }

  /* Size field 11 in these rows is CMP2/CHK2, CAS and CAS2, which are not
   * decoded here yet; reporting invalid is honest, mis-decoding them as a
   * wider ORI would not be. */
  if (size_field == 0x3u) {
    return out;
  }

  switch (row) {
  case 0x0u: out.kind = AP_M68030_IMM_ORI; break;
  case 0x1u: out.kind = AP_M68030_IMM_ANDI; break;
  case 0x2u: out.kind = AP_M68030_IMM_SUBI; break;
  case 0x3u: out.kind = AP_M68030_IMM_ADDI; break;
  case 0x5u: out.kind = AP_M68030_IMM_EORI; break;
  case 0x6u: out.kind = AP_M68030_IMM_CMPI; break;
  case 0x7u: out.kind = AP_M68030_IMM_MOVES; break;
  default: return out;
  }
  out.size = 1u << size_field;
  out.ea = ap_m68030_ea_decode(mode, reg);
  if (out.ea.kind == AP_M68030_EA_INVALID) {
    out.kind = AP_M68030_IMM_INVALID;
  }
  return out;
}

bool ap_m68030_immediate_privileged(ap_m68030_immediate_kind_t kind) {
  switch (kind) {
  /* These write the status register's system byte. */
  case AP_M68030_IMM_ORI_TO_SR:
  case AP_M68030_IMM_ANDI_TO_SR:
  case AP_M68030_IMM_EORI_TO_SR:
  /* MOVES reaches an arbitrary address space through SFC/DFC. */
  case AP_M68030_IMM_MOVES:
    return true;
  case AP_M68030_IMM_ORI:
  case AP_M68030_IMM_ANDI:
  case AP_M68030_IMM_SUBI:
  case AP_M68030_IMM_ADDI:
  case AP_M68030_IMM_EORI:
  case AP_M68030_IMM_CMPI:
  case AP_M68030_IMM_ORI_TO_CCR:
  case AP_M68030_IMM_ANDI_TO_CCR:
  case AP_M68030_IMM_EORI_TO_CCR:
  case AP_M68030_IMM_BTST:
  case AP_M68030_IMM_BCHG:
  case AP_M68030_IMM_BCLR:
  case AP_M68030_IMM_BSET:
  case AP_M68030_IMM_MOVEP:
  case AP_M68030_IMM_INVALID:
    return false;
  }
  return false;
}

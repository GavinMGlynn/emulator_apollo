/* MC68030 family 0100: LEA, CHK and the $48/$4C subtree. See
 * ap_m68030_misc.h for why the register-direct cases must be decoded first. */

#include "cpu/m68030/ap_m68030_misc.h"

ap_m68030_misc_t ap_m68030_misc_decode(uint16_t instruction) {
  ap_m68030_misc_t misc = {.kind = AP_M68030_MISC_INVALID};

  if ((instruction & 0xF000u) != 0x4000u) {
    return misc;
  }

  const unsigned opmode = (unsigned)((instruction >> 6) & 0x7u);
  const unsigned mode = (unsigned)((instruction >> 3) & 0x7u);
  const unsigned reg = (unsigned)(instruction & 0x7u);
  const unsigned register_field = (unsigned)((instruction >> 9) & 0x7u);

  /* LEA and CHK share the register-field form, bits 8-6 choosing between them.
   * The 68020's long CHK sits *below* the word form rather than above it. */
  if (opmode == 0x7u) {
    /* LEA loads an *address*, so a data register source is meaningless -- and
     * that hole is EXTB.L, the 68020's byte-to-long. Its own encoding fixes
     * bits 11-9 at 100, so the register field is not free here either. This
     * must be tested before LEA, exactly as SWAP must be tested before PEA. */
    if (mode == 0x0u && register_field == 0x4u) {
      misc.kind = AP_M68030_MISC_EXTB_LONG;
      misc.reg = reg;
      return misc;
    }
    misc.kind = AP_M68030_MISC_LEA;
    misc.reg = register_field;
    misc.ea = ap_m68030_ea_decode(mode, reg);
    return misc;
  }
  if (opmode == 0x6u || opmode == 0x4u) {
    misc.kind = (opmode == 0x6u) ? AP_M68030_MISC_CHK_WORD
                                 : AP_M68030_MISC_CHK_LONG;
    misc.reg = register_field;
    misc.ea = ap_m68030_ea_decode(mode, reg);
    return misc;
  }

  /* Everything below is the $48/$4C subtree, which needs bit 11 set. */
  if ((instruction & 0x0800u) == 0u) {
    return misc;
  }

  /* MOVEM: bits 9-7 are 001, bit 10 the direction, bit 6 the size. Decoded
   * before the narrower $48xx forms would be, but *after* the register-direct
   * cases those forms occupy -- see below. */
  const bool movem_shape = ((instruction & 0x0380u) == 0x0080u);

  /* $4840-$487F is PEA, except where PEA cannot go. */
  if ((instruction & 0x0FC0u) == 0x0840u) {
    if (mode == 0x0u) {
      /* PEA cannot push a data register, so SWAP lives here. */
      misc.kind = AP_M68030_MISC_SWAP;
      misc.reg = reg;
      return misc;
    }
    if (mode == 0x1u) {
      /* Nor an address register: BKPT, whose low bits are its vector. */
      misc.kind = AP_M68030_MISC_BKPT;
      misc.reg = reg;
      return misc;
    }
    misc.kind = AP_M68030_MISC_PEA;
    misc.ea = ap_m68030_ea_decode(mode, reg);
    return misc;
  }

  /* $4800-$483F is NBCD. */
  if ((instruction & 0x0FC0u) == 0x0800u) {
    misc.kind = AP_M68030_MISC_NBCD;
    misc.ea = ap_m68030_ea_decode(mode, reg);
    return misc;
  }

  if (movem_shape) {
    /* MOVEM cannot take a data register operand, so mode 000 is EXT. This test
     * must come first: checking the MOVEM shape alone would decode EXT.W D3 as
     * a register-list move. */
    if (mode == 0x0u) {
      /* EXT's encoding fixes bits 11-9 at 100, which is MOVEM's
       * registers-to-memory direction. The memory-to-registers direction puts 6
       * there, and no EXT exists in it -- so a data register operand is simply
       * invalid rather than another EXT. */
      if (register_field != 0x4u) {
        return misc;
      }
      switch (opmode) {
      case 0x2u:
        misc.kind = AP_M68030_MISC_EXT_WORD;
        misc.reg = reg;
        return misc;
      case 0x3u:
        misc.kind = AP_M68030_MISC_EXT_LONG;
        misc.reg = reg;
        return misc;
      default:
        return misc; /* invalid */
      }
    }

    misc.kind = ((instruction >> 10) & 1u) ? AP_M68030_MISC_MOVEM_TO_REGISTERS
                                           : AP_M68030_MISC_MOVEM_TO_MEMORY;
    /* "SIZE" is one bit here: clear is word, set is long. */
    misc.size = ((instruction >> 6) & 1u) ? 4u : 2u;
    misc.ea = ap_m68030_ea_decode(mode, reg);
    return misc;
  }

  return misc;
}

unsigned ap_m68030_misc_length(const ap_m68030_misc_t *misc) {
  switch (misc->kind) {
  case AP_M68030_MISC_MOVEM_TO_MEMORY:
  case AP_M68030_MISC_MOVEM_TO_REGISTERS:
    return 4; /* the 16-bit register list mask */
  case AP_M68030_MISC_LEA:
  case AP_M68030_MISC_CHK_WORD:
  case AP_M68030_MISC_CHK_LONG:
  case AP_M68030_MISC_PEA:
  case AP_M68030_MISC_SWAP:
  case AP_M68030_MISC_BKPT:
  case AP_M68030_MISC_EXT_WORD:
  case AP_M68030_MISC_EXT_LONG:
  case AP_M68030_MISC_EXTB_LONG:
  case AP_M68030_MISC_NBCD:
  case AP_M68030_MISC_INVALID:
    return 2;
  }
  return 2;
}

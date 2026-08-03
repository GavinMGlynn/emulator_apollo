/* MC68030 arithmetic and logic families. See ap_m68030_arith.h for the shape
 * the five share and for what fills the register-register holes. */

#include "cpu/m68030/ap_m68030_arith.h"

ap_m68030_arith_t ap_m68030_arith_decode(uint16_t instruction) {
  ap_m68030_arith_t out = {.kind = AP_M68030_ARITH_INVALID};

  const unsigned family = (unsigned)((instruction >> 12) & 0xFu);
  const unsigned reg = (unsigned)((instruction >> 9) & 0x7u);
  const unsigned opmode = (unsigned)((instruction >> 6) & 0x7u);
  const unsigned mode = (unsigned)((instruction >> 3) & 0x7u);
  const unsigned ea_reg = (unsigned)(instruction & 0x7u);

  out.reg = reg;
  out.source_reg = ea_reg;
  out.memory_operands = ((instruction >> 3) & 1u) != 0u;

  /* The wide forms: opmode 011 is word, 111 is long. What they *are* depends on
   * the family -- a divide or multiply in 1000 and 1100, an address register
   * form in the other three. */
  if (opmode == 0x3u || opmode == 0x7u) {
    const bool longword = (opmode == 0x7u);
    switch (family) {
    case 0x8u:
      out.kind = longword ? AP_M68030_ARITH_DIVS : AP_M68030_ARITH_DIVU;
      out.size = 2; /* both divide a long by a word */
      break;
    case 0xCu:
      out.kind = longword ? AP_M68030_ARITH_MULS : AP_M68030_ARITH_MULU;
      out.size = 2;
      break;
    case 0x9u:
      out.kind = AP_M68030_ARITH_SUBA;
      out.size = longword ? 4u : 2u;
      break;
    case 0xBu:
      out.kind = AP_M68030_ARITH_CMPA;
      out.size = longword ? 4u : 2u;
      break;
    case 0xDu:
      out.kind = AP_M68030_ARITH_ADDA;
      out.size = longword ? 4u : 2u;
      break;
    default:
      return out;
    }
    out.ea = ap_m68030_ea_decode(mode, ea_reg);
    if (out.ea.kind == AP_M68030_EA_INVALID) {
      out.kind = AP_M68030_ARITH_INVALID;
    }
    return out;
  }

  if (opmode > 0x7u) {
    return out;
  }

  out.to_effective_address = (opmode >= 0x4u);
  out.size = 1u << (opmode & 0x3u);

  if (out.to_effective_address) {
    /* These write to the effective address, so a register destination is not a
     * memory destination -- and each family fills that hole differently.
     *
     * **`EOR` is the exception, and it is not a hole at all.** Four of the five
     * say "only memory alterable addressing modes can be used" of their
     * destination, which is what makes `OR Dn,Dn` unencodable and leaves room
     * for `SBCD`, `SUBX`, `ADDX` and `ABCD`. `EOR`'s page says **data**
     * alterable, so `EOR Dn,Dn` is an ordinary instruction -- and a common one.
     * Family B at mode 000 therefore skips this block and decodes normally.
     * Treating all five alike made a legal instruction illegal. */
    const bool eor_to_data_register = family == 0xBu && mode == 0x0u;
    if ((mode == 0x0u || mode == 0x1u) && !eor_to_data_register) {
      switch (family) {
      case 0x8u:
        if (opmode == 0x4u) {
          out.kind = AP_M68030_ARITH_SBCD;
          out.size = 1;
          return out;
        }
        break;
      case 0x9u:
        out.kind = AP_M68030_ARITH_SUBX;
        return out;
      case 0xDu:
        out.kind = AP_M68030_ARITH_ADDX;
        return out;
      case 0xBu:
        /* CMPM is mode 001 only -- postincrement of both operands. */
        if (mode == 0x1u) {
          out.kind = AP_M68030_ARITH_CMPM;
          return out;
        }
        break;
      case 0xCu:
        if (opmode == 0x4u) {
          out.kind = AP_M68030_ARITH_ABCD;
          out.size = 1;
          return out;
        }
        /* EXG's opmodes are 01000, 01001 and 10001 across bits 7-3, and which
         * one decides whether the pair is data, address, or one of each. */
        if (opmode == 0x5u) {
          out.kind = AP_M68030_ARITH_EXG;
          out.size = 4;
          out.exg = (mode == 0x0u) ? AP_M68030_EXG_DATA : AP_M68030_EXG_ADDRESS;
          return out;
        }
        if (opmode == 0x6u && mode == 0x1u) {
          out.kind = AP_M68030_ARITH_EXG;
          out.size = 4;
          /* "If the exchange is between data and address registers, this field
           * always specifies the data register" of Rx, and the address register
           * of Ry -- so the roles are fixed by the encoding, not by order. */
          out.exg = AP_M68030_EXG_MIXED;
          return out;
        }
        break;
      default:
        break;
      }
      /* No special form here, and a register is not a legal destination for the
       * ordinary instruction either. */
      return out;
    }
  }

  switch (family) {
  case 0x8u:
    out.kind = AP_M68030_ARITH_OR;
    break;
  case 0x9u:
    out.kind = AP_M68030_ARITH_SUB;
    break;
  case 0xBu:
    /* CMP and EOR do not overlap: CMP takes the register direction, EOR the
     * memory one. `EOR Dn,Dn` reaches here rather than the hole above, because
     * EOR's destination is data alterable. */
    out.kind = out.to_effective_address ? AP_M68030_ARITH_EOR
                                        : AP_M68030_ARITH_CMP;
    break;
  case 0xCu:
    out.kind = AP_M68030_ARITH_AND;
    break;
  case 0xDu:
    out.kind = AP_M68030_ARITH_ADD;
    break;
  default:
    return out;
  }

  out.ea = ap_m68030_ea_decode(mode, ea_reg);
  if (out.ea.kind == AP_M68030_EA_INVALID) {
    out.kind = AP_M68030_ARITH_INVALID;
  }
  return out;
}

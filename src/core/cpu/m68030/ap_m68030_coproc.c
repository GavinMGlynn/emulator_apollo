/* MC68030 family 1111. See ap_m68030_coproc.h for why an unsupported cpID-0
 * instruction takes a different vector in user and supervisor state. */

#include "cpu/m68030/ap_m68030_coproc.h"

#include "cpu/m68030/ap_m68030_exception.h"

ap_m68030_coproc_t ap_m68030_coproc_decode(uint16_t instruction) {
  ap_m68030_coproc_t out = {0};

  if ((instruction & 0xF000u) != 0xF000u) {
    return out;
  }

  out.valid = true;
  out.cpid = (unsigned)((instruction >> 9) & 0x7u);
  out.is_mmu = (out.cpid == AP_M68030_CPID_MMU);

  /* Bits 5-0 are the effective address field for the types that take one, which
   * on this part is how PMOVE, PTEST and PFLUSH name their operand. Only
   * "control alterable addressing modes" are legal there -- a restriction the
   * executor applies, since this reports what the field says. */
  out.ea = ap_m68030_ea_decode((unsigned)((instruction >> 3) & 0x7u),
                               (unsigned)(instruction & 0x7u));

  const unsigned type = (unsigned)((instruction >> 6) & 0x7u);
  out.type = (type <= 0x5u) ? (ap_m68030_coproc_type_t)type
                            : AP_M68030_CP_RESERVED;
  return out;
}

unsigned ap_m68030_coproc_unsupported_vector(const ap_m68030_coproc_t *coproc,
                                             bool supervisor) {
  if (!coproc->valid) {
    return 0;
  }

  /* "F-line instructions with a CpID other than zero are executed as
   * coprocessor instructions by the MC68030" -- an unsupported one is simply
   * unimplemented, with no privilege rule attached. */
  if (!coproc->is_mmu) {
    return AP_M68030_VECTOR_LINE_F;
  }

  /* Coprocessor zero is the MMU, and the vector depends on where the attempt
   * came from: F-line from supervisor state, privilege violation from user
   * state. Reporting F-line in both would let a user program tell
   * "unimplemented" from "not allowed". */
  return supervisor ? AP_M68030_VECTOR_LINE_F
                    : AP_M68030_VECTOR_PRIVILEGE_VIOLATION;
}

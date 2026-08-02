/* Family-aware instruction decode. See the header for why this wraps the
 * shared decoder rather than replacing or parameterising it. */

#include "cpu/m68020/ap_m68020_decode.h"

ap_cpu_decoded_t ap_cpu_decode(uint16_t instruction, ap_cpu_t family) {
  ap_cpu_decoded_t out = {.base = ap_m68030_decode(instruction)};

  if (!ap_cpu_features(family).has_module_calls) {
    /* Every other family: the shared decoder's verdict stands, and for
     * `$06C0`-`$06FF` that verdict is illegal. */
    return out;
  }

  const ap_m68020_module_decode_t module =
      ap_m68020_module_decode(instruction);
  if (module.opcode == AP_M68020_MODULE_NOT_A_MODULE_INSTRUCTION) {
    return out;
  }

  /* Only ever an *upgrade* from illegal. If the shared decoder claimed the
   * word for a real instruction then the module decoder has overreached, and
   * the shared decoder is the one that has seen every family's table --
   * deferring to it here is what keeps this wrapper from inventing overlaps. */
  if (out.base.kind != AP_M68030_DECODED_ILLEGAL) {
    return out;
  }

  out.is_module_call = true;
  out.module = module;
  return out;
}

bool ap_cpu_instruction_is_illegal(uint16_t instruction, ap_cpu_t family) {
  const ap_cpu_decoded_t decoded = ap_cpu_decode(instruction, family);
  if (decoded.is_module_call) {
    return false;
  }
  return decoded.base.kind == AP_M68030_DECODED_ILLEGAL;
}

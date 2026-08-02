/* Family-aware instruction decode: what a 68020 decodes that a 68030 does not.
 *
 * ## Why this is a wrapper and not a second decoder
 *
 * The two parts share an instruction set save for two opcodes. Copying the
 * 68030's decoder to add them would leave two tables to keep in step, and the
 * one that is not exercised by a booting machine would drift; threading a
 * family argument through `ap_m68030_decode()` would put 68020 variance inside
 * the 68030's module, which is the thing `CLAUDE.md` forbids. So this asks the
 * shared decoder first and reinterprets exactly what the family changes.
 *
 * ## What the family changes is one word range
 *
 * `CALLM` and `RTM` occupy `$06C0`-`$06FF`, which the 68030 decodes as illegal
 * because it has no such instructions. On a 68020 they are real, so the same
 * word must fault on a DN3500 and execute on a DN3000 -- the sharpest CPU
 * family difference in the core, and the reason `ap_cpu_features()` carries
 * `has_module_calls` rather than the machine table carrying a flag.
 *
 * Everything else the 68020 lacks -- the on-chip MMU, the second cache, burst
 * termination -- is absent from its *hardware* rather than its instruction set,
 * and is expressed elsewhere. Only the module calls reach the decoder.
 */

#ifndef APOLLO_CPU_M68020_AP_M68020_DECODE_H
#define APOLLO_CPU_M68020_AP_M68020_DECODE_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68020/ap_m68020_module.h"
#include "cpu/m68030/ap_m68030_decode.h"
#include "model/ap_model.h"

typedef struct {
  /* The shared decoder's verdict, unchanged except where the family overrides
   * it. A caller that only needs to know "is this legal here" can read this. */
  ap_m68030_decoded_t base;
  /* Set when this family turns the shared decoder's verdict into a module
   * call. `base.kind` is left as the shared decoder reported it, so nothing
   * downstream can mistake a module call for an instruction it understands. */
  bool is_module_call;
  ap_m68020_module_decode_t module;
} ap_cpu_decoded_t;

/* Decode for a given CPU family. */
[[nodiscard]] ap_cpu_decoded_t ap_cpu_decode(uint16_t instruction,
                                             ap_cpu_t family);

/* Whether this family would take an illegal instruction exception for the
 * word. The question the step actually asks, and the one that differs between
 * a DN3000 and a DN3500 for exactly two opcodes. */
[[nodiscard]] bool ap_cpu_instruction_is_illegal(uint16_t instruction,
                                                 ap_cpu_t family);

#endif /* APOLLO_CPU_M68020_AP_M68020_DECODE_H */

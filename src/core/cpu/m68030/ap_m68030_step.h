/* MC68030 instruction step: fetch, decode, execute, advance.
 *
 * The loop that ties the pieces together. Everything below it -- the pipe, the
 * caches, the MMU, the bus, the decoders, the address calculation -- is built
 * and tested; this is what makes a program run through them.
 *
 * ## Unimplemented is not illegal, and must never look like it
 *
 * Only a named subset of instructions has semantics so far. The step reports an
 * instruction it decoded but cannot execute as `UNIMPLEMENTED`, which is a
 * distinct outcome from `ILLEGAL`. That distinction is the whole reason this
 * module can be committed while incomplete:
 *
 *   - Silently doing nothing would make a program appear to run while producing
 *     wrong results, and the clock count would be wrong too.
 *   - Reporting it as illegal would be a lie about the *hardware* -- the
 *     instruction is perfectly legal, this model just has not got to it -- and
 *     would send a probe down an exception path the real machine never takes.
 *
 * So an unimplemented instruction stops the step and says so, and the PC does
 * not advance past it. A caller can count how far a program got, which is a
 * useful measure of progress and a useless one to fake.
 *
 * ## What executes today
 *
 * `NOP`, `MOVEQ`, the 8-bit forms of `BRA` and `Bcc`, and `MOVE`/`MOVEA` in the
 * addressing modes that need no extension word -- register direct, `(An)`,
 * `(An)+` and `-(An)`.
 *
 * The extension-word modes are excluded for a concrete reason rather than an
 * arbitrary one: the step does not yet fetch extension words from the
 * instruction stream, and MOVE is the instruction that makes that hard, since
 * its destination's extension words sit after its source's. Reporting those
 * modes unimplemented is honest; guessing at a displacement of zero would run
 * and be wrong.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_STEP_H
#define APOLLO_CPU_M68030_AP_M68030_STEP_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_decode.h"
#include "cpu/m68030/ap_m68030_fetch.h"
#include "cpu/m68030/ap_m68030_regs.h"

typedef enum {
  AP_M68030_STEP_EXECUTED,
  AP_M68030_STEP_UNIMPLEMENTED, /* decoded, but this model has no semantics */
  AP_M68030_STEP_ILLEGAL,       /* the hardware would fault too */
  AP_M68030_STEP_FAULT,         /* a memory fault during fetch */
} ap_m68030_step_status_t;

typedef struct {
  ap_m68030_regs_t regs;
  ap_m68030_fetch_t fetch;
  /* The data side of the machine, which is a *different* cache from the
   * instruction side even when it shares a memory system. */
  ap_m68030_access_ctx_t *data;
  uint8_t data_function_code;
  uint64_t clocks; /* accumulated across steps */
} ap_m68030_cpu_t;

typedef struct {
  ap_m68030_step_status_t status;
  uint32_t clocks;               /* this step's cost */
  uint16_t instruction;          /* the word that was decoded */
  ap_m68030_decoded_kind_t kind;
  bool branch_taken;
} ap_m68030_step_result_t;

/* Point the processor at an address, emptying the pipe. */
void ap_m68030_cpu_reset(ap_m68030_cpu_t *cpu, uint32_t pc);

/* Execute one instruction. */
[[nodiscard]] ap_m68030_step_result_t ap_m68030_step(ap_m68030_cpu_t *cpu);

#endif /* APOLLO_CPU_M68030_AP_M68030_STEP_H */

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
 * `NOP`, `MOVEQ`, the 8-bit forms of `BRA` and `Bcc`, `MOVE`/`MOVEA`, the six
 * ALU operations in both directions, the `xxxI` immediate forms,
 * `CLR`/`NEG`/`NOT`/`TST`, `ADDQ`/`SUBQ`/`Scc`/`DBcc`, `ADDA`/`SUBA`/`CMPA`,
 * the bit operations, the shifts and rotates, the multiplies and divides, and
 * the extended forms `ADDX`/`SUBX`/`ABCD`/`SBCD`/`CMPM`/`EXG`.
 *
 * Still excluded, each for a concrete reason rather than an arbitrary one: the
 * full-format indexed modes, whose extension word declares its own
 * displacement sizes so the word count is not known until it is decoded; the
 * memory indirect modes, which need a bus read partway through the address
 * calculation; and the instructions whose semantics are exception processing
 * itself. Reporting those unimplemented is honest; guessing would run and be
 * wrong.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_STEP_H
#define APOLLO_CPU_M68030_AP_M68030_STEP_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_cache.h"
#include "cpu/m68030/ap_m68030_decode.h"
#include "cpu/m68030/ap_m68030_fetch.h"
#include "cpu/m68030/ap_m68030_regs.h"

typedef enum {
  AP_M68030_STEP_EXECUTED,
  AP_M68030_STEP_UNIMPLEMENTED, /* decoded, but this model has no semantics */
  AP_M68030_STEP_ILLEGAL,       /* the hardware would fault too */
  AP_M68030_STEP_FAULT,         /* a memory fault during fetch */
  /* The instruction ran and raised an exception: the frame is stacked and the
   * PC is the handler's. Distinct from EXECUTED because the caller's idea of
   * "where did the program get to" is now the handler and not the next
   * instruction, and distinct from ILLEGAL because the processor is in a
   * defined state rather than stopped. */
  AP_M68030_STEP_EXCEPTION,
  /* The processor is stopped: STOP ran, and nothing executes until an interrupt
   * or a reset. Distinct from every other outcome because no instruction was
   * fetched -- a caller looping on "did that execute" must stop looping. */
  AP_M68030_STEP_STOPPED,
} ap_m68030_step_status_t;

typedef struct {
  ap_m68030_regs_t regs;
  ap_m68030_fetch_t fetch;
  /* The data side of the machine, which is a *different* cache from the
   * instruction side even when it shares a memory system. */
  ap_m68030_access_ctx_t *data;
  uint8_t data_function_code;
  uint64_t clocks; /* accumulated across steps */

  /* The cache control and cache address registers, which are CPU state rather
   * than cache state: MOVEC reaches them, and writing CACR performs the cache
   * clears the write requests. Keeping them here rather than inside either
   * cache is what lets one write touch both. */
  ap_m68030_cacr_t cacr;
  uint32_t caar;

  /* "STOP ... Immediate Data -> SR; STOP". The processor "stops fetching and
   * executing instructions" until an interrupt or a reset -- so this is a state
   * a step can be in, not something a step does. */
  bool stopped;

  /* RESET "asserts the RSTO signal ... resetting all external devices" without
   * affecting the processor. Counted rather than acted on: this module has no
   * external devices yet, and a count is what a test can observe. */
  unsigned external_resets;

  /* An exception an executing instruction raised, held until the step can take
   * it: an executor knows a divide had a zero divisor, but not the length of
   * the instruction it is inside, and Table 8-6 wants both the faulting
   * instruction's address and the next one's. Zero means none -- vector 0 is
   * the reset stack pointer, which no instruction can raise. */
  unsigned pending_vector;
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

/* ---------------------------------------------------------------------------
 * Taking an exception, `[030]` §8.1, "Exception Processing Sequence".
 *
 * The manual's four steps, and their order is the whole of the difficulty:
 *
 *   1. "The processor makes an internal copy of the status register. Then the
 *      processor sets the S bit, changing to the supervisor privilege level.
 *      Next, the processor inhibits tracing of the exception handler by
 *      clearing the T1 and T0 bits."
 *   2. Determine the vector number -- the caller's job, since for interrupts it
 *      comes off the bus and for the rest from internal logic.
 *   3. "The processor creates an exception stack frame on the active supervisor
 *      stack" -- *active*, so after step 1 has set S, and it is the copy from
 *      step 1 that is stacked, not the modified register.
 *   4. Multiply the vector by four, add the VBR, load the PC from there.
 *
 * Stacking the modified SR instead of the copy is the mistake that survives
 * casual testing: the handler runs correctly and RTE returns with S still set,
 * so a user program that trapped comes back in supervisor state. Nothing
 * faults; the privilege boundary is simply gone.
 *
 * `stacked_pc` is what the frame's PC field gets, and Table 8-6 is explicit
 * that it differs per exception -- the *next* instruction for TRAP and the
 * interrupts, the faulting instruction itself for illegal, A-line and F-line,
 * and "first word of instruction causing Privilege Violation". Passing it in
 * rather than deriving it here is deliberate: only the caller knows which.
 *
 * `instruction_address` fills the six-word frame's extra long word, "the
 * address of the instruction that caused the exception", and is ignored for the
 * four-word frame.
 *
 * Not handled here, and each declined rather than approximated: reset, which
 * stacks nothing at all; the bus and address error frames, which carry internal
 * state this model does not have; the coprocessor mid-instruction frame; and
 * the interrupt case where "the M bit of the status register is set" and a
 * second, throwaway frame goes on the interrupt stack -- that belongs with the
 * interrupt item, along with the priority mask update.
 * ------------------------------------------------------------------------- */

typedef struct {
  bool ok;
  uint32_t clocks;
  uint32_t frame_address;  /* where the frame was built: the new A7 */
  uint32_t vector_address; /* VBR + vector * 4 */
  uint32_t handler;        /* the PC that was loaded */
} ap_m68030_exception_result_t;

[[nodiscard]] ap_m68030_exception_result_t
ap_m68030_take_exception(ap_m68030_cpu_t *cpu, unsigned vector,
                         uint32_t stacked_pc, uint32_t instruction_address);

#endif /* APOLLO_CPU_M68030_AP_M68030_STEP_H */

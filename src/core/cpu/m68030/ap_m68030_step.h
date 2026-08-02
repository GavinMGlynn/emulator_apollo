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
 * ## Nor may a fault look unimplemented
 *
 * The same argument runs the other way, and for a long time this module only
 * honoured half of it. Every executor signals failure by returning `false`, and
 * two entirely different things arrive that way: an instruction with no
 * semantics, and an instruction whose *operand access faulted*. Reporting both
 * as `UNIMPLEMENTED` blames the processor for the memory system's answer.
 *
 * That is not a cosmetic mislabel. It sent an investigation of the DN3500 boot
 * PROM after a `CMPI.B` that had been implemented and tested for weeks, when
 * what had actually happened was a read of an address this machine does not
 * decode. The status is the only thing a caller has to go on, and a status that
 * names the wrong subsystem is worse than no status at all.
 *
 * `access_faulted` on the CPU carries the distinction from the access, which is
 * the last place that still knows, to the status, which is where it is needed.
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
#include "cpu/m68030/ap_m68030_tc.h"
#include "cpu/m68030/ap_m68030_tt.h"
#include "cpu/m68030/ap_m68030_walk.h"
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

/* What an interrupt acknowledge cycle returned. `[030]` §8.1.9: the processor
 * "attempts to obtain a vector number from the interrupting device using an
 * interrupt acknowledge bus cycle"; "For a device that cannot supply an
 * interrupt vector, the autovector signal (AVEC) can be asserted"; and "If
 * external logic indicates a bus error during the interrupt acknowledge cycle,
 * the interrupt is considered spurious". Three outcomes, so three fields rather
 * than a vector number with two magic values. */
typedef struct {
  bool autovector; /* AVEC: use vector 24 + level */
  bool bus_error;  /* spurious: vector 24 */
  unsigned vector; /* the device's own vector, when neither */
} ap_m68030_iack_t;

typedef ap_m68030_iack_t (*ap_m68030_iack_fn)(void *context, unsigned level);

typedef struct {
  ap_m68030_regs_t regs;
  ap_m68030_fetch_t fetch;
  /* The data side of the machine, which is a *different* cache from the
   * instruction side even when it shares a memory system. */
  ap_m68030_access_ctx_t *data;
  uint8_t data_function_code;
  uint64_t clocks; /* accumulated across steps */

  /* The MMU registers, which `PMOVE` writes and reads. They live here because
   * there is one MMU and two access paths through it: a caller that wants
   * translation to follow a `PMOVE` points both access contexts' `tc`, `root`,
   * `tt0` and `tt1` at these rather than at storage of its own.
   *
   * `crp` and `srp` are root pointer *descriptors* -- "The field descriptions in
   * the preceding section apply to corresponding fields of the CRP and SRP" --
   * so they are unpacked by the same code that unpacks a long-format table
   * descriptor, rather than by a second transcription of the same layout. */
  ap_m68030_tc_t tc;
  ap_m68030_root_t crp;
  ap_m68030_root_t srp;
  ap_m68030_tt_t tt0;
  ap_m68030_tt_t tt1;
  uint16_t mmusr;

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

  /* Set when an access made *during* an instruction faulted, so that the step
   * can tell a bus that said no from an instruction this model cannot execute.
   *
   * Both arrive at the executors as a plain `false` return, and without this
   * they were reported identically -- as `UNIMPLEMENTED`. That is the one lie
   * this module's whole design is built to avoid: it blames the CPU for the
   * memory system's answer, and it points an investigation at a decoder that is
   * working correctly. The distinction cannot be recovered from the return
   * value, because at the point the executor gives up it no longer has the
   * access result; so it is recorded when the access fails, and read back when
   * the status is chosen.
   *
   * Cleared at the top of every step: it describes this instruction, not the
   * machine, and a stale one would mislabel the next failure. */
  bool access_faulted;

  /* What faulted, which is what the bus fault frame is made of. Recorded at the
   * access rather than reconstructed afterwards: by the time the step chooses a
   * status, the address and size are gone, and a handler given the wrong ones
   * repairs the wrong location.
   *
   * `fault_instruction_stream` separates a prefetch fault from a data cycle
   * fault. They set different halves of the special status word, and a prefetch
   * fault has no data cycle at all -- reporting one as the other tells a handler
   * to repair a data access that never happened. */
  uint32_t fault_address;
  unsigned fault_size;
  bool fault_read;
  uint8_t fault_function_code;
  bool fault_instruction_stream;
  uint32_t fault_data_output; /* the value a faulted write was carrying */

  /* The interrupt request level standing on IPL2-IPL0, and what it was before,
   * which level 7 needs: it is "transition sensitive", so holding the line at 7
   * does not re-interrupt but dropping and raising it does. A caller drives
   * `interrupt_level`; the step maintains `previous_interrupt_level` itself. */
  unsigned interrupt_level;
  unsigned previous_interrupt_level;

  /* The interrupt acknowledge cycle. NULL means no device answers, which is a
   * spurious interrupt rather than a reason not to take one -- the same
   * outcome the hardware reaches when the cycle bus errors. */
  ap_m68030_iack_fn acknowledge;
  void *acknowledge_context;

  /* Extension words this step has taken from the instruction stream. The PC
   * advances by the instruction word plus these, rather than by a length
   * predicted from the instruction word alone -- which cannot be right for the
   * full-format indexed modes, where the extension word declares its own
   * displacement sizes and so its own length. Counting what was actually read
   * makes the fetch and the PC agree by construction.
   *
   * Reset at the start of every step. */
  unsigned extension_words;

  /* Which of §11.6.15's three DBcc cases the last one was, recorded because
   * only the execution knows and only the timing tail needs it. */
  bool dbcc_condition_true;
  bool dbcc_count_expired;

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

/* The reset exception, `[030]` §8.1.1's ten numbered steps.
 *
 * Distinct from `ap_m68030_cpu_reset`, which places a program counter and empties
 * the pipe: that is a harness operation and this is the *exception*, which reads
 * its stack pointer and program counter from the vector at offset zero and puts
 * the whole processor into a defined state on the way.
 *
 * Four of the ten are the ones a plausible implementation drops, and none of
 * them faults when missed:
 *
 *  - "setting the supervisor bit **and clearing the master bit**" -- leaving M
 *    set puts the machine on the master stack when every later exception
 *    expects the interrupt stack.
 *  - "Initializes the vector base register to zero" -- a stale VBR sends every
 *    subsequent vector fetch into whatever the previous system used.
 *  - "Clears the enable, freeze, and burst enable bits for both on-chip caches
 *    **and the write-allocate bit** for the data cache".
 *  - "Clears the enable bit in the translation control register and the enable
 *    bits in **both** transparent translation registers" -- a machine that
 *    reset with translation still on would fetch its first instruction through
 *    a tree the new system has not built.
 *
 * And two explicit negatives, which are as load-bearing as the steps: "The
 * reset exception does **not** flush the address translation cache (ATC), nor
 * does it save the value of either the program counter or the status register."
 * So there is no frame, and the ATC survives -- a model that flushed it would
 * be tidier and wrong.
 *
 * Returns false if the vector could not be read, which is a machine with no
 * memory at zero rather than a defect here. */
[[nodiscard]] bool ap_m68030_take_reset(ap_m68030_cpu_t *cpu);

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

/* Take a bus or address error, building the fault frame from the access the CPU
 * recorded when it faulted.
 *
 * Separate from `ap_m68030_take_exception` because the fault frames are not
 * simply larger: which of the two applies is decided by the special status
 * word rather than by the vector, the stacked PC means different things in
 * each, and the frame carries state that only the faulted access knows. A
 * caller cannot supply any of that, so it is read from the CPU rather than
 * passed in.
 *
 * Declines when nothing has faulted -- a frame built from cleared fault state
 * would send a handler to repair address zero. */
[[nodiscard]] ap_m68030_exception_result_t
ap_m68030_take_bus_fault(ap_m68030_cpu_t *cpu, unsigned vector,
                         uint32_t instruction_address);

/* Take an address error: vector 3, on an attempt to prefetch from an odd
 * program counter.
 *
 * "This exception is similar to a bus error exception, but is internally
 * initiated. A bus cycle is not executed" -- so there is no faulted access to
 * describe, and the frame is built from the program counter alone. The special
 * status word carries the rerun bits *without* the fault bits, which is what
 * distinguishes an address error from a bus error in the frame.
 *
 * Only instruction prefetch reaches here. Misaligned data accesses are legal on
 * the MC68030 and merely cost extra bus cycles. */
[[nodiscard]] ap_m68030_exception_result_t
ap_m68030_take_address_error(ap_m68030_cpu_t *cpu);

/* Take an interrupt, if one is recognised at the current level and mask.
 *
 * `[030]` §8.1.9's order, which differs from every other exception in three
 * ways: the status register copy is taken **before** the interrupt mask is
 * raised -- otherwise RTE restores the *handler's* mask rather than the
 * interrupted code's, and the interrupted code never receives another interrupt
 * at its own level again; the vector comes off the bus rather than from
 * internal logic; and "If the M bit of the status register is set, the processor
 * clears the M bit and creates a throwaway exception stack frame on top of the
 * interrupt stack", so one interrupt can build *two* frames on two different
 * stacks.
 *
 * The throwaway frame "contains the same program counter value and vector
 * offset as the frame created on top of the master stack, but has a format
 * number of 1", and its status register copy "is exactly the same as that placed
 * on the master stack except that the S bit is set".
 *
 * Returns `ok` false when no interrupt is recognised, which is not a failure --
 * it is the ordinary case. */
[[nodiscard]] ap_m68030_exception_result_t
ap_m68030_take_interrupt(ap_m68030_cpu_t *cpu);

#endif /* APOLLO_CPU_M68030_AP_M68030_STEP_H */

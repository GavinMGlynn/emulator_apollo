/* MC68030 three-word instruction pipe and cache holding register.
 *
 * `[030]` §11.2.2, p. 11-2, cited throughout.
 *
 * ## Why this module exists at all
 *
 * `docs/references/M68030_TIMING.md` records the fact the whole CPU phase turns
 * on: Motorola's published no-cache-case instruction times are *the average of
 * the odd-word-aligned and even-word-aligned cases, rounded up* (§11.3.3,
 * p. 11-8). A table-driven emulator reproduces that average, which is a number
 * the hardware never exhibits on any particular run.
 *
 * This is the mechanism that average is taken over. Get it right and alignment
 * stops being a fudge factor and becomes a consequence.
 *
 * ## The pipe
 *
 * "The MC68030 contains a three-word instruction pipe where instruction opcodes
 * are decoded. Instruction words (instruction operation words and all extension
 * words) enter the pipe at stage B and proceed to stages C and D. An
 * instruction word is completely decoded when it reaches stage D."
 *
 * "Each of the pipe stages has a status bit that reflects whether the word in
 * the stage was loaded with data from a bus cycle that was terminated
 * abnormally." That bit is carried here rather than deferred, because a bus
 * error must fault when the *word is used*, not when it was fetched -- a
 * prefetch that faults down a path never executed must not raise anything.
 *
 * "Stages of the pipe are only filled in response to specific prefetch requests
 * issued by the microsequencer." Nothing here prefetches on its own.
 *
 * ## The cache holding register, which is where alignment comes from
 *
 * "While the individual stages of the pipe are only 16 bits wide, the cache
 * holding register is 32 bits wide and contains the entire long word."
 *
 * "When the microsequencer requests an even-word (long-word aligned) prefetch,
 * the entire long word is accessed ... and loaded into the cache holding
 * register, and the high-order word is also loaded into stage B of the pipe.
 * The instruction word for the next sequential prefetch can then be accessed
 * directly from the cache holding register, and **no external bus cycle or
 * instruction cache access is required**."
 *
 * So a long-word-aligned pair of sequential prefetches costs *one* external
 * cycle and a misaligned pair costs *two*. That factor of two, averaged and
 * rounded, is exactly what §11.3.3 publishes.
 *
 * "The cache holding register provides instruction words to the pipe,
 * regardless of whether the instruction cache is enabled or disabled" -- so
 * this saving survives `MD`'s `IC` command turning the cache off, and a
 * cache-off probe still will not see two bus cycles per two words.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_PIPE_H
#define APOLLO_CPU_M68030_AP_M68030_PIPE_H

#include <stdbool.h>
#include <stdint.h>

/* One pipe stage: a 16-bit instruction word plus the status bit `[030]` gives
 * it. `valid` distinguishes an empty stage from one holding zero, which is a
 * legal opcode word. */
typedef struct {
  uint16_t word;
  bool valid;
  bool abnormal; /* loaded from an abnormally terminated bus cycle */
} ap_m68030_pipe_stage_t;

typedef struct {
  /* Words enter at b and proceed to c then d; d is fully decoded. */
  ap_m68030_pipe_stage_t b, c, d;

  /* The cache holding register: one long word, and the long-word-aligned
   * address it was fetched from. */
  uint32_t holding_data;
  uint32_t holding_address;
  bool holding_valid;
  bool holding_abnormal;
} ap_m68030_pipe_t;

/* Empty the pipe and invalidate the holding register. */
void ap_m68030_pipe_reset(ap_m68030_pipe_t *pipe);

/* True when a prefetch of the instruction word at `address` can be satisfied
 * from the cache holding register, needing no external bus cycle and no
 * instruction cache access. */
[[nodiscard]] bool ap_m68030_pipe_holds(const ap_m68030_pipe_t *pipe,
                                        uint32_t address);

/* Supply the long word a bus cycle fetched for a prefetch of `address`. Loads
 * the holding register and puts the addressed word into stage B. `abnormal`
 * records that the bus cycle terminated abnormally. */
void ap_m68030_pipe_fill(ap_m68030_pipe_t *pipe, uint32_t address,
                         uint32_t longword, bool abnormal);

/* Satisfy a prefetch of `address` from the holding register, loading stage B.
 * Only legal when ap_m68030_pipe_holds() is true. */
void ap_m68030_pipe_load_from_holding(ap_m68030_pipe_t *pipe, uint32_t address);

/* Advance the pipe one stage: D is discarded, C becomes D, B becomes C, and B
 * is emptied for the next prefetch. */
void ap_m68030_pipe_advance(ap_m68030_pipe_t *pipe);

/* The fully decoded stage. Returns false when D holds nothing yet. */
[[nodiscard]] bool ap_m68030_pipe_decoded(const ap_m68030_pipe_t *pipe,
                                          uint16_t *word, bool *abnormal);

#endif /* APOLLO_CPU_M68030_AP_M68030_PIPE_H */

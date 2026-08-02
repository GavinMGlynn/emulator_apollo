/* MC68030 instruction prefetch: the pipe driven from real memory.
 *
 * `[030]` §11.2.2 for the pipe, §6.1 for the access it performs. `ap_m68030_pipe`
 * models the three stages and the cache holding register against words a test
 * hands it; this drives it from `ap_m68030_access`, so a prefetch costs what a
 * real instruction fetch costs.
 *
 * ## Half of all prefetches are free, and which half depends on alignment
 *
 * The holding register holds one *long word*. A prefetch of the word at its
 * odd half needs no bus cycle and no cache access at all -- the word is already
 * there. So sequential prefetching alternates: fetch, free, fetch, free.
 *
 * That is why the manual's published prefetch figures are averages rather than
 * counts, and why this project models the pipe at all. Four sequential words
 * cost **two** fetches from a long-word-aligned start and **three** from an odd
 * one, and no single number describes both. `pipe_suite` already pins that
 * against synthetic words; here it falls out of the memory path.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_FETCH_H
#define APOLLO_CPU_M68030_AP_M68030_FETCH_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_access.h"
#include "cpu/m68030/ap_m68030_pipe.h"

typedef struct {
  ap_m68030_pipe_t pipe;
  ap_m68030_access_ctx_t *access; /* the instruction side of the machine */
  uint32_t address;               /* the next word to prefetch */
  uint8_t function_code;

  /* Clocks spent on *instruction* bus cycles since reset, running total.
   *
   * Kept apart from `ap_m68030_cpu_t::clocks` because §11.6's timing model
   * treats the two differently and this core cannot otherwise tell them apart:
   * an operand access is waited on by the microcode that consumes it, while a
   * prefetch is not, and how much of a prefetch hides is a published
   * per-instruction quantity (`ap_m68030_prefetch_exposure`). Summing them into
   * one figure loses exactly the distinction the model turns on.
   *
   * A running total rather than per-step scratch, so it is honest state that
   * hashes with the rest: a step reads it either side of its work and takes the
   * difference. It also answers "what did prefetching cost this run", which is
   * the quantity `FINDINGS.md` C7's alternation is about. */
  uint64_t bus_clocks;
} ap_m68030_fetch_t;

typedef struct {
  bool ok;
  uint32_t clocks;   /* zero when the holding register answered */
  bool from_holding; /* no bus cycle and no cache access was needed */
  bool fault;
} ap_m68030_fetch_result_t;

/* Point the unit at an address and empty the pipe, as a branch does. */
void ap_m68030_fetch_reset(ap_m68030_fetch_t *fetch, uint32_t address);

/* Prefetch one word into stage B, advancing the address by two. */
[[nodiscard]] ap_m68030_fetch_result_t
ap_m68030_fetch_prefetch(ap_m68030_fetch_t *fetch);

#endif /* APOLLO_CPU_M68030_AP_M68030_FETCH_H */

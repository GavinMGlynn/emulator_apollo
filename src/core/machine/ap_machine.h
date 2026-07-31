/* A constructed machine: a 68030 wired to flat RAM, with nothing else.
 *
 * ## Why this exists, and why now
 *
 * Phase 1 asks for "probes side-loadable into post-boot machine state, so CI
 * needs no copyrighted firmware", and `tools/mame-oracle/FINDINGS.md` C4 is the
 * reason it is being built *first* rather than after the boot-PROM route: the
 * PROM does not reach the Mnemonic Debugger prompt under the oracle, so every
 * item gated on "measure against the oracle" was gated on a route not shown to
 * work. A machine that can be constructed, poked and stepped needs no firmware
 * at all, and is what the instruction-timing measurement will run on.
 *
 * It also removes a duplication: every test suite had grown its own RAM, caches,
 * ATC and access contexts, each subtly different. `step_suite`'s copy is where
 * an out-of-range write corrupted the stack and surfaced as a segfault three
 * tests later.
 *
 * ## What it is not
 *
 * Not the DN3500. There is no I/O, no device, no interrupt controller and no
 * bus arbitration point — those are Phase 3, and the model table is where
 * machine variance belongs, not here. This is the smallest thing that can run a
 * program and be measured: exactly what a probe needs and nothing more.
 *
 * ## RAM is supplied, not allocated
 *
 * The core allocates nothing. The caller owns the buffer, chooses its size, and
 * keeps it alive — which is what lets a probe pick a size to suit, lets a test
 * put one on the stack, and keeps the core free of any allocator's behaviour in
 * a component whose whole value is determinism.
 *
 * ## Outside the RAM is a bus error, not a wrap and not a zero
 *
 * An access beyond the supplied RAM terminates with `BERR`. The real machine
 * faults on an unmapped address, and the two alternatives are both worse than
 * a fault: wrapping invents an alias the hardware does not have, and reading
 * zero makes an out-of-range probe look like a working one that found empty
 * memory.
 */

#ifndef APOLLO_MACHINE_AP_MACHINE_H
#define APOLLO_MACHINE_AP_MACHINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_atc.h"
#include "cpu/m68030/ap_m68030_cache.h"
#include "cpu/m68030/ap_m68030_step.h"

struct ap_board;
#include "state/ap_hash.h"

typedef struct {
  uint8_t *ram;
  uint32_t ram_bytes;

  /* Counted rather than merely refused, so a probe can tell "my program ran and
   * touched nothing outside its memory" from "my program ran". */
  unsigned bus_errors;

  /* The two caches are separate objects because the part has two, and a machine
   * that shared one would hide every instruction/data interaction. */
  ap_m68030_cache_t instruction_cache;
  ap_m68030_cache_t data_cache;
  ap_m68030_atc_t atc;

  ap_m68030_access_ctx_t instruction_access;
  ap_m68030_access_ctx_t data_access;

  ap_m68030_cpu_t cpu;

  /* Optional: when set, every access is routed through the DN3500's address
   * map instead of the flat RAM above.
   *
   * Optional rather than mandatory because the probes want flat memory -- a
   * probe puts its program at a known address and wants nothing else in the
   * way, and a probe harness that had to be a whole DN3500 would be a worse
   * probe harness. Firmware wants the opposite, and `FINDINGS.md` C28 is what
   * flat memory cost it: 5634 accesses that read as zero because the device
   * addresses fell inside the RAM. */
  struct ap_board *board;
} ap_machine_t;

/* Wire a machine over `ram`, which the caller owns and must keep alive for as
 * long as the machine is used. The RAM is *not* cleared: a probe that wants a
 * known background writes one, and a machine that silently zeroed would hide a
 * probe forgetting to.
 *
 * Both access contexts point at the CPU's own MMU registers, so a `PMOVE` takes
 * effect on translation rather than only on a register nobody reads. */
void ap_machine_init(ap_machine_t *machine, uint8_t *ram, uint32_t ram_bytes);

/* Route this machine's accesses through a core-board address map. Pass NULL to
 * return it to flat RAM. The board is caller-owned, as the RAM is. */
void ap_machine_set_board(ap_machine_t *machine, struct ap_board *board);

/* Point the processor at an address with a stack, as a reset vector fetch
 * would, and empty the pipe and both caches.
 *
 * Supervisor state with interrupts masked at 7, which is what reset leaves:
 * `[030]` §8.1.1. A probe that wants user state writes the status register
 * afterwards, and can then see the privilege boundary work. */
void ap_machine_reset(ap_machine_t *machine, uint32_t pc, uint32_t stack);

/* Byte-granular access for setting a probe up and reading it back. Big endian,
 * matching the operand layer, so a long word written here reads back through an
 * instruction as the same number.
 *
 * These bypass the caches deliberately: they are the *operator's* view, not the
 * processor's, and a probe setting up memory has not run a bus cycle. Anything
 * already cached is therefore stale afterwards, which is why they refuse to run
 * once the machine has stepped -- see `ap_machine_poke_long`. */
bool ap_machine_write(ap_machine_t *machine, uint32_t address, unsigned size,
                      uint32_t value);
[[nodiscard]] bool ap_machine_read(const ap_machine_t *machine,
                                   uint32_t address, unsigned size,
                                   uint32_t *value);

/* Execute one instruction. */
[[nodiscard]] ap_m68030_step_result_t ap_machine_step(ap_machine_t *machine);

/* Run until `limit` instructions have been executed or the processor stops
 * making progress -- an unimplemented or illegal instruction, a fault, or a
 * `STOP`. Returns how many executed, and reports why it ended.
 *
 * A limit is required rather than optional: a probe that loops forever must end
 * as a failed probe rather than as a hung harness. */
typedef struct {
  unsigned executed;
  ap_m68030_step_status_t status; /* why it ended */
} ap_machine_run_t;

[[nodiscard]] ap_machine_run_t ap_machine_run(ap_machine_t *machine,
                                              unsigned limit);

/* The whole machine as one number: the processor's state and the RAM.
 *
 * This is the identity harness's machine-level answer, and it includes the RAM
 * because a run that left different memory behind is a different run however
 * well its registers agree. */
[[nodiscard]] uint64_t ap_machine_hash(const ap_machine_t *machine);

#endif /* APOLLO_MACHINE_AP_MACHINE_H */

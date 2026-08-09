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

#include "time/ap_time.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_atc.h"
#include "cpu/m68030/ap_m68030_cache.h"
#include "cpu/m68030/ap_m68030_step.h"
#include "cpu/m68882/ap_m68882.h"
#include "model/ap_model.h"

struct ap_board;
#include "state/ap_hash.h"

/* How many `PMOVE`s a run keeps. A whole Domain/OS boot to the crash performs
 * eight, and a boot that reaches `login:` performs a few thousand -- almost all
 * of them one scheduler switching between the same handful of trees. Thirty-two
 * holds every load of the interesting phase, which is the firmware bringing
 * translation up and the operating system taking it over; past that the total
 * carries the information and the addresses repeat. */
#define AP_MACHINE_MMU_WRITES 32u

/* One address the bus never answered, with who first asked and how often.
 *
 * The PC is the *first* one to fault here rather than the last, because the
 * question a fault profile answers is where a place was first reached from --
 * a device probe returns to the same address from the same loop, and its
 * hundredth PC says nothing its first did not. */
typedef struct {
  uint32_t address;
  uint32_t pc;
  unsigned count;
} ap_fault_site_t;

typedef struct {
  uint8_t *ram;
  uint32_t ram_bytes;

  /* Counted rather than merely refused, so a probe can tell "my program ran and
   * touched nothing outside its memory" from "my program ran". */
  /* What the previous instruction cost, carried **across calls**.
   *
   * The bus advances at the processor's rate, so `ap_machine_run` charges the
   * board the clocks the last instruction spent. That figure was a local, reset
   * to zero on entry -- so a caller stepping one instruction at a time got a
   * bus tick count of zero, for ever. Every boot with scripted input, a console
   * or a trace does exactly that, which is why no DMA transfer had ever run
   * outside the unit tests. */
  uint64_t last_instruction_clocks;

  /* How many F-line exceptions had been taken when this machine last looked.
   * The board's FP trap status bit is set by *taking* one while the control
   * register holds the coprocessor off -- see `board/ap_boardreg.h` -- and a
   * count is how a caller notices one happened. */
  unsigned last_line_f_exceptions;

  /* One address, and who last wrote to it.
   *
   * A value that is wrong when it is read says nothing about how it got that
   * way, and the difference between "some instruction stored this" and "nothing
   * ever stored anything, and this is what was loaded here" is the whole
   * question when a loader hands the firmware a block number it cannot have
   * meant. Zero disables it, which is why the count is reported beside the
   * address rather than inferred from it.
   *
   * Our record of watching the machine, like the bus-error counters below and
   * deliberately outside the state hash for the same reason. */
  uint32_t watch_write_address;
  unsigned watch_writes;
  /* The address of the instruction *being executed*, which a caller stepping
   * one at a time sets before each step. The watch reports this rather than
   * `regs.pc`, because the program counter during a store points into the
   * middle of the instruction doing the storing -- past its opcode, somewhere
   * among its extension words -- and disassembling from there decodes a
   * different instruction that happens to start at the wrong byte. */
  uint32_t executing_address;
  uint32_t watch_write_pc;
  uint32_t watch_write_value;
  unsigned watch_write_size;

  /* ## The same watch, on the read side
   *
   * A write is the easier half and was built first, because a firmware that
   * writes somewhere surprising announces itself. A *read* announces nothing,
   * and three separate questions in this project have come down to "which
   * instruction reads this address": the display controller's status poll, the
   * DUART register the keyboard test waits on, and the calendar's validity
   * longword at `010912` — whose address is computed rather than literal, so
   * grepping the PROM for it finds nothing.
   *
   * Reported with the value the machine answered, because "it read there and
   * got zero" and "it never read there" are the two hypotheses this separates,
   * and a count alone cannot. */
  uint32_t watch_read_address;
  unsigned watch_reads;
  uint32_t watch_read_pc;
  uint32_t watch_read_value;
  unsigned watch_read_size;

  unsigned bus_errors;
  uint32_t first_bus_error;
  uint32_t last_bus_error;
  uint32_t last_bus_error_pc;
  /* The distinct addresses that went unanswered, earliest first. A count says
   * how often; a first and a last say where a run started and stopped. Neither
   * says *which places*, and a scan that faults 130 times over one address is a
   * different machine from one that faults 130 times over 130.
   *
   * Each site carries the PC that first reached it and how often it faulted,
   * because the three answer different questions: a device probe faults many
   * times over a range from one loop, and a program following a wild pointer
   * faults once from somewhere that should never have been there. A boot that
   * ends in a fault is asking which of the two it is, and an address list alone
   * cannot say.
   *
   * The cap was sixteen and a boot fills it during device probing alone -- 14
   * of the 16 slots went to one `FD80x000` scan -- so every later fault in the
   * run was invisible and the list *looked* complete. `sites_dropped` counts
   * what the cap refused, because a truncated list that says so is evidence and
   * one that stays silent is a wrong answer. */
  ap_fault_site_t fault_sites[64];
  unsigned distinct_fault_count;
  unsigned fault_sites_dropped;
  /* Address translation, which the boot PROM turns on partway through and every
   * later access depends on. Counted because "the MMU is enabled" and "the MMU
   * has translated something" are different claims and a boot needs both. */
  unsigned table_fetches;
  unsigned table_updates;
  /* The *observer's* table searches, kept apart from the machine's.
   *
   * `ap_machine_translate` is a probe: it walks the tree without the ATC and
   * without setting used bits, so a caller can ask "where would this logical
   * address go" without perturbing the machine. It shares the fetch callback
   * with the real access path, so its walks landed in `table_fetches` -- and a
   * frontend that reads one word back per step, as the boot trace does, added
   * one whole walk per instruction to a counter read as the machine's own.
   *
   * That misreported figure was believed: a boot showed a flat 3.0 descriptor
   * fetches per instruction and it was written up as "the ATC is not retaining
   * entries". It was three per *stepped instruction* -- the depth of the tree
   * being probed -- and subtracting it leaves 45 real fetches in 15 million
   * instructions, which is an ATC working almost perfectly. An instrument that
   * inflates what it measures is worse than no instrument, so the two are
   * counted separately and both are reported. */
  unsigned probe_fetches;
  bool probing;

  /* Every `PMOVE` a run performs, in order, with the instruction that made it.
   *
   * The final `CRP` is the *last* of a sequence and the interesting one is
   * usually earlier, or is a load that never happened at all -- so a register
   * dump at exit cannot answer "which tree did it install, and when". Two
   * sessions reconstructed this by dumping memory and searching it for `PMOVE`
   * opcodes, which finds instructions rather than executions and found two
   * false positives for every real one.
   *
   * Bounded, oldest kept, with the overflow counted: the first loads are the
   * firmware bringing translation up and are the ones worth keeping, and a
   * scheduler switching address spaces thousands of times must not be able to
   * push them out. */
  struct {
    uint32_t pc;
    uint32_t high;
    uint32_t low;
    uint8_t which; /* an `ap_m68030_mmu_register_t` */
  } mmu_writes[AP_MACHINE_MMU_WRITES];
  unsigned mmu_write_count;   /* how many are kept below */
  unsigned mmu_writes_total;  /* how many happened, which is the honest total */

  /* `PMOVE`s that *read* an MMU register out to memory, counted rather than
   * logged: the question they answer is "did the program ever look at the MMU",
   * and a count settles that. Which register was read is kept as a bitmask, so
   * "it read the CRP" and "it read the status register" are told apart without
   * a second log. */
  unsigned mmu_reads_total;
  uint8_t mmu_reads_mask;

  /* The first few, with the instruction that made them -- kept for the same
   * reason the writes are: a count says *whether* the program looked and the PC
   * says **who**. That distinction was not free. A boot that reads the MMU 290
   * times reads very differently depending on whether those are the kernel
   * inspecting what the firmware left or the PROM's crash handler dumping state
   * afterwards, and the mask alone cannot tell them apart. */
  struct {
    uint32_t pc;
    uint8_t which;
  } mmu_reads[AP_MACHINE_MMU_WRITES];
  unsigned mmu_read_count;

  /* The two caches are separate objects because the part has two, and a machine
   * that shared one would hide every instruction/data interaction. */
  ap_m68030_cache_t instruction_cache;
  ap_m68030_cache_t data_cache;
  ap_m68030_atc_t atc;

  ap_m68030_access_ctx_t instruction_access;
  ap_m68030_access_ctx_t data_access;

  ap_m68030_cpu_t cpu;
  /* The floating-point coprocessor, which the CPU holds by pointer because
   * fitted-or-not is a machine property -- and now *is* one: `cpu.fpu` points
   * here only when the model's CPU family has a coprocessor to point at. */
  ap_m68882_t fpu;
  /* Which machine this is. Held rather than derived because every other
   * variance in this core comes from the one table in `src/core/model/`, and a
   * machine that did not know its own model could not consult it. */
  const ap_model_t *model;

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
  /* The CPU's clock, and the time it has produced. Kept here rather than on the
   * CPU because time is the *machine's*: the processor counts its own cycles,
   * and only something that knows every clock in the box can turn those into a
   * shared unit. */
  ap_clock_t cpu_clock;
  ap_time_t now;
} ap_machine_t;

/* Wire a machine over `ram`, which the caller owns and must keep alive for as
 * long as the machine is used. The RAM is *not* cleared: a probe that wants a
 * known background writes one, and a machine that silently zeroed would hide a
 * probe forgetting to.
 *
 * Both access contexts point at the CPU's own MMU registers, so a `PMOVE` takes
 * effect on translation rather than only on a register nobody reads. */
void ap_machine_init(ap_machine_t *machine, uint8_t *ram, uint32_t ram_bytes);

/* The same, as a named model. `ap_machine_init` is this with the DN3500 -- the
 * reference superset -- so every existing caller keeps the machine it had.
 *
 * This is what makes "fitted or not is a machine property" true rather than
 * aspirational: a DN3000 is a 68020 without a coprocessor, and until the machine
 * could be told which model it was, that difference was not expressible and an
 * F-line instruction executed on every machine this core built.
 *
 * The processor's clock rate comes from the same row, so a DN3000 machine keeps
 * time at 12 MHz and a DN4500 at 33 MHz without anyone being told twice. */
void ap_machine_init_model(ap_machine_t *machine, uint8_t *ram,
                           uint32_t ram_bytes, ap_model_id_t model);

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

/* The same read, but of the address the *program* named rather than the one the
 * bus carried: transparent translation first, then the tables, exactly as an
 * access would resolve them.
 *
 * A trace that prints "the word at the PC" has to do this once the MMU is on,
 * and the reason to say so here is that the obvious version is silently wrong
 * rather than merely approximate. `ap_machine_read` of a logical PC reads the
 * physical address that happens to have the same number, which for an operating
 * system running at `3FFA24FC` on a machine whose memory ends at `01FFFFFF` is
 * nothing at all -- so every instruction in the trace read back as `0000` and
 * the column looked like a machine executing zeros.
 *
 * Nothing is disturbed. The ATC is not filled and the tree's history bits are
 * not updated, which is the discipline `PTEST` follows and for the same reason:
 * an observer that changes what it observes is not an observer. It is a read of
 * memory and not of the bus, so a device address answers `false` here even
 * though the processor would have got a value from it.
 *
 * `function_code` selects the root when `TC`'s SRE splits them, so a supervisor
 * program fetch is 6 and user data is 1, as on the bus. */
[[nodiscard]] bool ap_machine_read_logical(ap_machine_t *machine,
                                           uint32_t logical,
                                           uint8_t function_code, unsigned size,
                                           uint32_t *value);

/* Where an address translates to, without translating anything else. Answers
 * `false` for a logical address the tables do not map -- which a caller must
 * distinguish from a physical zero. */
[[nodiscard]] bool ap_machine_translate(ap_machine_t *machine, uint32_t logical,
                                        uint8_t function_code,
                                        uint32_t *physical);

/* Execute one instruction. */
[[nodiscard]] ap_m68030_step_result_t ap_machine_step(ap_machine_t *machine);

/* How many clocks a machine will stand stalled by another bus master before it
 * gives up and runs anyway.
 *
 * Not a timeout: a master that never releases the bus is a broken machine, and
 * a reference core that spun forever inside a bounded run would turn that into
 * a hung harness instead of a visible fault -- the same reason the run below
 * takes a limit at all. Generous enough that no real transfer reaches it. */
#define AP_MACHINE_STALL_LIMIT 4096u

/* Run until `limit` instructions have been executed or the processor stops
 * making progress -- an unimplemented or illegal instruction, a fault, or a
 * `STOP`. Returns how many executed, and reports why it ended.
 *
 * A limit is required rather than optional: a probe that loops forever must end
 * as a failed probe rather than as a hung harness.
 *
 * ## With a board attached, the processor does not always get to run
 *
 * The bus advances at the processor's rate -- it is given the clocks the last
 * instruction spent -- and while another master holds it the processor stalls,
 * accumulating clocks without executing. That is the whole of how contention
 * reaches a program: nothing computes a delay, and the processor is simply the
 * lowest-priority claimant of a bus somebody else has. */
typedef struct {
  unsigned executed;
  ap_m68030_step_status_t status; /* why it ended */
  /* And on what word. A run that ends `ILLEGAL` or `UNIMPLEMENTED` is a report
   * that some opcode is missing, and the opcode is the only part of that a
   * caller cannot recover afterwards: the PC is logical, so reading the word
   * back needs the translation the run has already moved on from, and the
   * caches make even that not quite the byte the processor decoded. The core
   * has the word in its hand at the moment it gives up, so it hands it over. */
  uint16_t instruction;
} ap_machine_run_t;

[[nodiscard]] ap_machine_run_t ap_machine_run(ap_machine_t *machine,
                                              unsigned limit);

/* The whole machine as one number: the processor's state, the RAM, the board's
 * devices when one is attached, and the elapsed time.
 *
 * This is the identity harness's machine-level answer, and it includes the RAM
 * because a run that left different memory behind is a different run however
 * well its registers agree.
 *
 * ## What is in it, and what is beside it
 *
 * Everything the emulated machine holds. Not the counters -- `bus_errors` here,
 * and the board's unmapped, empty-slot and per-region tallies -- which are our
 * record of *watching* the machine rather than state it has. Two reasons, and
 * `board/ap_board_state.h` gives them in full: a counter in the hash would make
 * adding an instrument change every golden with no emulated behaviour changing,
 * and would make two machines that behave identically compare unequal because
 * one was watched more closely. Nothing is lost, because they are reported
 * beside the hash by `ap_machine_state` and pinned as their own column in
 * `tests/goldens/probes.txt`.
 *
 * Elapsed time *is* in it. The CPU's clock count is hashed with its registers
 * for the reason `ap_m68030_state.h` gives, and the machine's own `now` is
 * hashed here as well: they are not the same quantity once a device advances on
 * a clock of its own, and a fast mode that reached the same registers at a
 * different instant is exactly the divergence this must catch. */
[[nodiscard]] uint64_t ap_machine_hash(const ap_machine_t *machine);

/* The hash with the numbers that localise a disagreement, reported together.
 *
 * A hash answers "are these two runs the same" and nothing else: when it says
 * no, it says nothing about where they parted. The instruction count, the clock
 * and the PC are what turn that into a place to look -- two runs that agree for
 * 40,000 instructions and diverge at one are found by bisection on the clock,
 * not by staring at two 64-bit numbers. So a caller reporting a hash reports
 * these beside it, and the frontends do.
 *
 * The counters are here for the same reason: they are excluded from the hash
 * deliberately, and this is where they are not lost. */
typedef struct {
  uint64_t hash;      /* the whole machine as one number */
  uint64_t clocks;    /* CPU clocks since reset */
  ap_time_t now;      /* elapsed time since reset, in AP_TIME_BASE_HZ units */
  uint32_t pc;        /* where the processor is */
  unsigned bus_errors;/* how many accesses went unanswered */
  /* Where the first and the last of them were. Diagnostics, like the count. */
  uint32_t first_bus_error;
  uint32_t last_bus_error;
  uint32_t last_bus_error_pc;
  unsigned table_fetches;
  unsigned table_updates;
  /* What `ap_machine_translate` cost, which is the observer's and not the
   * machine's. Reported beside `table_fetches` so the two can never be added
   * together by mistake again. */
  unsigned probe_fetches;
} ap_machine_state_t;

[[nodiscard]] ap_machine_state_t ap_machine_state(const ap_machine_t *machine);

/* ## The machine's clock
 *
 * `ap_machine_now` is absolute time since reset, in `AP_TIME_BASE_HZ` units --
 * never CPU cycles. Several nodes of different models share one 12 Mbit/s ring,
 * so no CPU's cycle is a legal unit of account, and a machine that counted in
 * them would have to convert at every boundary that matters.
 *
 * It advances by the CPU's own clock rate: each step reports the clocks it
 * cost, and those are converted once, here, through `ap_clock_duration`. That
 * conversion is the *only* place a CPU cycle becomes a time, which is what
 * keeps the rest of the machine honest about its units.
 *
 * This is the first piece of the tick loop and not the loop itself. Nothing
 * else advances inside it yet: the devices are still inert, and the five things
 * waiting on them are named in `docs/COMPLETION_PLAN.md`. What exists is a
 * clock that is correct and in the right units, so that when subsystems are
 * added they have something true to advance against rather than a number
 * invented alongside them. */
[[nodiscard]] ap_time_t ap_machine_now(const ap_machine_t *machine);

/* There is deliberately no setter for the CPU's clock rate. It is a property of
 * the model, `ap_machine_init_model` reads it from the table, and a caller that
 * could override it could build a DN3500 whose processor runs at some other
 * machine's rate -- which is exactly the variance `CLAUDE.md` requires to come
 * from `src/core/model/` and nowhere else. The frontend used to look up
 * `dn3500` by name and set the rate itself, in a machine that already knew. */

#endif /* APOLLO_MACHINE_AP_MACHINE_H */

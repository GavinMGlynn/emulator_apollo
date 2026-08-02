/* MC68040 instruction timing: the pipeline model the tables are written
 * against.
 *
 * `MC68040 User's Manual (1993)` §10.1 and Table 10-2.
 *
 * ## A different shape from the 68030's
 *
 * Phase 2 modelled the 68030's timing as §11.6's `(r/p/w)` triples composed by
 * Equations 11-1 and 11-2, with head and tail overlap between instructions.
 * The 68040 does not work that way, and its tables cannot be read as if it did:
 *
 *   - **Three pipeline stages are priced separately** -- `<ea> calculate`,
 *     `<ea> fetch` and `execute` -- rather than one figure per instruction.
 *   - **The fetch stage is not in the tables at all.** "The <ea> fetch timing is
 *     not listed in the following tables because most instructions require one
 *     clock in the <ea> fetch stage for each memory access ... An instruction
 *     requires one clock to pass through the <ea> fetch stage even if no
 *     operand is fetched." So it is derived from Table 10-2, and the floor of
 *     one clock is the part a naive reading drops: an instruction with no
 *     operand still costs a clock there.
 *   - **Execute time is two numbers, not one.** "This number is presented as a
 *     lead time and a base time. The lead time is the number of clocks the
 *     instruction can stall when entering the execution stage without delaying
 *     the instruction execution ... if an execution time is listed as 2L + 1,
 *     the lead time is two clocks and the base time is one for a total
 *     execution time of three."
 *
 * ## The interlock is why the lead time is worth carrying
 *
 * "The <ea> calculate and execute stages operate in an interlocked manner for
 * all instructions using the brief and full extension word formats. If an
 * instruction using one of these formats is stalled for more than nL clocks
 * waiting to begin execution in the execute stage, a similar increase in the
 * <ea> calculate time will result. For example, if the execution time listed is
 * 2L + 1 and the instruction stalls for three clocks, then the <ea> calculate
 * time increases by one clock (3 - 1 = 2L)."
 *
 * The manual's arithmetic in that example is loose -- three clocks of stall
 * against two of lead gives one clock of increase, and it writes that as
 * "3 - 1 = 2L" rather than "3 - 2 = 1". The rule it describes is unambiguous
 * even so: the increase is the stall *beyond* the lead, so a lead time absorbs
 * stalls up to its own size and costs nothing until it is exhausted.
 *
 * ## What the tables assume
 *
 * §10.1 lists four suppositions, and two of them bound how far these numbers
 * can be trusted for a whole machine:
 *
 *   2. "All memory accesses hit in the caches; no table searches occur as a
 *      result of ATC misses" -- so a table figure is a *best case*, and the
 *      manual says to add the access time for each operand fetch that misses.
 *   3. "All accesses are aligned to a byte boundary that is a multiple of the
 *      operand size", and misaligned fetch timing is left to the reader: "the
 *      user must perform his own calculations for <ea> fetch timing for
 *      misaligned accesses."
 *
 * Both are recorded here rather than silently assumed, because a cycle-stepped
 * core that reported table figures as though they were measurements would be
 * claiming a precision the manual does not offer.
 *
 * Write-back is deliberately absent: "write-back times are not listed because
 * they are system dependent and do not affect either <ea> calculate or execute
 * stages of the pipeline."
 */

#ifndef APOLLO_CPU_M68040_AP_M68040_TIMING_H
#define APOLLO_CPU_M68040_AP_M68040_TIMING_H

#include <stdbool.h>
#include <stdint.h>

/* Table 10-2's rows. Named rather than encoded as mode/register pairs, because
 * the table groups `(xxx).W` with `(xxx).L` and distinguishes the four memory
 * indirect forms that share an addressing mode. */
typedef enum {
  AP_M68040_EA_DATA_REGISTER,        /* Dn */
  AP_M68040_EA_ADDRESS_REGISTER,     /* An */
  AP_M68040_EA_INDIRECT,             /* (An) */
  AP_M68040_EA_POSTINCREMENT,        /* (An)+ */
  AP_M68040_EA_PREDECREMENT,         /* -(An) */
  AP_M68040_EA_DISPLACEMENT,         /* (d16,An) */
  AP_M68040_EA_PC_DISPLACEMENT,      /* (d16,PC) */
  AP_M68040_EA_ABSOLUTE,             /* (xxx).W, (xxx).L */
  AP_M68040_EA_IMMEDIATE,            /* #<xxx> */
  AP_M68040_EA_INDEXED,              /* (d8,An,Xn) */
  AP_M68040_EA_PC_INDEXED,           /* (d8,PC,Xn) */
  AP_M68040_EA_BASE_INDEXED,         /* (BR,Xn) */
  AP_M68040_EA_BASE_DISPLACEMENT,    /* (bd,BR,Xn) */
  AP_M68040_EA_MEMORY_PREINDEXED,    /* ([bd,BR,Xn]) */
  AP_M68040_EA_MEMORY_PREINDEXED_OD, /* ([bd,BR,Xn],od) */
  AP_M68040_EA_MEMORY_POSTINDEXED,   /* ([bd,BR],Xn) */
  AP_M68040_EA_MEMORY_POSTINDEXED_OD, /* ([bd,BR],Xn,od) */
  AP_M68040_EA_COUNT,
} ap_m68040_ea_t;

/* Table 10-2's two columns: an addressing mode costs different numbers of
 * accesses depending on whether the operand is fetched or only its address is
 * handed to the execution stage. */
[[nodiscard]] unsigned ap_m68040_ea_accesses_fetching(ap_m68040_ea_t ea);
[[nodiscard]] unsigned ap_m68040_ea_accesses_sending(ap_m68040_ea_t ea);

/* Clocks in the `<ea> fetch` stage. "One clock ... for each memory access", but
 * never fewer than one: "an instruction requires one clock to pass through the
 * <ea> fetch stage even if no operand is fetched." */
[[nodiscard]] unsigned ap_m68040_fetch_clocks(unsigned accesses);

/* An execute time as the tables write it: `nL + b`. */
typedef struct {
  unsigned lead;
  unsigned base;
} ap_m68040_execute_t;

/* Total execution when nothing stalls: "the lead time is two clocks and the
 * base time is one for a total execution time of three." */
[[nodiscard]] unsigned
ap_m68040_execute_total(ap_m68040_execute_t execute);

/* How much a stall adds to the `<ea> calculate` stage: the stall beyond the
 * lead, and nothing while the lead absorbs it. */
[[nodiscard]] unsigned
ap_m68040_interlock_penalty(ap_m68040_execute_t execute, unsigned stall);

/* §10.1's supposition 1: "For BR = PC, 1 and 1L clocks to the <ea> calculate
 * and execution times unless otherwise noted" -- one clock of calculate and one
 * of *lead*, not of base, so a program-counter-relative base register costs
 * nothing unless the pipeline was already stalling. */
[[nodiscard]] ap_m68040_execute_t
ap_m68040_pc_relative_execute(ap_m68040_execute_t execute);
#define AP_M68040_PC_RELATIVE_CALCULATE 1u

/* Whether this addressing mode uses a brief or full extension word, and so
 * whether the interlock applies: "the <ea> calculate and execute stages operate
 * in an interlocked manner for all instructions using the brief and full
 * extension word formats." */
[[nodiscard]] bool ap_m68040_ea_is_interlocked(ap_m68040_ea_t ea);

#endif /* APOLLO_CPU_M68040_AP_M68040_TIMING_H */

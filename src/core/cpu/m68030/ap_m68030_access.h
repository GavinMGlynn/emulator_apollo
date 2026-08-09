/* MC68030 logical memory access: the path a read actually takes.
 *
 * `[030]` §6.1, cited throughout. This is the join over everything below it --
 * the caches, the transparent translation registers, the ATC, the table walk
 * and the bus -- and its whole content is the *order*.
 *
 * ## A cache hit does not consult the MMU at all
 *
 * "Whenever a read access occurs and the required instruction word or data
 * operand is resident in the appropriate on-chip cache (no external bus cycle
 * is required), **the MMU is completely ignored** ... Therefore, the state of
 * the corresponding CI bits in the MMU are also ignored. The MMU is used to
 * validate all accesses that require external bus cycles."
 *
 * That is the reverse of the intuitive order, and it is only possible because
 * the 68030's caches are **logically** addressed: their tag is the logical
 * address with the function code, not a physical one. So the cache can answer
 * before anything has been translated.
 *
 * It has two consequences worth stating, because both look like bugs:
 *
 *   - A page's protection is not checked on a cache hit. A write-protected page
 *     already in the data cache is *read* without the MMU objecting, because the
 *     MMU is not asked.
 *   - The cache's CI bit is irrelevant on a hit, since CI comes from the MMU
 *     and the MMU is not consulted.
 *
 * A model that translates first and then looks in the cache produces the same
 * *values* and the wrong *timing* -- and wrong faults, since it would check
 * protections the hardware skips.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_ACCESS_H
#define APOLLO_CPU_M68030_AP_M68030_ACCESS_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_atc.h"
#include "cpu/m68030/ap_m68030_cache.h"
#include "cpu/m68030/ap_m68030_tt.h"
#include "cpu/m68030/ap_m68030_walk.h"

/* Named with a `_ctx_` because `ap_m68030_tt.h` already owns the plain
 * `ap_m68030_access_t` for *its* notion of an access -- the address, function
 * code and direction a TTx register compares against. Two headers cannot define
 * the same typedef, and this module includes that one. */
/* Perform the external write cycle of a writethrough store, returning whether
 * the memory system accepted it.
 *
 * Returning `void` was a real defect and not merely an omission. A write to an
 * address nothing decodes would be *counted* by the memory system and then
 * silently succeed, because there was no way to say otherwise -- so a write
 * could never raise a bus error, an exception frame could be stacked into
 * undecoded space, and a fault loop that the real machine ends in a double
 * fault ran forever instead. A signal a callee cannot send is a signal the
 * caller will assume never happens. */
typedef bool (*ap_m68030_store_fn)(void *context, uint32_t physical,
                                   uint32_t value, unsigned size);

/* Which register a `PMOVE` addressed.
 *
 * Public because a caller watching MMU loads needs to say *which* one moved,
 * and "the root pointer changed" is a different event from "translation was
 * switched on". `[030]` §9.7.4 gives the encoding these are decoded from. */
typedef enum {
  AP_M68030_MMU_TC,
  AP_M68030_MMU_SRP,
  AP_M68030_MMU_CRP,
  AP_M68030_MMU_TT0,
  AP_M68030_MMU_TT1,
  AP_M68030_MMU_MMUSR,
  AP_M68030_MMU_NONE,
} ap_m68030_mmu_register_t;

/* Told after a `PMOVE` has written `which`, with the operand as it arrived on
 * the bus: `high` is the upper long of a 64-bit register and zero for the
 * 32-bit ones, `low` the lower. Reporting the raw operand rather than the
 * decoded register keeps this an observation of what the program did, which is
 * the question, and not of what this core made of it. */
typedef void (*ap_m68030_mmu_write_fn)(void *context,
                                       ap_m68030_mmu_register_t which,
                                       uint32_t high, uint32_t low);

/* Told after a `PMOVE` has *read* `which` out to memory, with the operand as it
 * was placed on the bus.
 *
 * The write side answers "which root pointer did the program load". This one
 * answers a question the write side cannot: **did the program ever look?** A
 * kernel that inspects the MMU before configuring it behaves differently from
 * one that assumes, and an absence of reads is evidence about the software
 * rather than about this core. It was added because an investigation needed to
 * know whether Domain/OS consults what the firmware left in `CRP`, and counting
 * the instruction was the only way to answer it that does not depend on
 * disassembling memory that may since have been paged over. */
typedef void (*ap_m68030_mmu_read_fn)(void *context,
                                      ap_m68030_mmu_register_t which,
                                      uint32_t high, uint32_t low);

typedef struct {
  ap_m68030_cache_t *cache; /* the instruction or data cache, as appropriate */
  ap_m68030_atc_t *atc;
  const ap_m68030_tt_t *tt0; /* either may be NULL: that register is absent */
  const ap_m68030_tt_t *tt1;
  const ap_m68030_tc_t *tc;
  const ap_m68030_root_t *root;

  bool cache_enabled; /* CACR's EI or ED for this cache */
  bool cache_frozen;  /* FI or FD */
  bool burst_enabled; /* IBE or DBE */
  bool write_allocate; /* CACR's WA, for the data cache's write misses */
  bool cache_disable; /* the CDIS signal, which overrides CACR */
  /* Whether translation is on is **not** kept here. It is `tc->enable`, and a
   * copy of it was: set false when the machine was built and updated by
   * nothing, so `PMOVE` to TC could switch the MMU on and no access would ever
   * notice. Nothing failed visibly for as long as nothing enabled translation,
   * and the first program that did was a Domain/OS diagnostic loaded off the
   * disk. `ap_m68030_translating` reads the register instead. */

  /* RMC, held across the read and the write of an indivisible operation. It
   * lives on the context rather than on a bus object because each access
   * creates its own cycle -- the signal spans two of them, so nothing shorter
   * than the context can hold it. `[030]` §7.3.5: assert before the read,
   * negate after the write. */
  bool rmc; /* TC's E bit */

  /* The table search's bus access, and the fill's, as callbacks -- the same
   * shape `ap_m68030_walk` and `ap_m68030_cache_read` already use. */
  ap_m68030_fetch_fn table_fetch;
  ap_m68030_update_fn table_update;

  /* Told whenever a `PMOVE` writes an MMU register. Optional; NULL is a
   * processor that does the same thing and reports nothing.
   *
   * This exists because *which* root pointer an operating system loads, and
   * from where, is the one MMU fact a register dump at the end cannot give:
   * the final `CRP` is the last of a sequence, and the question is usually
   * about an earlier one, or about a load that never happened. Recovering that
   * by dumping memory and searching it for `PMOVE` opcodes was tried and is
   * both slow and unsound -- it finds the instructions, not the executions. */
  ap_m68030_mmu_write_fn mmu_register_written;

  /* Told whenever a `PMOVE` reads an MMU register out to memory. Optional, and
   * separate from the write hook because the two answer different questions --
   * what was installed, and whether anything looked. */
  ap_m68030_mmu_read_fn mmu_register_read;
  ap_m68030_fill_fn fill;
  /* The external write cycle. Writethrough means this happens on *every* write
   * that reaches memory, hit or miss -- a cache update is not a substitute for
   * it, and an access module that omitted it would report writethrough while
   * behaving like writeback. */
  ap_m68030_store_fn store;

  /* How long the addressed device takes to answer, in **wait states** — whole
   * clocks inserted before the cycle may advance.
   *
   * `[030]` §7.3.1: "If DSACKx is not recognized by the start of S3, the
   * processor inserts wait states instead of proceeding to S4 and S5 ... the
   * processor continues to sample the DSACKx signals on the falling edges of
   * the clock until one is recognized." With none, "the bus cycle runs at its
   * maximum speed (three clocks per cycle)".
   *
   * **Why a callback rather than a field on the fill answer.** A write has no
   * fill answer, and the two directions must charge the same device the same
   * time or a program could be made faster by writing. It is also the truer
   * shape: how long a port takes to answer is a property of the *port*, which
   * is what drives `DSACK`/`STERM` in §7.3, not of the transfer.
   *
   * NULL means every device answers at the minimum — what this core did before
   * there was any way to say otherwise, and what §11's tables assume
   * throughout: "All memory accesses occur with two-clock bus cycles and no
   * wait states." So the default changes no existing figure.
   *
   * Until a board supplies one, contention is emergent in *who* holds the bus
   * and not in *how long*, which is what this exists to end. */
  ap_m68030_wait_states_fn wait_states;

  /* `CIIN`, from the board. NULL means nothing is inhibited. See
   * `ap_m68030_cache.h` for why the processor cannot answer this itself. */
  ap_m68030_cache_inhibit_fn inhibits_cache;

  /* A read of exactly `size` bytes at exactly `address`, for accesses the board
   * has inhibited.
   *
   * The ordinary read path asks the memory system for a **long word**, because
   * that is the unit a cache line is built from and memory does not care how
   * much of it the program wanted. A *device* cares enormously: reading four
   * bytes across a DUART pops its receive FIFO twice, clears a read-to-clear
   * status the program never asked for, and hands back the wrong one of the
   * values it took. The boot PROM's console read `MOVE.B ($0016,A0),D1` became
   * a long word spanning two registers, and the byte the program got was the
   * *second* pop -- an empty FIFO -- so a character that had arrived correctly
   * was read as zero.
   *
   * NULL falls back to the long-word path, which is right for memory and for
   * every machine with no board. */
  ap_m68030_read_sized_fn read_sized;

  void *context;
} ap_m68030_access_ctx_t;


/* Whether this context translates: `TC`'s E bit, read from the register.
 *
 * It used to be a `bool` on the context, set false when the machine was built
 * and updated by nothing -- so a `PMOVE` to TC could switch the MMU on and no
 * access would notice. Nothing failed visibly for as long as nothing enabled
 * translation, and the first program that did was a Domain/OS diagnostic the
 * machine loaded off its own disk. A context with no TC at all does not
 * translate, which is what a probe on flat memory is. */
[[nodiscard]] static inline bool ap_m68030_translating(
    const ap_m68030_access_ctx_t *access) {
  return access->tc != nullptr && access->tc->enable;
}
typedef struct {
  bool ok;
  uint32_t value;
  uint32_t physical; /* meaningful only when the MMU was consulted */
  uint32_t clocks;   /* external clocks: zero on a cache hit */

  bool cache_hit;
  bool mmu_consulted;  /* false on a cache hit, per §6.1 */
  bool transparent;    /* a TTx register matched */
  bool fault;
  unsigned descriptor_fetches; /* the table search's cost, when one ran */
} ap_m68030_access_result_t;

/* ## A write can never skip the MMU
 *
 * The read path above turns on a cache hit avoiding the external cycle. A write
 * cannot: the data cache is **writethrough**, so "the data is written both to
 * the cache and to external memory" on every write that reaches it. An external
 * cycle always happens, and "the MMU is used to validate all accesses that
 * require external bus cycles" -- so a write always consults the MMU, hit or
 * miss.
 *
 * That asymmetry is the whole difference between the two entry points, and it
 * is also why protection works at all: if a write could be answered from the
 * cache alone, a write-protected page already resident would be writable.
 *
 * ## A write to a clean page costs a table search a read would not
 *
 * `[030]` §9.4's consequence, now visible end to end. An ATC entry created by a
 * *read* has M clear, and a write to it "aborts the access and initiates a table
 * search" so the M bit is set in both the entry and the page descriptor. So the
 * first write to a page that has only been read pays for a full table search
 * even though the translation was already cached.
 */

/* Read one long word at a logical address. */
[[nodiscard]] ap_m68030_access_result_t
ap_m68030_access_read(ap_m68030_access_ctx_t *access, uint32_t logical,
                      uint8_t function_code);

/* The same, for a caller that knows how many bytes it actually wants.
 *
 * Identical to the above for anything the board has not inhibited -- memory is
 * read a long word at a time whatever the program asked for, which is what the
 * cache needs. For an inhibited address it runs a bus cycle of exactly `size`
 * bytes, because a device register is not memory and reading past it has
 * effects. The value is returned positioned within the long word exactly as the
 * wide path would place it, so a caller extracting with a shift is unchanged.
 *
 * `size` is 1, 2 or 4 and must not cross the long-word boundary; the operand
 * layer already splits accesses that would. */
[[nodiscard]] ap_m68030_access_result_t
ap_m68030_access_read_sized(ap_m68030_access_ctx_t *access, uint32_t logical,
                            uint8_t function_code, unsigned size);

/* Write one operand of `size` bytes at a logical address, in a single bus
 * cycle -- so the operand must not straddle a long-word boundary; the operand
 * layer above is what splits a misaligned transfer into cycles this can take.
 *
 * The size reaches the store callback, because a byte write is a byte write:
 * telling the memory system every write is four bytes wide would have a byte
 * store clobber its three neighbours. It also feeds the data cache's write
 * allocation rule, which validates an allocated entry only for an *aligned
 * long* -- so the rule is derived here from the size and the address rather
 * than asserted by the caller, which is one fewer thing to get wrong. */
[[nodiscard]] ap_m68030_access_result_t
ap_m68030_access_write(ap_m68030_access_ctx_t *access, uint32_t logical,
                       uint8_t function_code, uint32_t value, unsigned size);

#endif /* APOLLO_CPU_M68030_AP_M68030_ACCESS_H */

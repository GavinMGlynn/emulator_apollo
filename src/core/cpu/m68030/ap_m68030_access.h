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
  bool translation_enabled;

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

  void *context;
} ap_m68030_access_ctx_t;

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

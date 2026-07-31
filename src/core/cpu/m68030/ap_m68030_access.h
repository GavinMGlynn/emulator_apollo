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
  bool cache_disable; /* the CDIS signal, which overrides CACR */
  bool translation_enabled; /* TC's E bit */

  /* The table search's bus access, and the fill's, as callbacks -- the same
   * shape `ap_m68030_walk` and `ap_m68030_cache_read` already use. */
  ap_m68030_fetch_fn table_fetch;
  ap_m68030_update_fn table_update;
  ap_m68030_fill_fn fill;
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

/* Read one long word at a logical address. */
[[nodiscard]] ap_m68030_access_result_t
ap_m68030_access_read(ap_m68030_access_ctx_t *access, uint32_t logical,
                      uint8_t function_code);

#endif /* APOLLO_CPU_M68030_AP_M68030_ACCESS_H */

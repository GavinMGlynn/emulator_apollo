/* MC68040 cache maintenance instruction timing: `CINV` and `CPUSH`.
 *
 * `MC68040 User's Manual (1993)` §10.3, Tables 10-3 and 10-4.
 *
 * ## These figures are formulae, not numbers
 *
 * Every other timing section gives clocks. This one gives clocks plus two free
 * parameters, and says why:
 *
 *   - **`Idle`** -- "the number of clocks required for all pending writes and
 *     instruction prefetches to complete". It depends on what ran *before*:
 *     "the total time required to execute a cache invalidate instruction is
 *     dependent on the previous instruction stream."
 *   - **`Line`** -- "the number of clocks required in the user's system for a
 *     line transfer". It depends on the memory the part is soldered to, which
 *     no CPU manual can know.
 *
 * And for `CPUSH` the manual refuses an equation outright: "since the
 * distribution of dirty data within the cache is entirely dependent on the
 * nature of the user's code, it is impossible to provide an equation for
 * execution time that works for all code sequences. Table 10-4 provides
 * baseline information indicating best and worst case execution times."
 *
 * So this module exposes the *shape* of each figure and leaves the parameters
 * to the caller. A core that folded a guessed `Line` into a constant would be
 * inventing the one number the manual explicitly declines to supply.
 *
 * ## `CPUSHP`'s worst case confirms the cache geometry
 *
 * "11 + 256 x Line + Idle". Two hundred and fifty-six is the number of lines in
 * a cache -- 64 sets of four ways, from §4.1 -- so the worst case is every line
 * dirty and pushed. That the timing table and the cache chapter agree on 256
 * without either citing the other is worth an assertion: two independent
 * statements of one geometry.
 */

#ifndef APOLLO_CPU_M68040_AP_M68040_CACHE_TIMING_H
#define APOLLO_CPU_M68040_AP_M68040_CACHE_TIMING_H

#include <stdint.h>

/* Table 10-3's rows. `CINVL` invalidates a line, `CINVP` a page, `CINVA`
 * everything -- and the page form costs vastly more than either, because it
 * must examine every line to find the ones the page owns. */
typedef enum {
  AP_M68040_CINV_LINE,
  AP_M68040_CINV_PAGE,
  AP_M68040_CINV_ALL,
} ap_m68040_cinv_t;

/* Table 10-3: "9 + Idle" for line and all, "266 + Idle" for page. */
[[nodiscard]] unsigned ap_m68040_cinv_clocks(ap_m68040_cinv_t which,
                                             unsigned idle);

/* Table 10-4's rows. `CPUSHP` and `CPUSHA` share a row. */
typedef enum {
  AP_M68040_CPUSH_LINE,
  AP_M68040_CPUSH_PAGE_OR_ALL,
} ap_m68040_cpush_t;

/* "Best case corresponds to a cache containing no dirty entries" -- so nothing
 * is written back and neither parameter appears. */
[[nodiscard]] unsigned ap_m68040_cpush_best_case(ap_m68040_cpush_t which);

/* "The worst case corresponds to all lines dirty and requiring line pushes."
 * `CPUSHL` pushes one line; `CPUSHP`/`CPUSHA` push all 256. */
[[nodiscard]] unsigned ap_m68040_cpush_worst_case(ap_m68040_cpush_t which,
                                                  unsigned line,
                                                  unsigned idle);

/* The number of line pushes each worst case assumes, which is where the cache
 * geometry enters the timing table. */
[[nodiscard]] unsigned ap_m68040_cpush_worst_case_lines(ap_m68040_cpush_t which);

#endif /* APOLLO_CPU_M68040_AP_M68040_CACHE_TIMING_H */

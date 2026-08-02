/* MC68040 §10.3, Tables 10-3 and 10-4, from the page image. See the header for
 * why these are formulae rather than numbers. */

#include "cpu/m68040/ap_m68040_cache_timing.h"

#include "cpu/m68040/ap_m68040_cache.h"

unsigned ap_m68040_cinv_clocks(ap_m68040_cinv_t which, unsigned idle) {
  switch (which) {
  case AP_M68040_CINV_PAGE:
    /* "266 + Idle". A page invalidate must examine every line to find the ones
     * the page owns, where a line invalidate knows its line already -- which is
     * why this costs nearly thirty times as much. */
    return 266u + idle;
  case AP_M68040_CINV_LINE:
  case AP_M68040_CINV_ALL:
    /* "9 + Idle" for both. Invalidating everything is as cheap as invalidating
     * one line, because clearing every valid bit at once needs no search. */
    return 9u + idle;
  }
  return 9u + idle;
}

unsigned ap_m68040_cpush_best_case(ap_m68040_cpush_t which) {
  /* "Best case corresponds to a cache containing no dirty entries", so nothing
   * is pushed and neither parameter appears. */
  return (which == AP_M68040_CPUSH_LINE) ? 6u : 267u;
}

unsigned ap_m68040_cpush_worst_case_lines(ap_m68040_cpush_t which) {
  /* Table 10-4's multiplier: one line for `CPUSHL`, and for the page and all
   * forms every line in the cache. */
  return (which == AP_M68040_CPUSH_LINE)
             ? 1u
             : (AP_M68040_CACHE_SETS * AP_M68040_CACHE_WAYS);
}

unsigned ap_m68040_cpush_worst_case(ap_m68040_cpush_t which, unsigned line,
                                    unsigned idle) {
  /* "6 + Line + Idle" and "11 + 256 x Line + Idle". */
  const unsigned fixed = (which == AP_M68040_CPUSH_LINE) ? 6u : 11u;
  return fixed + ap_m68040_cpush_worst_case_lines(which) * line + idle;
}

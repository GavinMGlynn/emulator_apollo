/* MC68030 translation table search (the table walk).
 *
 * `[030]` §9.2 and §9.5, cited throughout. This is the module that joins the
 * other four: it uses `ap_m68030_tc` to split the logical address, walks the
 * tree fetching descriptors, applies `ap_m68030_desc`'s rules to each, and
 * produces what `ap_m68030_atc` will cache.
 *
 * ## Why it counts fetches
 *
 * The ATC costs nothing on a hit (`[030]` §9.4). *All* MMU time is here, in the
 * descriptor fetches a miss has to perform, and each of those is a real bus
 * cycle through `ap_m68030_bus`. So the walk reports `descriptor_fetches`: it
 * is the quantity a timing probe measures, and the reason a three-level tree
 * costs more than an early-terminating one.
 *
 * ## Descriptors arrive decoded, and that is deliberate
 *
 * The walk asks its caller for a descriptor rather than reading raw long words,
 * because the descriptor *status* bit positions did not survive the scan.
 * `[030]` Figures 9-10 and 9-11 do give the word-level structure -- a
 * short-format table descriptor is a 28-bit TABLE ADDRESS at 31-4 over a 4-bit
 * status field, and a long-format one is LIMIT at 31-16 with a 16-bit status
 * word, then TABLE ADDRESS at 31-4 -- but which status bit is U, WP, DT, LU, S,
 * CI or M is lost, exactly as it was for the transparent translation registers.
 *
 * So the *walk* is implemented and tested in full, and the bit unpacking is a
 * named tail rather than an invention. This mirrors `ap_m68030_tt.h`; the
 * project's rule is that a layout is transcribed or deferred, never guessed.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_WALK_H
#define APOLLO_CPU_M68030_AP_M68030_WALK_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_desc.h"
#include "cpu/m68030/ap_m68030_tc.h"

/* A descriptor as the walk needs it, already decoded from memory. */
typedef struct {
  ap_m68030_dt_t dt;
  uint32_t address_field; /* TABLE, PAGE, or DESCRIPTOR ADDRESS, as the DT says */
  uint16_t limit;         /* long format only */
  bool lower_limit;       /* LU: limit is a lower bound rather than an upper */
  bool has_limit;         /* short-format descriptors carry no limit field */
  bool write_protect;     /* WP */
  bool supervisor;        /* S */
  bool cache_inhibit;     /* CI */
  bool modified;          /* M */
} ap_m68030_descriptor_t;

/* Fetch the descriptor at a physical address. Returns false for a bus error,
 * which `[030]` §9.4 says sets the B bit of the resulting ATC entry.
 * `long_format` says whether the table being indexed holds 8-byte descriptors,
 * which the *previous* descriptor's DT determined. */
typedef bool (*ap_m68030_fetch_fn)(void *context, uint32_t physical,
                                   bool long_format,
                                   ap_m68030_descriptor_t *out);

typedef struct {
  bool ok;               /* a translation was produced */
  uint32_t physical;     /* the translated address, when ok */
  ap_m68030_search_t search; /* accumulated WP/S/CI and the failure flags */
  unsigned descriptor_fetches; /* bus cycles the search cost */
  unsigned levels_walked;
  bool early_termination; /* ended on a page descriptor above the page table */
  bool used_indirect;
} ap_m68030_walk_result_t;

/* Where the walk starts: the root pointer's table address and the format of the
 * table it points at, which the root pointer's own DT gives. */
typedef struct {
  uint32_t table_address;
  bool long_format;
  uint16_t limit;
  bool lower_limit;
  bool has_limit;
} ap_m68030_root_t;

/* Perform a table search for `address`.
 *
 * Applies the limit check at each level, accumulates protection down the tree,
 * follows an indirect descriptor at the bottom, and honours an early
 * termination page descriptor by taking every remaining logical address bit as
 * page offset. */
[[nodiscard]] ap_m68030_walk_result_t
ap_m68030_walk(const ap_m68030_tc_t *tc, const ap_m68030_root_t *root,
               uint32_t address, ap_m68030_fetch_fn fetch, void *context);

#endif /* APOLLO_CPU_M68030_AP_M68030_WALK_H */

/* MC68040 `MOVE` instruction timing.
 *
 * `MC68040 User's Manual (1993)` §10.4, transcribed from the page images of
 * pages 10-9 and 10-10.
 *
 * ## One instruction, one hundred and eighty figures
 *
 * `MOVE` is the only instruction the manual prices as a full cross product --
 * fifteen source modes against twelve destination modes -- because it is the
 * only one whose two effective addresses are both general. Every other
 * instruction has at most one `<ea>` and fits a one-dimensional table.
 *
 * ## The source is `BR`-relative and the destination is not
 *
 * The source column names `(bd,BR,Xn)` and `([bd,BR],Xn)` where the destination
 * column names `(bd,An,Xn)` and `([bd,An],Xn)`. That is not an inconsistency:
 * a `MOVE` destination cannot be program-counter relative, so its base register
 * is always an address register, while a source may be either. §10.1's
 * supposition 1 then applies to the source alone -- "for BR = PC, 1 and 1L
 * clocks to the <ea> calculate and execution times" -- which is why the table
 * lists `(d16,PC)` and `(d8,PC,Xn)` as separate source rows but has no
 * PC-relative destination at all.
 *
 * ## What the shape of the table shows
 *
 * Two patterns are worth stating because they make transcription errors
 * visible:
 *
 *   - For every destination from `(d8,An,Xn)` onward, the first eight source
 *     rows -- the ones with no index register -- carry *identical* figures.
 *     The destination's own address calculation dominates and the source
 *     contributes nothing beyond its fetch.
 *   - A `(d16,PC)` source always costs more than the `(d16,An)` source in the
 *     same column, and by a margin that grows with the destination's
 *     complexity. That is supposition 1's `1L` compounding through the
 *     interlock, not a flat penalty.
 */

#ifndef APOLLO_CPU_M68040_AP_M68040_MOVE_TIMING_H
#define APOLLO_CPU_M68040_AP_M68040_MOVE_TIMING_H

#include <stddef.h>
#include <stdint.h>

#include "cpu/m68040/ap_m68040_timing.h"

/* §10.4's source column, in the manual's order. */
typedef enum {
  AP_M68040_MOVE_SRC_DN,
  AP_M68040_MOVE_SRC_INDIRECT,
  AP_M68040_MOVE_SRC_POSTINCREMENT,
  AP_M68040_MOVE_SRC_PREDECREMENT,
  AP_M68040_MOVE_SRC_DISPLACEMENT,
  AP_M68040_MOVE_SRC_PC_DISPLACEMENT,
  AP_M68040_MOVE_SRC_ABSOLUTE,
  AP_M68040_MOVE_SRC_IMMEDIATE,
  AP_M68040_MOVE_SRC_INDEXED,
  AP_M68040_MOVE_SRC_PC_INDEXED,
  AP_M68040_MOVE_SRC_BASE_DISPLACEMENT,
  AP_M68040_MOVE_SRC_MEMORY_PREINDEXED,
  AP_M68040_MOVE_SRC_MEMORY_PREINDEXED_OD,
  AP_M68040_MOVE_SRC_MEMORY_POSTINDEXED,
  AP_M68040_MOVE_SRC_MEMORY_POSTINDEXED_OD,
  AP_M68040_MOVE_SRC_COUNT,
} ap_m68040_move_source_t;

/* §10.4's destination column. Three source modes have no destination
 * counterpart -- the two PC-relative forms and immediate -- because a `MOVE`
 * cannot write to any of them. */
typedef enum {
  AP_M68040_MOVE_DST_DN,
  AP_M68040_MOVE_DST_INDIRECT,
  AP_M68040_MOVE_DST_POSTINCREMENT,
  AP_M68040_MOVE_DST_PREDECREMENT,
  AP_M68040_MOVE_DST_DISPLACEMENT,
  AP_M68040_MOVE_DST_ABSOLUTE,
  AP_M68040_MOVE_DST_INDEXED,
  AP_M68040_MOVE_DST_BASE_DISPLACEMENT,
  AP_M68040_MOVE_DST_MEMORY_PREINDEXED,
  AP_M68040_MOVE_DST_MEMORY_PREINDEXED_OD,
  AP_M68040_MOVE_DST_MEMORY_POSTINDEXED,
  AP_M68040_MOVE_DST_MEMORY_POSTINDEXED_OD,
  AP_M68040_MOVE_DST_COUNT,
} ap_m68040_move_destination_t;

typedef struct {
  unsigned calculate;
  ap_m68040_execute_t execute;
} ap_m68040_move_timing_t;

[[nodiscard]] ap_m68040_move_timing_t
ap_m68040_move_timing(ap_m68040_move_source_t source,
                      ap_m68040_move_destination_t destination);

/* Whether a source mode carries an index register, and so whether §10.1's
 * interlock reaches it. The first eight source rows do not. */
[[nodiscard]] bool
ap_m68040_move_source_is_indexed(ap_m68040_move_source_t source);

#endif /* APOLLO_CPU_M68040_AP_M68040_MOVE_TIMING_H */

/* MC68040 instruction and data caches.
 *
 * `MC68040 User's Manual (1993)` §4.1 and Figure 4-2.
 *
 * ## The third cache organisation in this core, and the first that is set
 * associative
 *
 * "Both four-way set-associative caches have 64 sets of four 16-byte lines."
 * That is 4 Kbytes each, against 256 bytes on both earlier parts, and the
 * arrangement differs from either:
 *
 *     68020   64 entries of one long word, direct mapped, logical tag + FC2
 *     68030   16 lines of four long words, direct mapped
 *     68040   64 sets x 4 ways x four long words, **physically** tagged
 *
 * The physical tag is the change that matters beyond size. An earlier part
 * caches by logical address, so a context switch can alias; the 68040 caches
 * what the MMU produced, so its lines survive a switch and its snoop logic can
 * compare against bus addresses directly.
 *
 * ## Only the data cache has dirty state, and it has four of them
 *
 * "The status information for the instruction cache line address tag consists
 * of a single valid bit for the entire line. The status information for the
 * data cache line address tag contains a valid bit and four additional bits to
 * indicate dirty status for each long word in the line."
 *
 * A dirty bit *per long word*, not per line. So a copyback of a partly-written
 * line writes back only the long words that changed, and a model with one dirty
 * bit per line would write back clean data -- harmless to memory contents and
 * wrong in the bus traffic a probe would measure.
 *
 * ## "Pseudo-random" replacement is fully deterministic
 *
 * "Each cache contains a 2-bit counter, which is incremented for each access to
 * the cache ... When a miss occurs and all four lines in the set are valid, the
 * line pointed to by the current counter" is replaced. One counter per *cache*,
 * not per set. Motorola's name for it is misleading and the behaviour is
 * exactly reproducible, which is what a reference core needs.
 *
 * An invalid line is always preferred: "if all lines in the set are already
 * valid, a pseudo-random replacement algorithm is used" -- so the counter only
 * matters once a set is full.
 *
 * ## Reset does not clear them
 *
 * "Both caches should be explicitly cleared after a hardware reset of the
 * processor since reset does not invalidate the cache lines." The third reset
 * trap in this part, after the `TCR` page size and the ATCs.
 */

#ifndef APOLLO_CPU_M68040_AP_M68040_CACHE_H
#define APOLLO_CPU_M68040_AP_M68040_CACHE_H

#include <stdbool.h>
#include <stdint.h>

#define AP_M68040_CACHE_SETS 64u
#define AP_M68040_CACHE_WAYS 4u
#define AP_M68040_CACHE_LINE_LONGS 4u
#define AP_M68040_CACHE_LINE_BYTES 16u
#define AP_M68040_CACHE_BYTES 4096u

typedef struct {
  /* "An address tag consisting of the upper 22 bits of the physical address." */
  uint32_t tag;
  bool valid;
  /* "Four additional bits to indicate dirty status for each long word in the
   * line." Meaningful in the data cache only: "only the data cache supports
   * dirty cache lines." */
  bool dirty[AP_M68040_CACHE_LINE_LONGS];
  uint32_t data[AP_M68040_CACHE_LINE_LONGS];
} ap_m68040_cache_line_t;

typedef struct {
  ap_m68040_cache_line_t line[AP_M68040_CACHE_SETS][AP_M68040_CACHE_WAYS];
  /* The replacement counter, one per cache. Two bits, so it wraps across the
   * four ways. */
  unsigned counter;
  /* Whether this cache keeps dirty state. The instruction cache does not, and
   * saying so here rather than in two near-identical types keeps one line
   * format with one lookup. */
  bool has_dirty_state;
} ap_m68040_cache_t;

/* "A cache line is always in one of three states." Reported as a state rather
 * than two bits because the three are not independent: dirty implies valid,
 * and "valid lines have their V-bit set and D-bits cleared". */
typedef enum {
  AP_M68040_LINE_INVALID,
  AP_M68040_LINE_VALID, /* consistent with memory */
  AP_M68040_LINE_DIRTY, /* one or more long words not written back */
} ap_m68040_line_state_t;

[[nodiscard]] ap_m68040_line_state_t
ap_m68040_cache_line_state(const ap_m68040_cache_line_t *line);

void ap_m68040_cache_init(ap_m68040_cache_t *cache, bool has_dirty_state);

/* Invalidate every line. Not what reset does -- see the header -- but what
 * `CINV` with an "all" scope does, and what software must issue after reset. */
void ap_m68040_cache_invalidate_all(ap_m68040_cache_t *cache);

/* The set index: address bits 9-4, since 64 sets of 16-byte lines account for
 * the low ten bits. */
[[nodiscard]] unsigned ap_m68040_cache_set(uint32_t address);

/* The long word within a line: address bits 3-2. */
[[nodiscard]] unsigned ap_m68040_cache_long(uint32_t address);

/* The tag: the upper 22 bits of the *physical* address. */
[[nodiscard]] uint32_t ap_m68040_cache_tag(uint32_t address);

/* Find the way holding this address in its set, or `AP_M68040_CACHE_WAYS` for a
 * miss. Set associative, so every way in the set is compared and none outside
 * it is. */
[[nodiscard]] unsigned ap_m68040_cache_lookup(const ap_m68040_cache_t *cache,
                                              uint32_t address);

/* Choose the way a new line will occupy: an invalid one if the set has any,
 * else the one the counter points at. */
[[nodiscard]] unsigned ap_m68040_cache_select_way(const ap_m68040_cache_t *cache,
                                                  uint32_t address);

/* "A 2-bit counter, which is incremented for each access to the cache." Kept
 * separate from lookup and fill because the manual counts *accesses* -- half
 * lines read, full lines written in copyback, bus transfers in write-through --
 * and only the caller knows which of those just happened. */
void ap_m68040_cache_tick(ap_m68040_cache_t *cache);

/* Install a line. `dirty_mask` is one bit per long word and is ignored by a
 * cache with no dirty state. */
void ap_m68040_cache_fill(ap_m68040_cache_t *cache, unsigned way,
                          uint32_t address,
                          const uint32_t data[AP_M68040_CACHE_LINE_LONGS],
                          unsigned dirty_mask);

/* Mark one long word written. Refused by a cache with no dirty state, since
 * "only the data cache supports dirty cache lines". */
void ap_m68040_cache_mark_dirty(ap_m68040_cache_t *cache, unsigned way,
                                uint32_t address);

/* Which long words a copyback must write back: one bit per long word. Zero for
 * a clean or invalid line, and never the whole line merely because part of it
 * changed. */
[[nodiscard]] unsigned
ap_m68040_cache_writeback_mask(const ap_m68040_cache_line_t *line);

#endif /* APOLLO_CPU_M68040_AP_M68040_CACHE_H */

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
 *
 * ## The line state machine is a table, so it is encoded as one
 *
 * §4.7's Table 4-3 (instruction cache, six operations x two states) and Table
 * 4-4 (data cache, thirteen operations x three states) give every transition
 * with its actions, including the cells that cannot occur. `ap_m68040_cache_
 * transition` returns a row of them rather than scattering the rules through
 * whatever eventually drives the cache, because the tables are the
 * specification and a table is checkable against the page.
 *
 * Three of its rows are the ones a plausible model gets wrong:
 *
 *   - **`CPUSH` invalidates.** Table 4-4 D8 is "write dirty data to memory; go
 *     to invalid state", and V8 and I8 also end invalid. On this part a push is
 *     a push *and* an invalidate; a model that only writes back leaves a line
 *     the manual says is gone.
 *   - **A clean line hit by a sinking snoop write does not sink.** V12 and V13
 *     are both "no action; go to invalid state"; only a *dirty* line takes the
 *     alternate master's data (D12). So the sink path is reachable from one
 *     state out of three.
 *   - **`CINV` on a dirty line loses the data**, in as many words: "no action
 *     (dirty data lost)". That is the whole difference between the two
 *     instructions and it is the reason `CINV` is not a cheap `CPUSH`.
 *
 * ## Table 4-1's write column is misprinted, and the manual corrects itself
 *
 * The `SC1-SC0 = 01` write cell reads "Sink Byte/Word/Long/Long Word" -- read
 * on the page image at 600 dpi, so this is the print and not an extraction.
 * The four transfer sizes of this part are byte, word, long word and line, so
 * "Long/Long Word" is a duplication with `Line` lost out of it. Two other
 * passages in the same manual give the partition unambiguously: Table 4-4
 * splits the sinking rows into "Size != Line" (D12, sink into the line, stay
 * dirty) and "Size = Line" (D13, go invalid), and §4.4 says "for snooped writes
 * of byte, word, or long-word size that hit a dirty line, the processor inhibits
 * memory and responds to the alternate bus master as a slave, sinking the data".
 * Encoded from those.
 *
 * ## Nothing drives this yet
 *
 * The snoop transitions need an alternate bus master and the fill and push
 * transitions need the bus controller of §7; this part has neither in this core
 * yet, and there is no 68040 stepper to issue the CPU cases. The table is
 * modelled anyway because it is what the module is *for*: `[040]` §4 is a
 * finished specification, and a transition nobody calls is still one nobody has
 * to derive later.
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

/* ---------------------------------------------------------------------------
 * Snoop control, `[040]` Table 4-1.
 * ------------------------------------------------------------------------- */

/* What the alternate bus master is asking for, decoded from the two snoop
 * control pins. The read and write columns of Table 4-1 name different
 * operations for the same encoding, so the decode takes the direction. */
typedef enum {
  AP_M68040_SNOOP_INHIBIT,             /* both columns of SC = 00 and 11 */
  AP_M68040_SNOOP_SUPPLY_LEAVE_DIRTY,  /* read, SC = 01 */
  AP_M68040_SNOOP_SUPPLY_MARK_INVALID, /* read, SC = 10 */
  AP_M68040_SNOOP_SINK,                /* write, SC = 01 */
  AP_M68040_SNOOP_INVALIDATE,          /* write, SC = 10 */
} ap_m68040_snoop_request_t;

/* `sc` is SC1-SC0 as a two-bit value. "Reserved (Snoop Inhibited)" for 11, so
 * it decodes to the same request as 00 rather than to an error: the pins are an
 * input from another master and the part does not fault on them. */
[[nodiscard]] ap_m68040_snoop_request_t ap_m68040_snoop_request(unsigned sc,
                                                                bool write);

/* ---------------------------------------------------------------------------
 * Line state transitions, `[040]` Tables 4-3 and 4-4.
 * ------------------------------------------------------------------------- */

/* The rows of Table 4-4. Hit and miss are part of the operation because the
 * tables make them so -- "CPU Read Miss" and "CPU Read Hit" are separate rows
 * with different actions -- and the snoop rows carry their snoop control
 * encoding and transfer size for the same reason. Table 4-3's six rows are the
 * same enumeration with the dirty-only cases unreachable. */
typedef enum {
  AP_M68040_CACHE_OP_CPU_READ_MISS,                /* 1 */
  AP_M68040_CACHE_OP_CPU_READ_HIT,                 /* 2 */
  AP_M68040_CACHE_OP_CPU_WRITE_MISS_COPYBACK,      /* 3 */
  AP_M68040_CACHE_OP_CPU_WRITE_MISS_WRITE_THROUGH, /* 4 */
  AP_M68040_CACHE_OP_CPU_WRITE_HIT_COPYBACK,       /* 5 */
  AP_M68040_CACHE_OP_CPU_WRITE_HIT_WRITE_THROUGH,  /* 6 */
  AP_M68040_CACHE_OP_CINV,                         /* 7 */
  AP_M68040_CACHE_OP_CPUSH,                        /* 8 */
  AP_M68040_CACHE_OP_SNOOP_READ_LEAVE_DIRTY,       /* 9, SC = 01 */
  AP_M68040_CACHE_OP_SNOOP_READ_INVALIDATE,        /* 10, SC = 10 */
  AP_M68040_CACHE_OP_SNOOP_WRITE_INVALIDATE,       /* 11, SC = 10 */
  AP_M68040_CACHE_OP_SNOOP_WRITE_SINK_PARTIAL,     /* 12, SC = 01, size != line */
  AP_M68040_CACHE_OP_SNOOP_WRITE_SINK_LINE,        /* 13, SC = 01, size  = line */
} ap_m68040_cache_op_t;

/* One cell of Table 4-3 or 4-4: the state the line ends in, and each action the
 * cell names. Separate flags rather than an action enum because several cells
 * name three or four at once -- D1 buffers the dirty line, reads a new one,
 * supplies the CPU, updates the cache and writes the buffer back. */
typedef struct {
  /* False for the cells that read "Not Possible" or "Not possible; not
   * snooped". Every other field is then meaningless and `next` holds `state`. */
  bool possible;
  ap_m68040_line_state_t next;
  bool read_line;       /* "read line from memory" */
  bool supply_to_cpu;   /* "supply data to CPU" */
  bool write_to_cache;  /* "write data to cache" */
  bool set_dirty;       /* "set Dn bits of modified long words" */
  bool write_to_memory; /* "write data to memory" */
  bool buffer_dirty;    /* "buffer dirty cache line ... write buffered dirty
                         * data to memory" -- the push buffer of §4.6.2 */
  bool push_dirty;      /* "write dirty data to memory", `CPUSH` */
  bool dirty_data_lost; /* "no action (dirty data lost)", `CINV` on D7 */
  bool inhibit_memory;  /* "inhibit memory", the slave response */
  bool source_data;     /* "source data" to the alternate master */
  bool sink_data;       /* "sink data" from the alternate master */
  /* Table 4-4's NOTE: "dirty state transitions D4 and D6 are the result of a
   * system programming error and should be avoided even though they are
   * technically valid". Reported, not refused -- the manual calls them valid. */
  bool programming_error;
} ap_m68040_cache_transition_t;

/* Look up one cell. `has_dirty_state` picks the table: false is Table 4-3, the
 * instruction cache, where the dirty state never occurs and the two snoop write
 * encodings share a row (V6). */
[[nodiscard]] ap_m68040_cache_transition_t
ap_m68040_cache_transition(bool has_dirty_state, ap_m68040_line_state_t state,
                           ap_m68040_cache_op_t op);

#endif /* APOLLO_CPU_M68040_AP_M68040_CACHE_H */

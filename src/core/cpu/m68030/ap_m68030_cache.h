/* MC68030 on-chip instruction and data caches.
 *
 * `[030]` §6, cited throughout.
 *
 * "Both on-chip caches are 256-byte direct-mapped caches, each organized as 16
 * lines. Each line consists of four entries, and each entry contains four
 * bytes. The tag field for each line contains a valid bit for each entry in the
 * line; each entry is independently replaceable."
 *
 * ## The valid bit is per *entry*, not per line
 *
 * That one sentence shapes the whole module. A line has a single tag but four
 * independent valid bits, so a line can legitimately hold one valid long word
 * and three invalid ones. Modelling validity per line -- the obvious
 * simplification -- would make a burst fill and a single-entry fill
 * indistinguishable, and those cost very different numbers of bus cycles. It
 * would also make `CEI`/`CED` (clear *one* entry) unimplementable.
 *
 * ## Address decomposition
 *
 * From Figures 6-2 and 6-3, whose address rows survive the scan:
 *
 *     A1-A0    byte within the long word
 *     A3-A2    long word select -- which of the four entries
 *     A7-A4    1 of 16 line select
 *     A31-A8   tag, together with FC2-FC0
 *
 * The function code is part of the tag in *both* caches. §6.1.2 says the data
 * cache tag "contains function code bits FC0, FC1, and FC2 in addition to
 * address bits A31-A8", and Figure 6-2 shows the same three bits on the
 * instruction side. So a supervisor and a user access to the same address are
 * different entries, which is what makes the caches safe across a context
 * switch without a flush.
 *
 * ## What this module is, and is not
 *
 * This is the cache's *structure and policy*: what hits, what fills, what a
 * write does, and what `CACR` does to it. It deliberately does not model the
 * bus cycles a miss costs -- that is the join with `ap_m68030_bus`, exactly as
 * the ATC's cost lives in `ap_m68030_walk` rather than in `ap_m68030_atc`.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_CACHE_H
#define APOLLO_CPU_M68030_AP_M68030_CACHE_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_bus.h"

/* "each organized as 16 lines. Each line consists of four entries" */
#define AP_M68030_CACHE_LINES 16
#define AP_M68030_CACHE_ENTRIES 4
#define AP_M68030_CACHE_BYTES                                                  \
  (AP_M68030_CACHE_LINES * AP_M68030_CACHE_ENTRIES * 4)

typedef struct {
  uint32_t tag;                          /* FC2-FC0 and A31-A8 */
  bool valid[AP_M68030_CACHE_ENTRIES];   /* one per entry, per §6.1 */
  uint32_t entry[AP_M68030_CACHE_ENTRIES];
} ap_m68030_cache_line_t;

typedef struct {
  ap_m68030_cache_line_t line[AP_M68030_CACHE_LINES];
} ap_m68030_cache_t;

/* Decomposition helpers, so no caller re-derives the field boundaries. */
[[nodiscard]] unsigned ap_m68030_cache_line_index(uint32_t address);
[[nodiscard]] unsigned ap_m68030_cache_entry_index(uint32_t address);
[[nodiscard]] uint32_t ap_m68030_cache_tag(uint32_t address,
                                           uint8_t function_code);

/* Invalidate every entry. "The processor clears all valid bits" -- the tags and
 * data are untouched, which is what the manual describes and is observable:
 * disabling a cache "does not flush the entries. If it is enabled again, the
 * previously valid entries remain valid and can be used." */
void ap_m68030_cache_clear(ap_m68030_cache_t *cache);

/* Clear one entry, as CEI and CED do. "The processor clears only the specified
 * long word by clearing the valid bit for the entry ... regardless of the
 * states of the [enable] and [freeze] bits." */
void ap_m68030_cache_clear_entry(ap_m68030_cache_t *cache, uint32_t address);

/* Look up one long word. A miss leaves `*out` untouched. */
[[nodiscard]] bool ap_m68030_cache_lookup(const ap_m68030_cache_t *cache,
                                          uint32_t address,
                                          uint8_t function_code, uint32_t *out);

/* Fill one entry. A tag mismatch replaces the tag and invalidates the other
 * three entries, since they described a different address. */
void ap_m68030_cache_fill_entry(ap_m68030_cache_t *cache, uint32_t address,
                                uint8_t function_code, uint32_t value);

/* Fill a whole line, which is what a burst does: "the bus controller requests a
 * burst mode operation to replace an entire cache line." `values` is in
 * long-word-select order, entry 0 first. */
void ap_m68030_cache_fill_line(ap_m68030_cache_t *cache, uint32_t address,
                               uint8_t function_code,
                               const uint32_t values[AP_M68030_CACHE_ENTRIES]);

/* What a data-cache write did, which the bus join needs in order to charge for
 * it. The cache is writethrough, so an external write happens either way; this
 * reports only the *cache's* part. */
typedef enum {
  AP_M68030_CACHE_WRITE_HIT,        /* entry updated in place */
  AP_M68030_CACHE_WRITE_ALLOCATED,  /* tag replaced, this entry validated */
  AP_M68030_CACHE_WRITE_INVALIDATED,/* the write cleared valid bits instead */
  AP_M68030_CACHE_WRITE_UNTOUCHED,  /* the cache was not altered at all */
} ap_m68030_cache_write_t;

/* Apply a *long-word aligned* write to the data cache.
 *
 * "The data cache on the MC68030 is a writethrough cache. When a hit occurs on
 * a write cycle, the data is written both to the cache and to external memory
 * ... regardless of the operand size and even if the cache is frozen."
 *
 * The miss behaviour is what `WA` selects, and the manual's two cases are worth
 * keeping apart because the scan runs them into one sentence -- the boundary
 * between them is lost, and the reconstruction rests on the summary line that
 * follows, "an aligned long-word data write may replace a previously valid
 * entry; whereas, a misaligned data write or a write of data that is not long
 * word may invalidate a previously valid entry or entries":
 *
 *   WA = 0  "write cycles that miss do not alter the data cache contents ...
 *            The cache is updated only during a write hit."
 *   WA = 1  a tag miss on an aligned long-word write replaces the tag, validates
 *           only the long word written, and invalidates the other three; a tag
 *           miss on a misaligned or sub-long-word write writes nothing, leaves
 *           the tag alone, and clears the valid bit(s).
 *
 * `aligned_long_word` says which of those two the access is. */
ap_m68030_cache_write_t
ap_m68030_cache_write(ap_m68030_cache_t *cache, uint32_t address,
                      uint8_t function_code, uint32_t value,
                      bool aligned_long_word, bool write_allocate, bool frozen,
                      unsigned size);

/* The cache control register, `[030]` §6.3.1 pp. 6-21 ff.
 *
 * The bit positions are transcribed from prose rather than from Figure 6-14,
 * which is how they survived: "Bit 13, the WA bit", "Bit 12, the [D]BE bit",
 * "Bit 11, the C[D] bit", "Bit [10], the CE[D] bit", "Bit 9, the FD bit",
 * "Bit 8, the ED bit", "Bit 4, the IBE bit", "Bit 3, the CI bit", "Bit 2, the
 * CEI bit", "Bit 1, the FI bit", "Bit 0, the EI bit". */
#define AP_M68030_CACR_WA_BIT 13u
#define AP_M68030_CACR_DBE_BIT 12u
#define AP_M68030_CACR_CD_BIT 11u
#define AP_M68030_CACR_CED_BIT 10u
#define AP_M68030_CACR_FD_BIT 9u
#define AP_M68030_CACR_ED_BIT 8u
#define AP_M68030_CACR_IBE_BIT 4u
#define AP_M68030_CACR_CI_BIT 3u
#define AP_M68030_CACR_CEI_BIT 2u
#define AP_M68030_CACR_FI_BIT 1u
#define AP_M68030_CACR_EI_BIT 0u

/* Only the *persistent* bits are state. CD, CED, CI and CEI are actions taken
 * when the register is written and are "always read as zero", so they are not
 * fields here -- storing them would invent a readable bit the hardware does not
 * have. */
typedef struct {
  bool write_allocate;           /* WA */
  bool data_burst_enable;        /* DBE */
  bool freeze_data;              /* FD */
  bool enable_data;              /* ED */
  bool instruction_burst_enable; /* IBE */
  bool freeze_instruction;       /* FI */
  bool enable_instruction;       /* EI */
} ap_m68030_cacr_t;

[[nodiscard]] uint32_t ap_m68030_cacr_pack(const ap_m68030_cacr_t *cacr);

/* Write CACR, performing the clear actions the write requests.
 *
 * The clears happen "at the time a MOVEC instruction loads a one into" the bit,
 * so they are part of the write rather than a later effect, and the entry
 * clears use the CAAR index -- "The index field of the CAAR ... corresponding
 * to the index and long-word select portion of an address specifies the entry
 * to be cleared." */
void ap_m68030_cacr_write(ap_m68030_cacr_t *cacr, uint32_t word,
                          ap_m68030_cache_t *instruction,
                          ap_m68030_cache_t *data, uint32_t caar);

/* Whether the processor asserts CBREQ for this access — that is, whether a miss
 * asks the memory system for a whole line rather than one long word.
 *
 * `[030]` §7.3.7 gives two conditions, and it is an **or**, not an and:
 * "Either of the following conditions cause the MC68030 to initiate a cache
 * burst request (and assert CBREQ) for a cachable read cycle: The logical
 * address and function code signals ... do not match the indexed tag field ...
 * [or] All four long words corresponding to the indexed tag in the appropriate
 * cache are marked invalid."
 *
 * The second is the one worth stating aloud: a line whose tag *does* match but
 * whose entries are all invalid still bursts. Requiring a tag mismatch would
 * make a cleared cache refill one long word at a time.
 *
 * Three things suppress it outright. "If the appropriate cache is not enabled
 * or if the cache freeze bit for the cache is set, the processor does not
 * assert CBREQ. CBREQ is not asserted during the read or write cycles of any
 * read-modify-write operation." And the whole mechanism "is enabled by the data
 * burst enable (DBE) and instruction burst enable (IBE) bits".
 *
 * This is the cache's half of the timing join: whether a burst is *requested*.
 * Whether it happens is the memory system's answer — burst runs only "from
 * 32-bit ports that terminate bus cycles with STERM and respond to CBREQ by
 * asserting CBACK". */
[[nodiscard]] bool ap_m68030_cache_burst_request(const ap_m68030_cache_t *cache,
                                                 uint32_t address,
                                                 uint8_t function_code,
                                                 bool burst_enable,
                                                 bool cache_enabled,
                                                 bool frozen,
                                                 bool read_modify_write);

/* Whether a cache may be used for an access, given CACR and the two hardware
 * signals that override it.
 *
 * "System hardware can assert the cache disable (CDIS) signal to disable both
 * caches. The assertion of CDIS disables the caches, regardless of the state of
 * the enable bits in CACR." And CIOUT, driven from the MMU's CI bit, means "the
 * instruction and data caches are ignored for the access". */
[[nodiscard]] bool ap_m68030_cache_enabled(bool enable_bit, bool cache_disable,
                                           bool cache_inhibit);

/* ---------------------------------------------------------------------------
 * The miss cost, end to end.
 *
 * This is the join the plan item's verification is really about: a hit costs no
 * external bus cycle at all -- "Whenever a read access occurs and the required
 * instruction word or data operand is resident in the appropriate on-chip cache
 * (no external bus cycle is required), the MMU is completely ignored" -- and a
 * miss costs whatever the bus charges, which is 5 clocks for a burst line fill
 * against 8 for four single reads.
 *
 * The same split as the MMU: `ap_m68030_atc` holds the cache and
 * `ap_m68030_walk` spends the time. Here `ap_m68030_cache` holds the lines and
 * this function spends the time, through `ap_m68030_bus`.
 * ------------------------------------------------------------------------- */

/* What the memory system answers when asked to fill. `data` is in long-word
 * select order -- entry 0 is the line's lowest long word -- and only the first
 * element is used when the fill is not a burst.
 *
 * Modelling note: the 68030's burst supplies the line's four long words, and
 * the order in which a real device presents them relative to the requested one
 * is a property of the memory system rather than of the processor. Indexing by
 * position keeps that the device's business, which is where it belongs. */
/* Wait states the addressed device inserts before it answers a cycle, whole
 * clocks each. `read` distinguishes the two directions, since a port may answer
 * a write faster than a read or the reverse.
 *
 * Declared here rather than beside the access context because this is where the
 * cycle it lengthens is actually driven, and because `ap_m68030_access.h`
 * includes this file and not the reverse. */
typedef unsigned (*ap_m68030_wait_states_fn)(void *context, uint32_t physical,
                                             bool read);

/* `CIIN`: whether the *board* inhibits caching for this address.
 *
 * `[030]` §6.1.3 makes it the system's job -- "the cache inhibit in (CIIN)
 * signal ... allows the system to inhibit caching on a cycle-by-cycle basis" --
 * because nothing in the processor knows which addresses are registers. A core
 * without it caches device registers, and a firmware polling a status bit then
 * reads it once from the bus and forever out of the cache.
 *
 * NULL means nothing is inhibited, which is what a machine on flat RAM wants:
 * there are no devices to protect and no probe figure moves. */
typedef bool (*ap_m68030_cache_inhibit_fn)(void *context, uint32_t address);

/* A read of exactly `size` bytes at exactly `address`. See
 * `ap_m68030_access.h` for why a device needs one and memory does not. */
typedef bool (*ap_m68030_read_sized_fn)(void *context, uint32_t address,
                                        unsigned size, uint32_t *value);

typedef struct {
  ap_m68030_term_t termination; /* STERM, DSACK or BERR */
  bool burst_acknowledge;       /* CBACK -- only a 32-bit STERM port asserts it */
  uint32_t data[AP_M68030_BURST_BEATS];
} ap_m68030_fill_answer_t;

/* Asked once per fill, with the line's base address. */
typedef void (*ap_m68030_fill_fn)(void *context, uint32_t line_address,
                                  uint8_t function_code,
                                  ap_m68030_fill_answer_t *out);

typedef struct {
  bool hit;            /* answered from the cache, at no bus cost */
  uint32_t value;      /* the long word, when there is one */
  uint32_t clocks;     /* external clocks spent -- zero on a hit */
  bool burst;          /* the fill ran as a burst */
  unsigned long_words; /* long words actually transferred */
  bool bus_error;
} ap_m68030_cache_access_t;

/* Read one long word through the cache, filling on a miss.
 *
 * `cache_enabled` is the result of `ap_m68030_cache_enabled`, so CDIS and CIOUT
 * are already folded in: a disabled or inhibited access still *runs*, it simply
 * neither reads nor fills the cache. */
ap_m68030_cache_access_t
ap_m68030_cache_read(ap_m68030_cache_t *cache, uint32_t address,
                     uint8_t function_code, bool cache_enabled,
                     bool burst_enable, bool frozen, bool read_modify_write,
                     ap_m68030_fill_fn fill,
                     ap_m68030_wait_states_fn wait_states, void *context);

#endif /* APOLLO_CPU_M68030_AP_M68030_CACHE_H */

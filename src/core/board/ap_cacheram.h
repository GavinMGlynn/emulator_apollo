/* The Series 4000 virtual cache and its board-visible RAM: `[S3K]` §1.3.1,
 * §1.3.2 and Table 2-8.
 *
 * Not the 68030's on-chip cache, and not the 68020's instruction cache. This is
 * board logic on the **virtual** bus -- §1.3 has the MC68020 generating the
 * address, the cache sitting beside it, and a separate MC68851 PMMU downstream
 * -- and its whole architectural interest is that the board exposes it to the
 * processor as two 8-KB windows. A cache a program can read and write is not an
 * internal structure a model may omit.
 *
 * ## What the manual states, and it states all of the geometry
 *
 * §1.3.1: "implemented in the Series 4000. It is an **8-KB, direct-mapped**
 * cache that contains **2048 4-byte** instruction and/or data entries. The
 * virtual cache uses a **write-through with write-allocate** design that causes
 * the cache to be updated (along with main memory) for every memory write
 * placed in the cache."
 *
 * Those two numbers agree with each other -- 2048 x 4 is 8192 -- which is worth
 * saying because it is the only cross-check the section offers, and it settles
 * that the "8 KB" is data and not data plus tags. The tags are the *other*
 * window; see below.
 *
 * §1.3.2, the write buffer: "a Series 4000 device ... that resides on the
 * virtual bus **between the microprocessor and the PMMU**. This buffer allows
 * the CPU to concurrently update the cache and execute cache read cycles
 * **without waiting for write cycles to complete**."
 *
 * Table 2-8, which is what makes any of it addressable:
 *
 *   `012000`-`013FFF`   CACHE RAM                  8 KB
 *   `014000`-`015FFF`   CACHE CONDITION CODE RAM   8 KB
 *
 * ## Which models have one
 *
 * `dn4000` alone, and this is derived from the part rather than measured, so it
 * is worth writing out. §1.3.2 puts the write buffer *between the
 * microprocessor and the PMMU*, and §1.3.1's cache on the same virtual bus. A
 * **68030 has no such bus**: its MMU is on chip, so the position both
 * structures occupy does not exist on a DS3500 or a DS4500, and a 68040's is on
 * chip too. Of the models sharing Table 2-8's map -- `019411-A00` §4.2.1.4
 * groups "DS3500, DS4000, DS4500, DS5500" -- only the DS4000 is the 68020 plus
 * separate 68851 that Figure 1-2 draws the cache and the write buffer onto, and
 * Figure 1-1's DS3000 has neither.
 *
 * So the model table carries the flag and nothing here decides it, exactly as
 * `ap_atmap.h` does for the translation map.
 *
 * **Whether a DS3500 nevertheless *decodes* `012000` is not knowable from this
 * shelf.** Table 2-8 is titled for the DS4000; the DS3500 borrows it because
 * that model's own document, the *Hardware Architecture Handbook* `007861-A01`,
 * is unobtainable. This core therefore places the two windows only where the
 * cache exists, which is the conservative reading: an address that answers
 * nothing is reported unmapped and shows up in a trace, where 16 KB of
 * invented RAM would answer silently.
 *
 * ## The condition-code RAM, and what is genuinely not published
 *
 * 8 KB across 2048 entries is four bytes of condition code per four bytes of
 * data. What those 32 bits *contain* is stated nowhere in this manual, on this
 * shelf, or in any transcription of it found on the web. A direct-mapped
 * virtual cache needs a tag and a valid bit, and 2048 4-byte entries index on
 * virtual address bits <12:2>, which leaves bits <31:13> -- nineteen bits of
 * tag -- and that fits a 32-bit word with room to spare. That arithmetic is
 * suggestive and it is not a citation, so:
 *
 * **PROVISIONAL.** `AP_CACHERAM_CC_VALID` and `AP_CACHERAM_CC_TAG_SHIFT` below
 * are this core's layout, not Apollo's. What is *not* provisional is the
 * aggregate behaviour every plausible layout agrees on and which is the only
 * thing a diagnostic actually does: **a condition-code word of zero is an
 * invalid entry**, so clearing the window invalidates the cache. The bit
 * positions are recorded in `PROJECT_STATUS.md` as the thing to revisit if
 * `007861-A01` or a Series 4000 diagnostic listing ever turns up.
 *
 * ## Why the bus path is not intercepted
 *
 * This module is storage, geometry and policy, and the board serves the two
 * windows from it. It deliberately does **not** sit in the CPU's read path, and
 * the reason is that doing so would be observationally identical:
 *
 *   - The policy is **write-through**, so main memory is current after every
 *     write. A cache that is never stale cannot answer differently from memory.
 *   - The one thing that *would* make it visible is time -- a hit costing fewer
 *     cycles than a memory cycle -- and **no source gives a hit cost**, nor the
 *     write buffer's depth. `[S3K]` describes both structures qualitatively and
 *     publishes no figure for either.
 *
 * So intercepting the path would add a branch to the hottest loop in the core
 * in exchange for a difference no probe could measure, and it would need an
 * invented number to become measurable -- which is the one thing this project
 * does not do. The lookup is implemented and tested here, so a later timing
 * model has the mapping ready; what it needs is a published figure, and that is
 * a named item in `COMPLETION_PLAN.md`.
 *
 * ## References
 *
 * `[S3K]` *Domain Series 3000/4000 Technical Reference*, 008778-03, Aug 1987 --
 *         §1.3.1, §1.3.2, Figure 1-2, Table 2-8 (§2.10).
 * `[ADD]` *Addendum to Domain Personal Workstations and Servers Hardware
 *         Architecture Handbook*, 019411-A00, 1991 -- §4.2.1.4, for the group
 *         that shares Table 2-8's map.
 */

#ifndef APOLLO_BOARD_AP_CACHERAM_H
#define APOLLO_BOARD_AP_CACHERAM_H

#include <stdbool.h>
#include <stdint.h>

/* §1.3.1: "2048 4-byte instruction and/or data entries". */
#define AP_CACHERAM_ENTRIES 2048u
#define AP_CACHERAM_ENTRY_BYTES 4u

/* §1.3.1's "8-KB", and Table 2-8's two window sizes. The data window is the
 * entries; they are the same 8192 bytes counted two ways, and the assertion in
 * `ap_cacheram.c` keeps them that way. */
#define AP_CACHERAM_DATA_BYTES (AP_CACHERAM_ENTRIES * AP_CACHERAM_ENTRY_BYTES)

/* Four bytes of condition code per entry -- Table 2-8's second window is the
 * same 8 KB as the first. See the PROVISIONAL note above for what is in them. */
#define AP_CACHERAM_CC_BYTES (AP_CACHERAM_ENTRIES * 4u)

/* Table 2-8, inclusive limits as the table prints them. */
#define AP_CACHERAM_DATA_BASE 0x012000u
#define AP_CACHERAM_DATA_LIMIT 0x013FFFu
#define AP_CACHERAM_CC_BASE 0x014000u
#define AP_CACHERAM_CC_LIMIT 0x015FFFu

/* A direct-mapped cache of 2048 4-byte entries indexes on virtual address bits
 * <12:2> and tags on <31:13>. Both follow from §1.3.1's two numbers and from
 * nothing else; the *storage* of the tag is the provisional part, not this. */
#define AP_CACHERAM_INDEX_SHIFT 2u
#define AP_CACHERAM_INDEX_MASK (AP_CACHERAM_ENTRIES - 1u)
#define AP_CACHERAM_TAG_SHIFT 13u

/* **PROVISIONAL** -- this core's condition-code layout, not Apollo's. Valid in
 * the low bit so that a zeroed window is an invalidated cache, which is the one
 * behaviour any layout agrees on. */
#define AP_CACHERAM_CC_VALID 0x00000001u
#define AP_CACHERAM_CC_TAG_SHIFT 1u

typedef struct {
  /* Byte-addressed, because the window is: a program reads and writes these
   * through `012000` and `014000` at whatever width it likes, and storing
   * entries as words would put a byte-lane decision in front of every access
   * for no gain. Big-endian within an entry, like everything else this board
   * answers. */
  uint8_t data[AP_CACHERAM_DATA_BYTES];
  uint8_t cc[AP_CACHERAM_CC_BYTES];
  /* Whether this machine has the structure at all. Zero on every model but the
   * DS4000, and what the hasher walks -- so a board without a virtual cache
   * contributes no entries, exactly as `ap_atmap_t::entries` does for a map. */
  unsigned entries;
} ap_cacheram_t;

/* `entries` is `AP_CACHERAM_ENTRIES` when the model has the cache and 0 when it
 * has not. Storage is cleared either way. */
void ap_cacheram_init(ap_cacheram_t *cache, bool present);

[[nodiscard]] static inline bool ap_cacheram_present(const ap_cacheram_t *c) {
  return c->entries != 0u;
}

/* Which entry a virtual address lands in, and the tag it would carry. */
[[nodiscard]] unsigned ap_cacheram_index(uint32_t virtual_address);
[[nodiscard]] uint32_t ap_cacheram_tag(uint32_t virtual_address);

/* The two windows, addressed absolutely as Table 2-8 gives them. A read outside
 * the window returns 0xFF and a write outside it is dropped; the board decodes
 * first, so neither should be reachable, and both are what an unwired region
 * would look like rather than a silent aliasing. */
[[nodiscard]] uint8_t ap_cacheram_read_data(const ap_cacheram_t *cache,
                                            uint32_t address);
void ap_cacheram_write_data(ap_cacheram_t *cache, uint32_t address,
                            uint8_t value);
[[nodiscard]] uint8_t ap_cacheram_read_cc(const ap_cacheram_t *cache,
                                          uint32_t address);
void ap_cacheram_write_cc(ap_cacheram_t *cache, uint32_t address,
                          uint8_t value);

/* The condition-code word of one entry, assembled big-endian from the window's
 * bytes. PROVISIONAL layout; see the header comment. */
[[nodiscard]] uint32_t ap_cacheram_cc_word(const ap_cacheram_t *cache,
                                           unsigned index);
void ap_cacheram_set_cc_word(ap_cacheram_t *cache, unsigned index,
                             uint32_t word);

/* §1.3.1's lookup. True and `*value` set when the entry is valid and its tag
 * matches; false otherwise. The caller supplies the virtual address because
 * this cache is on the virtual bus -- ahead of the PMMU, so a physical address
 * is the wrong question to ask it. */
[[nodiscard]] bool ap_cacheram_lookup(const ap_cacheram_t *cache,
                                      uint32_t virtual_address,
                                      uint32_t *value);

/* Write-allocate on a miss, update in place on a hit: §1.3.1's policy for the
 * cache half of a write. The *memory* half is the caller's, and that it always
 * happens is what "write-through" means. */
void ap_cacheram_fill(ap_cacheram_t *cache, uint32_t virtual_address,
                      uint32_t value);

/* Every entry invalid, which is what a zeroed condition-code window means and
 * what the cache control register's enable bit produces on the way back up.
 * Data bytes are left alone: an invalid entry's data is not readable through
 * the lookup, and a diagnostic that clears condition codes and then reads the
 * data window expects to see what it wrote. */
void ap_cacheram_invalidate(ap_cacheram_t *cache);

#endif /* APOLLO_BOARD_AP_CACHERAM_H */

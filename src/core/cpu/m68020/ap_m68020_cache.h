/* MC68020 on-chip instruction cache.
 *
 * `MC68020 32-Bit Microprocessor User's Manual` §7.1.1 and Figure 7-1.
 *
 * ## Not the 68030's cache with a flag changed
 *
 * Both parts have 256 bytes of instruction cache and there the resemblance
 * stops. The 68020's is "a direct-mapped cache of **64 long word entries**",
 * indexed by A2-A7 with A1 selecting the word; the 68030's is 16 lines of four
 * entries, indexed by A4-A7 with A2-A3 selecting the entry within a line.
 *
 * That difference is not cosmetic and cannot be modelled by parameterising the
 * 68030's:
 *
 *  - **One valid bit per long word here, four per line there.** The 68030's
 *    per-entry validity is what makes a burst fill distinguishable from a
 *    single-entry fill; the 68020 has nothing to distinguish, because it has no
 *    burst at all.
 *  - **The tag is A8-A31 *and FC2*.** Twenty-four address bits plus one
 *    function code bit -- so supervisor and user instructions at the same
 *    address are different entries, and FC0/FC1 are *not* part of it.
 *  - **There is no data cache.** The 68020 caches instructions only, so an
 *    operand read always goes to memory. A model that gave it a data cache
 *    would make every data access cheaper than the hardware's.
 *
 * ## What it shares with the 68030
 *
 * The control bits: enable, freeze, clear entry and clear all live in a CACR
 * with the same names and different positions, and the cache is addressed by a
 * physical address on both. This module holds the organisation; the control
 * register belongs with the 68020's own register file when that lands.
 */

#ifndef APOLLO_CPU_M68020_AP_M68020_CACHE_H
#define APOLLO_CPU_M68020_AP_M68020_CACHE_H

#include <stdbool.h>
#include <stdint.h>

/* "A direct-mapped cache of 64 long word entries." */
#define AP_M68020_CACHE_ENTRIES 64u

typedef struct {
  /* "A tag field made up of the upper 24 address bits and the FC2 value." */
  uint32_t tag;
  bool valid;
  uint32_t data; /* "32 bits (two words) of instruction data" */
} ap_m68020_cache_entry_t;

typedef struct {
  ap_m68020_cache_entry_t entry[AP_M68020_CACHE_ENTRIES];
} ap_m68020_cache_t;

/* Clear every valid bit. The tags and data are deliberately left behind, as on
 * the 68030: what a clear does is make entries unreachable, and hashing or
 * comparing what it left would make two identically-behaving caches differ. */
void ap_m68020_cache_clear(ap_m68020_cache_t *cache);

/* The index into the cache: "the index field (A2-A7) of the access address". */
[[nodiscard]] unsigned ap_m68020_cache_index(uint32_t address);

/* The tag: A8-A31 with FC2 above them. FC2 alone -- not the whole function
 * code -- so supervisor and user instructions at one address occupy different
 * entries while program and data space do not. */
[[nodiscard]] uint32_t ap_m68020_cache_tag(uint32_t address,
                                           uint8_t function_code);

/* Look up a *word*. "Address bit A1 is used to select the proper word from the
 * cache entry", so a hit yields sixteen bits and not thirty-two -- the entry
 * holds two words and the instruction stream wants one. */
[[nodiscard]] bool ap_m68020_cache_lookup(const ap_m68020_cache_t *cache,
                                          uint32_t address,
                                          uint8_t function_code,
                                          uint16_t *word);

/* Fill an entry with a long word from memory. `frozen` refuses the write:
 * "unless the freeze cache bit has been set", which stops replacement without
 * stopping lookups -- a frozen cache still hits. */
void ap_m68020_cache_fill(ap_m68020_cache_t *cache, uint32_t address,
                          uint8_t function_code, uint32_t data, bool frozen);

/* Clear one entry, which is what the 68020's CACR clear-entry bit does. */
void ap_m68020_cache_clear_entry(ap_m68020_cache_t *cache, uint32_t address);

#endif /* APOLLO_CPU_M68020_AP_M68020_CACHE_H */

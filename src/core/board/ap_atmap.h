/* Apollo address translation map: `[ADD]` §4.2.1.4, `[S3K]` §1.2 and §2.4.7.
 *
 * Not the CPU's MMU. This is board logic sitting between the AT-compatible bus
 * and physical memory, and it exists for a reason the 68030's own translation
 * cannot serve: a DMA controller has no MMU. The 8237 puts a flat, small
 * address on the bus and expects contiguous memory behind it, while the
 * operating system has pages scattered across physical RAM. The map is what
 * reconciles those.
 *
 * `[ADD]` gives it two jobs, and they are the same mechanism seen from either
 * end:
 *
 *   - "It allows the DS3500, DS4000, DS4500, or DS5500 to perform DMA to or
 *     from noncontiguous physical memory (while it appears that the DMA
 *     transfer is taking place from contiguous physical memory)"
 *   - "It provides a 512-KB window through which external AT compatible bus
 *     masters can access CPU main memory."
 *
 * ## Which models have one
 *
 * `[S3K]` §1.2: "The Series 4000, unlike the Series 3000, incorporates an
 * address translation map in its architecture." So a DN3000 has none, and DMA
 * on that machine reaches physical memory directly.
 *
 * `[ADD]` replaces that section wholesale and names the models: DS3500, DS4000,
 * DS4500 and DS5500. Our reference machine is on that list, which is why this
 * is core-board rather than optional. The model table carries the flag; nothing
 * here decides it.
 *
 * ## The translation
 *
 * A page is 1 KB -- the offset is address bits <9:0> -- and an entry is a
 * 16-bit physical page number occupying bits <25:10> of the result. So the map
 * translates into a 26-bit (64 MB) physical address space, which `[ADD]` states
 * directly: "the DS3500 or DS4000 physical address space (64 MB)".
 *
 * The index depends on the width of the DMA transfer, because the 8237's
 * address space does:
 *
 *   8-bit:  "address bits <15:10> provide an index ... they select one of the
 *           64 entries contained within it", offset from bits <9:0>
 *   16-bit: "address bits <16:10> provide an index ... they select one of the
 *           128 entries contained within it", offset from bits <9:1>
 *
 * The 16-bit case takes its offset from <9:1> and not <9:0> because a 16-bit
 * DMA controller counts in words: it has no address bit 0 to give. The
 * resulting physical address is therefore always even. That is nine bits of
 * offset against the 8-bit case's ten, and it is not a transcription slip --
 * the concatenation still yields 26 bits, with bit 0 zero.
 *
 * ## Two figures that do not divide, recorded rather than reconciled
 *
 * `[S3K]` §2.5 puts the map at `017000`-`0177FF`, which is 2 KB of address
 * space. 128 entries of 16 bits is 256 bytes. The region is eight times the
 * entries, and nothing in either manual says what the rest of it decodes to --
 * an aperture wider than the RAM behind it is ordinary, but it is not stated.
 *
 * The 512-KB window and the entry count do reconcile, and worth writing down
 * because they look like they contradict: 128 entries of 1 KB is 128 KB, not
 * 512 KB. `[ADD]` distinguishes them itself -- "the physical address space
 * between addresses 000000 and 07FFFF is used by the Address Translation Map to
 * access the 64- to 128-KB bus master's address space". The 512 KB is the
 * *aperture* the AT bus decodes to the map; the 64 or 128 KB is how much of it
 * a transfer of that width can actually reach. Two different quantities, not
 * one inconsistent one.
 *
 * ## References
 *
 * `[ADD]` *Addendum to Domain Personal Workstations and Servers Hardware
 *         Architecture Handbook*, 019411-A00, 1991 -- §4.2.1.4, which replaces
 *         pages 4-10 and 4-11 of the handbook and is the only source that names
 *         the DS3500.
 * `[S3K]` *Domain Series 3000/4000 Technical Reference*, 008778-03, Aug 1987 --
 *         §1.2, §2.4.7, §2.5.
 */

#ifndef APOLLO_BOARD_AP_ATMAP_H
#define APOLLO_BOARD_AP_ATMAP_H

#include <stdbool.h>
#include <stdint.h>

/* "they select one of the 128 entries contained within it". The 8-bit case
 * reaches only the first 64; the map does not shrink. */
#define AP_ATMAP_ENTRIES 128u

/* Bits <9:0> of the physical address: a 1 KB page. */
#define AP_ATMAP_PAGE_SHIFT 10u
#define AP_ATMAP_PAGE_SIZE (1u << AP_ATMAP_PAGE_SHIFT)

/* `[S3K]` §2.5: `017000` - `0177FF`. */
#define AP_ATMAP_BASE 0x017000u
#define AP_ATMAP_LIMIT 0x0177FFu

/* The width of the transfer being translated, which decides how many address
 * bits index the map and where the page offset comes from. An enum rather than
 * a byte count because these are the only two `[ADD]` defines, and a caller
 * passing 4 would otherwise get a silent answer to a question the manual does
 * not ask. */
typedef enum {
  AP_ATMAP_TRANSFER_8BIT = 0,
  AP_ATMAP_TRANSFER_16BIT,
} ap_atmap_transfer_t;

typedef struct {
  /* "The 16-bit Address Translation Map entry (a physical page number, bits
   * <25:10>)". Stored as written by software; the translation shifts it. */
  uint16_t entry[AP_ATMAP_ENTRIES];
} ap_atmap_t;

void ap_atmap_init(ap_atmap_t *map);

/* How many entries a transfer of this width can index: 64 for 8-bit, 128 for
 * 16-bit. Exposed because it is the difference between the two cases that a
 * caller is most likely to get wrong. */
[[nodiscard]] unsigned ap_atmap_reachable_entries(ap_atmap_transfer_t transfer);

/* The index a DMA address selects, and the page offset it carries. Separate
 * from the translation so both halves can be tested against `[ADD]`'s wording
 * directly rather than only through their product. */
[[nodiscard]] unsigned ap_atmap_index(uint32_t dma_address,
                                      ap_atmap_transfer_t transfer);
[[nodiscard]] uint32_t ap_atmap_offset(uint32_t dma_address,
                                       ap_atmap_transfer_t transfer);

/* Translate a DMA-bus address to a 26-bit physical address.
 *
 * Total, not partial: the entry is concatenated with the offset exactly as
 * `[ADD]` describes, and the result is masked to 26 bits because that is the
 * physical address space the map targets. */
[[nodiscard]] uint32_t ap_atmap_translate(const ap_atmap_t *map,
                                          uint32_t dma_address,
                                          ap_atmap_transfer_t transfer);

/* Programmed access from the CPU, at `017000`-`0177FF`.
 *
 * `in_range` answers whether an address decodes to the map at all, so a caller
 * need not duplicate the bounds. Reads and writes are 16 bits: the entry width.
 *
 * PROVISIONAL in one respect, stated at the point it bites: the region is 2 KB
 * and 128 entries of 16 bits fill 256 bytes of it. The entry a given address
 * selects within the region is *assumed* to be `(address - base) / 2`, which is
 * the only reading with no gaps, but neither manual says so and the remaining
 * seven eighths of the region are undescribed. `ap_atmap_decodes_to_entry`
 * exists so a caller can tell "outside the map" from "inside the map, in the
 * part no manual accounts for" rather than having the two collapse. */
[[nodiscard]] bool ap_atmap_in_range(uint32_t address);
[[nodiscard]] bool ap_atmap_decodes_to_entry(uint32_t address);
[[nodiscard]] uint16_t ap_atmap_read(const ap_atmap_t *map, uint32_t address);
void ap_atmap_write(ap_atmap_t *map, uint32_t address, uint16_t value);

#endif /* APOLLO_BOARD_AP_ATMAP_H */

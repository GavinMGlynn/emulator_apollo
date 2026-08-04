/* The DS3000's DMA page register block, `008778-03` Table 2-6's
 * "DMA PAGE REGISTER" at `009200-0092FF`.
 *
 * ## What it is for
 *
 * An 8237A drives sixteen bits of address. A machine has more, and the two
 * families here bridge the gap differently: the Series 4000 uses the address
 * translation map (`board/ap_atmap.h`), and §1.2 says so in as many words --
 * "The Series 4000, unlike the Series 3000, incorporates an address translation
 * map in its architecture". A Series 3000 has no map, so its DMA reaches
 * physical memory directly and something has to supply the high bits. This is
 * that something, and Table 2-6 gives it a block of its own on exactly the
 * board that has no map.
 *
 * ## Storage and width only
 *
 * Sixteen byte-wide registers, aliased across the 256-byte block, holding what
 * was written to them. **No bit here has a meaning this core relies on**, which
 * is the same rule `board/ap_boardreg.h` keeps for the core registers and for
 * the same reason: Table 2-6 gives the block an address and a name and says
 * nothing about its contents, and no other manual in `docs/references/` lays it
 * out.
 *
 * What that costs is stated rather than hidden: a DS3000 DMA transfer's high
 * address bits are not modelled, so a transfer beyond 64 KB would land in the
 * wrong page. Nothing reaches that yet -- the DS3000 has no measured channel
 * assignments and no device driving a request -- and the register has to exist
 * first regardless, because the boot PROM writes it five times before it does
 * anything else and an unmapped write there faults the machine.
 *
 * ## The offsets look like the AT's, and that is not asserted
 *
 * The first write this core sees is to `009207`. On an AT the DMA page
 * registers live at ports `80`-`8F` with channel 0 at `87`, so `009200 + 7`
 * lands exactly where the AT's channel 0 page register would. That is a
 * suggestive fit and it is **not** claimed: the equivalent assumption about the
 * interrupt controllers was wrong on this machine (`FINDINGS.md` C11), and the
 * DMA cascade was only safe to claim once Table 2-4 stated it. The mapping from
 * offset to channel is recorded as an open question in `docs/PROJECT_STATUS.md`
 * and closes when a DS3000 transfer can be measured.
 */

#ifndef APOLLO_BOARD_AP_DMAPAGE_H
#define APOLLO_BOARD_AP_DMAPAGE_H

#include <stdbool.h>
#include <stdint.h>

/* Table 2-6. The DS4000 has no such block -- its map does this job. */
#define AP_DMAPAGE_ADDR 0x009200u
#define AP_DMAPAGE_RANGE 0x100u

/* Sixteen registers, which is what the AT's port range holds. Aliased through
 * the block, as every other byte-wide register range on this board is. */
#define AP_DMAPAGE_REGISTERS 16u

typedef struct {
  uint8_t page[AP_DMAPAGE_REGISTERS];
} ap_dmapage_t;

void ap_dmapage_reset(ap_dmapage_t *pages);

/* Which register an address selects. Aliased: the low nibble decides, so
 * `009207` and `009217` are the same register. */
[[nodiscard]] unsigned ap_dmapage_index(uint32_t address);

[[nodiscard]] uint8_t ap_dmapage_read(const ap_dmapage_t *pages,
                                      uint32_t address);
void ap_dmapage_write(ap_dmapage_t *pages, uint32_t address, uint8_t value);

#endif /* APOLLO_BOARD_AP_DMAPAGE_H */

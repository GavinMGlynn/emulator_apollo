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
 * ## Sixteen byte-wide registers, aliased across the block
 *
 * They hold what was written to them. Table 2-6 gives the block an address and
 * a name and says nothing about its contents, and this file said for a long
 * time that no other manual laid it out. **That was wrong, and the manual was
 * already on disk.**
 *
 * ## Which offset is which channel, from `002398-04` p. 12-25
 *
 * The Domain Engineering Handbook Rev 4's DN3000 chapter prints the table, and
 * prints the warning with it:
 *
 *     DMA Page Registers  [ 9200 | 3FFA200 ]
 *     ...
 *     Addresses of the DMA page registers (note the non-order):
 *       9207  page register for CH0      920B  page register for CH5
 *       9203  page register for CH1      9209  page register for CH6
 *       9201  page register for CH2      920A  page register for CH7
 *       9202  page register for CH3
 *
 * Seven entries for eight channels: **channel 4 has none**, and it is the
 * cascade -- Table 2-4's channel 4 carries the slave controller rather than a
 * device, so there is no transfer of its own to supply high bits for.
 *
 * The same page states the width and the reach: "Each byte is loaded with the
 * high 8 physical address bits for its corresponding DMA channel", which is
 * what turns the 8237A's sixteen address bits into the system's twenty-four.
 * It also says "DMA can operate on a maximum of 1024 bytes (each channel has
 * only ONE page register)" -- recorded as printed. Taken literally that bounds
 * a transfer far below the 64 KB the counter allows, and nothing else here
 * corroborates it, so it is quoted rather than enforced.
 *
 * ## The channel usage table, and where it disagrees with `008778-03`
 *
 * p. 12-23 lists what each channel is *for*, and it is a kernel allocation table
 * rather than a strapping one: "CH0 avail, **CH1 SDLC option**, CH2 floppy, CH3
 * avail" on the first chip and "CH4 cascade for chip 1, CH5 avail, CH6 avail,
 * CH7 avail" on the second.
 *
 * `008778-03` Table 2-4 assigns three of those: DRQ1 **Tape**, DRQ2 Floppy,
 * DRQ7 "Reserved for Winchester" -- and Table 8-1 independently straps the
 * SC-499 tape controller to "DMA Channel 1". Only the floppy and the cascade
 * agree between the two documents.
 *
 * The reading that fits both: "avail" means no card in the base configuration
 * claims the channel, and the option boards are strapped to channels the kernel
 * then allocates. It follows that a machine cannot carry both the SC-499 tape
 * and the SDLC option, since Apollo straps both to channel 1 -- which is a
 * configuration exclusivity worth knowing and is not stated anywhere as such.
 *
 * Nothing here changes: this core's channels come from Table 2-4 and from the
 * cards' own straps, which is the level the hardware works at.
 *
 * Two more from the same pages, neither modelled and both recorded. p. 12-23:
 * the first 8237 "supports only 8 bit transfers (channels 0-3) and the second
 * 16 bit transfers (channels 5-7)", which is Table 2-4's split stated as a chip
 * property. And p. 12-24 prints the **Clear Byte Pointer register at `900D`**,
 * which is the master clear: the 8237A's byte-pointer flip-flop is at offset
 * `C` and `D` resets the part, so a driver following that line would reset the
 * controller before loading every address. `AP_I8237_REG_CLEAR_FLIPFLOP` is the
 * datasheet's `C`, and the handbook's other five addresses on that page are all
 * right, which is what makes this one a slip rather than a board difference.
 *
 * ## It *is* the AT's layout, and it took a document to say so
 *
 * The first write this core sees is to `009207`, and on an AT channel 0's page
 * register is port `87`. This file recorded that as "a suggestive fit and it is
 * **not** claimed", because the equivalent assumption about the interrupt
 * controllers was wrong on this machine (`FINDINGS.md` C11). The refusal was
 * right and the resolution was not a measurement: the handbook states the
 * mapping for *this* board, and it happens to agree. The AT similarity is now a
 * remark rather than the evidence.
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

/* How many DMA channels the two cascaded controllers carry, and which one is
 * the cascade. Both are Table 2-4's, restated here because the page table is
 * indexed by channel and a caller needs the bound. */
#define AP_DMAPAGE_CHANNELS 8u
#define AP_DMAPAGE_CASCADE_CHANNEL 4u

/* Which register a channel's page byte lives in, per `002398-04` p. 12-25.
 * Returns `AP_DMAPAGE_REGISTERS` for channel 4, which has no page register, and
 * for any channel out of range -- the one value that cannot be a register
 * index, so a caller that ignores the check indexes nothing valid rather than
 * silently reading channel 0's. */
[[nodiscard]] unsigned ap_dmapage_index_for_channel(unsigned channel);

/* The high eight physical address bits a channel's transfer carries. Zero for
 * the cascade channel, which has no page register to hold any. */
[[nodiscard]] uint8_t ap_dmapage_channel_page(const ap_dmapage_t *pages,
                                              unsigned channel);

/* The full physical address a channel's 16-bit DMA address reaches: the page
 * byte above the controller's own sixteen bits. This is the whole reason the
 * block exists, and expressing it here rather than at the transfer keeps the
 * arithmetic in the part that is documented. */
[[nodiscard]] uint32_t ap_dmapage_physical(const ap_dmapage_t *pages,
                                           unsigned channel, uint16_t offset);

#endif /* APOLLO_BOARD_AP_DMAPAGE_H */

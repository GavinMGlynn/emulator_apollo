/* Apollo AT DMA controllers: two 8237As as the board wires them.
 *
 * `008778-03` Table 2-8: "010COO - 010CFF  DMA CONTROLLER #1" and
 * "010D00 - 010DFF  DMA CONTROLLER #2". The parts are `device/ap_i8237.h`.
 *
 * ## Two strides, both measured
 *
 * DMA 1 is **stride 1** -- sixteen registers in sixteen consecutive bytes,
 * aliased through the 256-byte range. DMA 2 is **stride 2**, its register `n` at
 * offset `2n`.
 *
 * Neither was assumed. A clean dump of thirty-two bytes at each gave
 * `00 x15 0F 00 x15 0F` for the first and `00 x30 0F 0F` for the second: the
 * `0F` is the all-mask register holding all four channels masked, which is what
 * a part out of reset contains, and where it lands is the stride.
 * `FINDINGS.md` C13.
 *
 * This is the third pair of adjacent byte-wide peripherals on this board with
 * different strides -- the interval timer is odd-address stride 2 and the
 * calendar beside it is stride 1. Three examples is enough to treat "measure
 * the placement" as the rule rather than the precaution.
 *
 * ## What is deliberately not asserted here
 *
 * On a stock AT the first controller cascades onto channel 0 of the second, and
 * this module does **not** claim that. The equivalent assumption about the
 * interrupt controllers was wrong on this machine -- the cascade is on IR3, not
 * the AT's IR2 (`FINDINGS.md` C11) -- and it was wrong precisely because it was
 * imported from a neighbouring system rather than checked here. The DMA cascade
 * will be measured the same way, once transfers exist to measure.
 */

#ifndef APOLLO_BOARD_AP_DMA_H
#define APOLLO_BOARD_AP_DMA_H

#include <stdbool.h>
#include <stdint.h>

#include "device/ap_i8237.h"

/* `008778-03` Table 2-4, as controller and channel. Controller 1 carries
 * DRQ0-3 and controller 2 DRQ4-7, so a DRQ number `n` is controller `n / 4`,
 * channel `n % 4`. */
#define AP_DMA_TAPE_UNIT 0u        /* DRQ1 */
#define AP_DMA_TAPE_CHANNEL 1u
#define AP_DMA_FLOPPY_UNIT 0u      /* DRQ2 */
#define AP_DMA_FLOPPY_CHANNEL 2u
#define AP_DMA_WINCHESTER_UNIT 1u  /* DRQ7, and 16-bit */
#define AP_DMA_WINCHESTER_CHANNEL 3u
/* DRQ6, the 802.3 Network Controller-AT. `008778-03` **Figure 14-3** -- the
 * standard jumper configuration for an AT-compatible slot -- labels the block
 * "DMA Channel 6 and Interrupt Level 10 Select" and shows 6 and 10 jumpered,
 * beside a "Control Status Registers Hex Address 300" that is this board's
 * `058000`. Read from the page image: it is a figure, and the text layer of
 * this chapter renders neighbouring tables as `IRQ?` and `SA?`. */
#define AP_DMA_ETHERNET_UNIT 1u
#define AP_DMA_ETHERNET_CHANNEL 2u

/* DRQ4: the second controller's channel 0, which the first controller's request
 * output arrives on. "It is not available on the AT-compatible bus." */
#define AP_DMA_CASCADE_UNIT 1u
#define AP_DMA_CASCADE_CHANNEL 0u

#define AP_DMA1_ADDR 0x010C00u
#define AP_DMA2_ADDR 0x010D00u
#define AP_DMA_RANGE 0x100u

typedef struct {
  ap_i8237_t controller[2];
} ap_dma_t;

void ap_dma_reset(ap_dma_t *dma);

/* Whether an address decodes to a controller, which one, and to which
 * register. */
[[nodiscard]] bool ap_dma_decode(uint32_t address, unsigned *unit,
                                 unsigned *reg);

[[nodiscard]] uint8_t ap_dma_read(ap_dma_t *dma, uint32_t address);
void ap_dma_write(ap_dma_t *dma, uint32_t address, uint8_t value);

#endif /* APOLLO_BOARD_AP_DMA_H */

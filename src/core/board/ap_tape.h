/* Apollo cartridge tape: the SC-499 as the board wires it.
 *
 * `008778-03` Table 2-9 places the drive at `050000`-`050F80`, AT `218`-`21F`.
 * The part is `device/ap_sc499.h`.
 *
 * ## Placement, measured and then explained
 *
 * The oracle's controller dumps as `00 40 FF FF FF FF FF FF`, repeating on an
 * eight-byte period: four registers at stride 1, the upper four addresses of
 * each eight not decoded, aliased through the 256-byte range.
 *
 * Only the first two read back, and for a while that looked like the whole part
 * (`FINDINGS.md` C17). The guide explains the rest: `BASE+2` and `BASE+3` are
 * write-triggered DMA commands with nothing behind them, so a read sweep cannot
 * see them (C18). The dump and the manual disagree only in what a read can
 * reach.
 *
 * That the range answers at all was itself worth establishing: with the card
 * removed from its slot the whole range reads `FF`. The DN3500 carries the tape
 * in `isa2` **by default**, beside the disk controller in `isa1` -- which is
 * what made the first attempt at that comparison measure one configuration
 * against itself (C16).
 *
 * ## Interrupt
 *
 * `008778-03` Table 2-3: "IRQ5 ... Tape Drive or User Device", priority 6.
 */

#ifndef APOLLO_BOARD_AP_TAPE_H
#define APOLLO_BOARD_AP_TAPE_H

#include <stdbool.h>
#include <stdint.h>

#include "device/ap_sc499.h"

#define AP_TAPE_ADDR 0x050000u
#define AP_TAPE_RANGE 0x100u

/* `008778-03` Table 2-3. */
#define AP_TAPE_IRQ 5u

typedef struct {
  ap_sc499_t controller;
} ap_tape_t;

void ap_tape_reset(ap_tape_t *tape);

/* False for the four undecoded addresses of each eight as well as for anything
 * outside the range: the dump reads `FF` there, and folding them onto the
 * registers would give a driver four aliases the hardware does not offer. */
[[nodiscard]] bool ap_tape_decode(uint32_t address, unsigned *reg);

[[nodiscard]] uint8_t ap_tape_read(ap_tape_t *tape, uint32_t address);
void ap_tape_write(ap_tape_t *tape, uint32_t address, uint8_t value);

[[nodiscard]] bool ap_tape_irq(const ap_tape_t *tape);

#endif /* APOLLO_BOARD_AP_TAPE_H */

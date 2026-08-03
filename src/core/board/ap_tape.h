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

#include <stddef.h>

#include "device/ap_qic.h"
#include "device/ap_sc499.h"

#define AP_TAPE_ADDR 0x050000u
#define AP_TAPE_RANGE 0x100u

/* `008778-03` Table 2-3. */
#define AP_TAPE_IRQ 5u

/* ## The join between controller and drive
 *
 * `[SC499]` puts the QIC-02 command set behind the data/command register at
 * `BASE+0`, and gates it with control bit 6, "Request to LSI chip". So a byte
 * written to the data register while that bit is set is a command to the drive,
 * and bytes read back are its data.
 *
 * **The per-byte handshake is not modelled.** `[SC499]` §1.13.2 describes the
 * QIC-02 interface timing -- the REQUEST and READY exchange that paces each byte
 * -- and that section has not been read. What is here transfers a byte per
 * access with no pacing, which is enough for a driver that polls the status
 * register and wrong for one that depends on the handshake's edges. Named so
 * that a driver failing in that way is diagnosed rather than puzzled over. */

typedef struct {
  ap_sc499_t controller;
  ap_qic_t drive;
  /* Where in the current block the next data read comes from. The controller
   * hands over one byte at a time; the drive deals in 512-byte blocks. */
  uint8_t block[AP_CT_BLOCK_SIZE];
  unsigned offset;
  bool block_valid;
} ap_tape_t;

void ap_tape_reset(ap_tape_t *tape);

/* Load a cartridge into the drive. The type is the caller's to supply; see
 * `device/ap_qic.h`. */
[[nodiscard]] bool ap_tape_load(ap_tape_t *tape, const uint8_t *data,
                                size_t size, ap_qic_cartridge_t cartridge);

/* False for the four undecoded addresses of each eight as well as for anything
 * outside the range: the dump reads `FF` there, and folding them onto the
 * registers would give a driver four aliases the hardware does not offer. */
[[nodiscard]] bool ap_tape_decode(uint32_t address, unsigned *reg);

[[nodiscard]] uint8_t ap_tape_read(ap_tape_t *tape, uint32_t address);
void ap_tape_write(ap_tape_t *tape, uint32_t address, uint8_t value);

[[nodiscard]] bool ap_tape_irq(const ap_tape_t *tape);

/* ---------------------------------------------------------------------------
 * The DMA side, which is the same data register reached a different way
 *
 * `008778-03` Table 2-4 puts the tape drive on **DRQ1** -- controller 1,
 * channel 1 -- and §8.3.2's Table 8-1 configures the controller board itself to
 * match: "Device Address 218, DMA Channel 1, Interrupt Request Level 5". All
 * three now agree with what this core had already placed from other evidence.
 *
 * A DMA cycle does not address the device. It is selected by `DACK` and the byte
 * moves on `IOR`/`IOW`, so these take no address: they are the data register
 * reached through the acknowledge instead of through the bus. Routing them to
 * anything else would model a second port the controller has not got.
 * ------------------------------------------------------------------------- */

/* Whether the drive is asking for a cycle. It asks while a read is in progress
 * and there are bytes left to hand over, which is what makes the request a
 * *block*-granular thing rather than a per-word one: the line stays asserted
 * across the whole transfer and drops when the drive has nothing more. */
[[nodiscard]] bool ap_tape_dma_request(const ap_tape_t *tape);

/* One byte out of the drive, and one byte in. */
[[nodiscard]] uint8_t ap_tape_dma_read(ap_tape_t *tape);
void ap_tape_dma_write(ap_tape_t *tape, uint8_t value);

#endif /* APOLLO_BOARD_AP_TAPE_H */

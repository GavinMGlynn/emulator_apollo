/* Apollo cartridge tape: the SC-499 as the board wires it.
 *
 * `008778-03` Table 2-9 places the drive at `050000`-`050F80`, AT `218`-`21F`.
 * The part is `device/ap_sc499.h`.
 *
 * ## The ISA address is `200`, not `218` -- settled by the handbook
 *
 * That `218` never added up. `008778-03`'s own physical column gives `050000`,
 * and this board maps ISA to physical by `(AT_addr/8) * $400 + $40000`, which
 * sends `218` to `050C00` and `200` to `050000`. The `008778-03` walk hit the
 * arithmetic three separate times -- at Table 2-7, at Table 8-1's prose
 * "Device Address (base address) 218 (hex)", and at Figure 15-5, where the SPE
 * board's *alternate* jumpering claims `218`-`21F` for its own serial port --
 * and each time recorded that our constant follows the physical column because
 * that is what the board decodes.
 *
 * **`002398-04` p. 12-1 gives the ISA address directly.** Its DN3000 address
 * table has three columns -- physical, virtual, I/O Bus -- and the row is
 * `50000 tape ... 200`. So the ISA address is **`200`**, `008778-03` prints
 * `218` in two places and its own physical column contradicts it, and the
 * measurement, the physical column and this handbook all agree.
 *
 * `AP_TAPE_ADDR` does not change; what changes is that it no longer rests on
 * preferring one column of a self-contradicting table. And the SPE collision
 * the walk recorded is real but is with the *alternate* SPE setting at `218`,
 * which is not where the tape is.
 *
 * The same table confirms every other device address this core places -- `win`
 * at `4D000`/ISA `1A0`, ethernet at `58000`/`300`, mono at `5D800`/`3B0`,
 * floppy at `5F800`/`3F0` -- and the core-board block: `8000 mmu/cpu`,
 * `8400 sios` (**one** SIO row, as the DS3000 has one 2681), `8800 timers`,
 * `8900 calendar`, `9000`/`9100` DMA, `9200` DMA page register, `9300` parity,
 * `9400`/`9500` the two interrupt controllers.
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
 * **The handshake, and what this note used to get wrong twice.** It said the
 * per-byte handshake was not modelled and that `[SC499]` §1.13.2 "has not been
 * read". Both parts were stale.
 *
 * §1.13.2 is read. It is a set of timing *figures* -- page images with no text
 * layer -- and `ap_sc499_handshake_duration` already takes its durations from
 * them, entry by entry: Figure 1-7 READY asserted, 1-8 exception asserted, 1-9
 * direction deasserted. So a command's handshake *is* paced.
 *
 * The granularity was also wrong. §1.13.1's WRITE and READ entries give the
 * data protocol in prose, and it is per **block**, not per byte: "The READY
 * line is activated when the device is ready for a data block transfer", and
 * "If the host starts transfer between blocks before READY is asserted, READY
 * MAY NOT BE ASSERTED."
 *
 * That last part is now **done**: `ensure_block` calls
 * `ap_sc499_block_boundary`, so READY drops when a block begins and returns
 * `AP_SC499_T_BLOCK_TO_READY` later -- Figure 1-5's T4 and T15. The delay is
 * `PROVISIONAL`, because the figure gives `100 us. <` as a *minimum* and taking
 * the bound models the fastest drive the specification permits. */

typedef struct {
  ap_sc499_t controller;
  ap_qic_t drive;
  /* Where in the current block the next data read comes from. The controller
   * hands over one byte at a time; the drive deals in 512-byte blocks. */
  uint8_t block[AP_CT_BLOCK_SIZE];
  unsigned offset;
  bool block_valid;
} ap_tape_t;

/* First use. See `ap_qic_init`: the drive's reset keeps its media, so it cannot
 * be the first thing called on uninitialised memory. */
void ap_tape_init(ap_tape_t *tape);

void ap_tape_reset(ap_tape_t *tape);

/* Carry the controller's handshake to `now`. The tape has nothing else that
 * moves with time -- the drive's motion is not modelled -- so this is the
 * controller's advance and nothing more. */
void ap_tape_advance(ap_tape_t *tape, ap_time_t now);

/* Load a cartridge into the drive. The type is the caller's to supply; see
 * `device/ap_qic.h`. */
[[nodiscard]] bool ap_tape_load(ap_tape_t *tape, uint8_t *data,
                                size_t size, ap_qic_cartridge_t cartridge, bool writable);

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

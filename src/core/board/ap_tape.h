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
 * ## And now the `218` has a provenance, which makes it a revision rather than an error
 *
 * The two editions of *Writing Device Drivers with GPIO Calls* print the same
 * Table 3-1 a year apart and **the tape moved between them**:
 *
 *     000959-10, Jun 1987   218-21F   Tape Controller
 *     000959-A00, Jul 1988  200-207   Tape Controller
 *
 * `008778-03` is **August 1987** -- contemporary with the first -- so it is not
 * contradicting itself out of carelessness: its prose carries the address of
 * its own moment and its physical column carries the one the board decodes.
 * The SR10 manual prints `200-207` outright.
 *
 * **So the arithmetic deduction and a later Apollo manual reach the same
 * answer independently**, and `002398-04` makes three. The interesting part is
 * that the wrong value was once right: a reader working from a 1987 document
 * would place this drive at `218` and be following the manual correctly.
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

  /* **READ STATUS's six bytes, on their way to the host.**
   *
   * `[SC499]` §1.13.1: after a READ STATUS "the device transfers the standard
   * six bytes to the host", and it transfers them through the *data* register
   * exactly as a data block goes. `ap_qic_read_status` composes the block and
   * clears the conditions it reports, but it has to be *called*, and until
   * 2026-09-09 the only caller was `qic_suite` -- so a firmware that issued
   * READ STATUS in answer to an exception was handed tape data or the
   * controller's own register instead, and gave up.
   *
   * Held here rather than in the drive because the boundary this crosses is the
   * controller's: the drive composes a block, the register hands out bytes, and
   * that is the same split `block`/`offset` already make for a data transfer. */
  uint8_t status_block[AP_QIC_STATUS_BYTES];
  unsigned status_offset;
  bool status_valid;
  /* **When the drive can hand over its next byte.** `008778-03` Table 9-1 gives
   * the drive 90,000 bytes a second, so the interface cannot go faster however
   * fast the bus is -- and the DMA request line was a level held for a whole
   * block, so the arbiter took 512 bytes in 20 us with the processor stalled
   * throughout. `FINDINGS.md` C268: the SR10.4 boot firmware writes DMAGO and
   * then, forty-six instructions later, the 8237 address the block belongs to.
   * A drive 11.1 us from its first byte can afford that and one that has
   * already delivered all 512 cannot.
   *
   * A deadline rather than a countdown, so a caller that asks twice in one tick
   * gets the same answer. Not hashed, for the same reason `ap_sc499_t`'s
   * `ready_at` is not: a scheduled instant rather than a fact about the tape. */
  ap_time_t next_byte_at;
  /* **The first block of a tape is handed to the host twice, and nothing
   * explains why.**
   *
   * Measured on this machine from the firmware's own DMA programming rather
   * than from any guess about the drive: for `EX DOMAIN_OS` the SR10.4 boot
   * PROM sets up sixteen transfers and the **first two name the same
   * destination** -- translation-map page `43F6` with 8237 base `0000`, twice
   * in a row, at `010FD800` -- with no BOT and no second READ between them.
   * Blocks 1 to 15 then land end to end from that same address, so the second
   * transfer is where block 0 of the image belongs: the host asks for block 0
   * twice. `FINDINGS.md` C268.
   *
   * **A second implementation needed exactly this.** MAME's `sc499.cpp` carries
   * a flag armed at reset and spent on one block, with its own comment -- "we
   * must read first block twice (in MD for 'di c' and 'ld' or 'ex ...') // why
   * is this necessary???". So the oracle reproduces the requirement and does
   * not explain it either.
   *
   * **And no document does.** `[SC499]`'s only mention of the card's 16K RAM
   * buffer in forty-two pages is the power-on test's LED assignment; `QIC-02
   * Rev D` §4.2.8 says a READ "following cartridge insertion or RESET shall
   * commence at BOT" and nothing about a second transfer within one READ;
   * Apollo's two tape documents defer to that standard.
   *
   * So this is a **deliberate approximation** in `CLAUDE.md`'s sense, with its
   * reason and cost to close on the record. It lives on the *card* rather than
   * the drive because the card is what holds a buffer -- `[SC499]` §1.8.1's
   * 16K RAM -- and because the drive's own contract, one block per
   * `ap_qic_read_block`, is worth keeping clean: putting it there made five
   * `qic_suite` tests count a block they had no reason to know about. That
   * placement is a modelling choice and not a claim about which chip repeats
   * the block. A document describing the card's buffer, or a probe of real
   * hardware, closes it. */
  bool first_block_pending;
  /* **The file mark's one byte has crossed the host's bus.**
   *
   * A READ that runs into a mark hands the host **one** more byte -- the mark
   * block's first -- and then stops. Measured on the oracle at the cartridge's
   * last mark, where its driver reads the channel's count as `21FE` against
   * this core's `21FF`: 24,065 bytes against 24,064, which is 47 whole blocks
   * plus one. `FINDINGS.md` C285.
   *
   * It is *not* the block: 512 bytes of `DEAFFAED` would land in whatever the
   * host was loading, and `FINDINGS.md` C266 is the measurement that says so.
   * One byte, once, and the ending follows it. */
  bool mark_byte_sent;
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

/* Take the cartridge out, which is the other half of `ap_tape_load` and was
 * missing until 2026-09-11.
 *
 * **The drive's side of this was already complete and had no caller.**
 * `device/ap_qic.h` says so against `ap_qic_eject`: "Nothing in any frontend
 * removes a cartridge from a running machine ... The drive's side is right and
 * complete; what is absent is a way to ask for it." This is that route, and it
 * exists because a Domain/OS install asks for it -- `008860-A03` Chapter 1's
 * Step 4 runs `minst`, which asks for each distribution cartridge in turn, and
 * a machine with one drive and no way to change its media cannot answer.
 *
 * **What it clears, and what it deliberately does not.** The controller holds
 * bytes of the cartridge that is leaving -- a partly-handed-over data block, a
 * composed status block -- and those must not be handed to a driver reading the
 * cartridge that arrives, so they go. `first_block_pending` is re-armed because
 * `QIC-02 Rev D` §4.2.7 and §4.2.8 say a READ or WRITE "following cartridge
 * insertion or RESET shall commence at BOT". The SC-499 is *not* reset: a
 * cartridge change is an operator action at the drive, not a power-on of the
 * card, and §5.2 makes `CNI` an operator-correctable **condition rather than a
 * latch** -- so an empty drive answers `CNI` while it is empty and stops when
 * it is not, and there is nothing to latch on the way through.
 *
 * Returns whether the cartridge actually came out. It does not when the drive
 * holds a soft lock, which `ap_qic_eject` honours because the lock is a lock on
 * the cartridge; a caller that swaps media under a running transfer is told so
 * rather than left to wonder. */
[[nodiscard]] bool ap_tape_eject(ap_tape_t *tape);

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

/* The `EOP` the 8237 drives at its terminal count, which is the only signal
 * that says a transfer is over: the card counts bytes through a FIFO and has no
 * length of its own. Forwarded to the controller, whose DONE bit is the thing
 * the firmware waits on. */
void ap_tape_dma_ended(ap_tape_t *tape);
void ap_tape_dma_write(ap_tape_t *tape, uint8_t value);

#endif /* APOLLO_BOARD_AP_TAPE_H */

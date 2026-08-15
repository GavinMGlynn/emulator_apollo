/* DN4500 Matrox graphics controller: the register interface.
 *
 * Sources are `docs/references/GRAPHICS.md`, whose numbered findings are cited
 * throughout rather than restated. As with the ring and the EtherLink Plus,
 * the map came out of the board's **own option ROM** -- `[ROMMX]`, Apollo part
 * 013748 -- because `[S3K]`'s graphics chapter covers the DN3000 and DN4000
 * controllers only, the web has no register-level material for this part, and
 * MAME does not register the 4500 variants at all. GRAPHICS.md records that
 * exhaustion rather than implying it.
 *
 * ## Three blocks, extracted mechanically
 *
 * Finding 5: every absolute long operand in the image resolves to one of three
 * bases. Finding 5a proves `$DA0000` is a *base* rather than an isolated
 * register, because `$4D8` does `movea.l #$da0000,a3` and then writes through
 * `(a0)+` from it.
 *
 *   `$D40000`  a **bidirectional data port** (finding 7). It receives the
 *              16-longword parameter table of finding 4a, then four literal
 *              words `$5AA5 $A534 $1744 $1345`, and `$5D6` reads it back.
 *   `$D80000`  a longword path at `+8` with a **ready bit at `+4` bit 7**,
 *              polled once after every transfer (finding 8). `+5` takes `$80`.
 *   `$DA0000`  the **microcode port** and the control block. `+0` and `+4`
 *              take an opening longword and word; `+6`/`+7` are
 *              command-over-status (finding 6).
 *
 * ## What the firmware asserts, which is all this models
 *
 * The routine ending at `$5E0` returns a verdict in `d3` -- `0` pass, `$FFFF`
 * fail -- and reaching the pass arm needs exactly two things of `$DA0006`:
 *
 *   `$59E`-`$5AE`  **bit 3 must read clear**, polled with a 15,728,640
 *                  iteration budget (`move.l #$f00000,d1`). Finding 9.
 *   `$5B8`-`$5CE`  **bit 6 must read clear**, tested once. A set bit stores
 *                  `$FFFF` exactly as the timeout does.
 *
 * Bits 4 and 5 are `btst`ed elsewhere (finding 6) and their required polarity
 * is **not** established -- the sites are reached only after this routine, so
 * nothing has measured them yet.
 *
 * **So the status register reads zero**, and that is a deliberate choice of the
 * same kind `RING.md` finding 62 made for the ring's ID lane: zero asserts
 * nothing the firmware did not, where any set bit would claim a condition no
 * measurement has seen. The bits this core has evidence for are clear; the rest
 * are clear because nothing says otherwise, and the next failure the firmware
 * reports is what will change that.
 *
 * ## The microcode, accepted and not executed
 *
 * Finding 4b: `$504` feeds **2358 words -- 4,716 bytes -- from ROM `+B22`** to
 * `$DA0000`, which is never incremented, and `$B22 + 2358x2` is exactly the
 * header's `length`. So the image runs to the last byte of the checksummed ROM.
 *
 * Nothing here executes it, and nothing needs to: the target processor is
 * unidentified and no display output depends on the program until there is a
 * frame buffer to draw into. What the model owes the firmware is to **accept**
 * the words, and what it owes the next reader is the count -- so the words are
 * counted and discarded, and a count that stops at 2358 is evidence the whole
 * image arrived.
 */

#ifndef APOLLO_DEVICE_AP_MATROX_H
#define APOLLO_DEVICE_AP_MATROX_H

#include <stdbool.h>
#include <stdint.h>

/* The three bases, from finding 5's mechanical scan. They sit inside AT bus
 * *memory* space, which this board already decodes -- so the card is checked
 * ahead of that window exactly as the ring and the EtherLink Plus are, and an
 * unfitted machine falls through to the empty-slot `FF`. */
#define AP_MATROX_DATA_ADDR 0x00D40000u
#define AP_MATROX_XFER_ADDR 0x00D80000u
#define AP_MATROX_CTL_ADDR 0x00DA0000u

/* **Extents are not established and these are the smallest that cover every
 * observed access**, which is the honest choice until something addresses
 * further. The firmware touches `$D40000+0`; `$D80000+4`, `+5`, `+8`; and
 * `$DA0000+0`, `+4`, `+6`, `+7`. A range claimed wider would be this core
 * asserting a decode nobody has seen -- the mistake `RING.md` 43b records
 * against the AT window itself. */
#define AP_MATROX_DATA_RANGE 0x10u
#define AP_MATROX_XFER_RANGE 0x10u
#define AP_MATROX_CTL_RANGE 0x10u

/* `$DA0006`'s two bits the firmware's verdict depends on, named so the tests
 * cite conditions rather than magic numbers. Both must read **clear**. */
#define AP_MATROX_STATUS_BUSY 0x08u  /* bit 3, polled at `$59E` with a budget */
#define AP_MATROX_STATUS_ERROR 0x40u /* bit 6, tested once at `$5B8` */

typedef struct {
  /* Words fed to the microcode port. Counted rather than stored: finding 4b
   * gives the image's length and its identity, so what a model can be checked
   * against is *how many arrived*, not what they were. */
  unsigned microcode_words;

  /* The last longword written to `$D80008`, and whether `$D80005` has been
   * armed with its `$80`. Kept because they are what the ready bit at `+4`
   * would report on, and because a transfer path that stored nothing could not
   * later be given one without changing the tests that describe it. */
  uint32_t last_transfer;
  bool transfer_armed;

  /* The last word written to the data port, which finding 7's write-then-read
   * signature check is about. */
  uint16_t data_latch;
} ap_matrox_t;

/* Power-on state. */
void ap_matrox_reset(ap_matrox_t *matrox);

/* Whether an address falls in one of the three blocks, and its offset within
 * whichever it is. `block` is the base, so a caller can tell them apart. */
[[nodiscard]] bool ap_matrox_decode(uint32_t address, uint32_t *block,
                                    uint32_t *offset);

[[nodiscard]] uint8_t ap_matrox_read8(ap_matrox_t *matrox, uint32_t block,
                                      uint32_t offset);
void ap_matrox_write8(ap_matrox_t *matrox, uint32_t block, uint32_t offset,
                      uint8_t value);

#endif /* APOLLO_DEVICE_AP_MATROX_H */

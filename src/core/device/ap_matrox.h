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
 * **Bit 5 is now established too, and it wants the opposite** (`GRAPHICS.md`
 * 13c): `$2EC` waits for it *set*. So the register reads `$20` -- bit 5 set,
 * bits 3 and 6 clear -- which is exactly the three conditions the firmware has
 * been measured to require and nothing more.
 *
 * Bit 4's polarity is still **not** established; its `btst` at `$3BA` has not
 * been reached. It reads clear for the reason the whole register used to:
 * `RING.md` finding 62's rule, that a set bit claims a condition no
 * measurement has seen. Each bit is turned on only by a firmware assertion,
 * which is what makes the sequence of failures a measurement rather than a
 * search.
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
/* Bit 5, and the firmware wants it the *other* way. `$2EC`-`$310` loads
 * `d0 = $FFF0`, polls `btst.b #$5,$da0006.l` and **leaves early on `bne`** --
 * so a set bit is what ends the wait -- with a `divs.w` between two polls as a
 * delay and a `dbra` as the bound. Reached only once a display is fitted and
 * the PROM's graphics path gets that far (`GRAPHICS.md` 13b, 13c). */
#define AP_MATROX_STATUS_READY 0x20u

/* ## The frame buffer, `GRAPHICS.md` 16-17b
 *
 * Located by measurement before it was named: 30.7 M reads and 50,744 writes
 * landed in the undecoded AT window with the **first write at `000C63AF`**, and
 * the board's own ROM does `movea.l #$c63b2,a3` at `$2E0`. Two witnesses to
 * *where*, neither depending on a manual.
 *
 * `019411-A00` Table 2-5 -- the DS5500 256-MB allocation, the only 32-bit
 * Apollo map on disk and the class this core uses for the DN3500/4500/5500 --
 * then names the range **"ALTERNATE MONO GRAPHICS MEMORY SPACE"**, with a
 * single-board ring controller sharing its upper half from `D0000`. The
 * measured write is below that, in the graphics half.
 *
 * **The geometry is a hypothesis with arithmetic behind it, not a measurement**
 * (17b): `0C0000`-`0DFFFF` is 128 KB = 1,048,576 bits = exactly **1024 x 1024
 * at one bit per pixel**, the depth "mono" implies -- and finding 4a's
 * parameter table, written to `$D40000` long before any of this, carries
 * `00000400` = 1024. Rendering is the discriminator: a wrong pitch shears a
 * picture that is still legible, a right one does not. */
#define AP_MATROX_FRAME_ADDR 0x0C0000u
#define AP_MATROX_FRAME_BYTES 0x20000u
#define AP_MATROX_FRAME_WIDTH 1024u
#define AP_MATROX_FRAME_HEIGHT 1024u

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

  /* The frame, **allocated by the frontend and borrowed here**, which is the
   * rule `src/core` follows for the screen's memories too: the core allocates
   * nothing. A null pointer is a card with no frame attached, and its range
   * then reads as an undriven bus does rather than as zeroes. */
  uint8_t *frame;
  uint32_t frame_bytes;
  /* Distinct bytes of the frame the machine has written, so a run can say
   * whether a picture was drawn at all without decoding one. A frame that is
   * written and still black and a frame that was never written are different
   * failures and report identically without this. */
  unsigned frame_writes;
} ap_matrox_t;

/* Lend the device a frame buffer. `bytes` under `AP_MATROX_FRAME_BYTES` is
 * accepted and bounds the decode, so a caller may attach a short buffer
 * deliberately; the range beyond it reads undriven. */
void ap_matrox_attach_frame(ap_matrox_t *matrox, uint8_t *frame,
                            uint32_t bytes);

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

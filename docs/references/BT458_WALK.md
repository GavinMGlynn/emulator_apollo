# Bt458 RAMDAC — walk coverage record

The DN3500's 8-plane colour lookup table. `008778-03` §10.3 gives the shape
("256 x 24") and never names the part; the databook never mentions Apollo; the
oracle drives it as a `bt458`. Identification reasoning is in `ap_bt458.h`.

| Tag | File | Pages | Text layer | State |
| --- | --- | --- | --- | --- |
| `[Bt458]` | `brooktree/Bt458_RAMDAC_Databook_1991.pdf` | 24 | yes | **in progress** — the Internal Registers section read 2026-08-22 |

Extracted from `1991_Brooktree_Product_Databook.pdf` (1106 pages), PDF 393-416 =
the Bt458/883 datasheet, section 4 "RAMDACS", document pages 4-111 onward.

## Why this one was picked out of the batch

A citation audit across the remaining parts — the check that had just corrected
the `[6840]` item — put `[3c505]` at 22 derived sections, `[QIC]` at 16 and
`[SC-499]` at 9, and **`[Bt458]` at one**, which is `008778-03` §10.3 rather
than the databook at all. So this was the genuine gap in the batch and the
others are largely derived.

*The audit undercounted here and the reason is worth recording*: `ap_bt458.h`
cites the databook by **"Table 1"** and by quotation rather than by section
number, so a `§`-shaped grep sees nothing. Counting `§` is a cheap first pass,
not a verdict — check the header before concluding a part is unread.

## What is already derived, and it is more than the audit suggested

`ap_bt458.h` carries Table 1's `C1`/`C0` selector, the **`ADDRa,b`
modulo-three component counter** the MPU cannot see, the rule that a colour is
concatenated and written only on the blue cycle, and the difference in address
advance between the two spaces — palette RAM wrapping `$FF` → `$00` while the
overlays run `$03` → `$04` into the read mask. Those are exactly the traps a
casual model gets wrong, and they are quoted from the page.

## The gap: the command register is stored and never decoded

PDF 7 (doc 4-117), *Internal Registers — Command Register*, read as a page
image. Eight bits, every one of which this core keeps as an opaque byte:

| bit | function | modelled? |
| --- | --- | --- |
| `CR7` | multiplex select, 4:1 or 5:1 | no — pipeline/timing, and see below |
| `CR6` | **RAM enable**: when the overlay select bits are 00, "whether to use the color palette RAM **or overlay color 0** to provide color information" | **no, and it changes pixels** |
| `CR5`,`CR4` | blink rate, in vertical retrace intervals: `00` 16 on/48 off, `01` 16/16, `10` 32/32, `11` 64/64 | no |
| `CR3` | OL1 blink enable | no |
| `CR2` | OL0 blink enable | no |
| `CR1`,`CR0` | overlay display enable (doc 4-118) | no |

`ap_bt458_t` holds `command`, `blink_mask` and `test` as plain `uint8_t` and
**nothing reads them** — `ap_graphics.c` has no reference to any of them. So a
driver that clears `CR6` to paint the screen from overlay colour 0, or enables
blinking, gets no change in any rendered pixel.

*How much this matters is not yet established*, and that is the honest state: it
needs a check of whether Domain/OS's colour driver writes the command register
at all, which the graphics register-write log can answer. Named as a plan item
with that discriminator rather than implemented on the strength of the table.

**And one initialisation fact to decide rather than inherit**: "The command
register may be written to or read by the MPU at any time, and **is not
initialized**." `ap_bt458_reset` zeroes the whole struct, so this core answers
zero where the part answers whatever it powered up with. Zero is the right
choice for a deterministic core — a golden cannot pin an undefined value — but
it is a *choice* and was not written down.

## Owed

PDF 1-6 and 8-24: the pin descriptions, the remaining internal registers
(read mask, blink mask, test), the frame-timing and pixel-path description, and
the electrical and packaging pages. 23 pages, with a text layer for navigation
but the register tables read as images.

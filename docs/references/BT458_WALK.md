# Bt458 RAMDAC — walk coverage record

The DN3500's 8-plane colour lookup table. `008778-03` §10.3 gives the shape
("256 x 24") and never names the part; the databook never mentions Apollo; the
oracle drives it as a `bt458`. Identification reasoning is in `ap_bt458.h`.

| Tag | File | Pages | Text layer | State |
| --- | --- | --- | --- | --- |
| `[Bt458]` | `brooktree/Bt458_RAMDAC_Databook_1991.pdf` | 24 | yes | **walked whole, 24/24, 2026-08-22** |

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

> **ANSWERED 2026-08-22, and the answer is that decoding would change no pixel.**
> A `--screen c8p` boot writes three of the four four times each and the test
> register never, settling at **read mask `FF`, blink mask `00`, command `40`**.
> `CR6` = 1 is "use color palette RAM", which this core does unconditionally;
> `CR3`/`CR2` = 0 and the blink mask `00` mean nothing blinks whatever the rate
> in `CR5`/`CR4`; `CR1`/`CR0` = 0 force overlay inputs this model has no pins
> for; `FF` masks no plane. The firmware programs the part into exactly the
> configuration already implemented, and the two registers the databook calls
> "not initialized" are set to their identity values.
> A documented approximation with proof. Detail in `PROJECT_STATUS.md`.

**And one initialisation fact to decide rather than inherit**: "The command
register may be written to or read by the MPU at any time, and **is not
initialized**." `ap_bt458_reset` zeroes the whole struct, so this core answers
zero where the part answers whatever it powered up with. Zero is the right
choice for a deterministic core — a golden cannot pin an undefined value — but
it is a *choice* and was not written down.

## WALKED WHOLE — 24 of 24, 2026-08-22

The remaining 22 pages read as images. Four things beyond the command-register
measurement above.

**Table 2 confirms that measurement from the truth table itself** (doc 4-115).
`CR6` = 1 with `OL1`/`OL0` = 00 addresses "color palette entry `$00`-`$FF`";
`CR6` = 0 with the same overlay inputs addresses "overlay color 0". So the
firmware's `command` = `40` puts the part in the row this core implements, read
off a table rather than inferred from bit descriptions.

**And doc 4-128 explains *why* the blink registers read as they do.** Under
*Setting the Pipeline Delay*: "if the multiple Bt458/883s are used in parallel,
the on-chip blink counters may not be synchronized. In this instance, **the
blink mask register should be `$00` and the overlay blink enable bits a logical
zero**. Blinking may be done under software control via the read mask register
and overlay display enable bits." The measured `blink mask 00` with `CR3`/`CR2`
= 0 is therefore **Brooktree's own recommended configuration** for not using the
hardware blink counters — not an incidental zero. *What to watch*: software
blinking would show as repeated read-mask writes; four writes is initialisation.

**One inference becomes a citation.** `ap_bt458.c` says of the control registers
"The address does not advance, and nothing in Table 1 says it should". Doc 4-113
says it outright: "The address register **does not increment** following read or
write cycles to the control registers, facilitating read-modify-write
operations."

**The test register is fully defined** (doc 4-119) and this core stores it as an
opaque byte: `D7`-`D4` are four bits of colour read back from the DAC inputs,
`D3` selects the low or high nibble, and `D2`/`D1`/`D0` are the blue, green and
red enables, of which exactly one may be set. "**When writing to the register,
the upper 4 bits (D4-D7) are ignored**", which this core does not mask. Left
alone: the measurement above shows the test register is **never written** on
this machine, so the divergence is unreachable, and the counter would report it
if that changed.

## The finding that is not about the Bt458: our A/D scale's justification

`ap_graphics.h` explains the video A/D's units and ends "70 for green in the
blanking interval is 0.70 V, **which is the sync level** a composite-sync-on-
green monitor expects." This walk put the part's own output levels on the shelf
for the first time (Figure 3, doc 4-116, and the DC characteristics, doc 4-131):

    green      sync 0.000 V   blank 0.286 V   black 0.340 V   white 1.000 V
    red, blue                 blank 0.000 V   black 0.054 V   white 0.714 V

**0.70 V is none of green's levels.** Green's sync level is zero and its blank
level is 0.286 V. Nor is it a scaling artefact — FS ADJUST states that "the IRE
relationships in Figure 3 are maintained, regardless of the full-scale output
current", so `RSET` moves all four together and 0.70 V remains 70% of full scale
where blank is 28.6%.

*The claim is deliberately narrow.* The values this core returns are unchanged
and are **not** asserted wrong: they came from the oracle, they satisfy the
firmware's own `[52, 70)` check, and the A/D measures the board's video output at
the beam rather than a DAC pin. What is established is that **the explanation
attached to the number cites a level the part does not produce** — a
justification that survived only because nobody had opened the datasheet.
Corrected in `ap_graphics.h`; the numbers stand where they always stood, on the
oracle and the firmware's check, with one fewer borrowed reason.
**What would settle it**: the DN3500 colour board's schematic, or a probe.

## Owed

Nothing of this document. *Original text, kept because it is what the coverage
looked like before the walk finished:* "PDF 1-6 and 8-24: the pin descriptions,
the remaining internal registers (read mask, blink mask, test), the frame-timing
and pixel-path description, and the electrical and packaging pages."
One item leaves this record for the plan: the A/D scale's justification, above.

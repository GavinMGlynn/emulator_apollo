# MC68030 Electrical Specifications — walk coverage record

`MC68030EC/D` Rev 1, 1990. **Walked whole, 19/19, 2026-09-07.**

| Tag | File | Pages | Text layer | State |
| --- | --- | --- | --- | --- |
| `[030EC]` | `motorola/MC68030EC.pdf` | 19 | **scanned, OCR** | **WALKED WHOLE, 19/19** |

## Why it was walked at all

`[030]` §13 is two pages — maximum ratings and PGA thermal resistance — and
names this document for everything else: "Detailed information on timing
specifications for power considerations, DC electrical characteristics, and AC
timing specifications can be found in the MC68030EC/D, *MC68030 Electrical
Specifications*."

It was found by the `[030]` §12-§14 walk on 2026-09-06 and opened as a plan
item on the rule that **a document naming another document is not finished until
the named one is on the list**.

*Method*: `pdfimages -list` shows full-page CCITT scans at 299 dpi, so page
images throughout.

## It is genuinely electrical — and it still carried three behavioural facts

The expectation was nanosecond signal timings with no part behaviour, and that
is most of what is here. But `[8259]` hid `TJLJH` in a range dismissed as
electrical and `[8237]` hid two rule pages in one dismissed as mechanical, and
the same thing happened again: **five of the AC specifications are given in
clock units, not nanoseconds**, and three of them corroborate work done earlier
in this same session.

| Spec | Characteristic | Value |
| --- | --- | --- |
| 32 | `RESET` Input Transition Time | 1.5 Clks max |
| 35 | `BR` Asserted to `BG` Asserted **(RMC Not Asserted)** | 1.5–3.5 Clks |
| 37, 37A | `BGACK` Asserted to `BG`/`BR` Negated | 1.5–3.5 / 0–1.5 Clks |
| **56** | **`RESET` Pulse Width (Reset Instruction)** | **512 Clks min**, at every frequency |
| 58, 59 | `BGACK`/`BG` Negated to Bus Driven | 1 Clk min |

**Spec 56 is a third independent witness for the `RESET` timing** added on
2026-09-06. `[030]` §11.6.17 gives the instruction as `518(0/0/0)` and `[PRM]`'s
`RESET` page says "asserts the RSTO signal for 512 ... clock periods"; this says
512 clocks in a table, in clock units, at all five speed grades. The 518 is
those 512 plus six of overhead, and it now rests on three documents.

**Spec 35's parenthesis is a fourth witness for the arbitration lock.** "`BR`
Asserted to `BG` Asserted **(RMC Not Asserted)**" — the grant timing is only
specified when `RMC` is *not* asserted, because when it is there is no grant to
time. That is `[030]` §7.7.1, §11.9 and §12.1.2 stated a fourth way, and it is
the rule `ap_board_set_processor_rmc` was wired up for on the same day.

**Figure 8, *Other Signal Timings*, is the electrical page for the signals the
`MMUDIS` item names.** It draws `IPEND`, **`MMUDIS`**, **`CDIS`**, **`STATUS`**
and **`REFILL`** against the clock, with spec 47A's asynchronous input setup on
`MMUDIS`/`CDIS` and specs 62/63 for `STATUS`/`REFILL` assertion and negation.
So the two emulator-support inputs this core does not model, and the two outputs
it does not drive, all have their timing here — which is what a board that
*did* drive them would need. Recorded with that item.

## Captured

- **Maximum ratings**: `VCC` −0.3 to +7.0 V, `Vin` −0.5 to +7.0 V, `TA` 0–70 °C
  for parts to 40 MHz, `TC` 80 °C for the 50 MHz part, `Tstg` −55 to 150 °C —
  and the footnote that "a continuous clock must be supplied to the MC68030 when
  it is powered up".
- **Thermal characteristics** (PGA): θJA 30 °C/W, θJC 15 °C/W, both estimated;
  and §"Power Considerations"'s four equations relating `TJ`, `PD`, `θJA` and
  the constant `K`.
- **DC specifications**: input/output levels, leakage, the four `IOL` groups
  with their signal lists, `PD` 2.6 W max at `TA` = 0 °C, `Cin` 20 pF, and load
  capacitance 50 pF for `ECS`/`OCS`, 70 for `CIOUT`/`STATUS`/`REFILL`, 130 for
  everything else.
- **AC clock input** (Figure 2) and **read/write cycle** specifications 6
  through 63, at **20, 25, 33.33, 40 and 50 MHz** — five speed grades.
- **Figures 1 and 3–8**: drive levels and test points, and the read, write,
  bus arbitration and other-signal waveform diagrams.
- **All fourteen notes**, including note 8 and note 14, which exist to guarantee
  interoperation with the **MC68881/MC68882** — spec 15A is there so that the
  68030 meets spec 13A of the FPU's manual, and spec 9B so a designer can
  qualify the FPU's `CS` with `AS` and still meet its spec 8B.

## One version note, and it points the other way from `[030]`

This document specifies **40 MHz and 50 MHz** parts. `[030]` §14.1's ordering
information lists only 20, 25 and 33.33 MHz, and §12.4.1 discusses a 16.67 MHz
part that §14.1 no longer lists. So the manual's ordering section is *behind* on
both ends — it dropped the 16 MHz part and had not yet added the 40 and 50 MHz
ones — while this Rev 1 sheet from the same year carries the full range. No
consequence for this project: every model in the table runs at 20, 25 or
33.33 MHz.

## Owed

Nothing.

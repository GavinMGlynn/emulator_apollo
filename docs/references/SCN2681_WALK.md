# SCN2681 DUART — walk coverage record

The DN3500's serial ports. `008778-03` §3.9: "All ports are implemented using
the **Signetics 2681** dual asynchronous control chip."

| Tag | File | Pages | Native | State |
| --- | --- | --- | --- | --- |
| `[2681]` | `signetics/SCN2681_DUART_Signetics_1986.pdf` | 19 | text layer | **walked whole, 19/19, 2026-08-22** |
| `[68681]` | `motorola/MC68681_Dual_Asynchronous_Receiver_Transmitter_DUART_Sep85.pdf` | — | — | the manual this core was built from |

Extracted from `1986_Signetics_Microprocessor.pdf` (bitsavers, 798 pages), PDF
pages 200-218 = document pages 2-189 to 2-207, dated February 20 1985.

## Why this was opened, and it is the `[765]` lesson repeating

`ap_mc68681.h` line 1 names the part "MC68681 / **SCN2681**" and then builds the
whole model from Motorola's datasheet, on the strength of one sentence it quotes
in full:

> "The MC2681 ... is functionally equivalent to the MC68681 **with some minor
> differences.**"

**The differences are never named** — not in the header, not anywhere in this
project. That is a vendor's one-line characterisation of a competitor's part,
used as an equivalence proof. It is the same shape as `[8000]` §1.3.1's "NEC765
**or equivalent**", which had just cost this project five floppy commands. The
walk was opened on that pattern rather than on a symptom, and it found two.

## Coverage

| doc pages | content | yield |
| --- | --- | --- |
| 2-189 | Description, features, pin configuration (40/28/24-pin DIP) | 18 fixed baud rates, 7-bit input port, 8-bit output port, change-of-state on **four** inputs. **No `IACK` pin in any package** |
| 2-190 | PLCC pinout, ordering codes, pin description | Bus is `CEN`/`WRN`/`RDN`/`A0-A3`/`D0-D7`/`INTRN` — a generic bus, no 68000 vector logic. **RESET puts `OP0`-`OP7` high** and clears `SRA`, `SRB`, `IMR`, `ISR`, `OPR`, `OPCR` |
| 2-191 | Pin description (cont.) | `OP0`/`OP1` = `RTSAN`/`RTSBN`; `IP0`/`IP1` = `CTSAN`/`CTSBN`; **`IP2` = counter/timer external clock**; `IP3`-`IP6` = per-channel external Tx/Rx clocks |
| 2-192 | Block diagram | Interrupt control contains **`IMR` and `ISR` only** — no vector register. Receive holding register is **3 deep** plus the shift register |
| 2-193 | Functional description of every block | **The 25-50 µs change-of-state filter** — see below. Output pins are the **complement** of `OPR`. Crystal 3.6864 MHz. Transmitter `TxRDY`/`THR`/`TxEMT` semantics |
| 2-194 | Transmitter tail, receiver, multidrop, programming | `CTS` gating mid-character; receiver start-bit search at 16X for 7½ clocks; FIFO of three; character vs block error mode; "the status register should be read **prior to** reading the FIFO" |
| 2-195 | **Table 1, register addressing**; `MR1A`; Table 2 begins | **Address `1100` is `*Reserved*` on read *and* write** — see below. `H'02'` and `H'0A'` reserved on read, "for internal diagnostics" |
| 2-196 | Table 2: `MR2`, `CSR`, `CR`, `SR`, `OPCR`, `ACR`, `IPCR` | Every field; `IPCR` carries deltas for `IP0`-`IP3` only; `ACR[3:0]` are the per-pin change-interrupt enables; `ACR[7]` selects the baud set |
| 2-197 | Table 2: `ISR`, `IMR`, `CTUR`, `CTLR`; `MR1A` fields; `MR2A` modes | The four channel modes with all conditions — normal, auto-echo (8), local loop (6), remote loop (7) |
| 2-198 | `MR2A` fields, `MR1B`/`MR2B`, `CSRA`/`CSRB` baud tables, `CRA` | The full 16-entry baud select per channel, both `ACR[7]` sets, with `IP3`-`IP6` external-clock encodings |
| 2-199 | `CRA` commands, `CRB`, `SRA` bit definitions, `OPCR[7]` | Every command and status bit, field by field |
| 2-200 | `OPCR` fields, `ACR`, `IPCR`, `ISR[7:3]` | **A read of the `IPCR` also clears `ISR[7]`.** `ACR[3:0]` gate propagation to `ISR[7]` only — the `IPCR` delta bit sets regardless |
| 2-201 | **Table 3 baud generator**, **Table 4 `ACR[6:4]`**, `ISR[2:0]`, `IMR`, `CTUR`/`CTLR` | Actual 16X clock and error per rate at 3.6864 MHz; the eight counter/timer modes and sources; **`CTUR`/`CTLR` are write-only**, minimum value `0002₁₆`; on reset the C/T runs in timer mode |
| 2-202 | Absolute maximum ratings, DC and AC characteristics | Electrical — not modelled |
| 2-203 | AC characteristics (cont.), Figure 1 | **The six actions that negate `INTRN`**: read `RHR`, write `THR`, reset command, stop C/T command, read `IPCR`, write `IMR`. `X1/CLK` 2.0-4.0 MHz, typical 3.6864 |
| 2-204 | Figure 2, bus timing | Electrical |
| 2-205 | Figures 3-5, port/interrupt/clock timing | Electrical, plus the crystal circuit |
| 2-206 | Figures 6-8, transmit/receive/**transmitter timing** | Figure 8 is behavioural: `TxRDY`, `CTSN` gating, start/stop break, `RTSN` auto-negation |
| 2-207 | Figures 9-10, **receiver timing**, wake-up mode | Figure 9 is behavioural: FIFO fill, overrun, `RTS` flow control |

## Finding 1 — register `0x0C` is Reserved, not an interrupt vector register

Table 1, address `A3-A0 = 1100`: **`*Reserved*`** on read and on write.

`[68681]` puts the **Interrupt Vector Register** there, and `ap_mc68681.h` follows
it — `AP_MC68681_IVR = 12u  /* R/W interrupt vector */`, with `ap_mc68681.c`
initialising it to `0F₁₆` on reset, per `[68681]` §2.4, and honouring reads and
writes.

*This is the difference Motorola's sentence was covering*, and it is exactly
where one would expect it: the MC68681 is the 68000-bus part with an `IACK` pin
and a vector to supply; the SCN2681 has neither. **No package of the SCN2681 has
an `IACK` pin** — checked across the 24-, 28-, 40-pin and PLCC pinouts.

**Whether it matters here is a separate question, and it is measurable.** The
DN3500's DUART interrupts reach the CPU through the **8259**, which supplies the
vector (`008778-03` Table 2-3, `IRQ1` "2681 SIO Port 1"), so the DUART's own
vector is not used for vectoring either way. What would differ is a driver that
*reads or writes* `0x0C`. **This core already counts that**: `ap_sio.h` keeps
`register_reads[2][AP_MC68681_REGISTERS]` and `register_writes`. So the
discriminator is a report line over an existing counter, not a new experiment —
made a plan item rather than guessed at.

## Finding 2 — the change-of-state detector has a 25-50 µs filter, and this core has none

Doc 2-193, in full:

> "A high-to-low or low-to-high transition of these inputs, **lasting longer than
> 25-50 µs**, will set the corresponding bit in the input port change register."

and the mechanism, which gives the number its shape:

> "The input port pulse detection circuitry uses a **38.4 kHz sampling clock**
> derived from one of the baud rate generator taps. This results in a sampling
> period of slightly more than 25 µs ... The detection circuitry, in order to
> guarantee that a true change in level has occurred, **requires two successive
> samples at the new logic level be observed**. As a consequence, the minimum
> duration of the signal change is 25 µs if the transition occurs coincident with
> the first sample pulse. The 50 µs time refers to the situation in which the
> change of state is 'just missed'."

`ap_mc68681_set_input` sets the `IPCR` delta bits **immediately**, on any change,
with no duration filter and no latency — verified, and there are zero hits for a
debounce anywhere in the file.

So this core reports a transition the part would ignore (shorter than 25 µs), and
reports every other transition **up to 50 µs early**.

**This lands on a live item.** The open `siologin`/DCD thread's remaining
sub-question is *ordering*: "every boot so far sets the pins **before** the driver
programs `ACR`, so no transition occurs after arming". A detector with a 25 µs
qualification and up to 50 µs of latency orders those two events differently from
one that fires instantly. That does not make the current reading wrong — it makes
it untested against the part's actual timing.

## What agrees, checked rather than assumed

The output pins as the complement of `OPR` (modelled, `ap_mc68681.c` line 465,
and the header cites the overbars); the four channel modes; the three-deep
receive FIFO; `ACR[3:0]` gating `ISR[7]` while the `IPCR` bit sets regardless;
`ACR[7]`'s two baud sets being chip-wide rather than per-channel, which the
header already records as a constraint; the counter/timer's eight modes.

## Owed

Nothing of the document. Two items open in `COMPLETION_PLAN.md`: the `0x0C`
divergence with its counter-based discriminator, and the change-of-state filter.

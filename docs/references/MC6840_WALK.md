# MC6840 programmable timer — walk coverage record

The DN3500's three system timers. `008778-03` §3.6 and `002398-04` p. 12-20.

| Tag | File | Pages | Text layer | State |
| --- | --- | --- | --- | --- |
| `[6840]` | `motorola/MC6840_PTM_Motorola.pdf` (datasheet) | 14 | **no** | **walked whole, 14/14, 2026-08-22** |
| `[6840UM]` | `motorola/MC6840UM_ProgrammableTimerModule.pdf` (user manual) | 56 | **no** | **chapter 3 derived**; see the audit |

Neither has a text layer — 14 characters for 14 pages and 56 for 56, which is
one page number each. Everything must be read as images.

## Opened with a citation audit, not with a page render — and that was right

`COMPLETION_PLAN.md` listed this part as "two printings on the shelf, **no
record**", which reads as *unread*. It is not. Grepping every section the model
cites gives **sixteen distinct user-manual sections**:

    §3.5  §3.5.1  §3.5.2  §3.6.1  §3.7  §3.7.1  §3.7.2  §3.8  §3.8.2
    §3.9  §3.10  §3.11  §4.1  and Figure 2-6

and they are derivations rather than references. `§3.11`'s two-step interrupt
clear is implemented with its sentence attached — "Read the status register (RS),
then read the timer (RT) causing the interrupt. (An interrupt that occurs between
RS and RT will not be cleared.)" — including the snapshot that makes the
parenthesis true. `§3.5.2`'s LSB buffer is latched on the MSB read so a 16-bit
read of a running counter is coherent. Figure 2-6's register selection is the
`ap_mc6840_rs_t` enum.

**So chapter 3 is `derived`, on this project's own definition**: taken from the
document, field by field, with the text quoted. It is not `walked`, because a
field-by-field pass could still find something — which is exactly what happened
to `[OMTI]` §6.4 and to `[146818]` Register B.

*This is the third time a plan item has called a document unread when the code
already derived it* — after `[OMTI]` §5 and `[OMTI]` §6.3. The check costs one
grep and should precede any decision to render seventy pages.

## Datasheet coverage, 2026-08-22

| p. | Content | Yield |
| --- | --- | --- |
| 1 | Overview, features, pin assignment, ordering | Three 16-bit counters, three control registers, one status register; ÷8 prescaler on Timer 3 only; `RESET` input; three maskable outputs |
| 5 | Figures 4-8, input setup/hold, output delay, `IRQ` release, `C3` synchronisation, test loads | `none` (electrical) |
| 6 | **DEVICE OPERATION**, bus interface, `RESET`, register selects | **`RESET`'s five effects** (a-e), including "all Control Register bits are cleared **with the exception of CR10** which is set" and latches preset to maximum. **`CR20` is an addressing bit**, so `CR#1` and `CR#3` share one address and the documented initialisation order is `CR3, CR2, CR1` |
| 7 | **Table 1 register selection**, `CR10`, `CR30`, **Table 2 control register bits** | Every bit of `CRX7`-`CRX0`. `CR10` holds all timers in preset while set, and "Counter Latches and Control Registers are **undisturbed** by an Internal Reset". The ÷8 prescaler sits between the clock input and Counter 3, usable with either clock source |
| 8 | Status register/interrupt flags, **counter latch initialization**, **Table 3 operating modes** | `INT = I1·CR16 + I2·CR26 + I3·CR36`; the **RS-RT** two-step clear and the three other ways a flag clears; `CRX2`'s two timeout formulas — `N+1` in 16-bit mode, **`(L+1)·(M+1)`** in dual 8-bit; one shared write-only MSB Buffer for all three timers, MSB first; `RESET` presets every latch to **65,535** |

*Checked against the model while reading, all four confirming*: the RS-RT
sequence with its between-reads exception, the shared LSB buffer (one, not one
per timer), the shared MSB buffer, and Figure 2-6's `CR20` addressing.

## `[6840]` datasheet FINISHED — 14 of 14, 2026-08-22

The nine remaining pages read as images. Pages 2-4 and 14 are the block diagram,
power considerations, DC/AC characteristics, bus timing and package dimensions —
electrical and mechanical throughout, recorded as read. Pages 9-13 are not:

| p. | Content | Yield |
| --- | --- | --- |
| 9 | Counter initialization, asynchronous I/O lines, clock and gate inputs, timer outputs | **Counter Initialization defined**: latches → counter *with* the individual interrupt flag cleared, on `RESET`=0 or `CR10`=1, and by a Write Timer Latches or a negative Gate transition depending on mode. External clocks take **four Enable periods** to be recognised (three to synchronise, the fourth decrements); a Gate transition likewise. `CRX7` = 0 holds the output low "regardless of the operating mode", and clearing it while the output is high drops it "during the first enable cycle following a write". **And a scope statement**: "The Continuous and Single-Shot Timer Modes are the **only** ones for which output response is defined in this data sheet" — the measurement modes' waveforms live in `[6840UM]` |
| 10 | **Table 4 operating modes**, continuous mode, **Table 5** | `CRX3`/`CRX4`/`CRX5` selecting continuous, single-shot, frequency comparison and pulse-width comparison. 16-bit period `(N+1)`, dual 8-bit `(L+1)(M+1)`, both already modelled. **Two special conditions** — see below |
| 11 | **Figure 10**'s dual 8-bit waveform, single-shot mode, **Table 6** | The three differences of single-shot: output enabled for one pulse, counter enable independent of Gate, and `L=M=0` or `N=0` disabling the output. Figure 10 works `03`/`04` through as 20 clocks, which is this model's `(L+1)(M+1)` |
| 12 | **Wave measurement modes**, Figure 7's interrupt conditions | Frequency comparison and pulse-width comparison in full, with `CRX5` selecting which side of the comparison interrupts. The output *is* defined in these modes — low between reinitialisation and the first Time Out, then changing state at each Time Out |
| 13 | **Table 8**, frequency comparison logic | The counter-initialization, counter-enable set/reset and interrupt-flag conditions as boolean expressions, plus the symbol key (`G↓`, `W`, `R`, `N`, `TO`, `I`) |

### The two special conditions are emergent here, and that is worth a test

p. 10 gives two "special time-out conditions" for dual 8-bit mode:

> "if `L`=0 ... the counter will revert to a mode similar to the single 16-bit
> mode, except Time Out occurs after **M+1** clock pulses."
> "If `M`=`L`=0, the internal counters do not change, but the output toggles at
> **½ the clock frequency**."

**Neither needs a special case in this model.** With `L` = 0 the LSB counter is
already zero on every tick, so each clock falls straight through to the MSB
branch and the nested countdown degenerates to `M+1` by itself; with both zero,
a time out occurs on every clock, which is what a half-frequency toggle is. Two
tests pin exactly that (`mc6840_suite` 34 → 36), because "the datasheet calls
this special and our code does not mention it" is the shape of an accidental
omission as often as of an elegant one — and the only way to tell is to run it.

## Owed

- `[6840UM]`'s **chapters 1, 2 and 5 onward**, and the parts of chapters 3-4 not
  among the sixteen cited sections. 56 image-only pages.
Budget `[6840UM]` as a session of its own. The yield to expect is the `[146818]` shape —
chapter 3 confirming, and something small and real in a corner nobody had a
reason to query.

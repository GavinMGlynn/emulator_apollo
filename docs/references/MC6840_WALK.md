# MC6840 programmable timer — walk coverage record

The DN3500's three system timers. `008778-03` §3.6 and `002398-04` p. 12-20.

| Tag | File | Pages | Text layer | State |
| --- | --- | --- | --- | --- |
| `[6840]` | `motorola/MC6840_PTM_Motorola.pdf` (datasheet) | 14 | **no** | 5 of 14 read 2026-08-22 |
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

## Owed

- `[6840]` datasheet pages **2-4 and 9-14** — DC/AC characteristics, the
  remaining operating-mode text, and packaging.
- `[6840UM]` **chapters 1, 2 and 5 onward**, and the parts of chapters 3-4 not
  among the sixteen cited sections. 56 pages, images only.

Budget it as a session of its own. The yield to expect is the `[146818]` shape —
chapter 3 confirming, and something small and real in a corner nobody had a
reason to query.

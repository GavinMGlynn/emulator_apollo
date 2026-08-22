# MC146818A real-time clock — walk coverage record

The DN3500's calendar. `002398-04` p. 12-24 and `008778-03` §3.7 both name it.

| Tag | File | Pages | Text layer | State |
| --- | --- | --- | --- | --- |
| `[146818]` | `motorola/MC146818A_RealTimeClockPlusRAM.pdf` | 21 | yes | **walked whole, 21/21, 2026-08-22** |

Motorola "Advance Information", ADI-1026, © 1984, printed 10-84. Page 21 is a
`datasheetcatalog.com` trailer and not part of the document. Every register and
timing table was read as a 200 dpi image.

**Yield: one defect, one approximation the walk can now close, two typos in the
datasheet, and a confirmation of a board-level decision this project had made on
other grounds.**

## Coverage

| p. | Content | Yield |
| --- | --- | --- |
| 1 | Features, pin assignment | 50 bytes user RAM + 14 clock/control = 64; three time bases; binary **or** BCD; 12/24-hour; DST; automatic end-of-month and leap year; three maskable interrupts; `MOT` selects Motorola or competitor bus timing |
| 2 | **Figure 1 block diagram**, maximum ratings | ÷4 then four ÷32 stages to 1 Hz; a **1-of-15 selector** shared by the periodic interrupt and `SQW`; Registers A-D, 10 clock bytes, 50 RAM bytes drawn as separate arrays |
| 3 | DC characteristics | `none` (electrical) |
| 4 | **Bus timing** table and Figure 2 | `none` (electrical) — the part is 1 MHz-bus compatible; this core's calendar is not bus-cycle timed |
| 5 | Figures 3-4, competitor bus read/write timing | `none` (electrical) |
| 6 | **Table 1 switching characteristics**, Figures 5-6 | `none` behaviourally: oscillator startup 100 ms, reset pulse 5 µs, `IRQ` release 2 µs, `VRT` bit delay 2 µs. All below anything this core resolves |
| 7 | **Figures 7-8, power-up and the conditions that clear `VRT`** | The note that settles our `VRT`: "The VRT bit is set to a '1' by reading Register D. The VRT bit **can only be cleared by pulling the PS pin low**" |
| 8 | **Signal descriptions**, Table 2 clock output frequencies | `MOT`, `OSC1/2`, `CKOUT`, `CKFS`, `SQW`, `AD0-AD7`, `AS`, `DS`, `R/W`, `CS`. The `MOTEL` circuit's two readings of `DS` and `R/W` |
| 9 | Figures 9-11, crystal circuits and parameters | `none` (electrical) |
| 10 | **`IRQ`, `RESET`, `STBY`, `PS`** | **The `RESET` pin's effects a) to j)** — and see the typo below. "**The RESET pin does not affect the clock, calendar, or RAM functions.**" `PS` low clears `VRT` |
| 11 | Power-down, **address map (Figure 14)**, time/calendar/alarm locations | The three read-only exceptions: Registers C and D, **bit 7 of Register A**, and **the high-order bit of the seconds byte**. Update lockout 248 µs / *1948* µs (see typo below). The **don't-care alarm code is any byte `C0`-`FF`** — two MSBs set — giving hourly, per-minute or per-second alarms |
| 12 | **Table 3, every data-mode range**; static RAM; interrupts | Binary vs BCD ranges for all ten bytes, including 12-hour mode's `$81`-`$8C` PM encoding. With the dividers held reset the user RAM extends to **59 bytes**. Register C's flags set **independent of** the Register B enables; a read clears them all, double-latched; "**if an interrupt flag is already set when the interrupt becomes enabled, the `IRQ` pin is immediately activated**" |
| 13 | Divider stages, **Table 4 divider configurations**, **Table 5 rates** | `DV2:DV0` = `000`/`001`/`010` select the three time bases, `11X` holds the chain reset. All sixteen `RS3:RS0` rates with both time-base columns |
| 14 | **Update cycle**, Figure 15 | `tUC` = 248 µs (fast bases) or **1984 µs** (32.768 kHz); `tBUC` = **244 µs** of `UIP` lead. Data unavailable "once every 4032 attempts" at random, 2032 by the `UIP` route. DST needs the time set **two seconds** before a rollover |
| 15 | **Registers A, B and C**, Table 6 | Every bit. `UIP` read-only and *not* affected by Reset; `DV` and `RS` bits not affected by Reset. Register B bit by bit — and `UIE`'s clearing rule, the defect below. `IRQF = PF·PIE + AF·AIE + UF·UIE` |
| 16 | **Register C (cont.), Register D**, typical interfacing | `VRT` at bit 7, bits 6-0 "cannot be written, but are always read as 0's" |
| 17-19 | Figures 17-21, host interfacing and a 6800 driver | `none` (host-side) |
| 20 | Package dimensions | `none` (mechanical) |
| 21 | `datasheetcatalog.com` trailer | not part of the document |

## The defect: `UIE` survives the `SET` bit going high

p. 15, `UIE`: "The `RESET` pin going low **or the SET bit going high** clears the
UIE bit."

`ap_mc146818_write` had no case for Register B — it fell through to the plain
store — so a program that raised `SET` to stop the clock and set the time kept
its update-ended interrupt armed, and would take one on the next update after
clearing `SET`. Fixed, with three tests.

**Implemented as a transition, not a level**, and the distinction is the
interesting part: the datasheet says "going high". A level test (`if SET is set,
clear UIE`) would make it *impossible* to arm `UIE` while the clock is held —
and holding `SET` while programming the other Register B bits is exactly what an
initialisation sequence does. One of the three tests pins that.

## The approximation the walk can now close

`ap_mc146818.c` never sets `UIP`, and says so: "this core's update is
instantaneous, so it never [sets it] ... Modelling the 248 microsecond window
would need the rate tables". **Table 6 and Figure 15 are those figures**: `tUC` =
248 µs or 1984 µs by time base, and `tBUC` = 244 µs of `UIP` lead before the
update begins. So the named blocker on that approximation is gone; what remains
is whether it is worth modelling, since a driver polling `UIP` to dodge the
update simply never sees it set and reads valid data every time — permissive
rather than wrong. Recorded as a plan item rather than done here.

## Two typos in the datasheet

**The `RESET` list on p. 10 has a duplicate and an omission.** It reads a) PIE,
b) **AIE**, c) **AIE**, d) UF, e) IRQF, f) PF, g) not accessible, h) AF, i) IRQ
high-Z, j) SQWE. Items (b) and (c) are the same bit. `UIE` appears nowhere in the
list, yet p. 15 states plainly that `RESET` going low clears it — so one of the
two duplicated lines should read `UIE`. Followed p. 15, which is the register's
own definition.

**The 32.768 kHz update-cycle time is printed twice and disagrees with itself**:
p. 11 says **1948 µs**, p. 14 and Table 6 on p. 15 both say **1984 µs**. A
digit transposition. Table 6 is the table and carries the figure twice; 1984 µs
is also `2^16 / 33` ms to within rounding, which is the shape a divider chain
produces. 1984 taken.

## What the walk confirmed rather than changed

- **The calendar is correctly excluded from the board's reset line.**
  `ap_board_reset_devices` leaves it out, commented "its RAM is the node ID" —
  a decision made from `AP_BOARDREG_CONTROL_RESET_DEVICES` and from this
  project's own painful history with battery-backed state. p. 10 says it
  outright: "**The RESET pin does not affect the clock, calendar, or RAM
  functions.**" A board-level inference and the part's datasheet agreeing is
  worth recording, because the failure mode — a machine reset wiping the node ID
  and the `DM` bit — has cost this project sessions before.
- `VRT` held set, with no discharged battery to model: p. 7's Figure 8 note has
  it cleared *only* by `PS` going low, and set by a read of Register D.
- The don't-care alarm mask `C0`-`FF`, the `IRQF` formula, `DM` = 1 meaning
  binary, and the two `DSE` special updates — all already exact.

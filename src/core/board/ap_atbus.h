/* How long the AT-compatible bus takes to answer: `008778-03` Appendix A
 * (Table A-1, Series 3000) and Appendix B (Table B-1, Series 4000).
 *
 * `cpu/m68030/ap_m68030_access.h` gave a device the ability to lengthen its own
 * cycle and nothing declared a figure, so every region answered at the minimum
 * and contention was emergent in *who* held the bus but never in *how long*.
 * This is the first published figure, and it is a large one: an AT-bus device
 * answers in hundreds of nanoseconds where main memory answers in tens.
 *
 * ## The two tables say the same thing at two clock rates
 *
 * Appendix A is a 12 MHz machine and Appendix B a 16 MHz one, and the figures
 * look unrelated until they are divided by each table's own BUS CLOCK period --
 * `#26`, 166 ns and 125 ns. Then almost every row is the *same number of bus
 * clocks* in both:
 *
 *     #30 memory write cycle    500/166 = 3.0     375/125 = 3.0
 *     #37 16-bit I/O command    250/166 = 1.5     185/125 = 1.48
 *     #48 8-bit I/O command     750/166 = 4.5     560/125 = 4.48
 *     #17 MEMR.L width          330/166 = 1.99    250/125 = 2.0
 *     #55 BALE width            830/166 = 5.0     625/125 = 5.0
 *     #80 0WS memory cycle      415/166 = 2.5     313/125 = 2.5
 *
 * The residue is rounding: the tables print whole nanoseconds for a period that
 * is 166.67 ns. `atbus_suite` asserts the agreement rather than describing it,
 * so a transcription error in either column fails a test.
 *
 * That matters because it says what an AT bus cycle *is*: a fixed number of bus
 * clocks, and the two appendices differ only because their bus clocks do. The
 * one row that genuinely differs is the memory read cycle -- `#18`, four bus
 * clocks on the Series 3000 and three on the Series 4000 -- which is a real
 * wait state on the slower board and not an artefact of the division.
 *
 * The figures also confirm what the diagrams say in words: Figure B-3 and B-7
 * annotate the two internal clocks "Internal signal on the CPU/Motherboard. Not
 * available on the Bus", and BUS CLOCK is CLOCK halved on both boards.
 *
 * ## Which figure is which cycle, established from the diagrams and not the row
 *
 * Table A-1 and B-1 both carry *two* rows called "IOR.L, IOW.L Width Asserted",
 * `#37` and `#48`, with no note saying which is which. The timing diagrams
 * settle it: Figure B-3 "Bus 16-Bit I/O Read Cycle" annotates IOR.L's width
 * with `37`, and Figure B-7 "Bus 8-Bit I/O Read Cycle" annotates it with `48`.
 * Reading the page images was the whole of that: the numbers are inside the
 * drawings and no text extraction carries them.
 *
 * ## A published lower bound for I/O, and why it is used anyway
 *
 * Neither table gives an I/O *cycle* time. It gives the command width, and a
 * cycle is that plus the address setup before it (`#44`) and the hold after
 * (`#39`), which are published separately. So the I/O figure here is a **lower
 * bound**, and it is deliberately not summed into a total: this project's rule
 * for the 68030's footnoted timing rows was to report the component as the
 * lower bound it is rather than construct a total that would read as measured.
 *
 * The alternative is not "no approximation" but "no wait states at all", which
 * on a 25 MHz processor is 80 ns against a documented 750 -- wrong by an order
 * of magnitude in the direction that hides every contention this core exists to
 * show.
 *
 * ## CLOSED: §3.4 publishes the cycle, and §2.4.2 says why
 *
 * That paragraph named its own price -- "a published AT I/O cycle time, or a
 * measurement" -- and the `008778-03` walk found the first. **§3.4**: "The
 * normal AT bus cycle takes **500 nanoseconds for 16-bit transfers**. It takes
 * **1 microsecond for 8-bit transfers to 8-bit devices**. It takes **2
 * microseconds for 16-bit transfers to 8-bit devices**. These are the minimum
 * cycle times for devices on the AT bus."
 *
 * **§2.4.2 gives the same fact as a count**, which is the form used here:
 * "devices that need to produce more wait states than the **nominal 1 for
 * 16-bit designs or 4 for 8-bit designs**". Against a two-clock base that is
 * **3 bus clocks** for 16-bit and **6** for 8-bit -- and at the Series 3000's
 * 166.67 ns bus clock those are 500 ns and 1000 ns, reproducing §3.4's printed
 * figures exactly. Three sections agreeing is what makes this a derivation
 * rather than a transcription.
 *
 * **The count is used rather than the nanoseconds, deliberately.** §3.4 gives
 * one pair of figures while discussing both families, and they match the
 * *Series 3000's* clock; the appendices show cycles are not equal in absolute
 * time across the two boards -- memory read is 666 ns against 375 ns. A wait
 * state count is family-independent where a nanosecond figure is not, so the
 * Series 4000 gets 3 and 6 of *its* bus clocks, 375 ns and 750 ns. If a source
 * ever states the DS4000's I/O cycle in nanoseconds and it is not those, this
 * is the reasoning to revisit.
 *
 * The 2 microsecond figure for a 16-bit transfer to an 8-bit device needs no
 * row: §2.4.1 says such a transfer "is converted to two 8-bit transfers", and
 * two 1 us cycles is 2 us.
 *
 * ## The DS3500 is in neither appendix, and this is the PROVISIONAL
 *
 * `008778-03`'s preface: "This document supports the Domain Series 3000 (DS3000)
 * and Series 4000 (DS4000) systems." Our reference machine is a DS3500, which
 * `019411-A00` covers and which publishes no bus cycle times at all. So the
 * board's bus clock rate is not pinned by any document on disk.
 *
 * What *is* pinned is the bracket: an AT-compatible bus runs at one of these two
 * rates, the cycle counts agree at both, and only the memory read cycle differs
 * between them. The board takes the Series 3000 set, marked `PROVISIONAL`, and
 * the disagreement it can produce is one bus clock on a memory read.
 *
 * **Both closing routes have now been tried, and both are dead.**
 *
 * The document: `019411-A00` is titled an *Addendum to* the "Domain Personal
 * Workstations and Servers Hardware Architecture Handbook", so a base handbook
 * exists and would be the DS3500 reference this wants. Bitsavers' entire Apollo
 * directory was listed and holds the addendum and not the handbook; a web search
 * for the handbook returns only the addendum again. It does not appear to be
 * public.
 *
 * **What the walk added (2026-08-20): these handbooks are a numbered series.**
 * `002398-04` p. 10-1 opens chapter 10 with "FOR INFORMATION SPECIFIC TO THE
 * DN5xx-T NODES, REFER TO THE **DN570-T/DN580-T/DSP500-T HARDWARE ARCHITECTURE
 * HANDBOOK (009490)**". So Apollo published a hardware architecture handbook
 * *per family*, each with its own order number, and the Personal Workstations
 * one is another member of that series rather than a one-off. Bitsavers holds
 * `009492-00` and `009496-00` and **not** `009490`, so the sibling is missing
 * too -- which is evidence that the series as a whole was not scanned, not that
 * this particular volume is unusually rare.
 *
 * That turns "search for the handbook" into "search for an order number", which
 * is a different and better-shaped search. `009490` is the only number of the
 * series this walk has recovered so far; the Personal Workstations volume's is
 * still unknown, and any later chapter of this handbook that cites it by number
 * would close that gap.
 *
 * The oracle: it does not know either, and says so in its own source.
 * `ext/mame/src/mame/apollo/apollo_m.cpp` instantiates the bus with the comment
 * **`// FIXME: determine ISA bus clock`**. A measurement against it would
 * recover MAME's placeholder rather than the hardware's rate, which is the
 * failure mode `CLAUDE.md` warns about in saying the oracle's job is what the
 * documents cannot answer -- here it cannot answer it either.
 *
 * So this stays `PROVISIONAL` on evidence rather than on an untried route, and
 * the bracket above is the best statement available. It closes on hardware, or
 * on the base handbook surfacing.
 */

#ifndef APOLLO_BOARD_AP_ATBUS_H
#define APOLLO_BOARD_AP_ATBUS_H

#include <stdbool.h>
#include <stdint.h>

#include "time/ap_time.h"

/* Which appendix. */
typedef enum {
  AP_ATBUS_SERIES_3000,
  AP_ATBUS_SERIES_4000,
} ap_atbus_series_t;

/* The kinds of cycle the tables give a figure for, and no others. */
typedef enum {
  /* The AT memory window. `#18` read, `#30` write. */
  AP_ATBUS_CYCLE_MEMORY,
  /* The AT I/O window, 8 bits wide -- what a card gets when it does not assert
   * `IO_CS16.L`, so it is the default rather than a choice. `#48`, the same
   * figure both directions: the tables give one command width for `IOR.L` and
   * `IOW.L` together. */
  AP_ATBUS_CYCLE_IO_8,
  /* `#37`, for a card that does assert `IO_CS16.L`. */
  AP_ATBUS_CYCLE_IO_16,
} ap_atbus_cycle_t;

/* One appendix, transcribed. Nanoseconds, as printed. */
typedef struct {
  ap_atbus_series_t series;
  const char *name;

  /* `#25` CLOCK cycle time and `#26` BUS CLOCK cycle time, both maxima -- so
   * the rates are 12/6 MHz and 16/8 MHz. Carried because they are what makes
   * the two tables comparable, and because the derivation above is checked
   * against them rather than asserted. */
  uint32_t clock_hz;
  uint32_t bus_clock_hz;

  uint32_t memory_read_ns;  /* #18 */
  uint32_t memory_write_ns; /* #30 */
  /* The **command widths**, `#37` and `#48` -- kept because the suite checks
   * the two appendices against each other through them, and because they are
   * what the tables actually print. They are *not* the cycle; see below. */
  uint32_t io_16_ns;        /* #37 */
  uint32_t io_8_ns;         /* #48 */
  /* The **cycles**, which the appendices do not give and §3.4 does. */
  uint32_t io_16_cycle_ns;
  uint32_t io_8_cycle_ns;
} ap_atbus_timing_t;

[[nodiscard]] const ap_atbus_timing_t *ap_atbus_timing(ap_atbus_series_t series);

/* How long the addressed cycle takes, in `AP_TIME_BASE_HZ` units -- not in
 * clocks of anybody's processor, which is this machine's standing rule and is
 * exactly right here: the bus takes the time it takes, and how many clocks that
 * costs is the asking processor's business.
 *
 * The base represents both bus clocks exactly (6 MHz -> 1100 units, 8 MHz ->
 * 825), so nothing is rounded on the way in. */
[[nodiscard]] ap_time_t ap_atbus_access_time(const ap_atbus_timing_t *timing,
                                             ap_atbus_cycle_t cycle, bool read);

/* The figure expressed in the table's own bus clocks, scaled by 100 so the
 * halves survive. This is the derivation above, made callable so the suite can
 * check the two appendices against each other instead of the code repeating
 * their agreement as a comment. */
[[nodiscard]] unsigned ap_atbus_centiclocks(const ap_atbus_timing_t *timing,
                                            ap_atbus_cycle_t cycle, bool read);

/* ---- DRAM access times and `IO_CH_RDY`'s ceiling, `008778-03` ------------ */

/* §3.3 gives both families the same DRAM figures -- **120 ns RAS** and
 * **60 ns CAS** -- and a **4 ms refresh period** over **256 row addresses** on
 * the DS3000 or **1000** on the DS4000. §2.3.2 caps `IO_CH_RDY` held low at
 * **2.5 us**.
 *
 * All four land on the time base with nothing left over: 2,585,088 units for
 * RAS, 1,292,544 for CAS, 53,856,000 for the `IO_CH_RDY` ceiling and
 * 86,169,600,000 for the refresh period. Asserted in `atbus_suite` rather than
 * trusted, on the same principle as the ring's byte time.
 *
 * **Named, and enforced by nothing.** No access here consumes RAS or CAS time,
 * no bus cycle is stolen for refresh, and a device holding `IO_CH_RDY` low
 * forever is not detected. These are constants so a later reader has the
 * figures with their citation, not a claim that memory timing is modelled --
 * `ap_atbus_access_time` above models the *bus* cycle, which is a different
 * thing from the DRAM behind it.
 *
 * ## The per-row interval, and a question the two figures raise
 *
 * A 4 ms period over 256 rows is **15.625 us** a row, which is §2.4.6's
 * refresh "at regular intervals (approximately 15 microseconds)" and the fixed
 * 15 us square wave this core already models on the 2681's `OP3`. Those agree,
 * and the agreement is worth having: the refresh *source* was modelled from
 * §3.9 without anything confirming the interval was right for the memory.
 *
 * **The DS4000 does not fit.** 4 ms over 1000 rows is **4 us** a row, which is
 * 1000/256 = **3.906 times** faster than the source this core models and than
 * §2.4.6 describes -- near four, and asserted as the row ratio rather than
 * rounded to it, because a test that rounded said 4 and the arithmetic says 3. One
 * of three things is true and no page held here says which: the DS4000 has a
 * different refresh source, or it refreshes several rows per cycle, or the
 * 1000-row figure counts something other than what must be walked in 4 ms.
 * `PROVISIONAL`, and on the plan. It matters because the DS4000 is a modelled
 * family, so a refresh interval that is wrong by four is wrong for it. */
#define AP_ATBUS_DRAM_RAS_TICKS ((ap_time_t)AP_TIME_BASE_HZ * 120u / 1000000000u)
#define AP_ATBUS_DRAM_CAS_TICKS ((ap_time_t)AP_TIME_BASE_HZ * 60u / 1000000000u)
#define AP_ATBUS_DRAM_REFRESH_PERIOD ((ap_time_t)AP_TIME_BASE_HZ * 4u / 1000u)
#define AP_ATBUS_DRAM_ROWS_DS3000 256u
#define AP_ATBUS_DRAM_ROWS_DS4000 1000u
#define AP_ATBUS_IO_CH_RDY_MAX ((ap_time_t)AP_TIME_BASE_HZ * 25u / 10000000u)

#endif /* APOLLO_BOARD_AP_ATBUS_H */

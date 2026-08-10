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
 * show. Cost to close: a published AT I/O cycle time, or a measurement.
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
  uint32_t io_16_ns;        /* #37 */
  uint32_t io_8_ns;         /* #48 */
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

#endif /* APOLLO_BOARD_AP_ATBUS_H */

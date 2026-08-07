/* Apollo calendar: the MC146818A as the board wires it.
 *
 * `008778-03` §3.6 and Table 2-8, "010900 - 0109FF  CALENDAR". The part is
 * `device/ap_mc146818.h`; this is the placement and the interrupt line.
 *
 * ## Byte-consecutive, unlike the interval timer
 *
 * The calendar is at **stride 1** — its sixty-four registers occupy sixty-four
 * consecutive bytes — where the timer next door is on odd addresses at stride 2.
 * Two byte-wide peripherals, two different placements, so neither can be
 * inferred from the other and both were measured.
 *
 * The evidence is a dump of `010900`: `21 00 09 00 89 00 06 31 07 26 ...`. Read
 * at stride 1 that is a coherent clock — 21 seconds, 09 minutes, hours `89`
 * (the twelve-hour mode's PM bit plus 9), weekday 6, day 31, month 07, year 26 —
 * and 2026-07-31 really is a Friday, which is weekday 6 when Sunday is 1. Read
 * at stride 2 the same bytes would make the minutes `89`, which is not a legal
 * BCD minute at all. One reading is a working clock and the other is impossible,
 * which is as clean as a placement measurement gets.
 *
 * The sixty-four registers then **alias** through the 256-byte range, also
 * measured: the month register reads `07` at offsets `08`, `48`, `88` and `C8`,
 * and a byte written to the RAM at `+10` reads back at `+50` and `+90`.
 */

#ifndef APOLLO_BOARD_AP_CALENDAR_H
#define APOLLO_BOARD_AP_CALENDAR_H

#include <stdbool.h>
#include <stdint.h>

#include "device/ap_mc146818.h"

/* `008778-03` Table 2-8. */
#define AP_CALENDAR_ADDR 0x010900u
#define AP_CALENDAR_RANGE 0x100u

/* `008778-03` Table 2-3: "IRQ8 ... MC146818 Calendar", the first line of the
 * slave controller and priority 4+1 — the highest of the cascaded group. */
#define AP_CALENDAR_IRQ 8u

/* ## The configuration table, in the battery RAM
 *
 * `008778-03` §3.6 says only that the part has "50 bytes of low-power static
 * RAM. The RAM is used to store configuration information", and stops there.
 * **`002398-04` p. 12-3 lays the information out**, and it is the manual this
 * project needed for a session and a half — the DN3000 chapter of the Domain
 * Engineering Handbook, on disk the whole time:
 *
 *     0E-11  CHECKSUM          } 50 bytes of battery backed up RAM
 *     12-15  VALID PATTERN     } used by diagnostics for config info
 *     16-1D  MEM BOARD ARRAY
 *     1E-21  NODEID
 *     22-25  DEV BIT ARRAY   <= bit 0 = flp      4 = ring
 *     26     RING TYPE          1 = ctape        5 = user device
 *     27     DISP TYPE          2 = win          6 = ethernet
 *     28     DISK TYPE          3 = fpu          7 = serial/parallel board
 *     29-3F  UNUSED
 *
 * **It corroborates a measurement exactly.** The boot PROM was found to touch
 * the calendar precisely once in a hundred million instructions — one 32-bit
 * read at `010912`, returning zero — and its whole judgement that
 * "Configuration information is not initialized" rests on that longword. `12`
 * is where the handbook puts the **VALID PATTERN**, and it is four bytes wide,
 * which is the width of the read. A measurement and a document arriving at the
 * same field independently is as good as this gets.
 *
 * Two things the handbook does *not* give, and neither may be invented:
 *
 * - **The valid pattern's value.** A four-byte constant the diagnostics
 *   recognise. Recoverable from the boot PROM's checker, which is a file read.
 * - **The checksum's algorithm and span.** Four bytes at `0E`, over some part
 *   of what follows. The handbook names the field and not the sum.
 *
 * So nothing is written here yet. The layout is recorded because it is what
 * turns "seed the battery RAM somehow" into two specific unknowns, and because
 * a structure known from a manual beats one inferred from a boot.
 */
#define AP_CALENDAR_CONFIG_CHECKSUM 0x0Eu
#define AP_CALENDAR_CONFIG_VALID_PATTERN 0x12u
#define AP_CALENDAR_CONFIG_MEM_BOARD_ARRAY 0x16u
#define AP_CALENDAR_CONFIG_NODEID 0x1Eu
#define AP_CALENDAR_CONFIG_DEV_BIT_ARRAY 0x22u
#define AP_CALENDAR_CONFIG_RING_TYPE 0x26u
#define AP_CALENDAR_CONFIG_DISP_TYPE 0x27u
#define AP_CALENDAR_CONFIG_DISK_TYPE 0x28u
#define AP_CALENDAR_CONFIG_UNUSED 0x29u

/* The `DEV BIT ARRAY`'s bits, from the same page. A device present is a bit
 * set; which of the four bytes at `22` carries them is not stated, and is not
 * guessed at here. */
#define AP_CALENDAR_DEV_FLOPPY 0u
#define AP_CALENDAR_DEV_CARTRIDGE_TAPE 1u
#define AP_CALENDAR_DEV_WINCHESTER 2u
#define AP_CALENDAR_DEV_FPU 3u
#define AP_CALENDAR_DEV_RING 4u
#define AP_CALENDAR_DEV_USER 5u
#define AP_CALENDAR_DEV_ETHERNET 6u
#define AP_CALENDAR_DEV_SERIAL_PARALLEL 7u

typedef struct {
  ap_mc146818_t rtc;
} ap_calendar_t;

/* Reset, with the clock set where the caller says. There is no default instant
 * here on purpose: a board that picked one would be choosing a wall clock, and
 * the whole point of this device's design is that it does not have one. */
[[nodiscard]] bool ap_calendar_reset(ap_calendar_t *calendar,
                                     const ap_mc146818_time_t *start);

/* Whether an address decodes to the calendar, and to which register. */
[[nodiscard]] bool ap_calendar_decode(uint32_t address, uint8_t *reg);

[[nodiscard]] uint8_t ap_calendar_read(ap_calendar_t *calendar,
                                       uint32_t address);
void ap_calendar_write(ap_calendar_t *calendar, uint32_t address,
                       uint8_t value);

void ap_calendar_advance(ap_calendar_t *calendar, ap_time_t now);

/* The IRQ pin, to be wired to `AP_CALENDAR_IRQ`. The board does the wiring. */
[[nodiscard]] bool ap_calendar_irq(const ap_calendar_t *calendar);

#endif /* APOLLO_BOARD_AP_CALENDAR_H */

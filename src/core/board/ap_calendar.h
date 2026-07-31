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

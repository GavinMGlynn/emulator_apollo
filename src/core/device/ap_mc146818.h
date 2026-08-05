/* Motorola MC146818A Real-Time Clock plus RAM.
 *
 * `[146818]` *MC146818A Real-Time Clock Plus RAM (RTC)*, Motorola. The scan
 * carries an OCR layer of poor quality, so the register definitions were read
 * from the page images; quotations below are transcribed from those.
 *
 * The Apollo calendar, `008778-03` §3.6: "The calendar chip used on the
 * CPU/Motherboard is the MC146818. This device combines three features: a
 * time-of-day clock with an alarm and a 100-year calendar, a programmable
 * periodic interrupt and square-wave generator, and 50 bytes of low-power
 * static RAM. The RAM is used to store configuration information." At `010900`,
 * and battery-backed.
 *
 * ## Time comes from the caller, never from the host
 *
 * This is the one place a device could quietly destroy the project's central
 * property. The oracle seeds its calendar from the **host clock** — a dump of
 * `010900` returns whatever today is — and a core that did the same would
 * produce a different state hash on every run, and goldens that rot overnight.
 *
 * So this module has no notion of the wall clock at all. It is advanced by
 * `ap_mc146818_advance` in time-base units like every other device, and the
 * date it starts from is whatever the caller sets. `CLAUDE.md` requires the
 * headless frontend to have "no wall clock"; a battery-backed clock is exactly
 * the device that tempts one in through the back door.
 *
 * ## What is modelled
 *
 * The ten time and calendar bytes, the four registers, the 50 RAM bytes, the
 * once-per-second update cycle with its carry into a real calendar, the alarm
 * with `[146818]`'s don't-care codes, the update-ended and alarm interrupt
 * flags, and Register C's read-to-clear.
 *
 * ## What is declined
 *
 * The **six fastest periodic-interrupt rates**, and for a reason that is a fact
 * about this machine rather than about this module. `[146818]` Table 5 gives
 * the rates as 32768/2^n Hz, and `AP_TIME_BASE_HZ` factors as
 * 2^9 * 3 * 5^8 * 11 — so it carries only 2^9, and the rates from 1.024 kHz up
 * to 32.768 kHz are **not exactly representable**. `CLAUDE.md`'s rule is that
 * such a clock means recomputing the base, and the cost of doing so is now
 * measured rather than guessed at:
 *
 *   including 32.768 kHz     base * 64, span falls from 88.6 years to 505 days
 *   including the 4.194304
 *   MHz crystal itself       base * 8192, span falls to 3.95 days
 *
 * The crystal can therefore never be a clock domain in a 64-bit base at all,
 * which is worth knowing on its own. The six fast rates could be, at a cost of
 * 64x the representable span, and that trade has not been made because nothing
 * has been observed using them — the Apollo firmware never writes the calendar.
 *
 * The nine slower rates, 512 Hz down to 2 Hz (1.953 ms to 500 ms), divide the
 * base exactly and **are** implemented. `ap_mc146818_rate_supported` reports
 * which case a given `RS3-RS0` falls in, so a rate this core cannot honour is
 * refused rather than approximated.
 *
 * The **square-wave output** pin is not modelled: nothing on this board is
 * wired to it. Its frequency shares the same selector and the same table.
 *
 * Also declined: the daylight-savings updates of the `DSE` bit, which shift the
 * clock on two specific calendar days. The bit is stored and honoured as
 * storage; the shift is not applied. Named here because a stored-but-inert
 * control bit is otherwise indistinguishable from an implemented one.
 */

#ifndef APOLLO_DEVICE_AP_MC146818_H
#define APOLLO_DEVICE_AP_MC146818_H

#include <stdbool.h>
#include <stdint.h>

#include "time/ap_time.h"

/* `[146818]`'s address space: ten clock bytes, four registers, then RAM. */
#define AP_MC146818_SECONDS 0x00u
#define AP_MC146818_SECONDS_ALARM 0x01u
#define AP_MC146818_MINUTES 0x02u
#define AP_MC146818_MINUTES_ALARM 0x03u
#define AP_MC146818_HOURS 0x04u
#define AP_MC146818_HOURS_ALARM 0x05u
#define AP_MC146818_DAY_OF_WEEK 0x06u
#define AP_MC146818_DAY_OF_MONTH 0x07u
#define AP_MC146818_MONTH 0x08u
#define AP_MC146818_YEAR 0x09u
#define AP_MC146818_REGISTER_A 0x0Au
#define AP_MC146818_REGISTER_B 0x0Bu
#define AP_MC146818_REGISTER_C 0x0Cu
#define AP_MC146818_REGISTER_D 0x0Du
#define AP_MC146818_RAM_BASE 0x0Eu
#define AP_MC146818_BYTES 0x40u

/* Register A. "UIP ... is a status flag that may be monitored by the program",
 * read-only; DV2-DV0 select the divider; RS3-RS0 the rate. */
#define AP_MC146818_A_UIP 0x80u
#define AP_MC146818_A_DIVIDER 0x70u
#define AP_MC146818_A_RATE 0x0Fu

/* Register B, all read/write. */
#define AP_MC146818_B_SET 0x80u   /* inhibits the update cycle */
#define AP_MC146818_B_PIE 0x40u   /* periodic interrupt enable */
#define AP_MC146818_B_AIE 0x20u   /* alarm interrupt enable */
#define AP_MC146818_B_UIE 0x10u   /* update-ended interrupt enable */
#define AP_MC146818_B_SQWE 0x08u  /* square-wave enable */
#define AP_MC146818_B_DM 0x04u    /* 1 binary, 0 BCD */
#define AP_MC146818_B_24HOUR 0x02u /* 1 twenty-four hour, 0 twelve */
#define AP_MC146818_B_DSE 0x01u   /* daylight savings enable */

/* Register C, read-only. "IRQF = PF*PIE + AF*AIE + UF*UIE". */
#define AP_MC146818_C_IRQF 0x80u
#define AP_MC146818_C_PF 0x40u
#define AP_MC146818_C_AF 0x20u
#define AP_MC146818_C_UF 0x10u

/* Register D, read-only. "VRT ... b6 TO b0 - The remaining bits of Register D
 * are unused. They cannot be written, but are always read as 0's." */
#define AP_MC146818_D_VRT 0x80u

/* A wall-clock instant, supplied by the caller. Not a host time: the caller
 * decides, and a deterministic frontend supplies a fixed one. */
typedef struct {
  unsigned year;        /* full year, e.g. 1987 */
  unsigned month;       /* 1-12 */
  unsigned day;         /* 1-31 */
  unsigned day_of_week; /* 1-7, `[146818]` numbers Sunday as 1 */
  unsigned hour;        /* 0-23, always twenty-four hour here */
  unsigned minute;      /* 0-59 */
  unsigned second;      /* 0-59 */
} ap_mc146818_time_t;

typedef struct {
  uint8_t ram[AP_MC146818_BYTES];
  /* The clock kept as plain numbers rather than in the register bytes, because
   * the register format depends on DM and 24/12, which software may change at
   * any moment. Keeping the truth separate means a format change re-presents
   * the same instant instead of reinterpreting the old bytes as the new
   * format -- which would silently move the clock. */
  ap_mc146818_time_t now;
  /* Base-unit time the last one-second update happened at. */
  ap_time_t updated_to;
  ap_clock_t second_clock;

  /* The periodic interrupt, kept separate because it runs at its own rate and
   * must not be quantised to the one-second update. */
  ap_time_t periodic_to;
  ap_clock_t periodic_clock;
  /* How many update cycles have run: a diagnostic, not machine state. A clock
   * that is not advancing and a clock whose seconds happen to read alike are
   * the same value and different faults. */
  unsigned update_cycles;

} ap_mc146818_t;

/* Reset, and set the clock to `start`. Returns false if the one-second tick is
 * not representable in the time base, which cannot happen but is checked
 * because the base is derived. */
[[nodiscard]] bool ap_mc146818_reset(ap_mc146818_t *rtc,
                                     const ap_mc146818_time_t *start);

[[nodiscard]] uint8_t ap_mc146818_read(ap_mc146818_t *rtc, uint8_t address);
void ap_mc146818_write(ap_mc146818_t *rtc, uint8_t address, uint8_t value);

/* Advance to absolute time `now`, running one update cycle per elapsed second.
 * Monotonic and by whole seconds, with the remainder carried. */
void ap_mc146818_advance(ap_mc146818_t *rtc, ap_time_t now);

/* The IRQ pin: `[146818]`'s "IRQF = PF*PIE + AF*AIE + UF*UIE". */
[[nodiscard]] bool ap_mc146818_irq(const ap_mc146818_t *rtc);

/* The periodic interrupt frequency selected by Register A's RS3-RS0, in hertz,
 * or zero for `[146818]` Table 5's "None" row.
 *
 * The table publishes an interval and a square-wave frequency side by side --
 * 30.517 us against 32.768 kHz, 500 ms against 2 Hz -- and the interval is the
 * reciprocal of the frequency throughout, so one number serves for both. */
[[nodiscard]] uint32_t ap_mc146818_periodic_hz(const ap_mc146818_t *rtc);

/* False when the selected rate is one this core cannot represent exactly. See
 * the header: the six fastest rates need 2^15 in a time base that carries 2^9.
 * A caller must check rather than assume, because a periodic interrupt running
 * at a rounded rate is indistinguishable from a correct one and would drift
 * whatever is built on it. */
[[nodiscard]] bool ap_mc146818_rate_supported(const ap_mc146818_t *rtc);

/* The clock as numbers, for tests and for a state hash that must not depend on
 * the register format software happens to have selected. */
[[nodiscard]] ap_mc146818_time_t ap_mc146818_now(const ap_mc146818_t *rtc);

#endif /* APOLLO_DEVICE_AP_MC146818_H */

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
 * The **periodic interrupt** and the square-wave output. Both are driven by taps
 * on the 22-stage divider selected by RS3-RS0, and `[146818]` publishes those
 * rates in a Table 5 that is not transcribed. A periodic interrupt running at a
 * guessed rate would be indistinguishable from a real one and would corrupt any
 * timing built on it, so `PF` is never set and `ap_mc146818_rate_supported`
 * says so. The Apollo firmware has not been observed programming it.
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

/* False whenever the periodic interrupt or square wave is enabled, because
 * neither is modelled. A caller must check rather than assume; see the header
 * on why a guessed periodic rate is worse than none. */
[[nodiscard]] bool ap_mc146818_rate_supported(const ap_mc146818_t *rtc);

/* The clock as numbers, for tests and for a state hash that must not depend on
 * the register format software happens to have selected. */
[[nodiscard]] ap_mc146818_time_t ap_mc146818_now(const ap_mc146818_t *rtc);

#endif /* APOLLO_DEVICE_AP_MC146818_H */

#include "board/ap_calendar.h"

bool ap_calendar_reset(ap_calendar_t *calendar,
                       const ap_mc146818_time_t *start) {
  return ap_mc146818_reset(&calendar->rtc, start);
}

bool ap_calendar_decode(uint32_t address, uint8_t *reg) {
  if ((address & ~(AP_CALENDAR_RANGE - 1u)) != AP_CALENDAR_ADDR) {
    return false;
  }
  /* Sixty-four registers aliased through a 256-byte range, measured: the month
   * reads alike at four offsets a quarter of the range apart, and a RAM write
   * reappears twice. Masking rather than bounding is what reproduces that -- a
   * decode that refused above `3F` would answer nothing where the hardware
   * answers a register. */
  *reg = (uint8_t)((address - AP_CALENDAR_ADDR) & (AP_MC146818_BYTES - 1u));
  return true;
}

uint8_t ap_calendar_read(ap_calendar_t *calendar, uint32_t address) {
  uint8_t reg;
  if (!ap_calendar_decode(address, &reg)) {
    return 0u;
  }
  return ap_mc146818_read(&calendar->rtc, reg);
}

void ap_calendar_write(ap_calendar_t *calendar, uint32_t address,
                       uint8_t value) {
  uint8_t reg;
  if (!ap_calendar_decode(address, &reg)) {
    return;
  }
  ap_mc146818_write(&calendar->rtc, reg, value);
}

void ap_calendar_advance(ap_calendar_t *calendar, ap_time_t now) {
  ap_mc146818_advance(&calendar->rtc, now);
}

bool ap_calendar_irq(const ap_calendar_t *calendar) {
  return ap_mc146818_irq(&calendar->rtc);
}

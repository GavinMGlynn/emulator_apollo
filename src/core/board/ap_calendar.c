#include "board/ap_calendar.h"

#include <stddef.h>

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

/* Straight into the RAM array rather than through `ap_mc146818_write`: this is
 * a battery holding its charge, not a program writing registers, and the write
 * path has side effects on the four control registers that a restore must not
 * trigger. The base is above them, so nothing here can reach one -- but going
 * through the register interface would make that a coincidence rather than a
 * property. */
void ap_calendar_load_battery(ap_calendar_t *calendar, const uint8_t *bytes,
                              unsigned count) {
  if (calendar == NULL || bytes == NULL) {
    return;
  }
  if (count > AP_CALENDAR_BATTERY_BYTES) {
    count = AP_CALENDAR_BATTERY_BYTES;
  }
  for (unsigned i = 0; i < count; i++) {
    calendar->rtc.ram[AP_MC146818_RAM_BASE + i] = bytes[i];
  }
}

unsigned ap_calendar_save_battery(const ap_calendar_t *calendar, uint8_t *out,
                                  unsigned capacity) {
  if (calendar == NULL || out == NULL) {
    return 0u;
  }
  const unsigned count = capacity < AP_CALENDAR_BATTERY_BYTES
                             ? capacity
                             : AP_CALENDAR_BATTERY_BYTES;
  for (unsigned i = 0; i < count; i++) {
    out[i] = calendar->rtc.ram[AP_MC146818_RAM_BASE + i];
  }
  return count;
}

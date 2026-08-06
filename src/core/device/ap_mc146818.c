#include "device/ap_mc146818.h"

#include <string.h>

/* Days in each month, index 1-12. February is corrected by `leap_year`. */
static const unsigned month_days[13] = {0,  31, 28, 31, 30, 31, 30,
                                        31, 31, 30, 31, 30, 31};

/* The Gregorian rule in full. `[146818]` calls this "a 100 year calendar" and
 * the part carries only a two-digit year, so the century is the caller's; this
 * core keeps the full year precisely so that 1900 and 2000 do not both become
 * `00` and take the same leap rule. */
static bool leap_year(unsigned year) {
  if ((year % 400u) == 0u) {
    return true;
  }
  if ((year % 100u) == 0u) {
    return false;
  }
  return (year % 4u) == 0u;
}

static unsigned days_in_month(unsigned year, unsigned month) {
  if (month == 2u && leap_year(year)) {
    return 29u;
  }
  if (month < 1u || month > 12u) {
    return 31u;
  }
  return month_days[month];
}

bool ap_mc146818_reset(ap_mc146818_t *rtc, const ap_mc146818_time_t *start) {
  memset(rtc, 0, sizeof *rtc);
  rtc->now = *start;
  /* "VRT ... indicates the condition of the contents of the RAM, provided the
   * power sense (PS) pin is satisfactorily connected." Set, because this core
   * has no discharged battery to model and a clear VRT would tell software its
   * configuration RAM is invalid. */
  rtc->ram[AP_MC146818_REGISTER_D] = AP_MC146818_D_VRT;
  return ap_clock_init(&rtc->second_clock, 1u);
}

/* `[146818]` Register B: "A '1' in DM signifies binary data, while a '0' in DM
 * specifies binary-coded-decimal (BCD) data." */
static bool binary_mode(const ap_mc146818_t *rtc) {
  return (rtc->ram[AP_MC146818_REGISTER_B] & AP_MC146818_B_DM) != 0u;
}

static bool twentyfour_hour(const ap_mc146818_t *rtc) {
  return (rtc->ram[AP_MC146818_REGISTER_B] & AP_MC146818_B_24HOUR) != 0u;
}

static uint8_t to_format(const ap_mc146818_t *rtc, unsigned value) {
  if (binary_mode(rtc)) {
    return (uint8_t)value;
  }
  return (uint8_t)(((value / 10u) << 4) | (value % 10u));
}

static unsigned from_format(const ap_mc146818_t *rtc, uint8_t value) {
  if (binary_mode(rtc)) {
    return value;
  }
  return (unsigned)(((value >> 4) & 0x0Fu) * 10u + (value & 0x0Fu));
}

/* The hours byte, which is the only one whose *value* depends on a mode bit
 * rather than just its encoding. "The 24/12 control bit establishes the format
 * of the hours bytes as either the 24-hour mode (a '1') or the 12-hour mode (a
 * '0')", and in 12-hour mode the high bit marks PM -- which the oracle's own
 * calendar shows, reading `89` for 9 in the evening. */
static uint8_t hours_byte(const ap_mc146818_t *rtc) {
  unsigned hour = rtc->now.hour;
  if (twentyfour_hour(rtc)) {
    return to_format(rtc, hour);
  }
  bool pm = hour >= 12u;
  unsigned twelve = hour % 12u;
  if (twelve == 0u) {
    twelve = 12u;
  }
  uint8_t byte = to_format(rtc, twelve);
  return pm ? (uint8_t)(byte | 0x80u) : byte;
}

static unsigned hours_from_byte(const ap_mc146818_t *rtc, uint8_t value) {
  if (twentyfour_hour(rtc)) {
    return from_format(rtc, value);
  }
  bool pm = (value & 0x80u) != 0u;
  unsigned twelve = from_format(rtc, (uint8_t)(value & 0x7Fu));
  if (twelve == 12u) {
    twelve = 0u;
  }
  return pm ? twelve + 12u : twelve;
}

/* One alarm byte against one time byte. "An alarm interrupt occurs for each
 * second that the three time bytes equal the three alarm bytes (including a
 * 'don't care' alarm code of binary 11XXXXXX)."
 *
 * The don't-care test is on the raw byte and not on its decoded value, because
 * `11XXXXXX` is not a valid BCD number at all -- decoding first would turn a
 * wildcard into a nonsense figure that matches nothing. */
static bool alarm_byte_matches(uint8_t alarm, uint8_t now) {
  if ((alarm & 0xC0u) == 0xC0u) {
    return true;
  }
  return alarm == now;
}

static bool alarm_matches(const ap_mc146818_t *rtc) {
  return alarm_byte_matches(rtc->ram[AP_MC146818_SECONDS_ALARM],
                            to_format(rtc, rtc->now.second)) &&
         alarm_byte_matches(rtc->ram[AP_MC146818_MINUTES_ALARM],
                            to_format(rtc, rtc->now.minute)) &&
         alarm_byte_matches(rtc->ram[AP_MC146818_HOURS_ALARM],
                            hours_byte(rtc));
}

/* One second of real time: `[146818]`'s update cycle. "The primary function of
 * the update cycle is to increment the [time and calendar bytes] ... The update
 * cycle also compares each alarm byte with the corresponding time byte and
 * issues an alarm interrupt if a match ... is found." */
static void update_cycle(ap_mc146818_t *rtc) {
  ap_mc146818_time_t *t = &rtc->now;

  t->second++;
  if (t->second >= 60u) {
    t->second = 0u;
    t->minute++;
  }
  if (t->minute >= 60u) {
    t->minute = 0u;
    t->hour++;
  }
  if (t->hour >= 24u) {
    t->hour = 0u;
    t->day++;
    /* "1-7, Sunday is 1" wraps rather than counting to 8. */
    t->day_of_week = (t->day_of_week % 7u) + 1u;
  }
  if (t->day > days_in_month(t->year, t->month)) {
    t->day = 1u;
    t->month++;
  }
  if (t->month > 12u) {
    t->month = 1u;
    t->year++;
  }

  /* `[146818]`'s two special updates, and they happen **after** the ordinary
   * carry above, because the datasheet defines them as what the time
   * *increments to*:
   *
   *   "DSE -- ... On the last Sunday in April the time increments from
   *    1:59:59 AM to 3:00:00 AM. On the last Sunday in October when the time
   *    first reaches 1:59:59 AM it changes to 1:00:00 AM."
   *
   * So the April rule fires on the tick that has just produced 02:00:00, and
   * the October rule on the one that has just produced 02:00:00 too -- both
   * follow 01:59:59 -- which is why they are one test on the hour and not two
   * different ones. `first reaches` is the whole of October's difficulty: the
   * hour repeats, and a rule that fired again at the second 02:00:00 would
   * hold the clock at one o'clock for ever. The `day_of_week` check is what
   * prevents that, since the second pass is the same Sunday and the same hour
   * but arrives only after a further 3600 ticks, by which time `hour` is 2
   * having come from 1 again -- so the guard is the flag below, set when the
   * shift is taken and cleared by any other hour.
   *
   * "Sunday is 1", and the last Sunday of a month is any Sunday whose date is
   * within seven of the month's end. */
  if ((rtc->ram[AP_MC146818_REGISTER_B] & AP_MC146818_B_DSE) != 0u &&
      t->hour == 2u && t->minute == 0u && t->second == 0u &&
      t->day_of_week == 1u &&
      t->day + 7u > days_in_month(t->year, t->month)) {
    if (t->month == 4u && !rtc->dst_shifted) {
      /* 1:59:59 -> 3:00:00: the ordinary carry has produced 02:00:00, and the
       * special update takes it on by another hour. */
      t->hour = 3u;
      rtc->dst_shifted = true;
    } else if (t->month == 10u && !rtc->dst_shifted) {
      /* 1:59:59 -> 1:00:00, "when the time **first** reaches" it. */
      t->hour = 1u;
      rtc->dst_shifted = true;
    }
  } else if (t->hour != 1u && t->hour != 2u) {
    /* Past the ambiguous window, so the next year's shift is allowed again.
     *
     * **One and two, not two alone.** October's fall-back lands the clock at
     * 01:00:00, and a guard cleared by "not hour two" would clear on that very
     * tick -- the flag would be gone before the hour repeated, the second
     * 02:00:00 would shift again, and the clock would sit at one o'clock for
     * ever. The whole point of "when the time **first** reaches 1:59:59 AM" is
     * that the hour comes round twice, so the flag has to survive all of it.
     * Found by the test, which ran an hour past the shift and expected two. */
    rtc->dst_shifted = false;
  }

  /* "The update-ended interrupt flag (UF) bit is set after each update
   * cycle" -- unconditionally, and independently of UIE, because "Each of the
   * three interrupt sources have separate flag bits in Register C, which are
   * set independent of the state of the corresponding enable bits in
   * Register B." */
  rtc->ram[AP_MC146818_REGISTER_C] |= AP_MC146818_C_UF;

  if (alarm_matches(rtc)) {
    rtc->ram[AP_MC146818_REGISTER_C] |= AP_MC146818_C_AF;
  }
}

void ap_mc146818_advance(ap_mc146818_t *rtc, ap_time_t now) {
  if (now <= rtc->updated_to) {
    return;
  }
  uint64_t seconds =
      ap_clock_cycles_in(&rtc->second_clock, now - rtc->updated_to);

  /* "When the SET bit is written to a '1', any update cycle in progress is
   * aborted and the program may initialize the time and calendar bytes without
   * an update occurring in the midst of initializing."
   *
   * Time held still, but the cursor still advances -- otherwise clearing SET
   * would deliver every second that passed while it was set, in one burst. A
   * clock that catches up is not a clock that was stopped. */
  bool held = (rtc->ram[AP_MC146818_REGISTER_B] & AP_MC146818_B_SET) != 0u;
  if (!held) {
    for (uint64_t i = 0; i < seconds; i++) {
      update_cycle(rtc);
      rtc->update_cycles++;
    }
  }
  rtc->updated_to += ap_clock_duration(&rtc->second_clock, seconds);

  /* The periodic interrupt runs on its own rate and is not stopped by SET:
   * `[146818]` ties SET to the *update cycle* only, and Figure 15 shows the
   * periodic flag continuing across an update. Its rate may also change at any
   * time, so the clock domain is rebuilt from Register A each call rather than
   * cached -- a cached period would keep the old rate until something else
   * happened to reset it. */
  uint32_t hz = ap_mc146818_periodic_hz(rtc);
  if (hz == 0u || !ap_time_base_divides(hz)) {
    /* No rate selected, or one this core refuses to approximate. Keep the
     * cursor with the clock so that selecting a rate later does not deliver a
     * backlog of interrupts for time that passed while none was chosen. */
    rtc->periodic_to = now;
    return;
  }
  if (!ap_clock_init(&rtc->periodic_clock, hz)) {
    rtc->periodic_to = now;
    return;
  }
  if (now <= rtc->periodic_to) {
    return;
  }
  uint64_t ticks =
      ap_clock_cycles_in(&rtc->periodic_clock, now - rtc->periodic_to);
  if (ticks != 0u) {
    /* "The periodic interrupt flag (PF) ... is set to a '1' independent of the
     * state of the PIE bit." One flag however many periods elapsed: it is a
     * flag and not a count, and software reading Register C cannot tell two
     * from twenty. */
    rtc->ram[AP_MC146818_REGISTER_C] |= AP_MC146818_C_PF;
    rtc->periodic_to += ap_clock_duration(&rtc->periodic_clock, ticks);
  }
}

bool ap_mc146818_irq(const ap_mc146818_t *rtc) {
  uint8_t c = rtc->ram[AP_MC146818_REGISTER_C];
  uint8_t b = rtc->ram[AP_MC146818_REGISTER_B];
  /* "IRQF = PF*PIE + AF*AIE + UF*UIE". */
  if ((c & AP_MC146818_C_PF) != 0u && (b & AP_MC146818_B_PIE) != 0u) {
    return true;
  }
  if ((c & AP_MC146818_C_AF) != 0u && (b & AP_MC146818_B_AIE) != 0u) {
    return true;
  }
  if ((c & AP_MC146818_C_UF) != 0u && (b & AP_MC146818_B_UIE) != 0u) {
    return true;
  }
  return false;
}

/* `[146818]` Table 5, the 4.194304/1.048576 MHz time-base column, transcribed.
 *
 * The table publishes an interval and a square-wave frequency for each code --
 * "30.517 us / 32.768 kHz" through "500 ms / 2 Hz" -- and throughout, the
 * interval is the reciprocal of the frequency. So one number serves for both,
 * and it is stored as the frequency because that is what a clock domain needs.
 *
 * The 32.768 kHz time-base column differs only in its first two rows, and this
 * board does not use it: the datasheet's interface figures all show a 4.194304
 * MHz crystal. */
static const uint32_t periodic_rate_hz[16] = {
    0u,     /* 0000: "None" */
    32768u, /* 0001: 30.517 us */
    16384u, /* 0010: 61.035 us */
    8192u,  /* 0011: 122.070 us */
    4096u,  /* 0100: 244.141 us */
    2048u,  /* 0101: 488.281 us */
    1024u,  /* 0110: 976.562 us */
    512u,   /* 0111: 1.953125 ms */
    256u,   /* 1000: 3.90625 ms */
    128u,   /* 1001: 7.8125 ms */
    64u,    /* 1010: 15.625 ms */
    32u,    /* 1011: 31.25 ms */
    16u,    /* 1100: 62.5 ms */
    8u,     /* 1101: 125 ms */
    4u,     /* 1110: 250 ms */
    2u,     /* 1111: 500 ms */
};

uint32_t ap_mc146818_periodic_hz(const ap_mc146818_t *rtc) {
  return periodic_rate_hz[rtc->ram[AP_MC146818_REGISTER_A] &
                          AP_MC146818_A_RATE];
}

bool ap_mc146818_rate_supported(const ap_mc146818_t *rtc) {
  uint32_t hz = ap_mc146818_periodic_hz(rtc);
  if (hz == 0u) {
    return true; /* "None" is a rate this core can honour exactly. */
  }
  /* The six fastest rates need 2^15 in a base that carries 2^9. See the header
   * for the measured cost of changing that. */
  return ap_time_base_divides(hz);
}

uint32_t ap_mc146818_square_wave_hz(const ap_mc146818_t *rtc) {
  /* Held low unless `SQWE` is set, and silent about rates this core cannot
   * represent exactly -- the same guard `ap_mc146818_rate_supported` exists
   * for, because a pin claimed to be driven at a rounded frequency is
   * indistinguishable from one driven correctly. */
  if ((rtc->ram[AP_MC146818_REGISTER_B] & AP_MC146818_B_SQWE) == 0u) {
    return 0u;
  }
  if (!ap_mc146818_rate_supported(rtc)) {
    return 0u;
  }
  return ap_mc146818_periodic_hz(rtc);
}

ap_mc146818_time_t ap_mc146818_now(const ap_mc146818_t *rtc) {
  return rtc->now;
}

uint8_t ap_mc146818_read(ap_mc146818_t *rtc, uint8_t address) {
  address &= (uint8_t)(AP_MC146818_BYTES - 1u);

  switch (address) {
  case AP_MC146818_SECONDS:
    return to_format(rtc, rtc->now.second);
  case AP_MC146818_MINUTES:
    return to_format(rtc, rtc->now.minute);
  case AP_MC146818_HOURS:
    return hours_byte(rtc);
  case AP_MC146818_DAY_OF_WEEK:
    return to_format(rtc, rtc->now.day_of_week);
  case AP_MC146818_DAY_OF_MONTH:
    return to_format(rtc, rtc->now.day);
  case AP_MC146818_MONTH:
    return to_format(rtc, rtc->now.month);
  case AP_MC146818_YEAR:
    /* Two digits, as the part carries. The century lives in this core's own
     * state and not in a register, because the part has no century byte. */
    return to_format(rtc, rtc->now.year % 100u);
  case AP_MC146818_REGISTER_A: {
    /* UIP is read-only and this core's update is instantaneous, so it never
     * reports an update in progress. Honest rather than convenient: software
     * that polls UIP to avoid reading mid-update will simply never see it set,
     * which is the correct answer for a clock that cannot be caught in the
     * middle. Modelling the 248 microsecond window would need the rate tables
     * that are declined. */
    return (uint8_t)(rtc->ram[AP_MC146818_REGISTER_A] & ~AP_MC146818_A_UIP);
  }
  case AP_MC146818_REGISTER_C: {
    /* "The flag bits in Register C are cleared (record of the interrupt event
     * is erased) when Register C is read. Double latching is included with
     * Register C so the bits which are set are stable throughout the read
     * cycle. All bits which are high when read by the program are cleared." */
    uint8_t value = (uint8_t)(rtc->ram[AP_MC146818_REGISTER_C] & 0x70u);
    if (ap_mc146818_irq(rtc)) {
      value |= AP_MC146818_C_IRQF;
    }
    rtc->ram[AP_MC146818_REGISTER_C] = 0u;
    return value;
  }
  case AP_MC146818_REGISTER_D:
    return (uint8_t)(rtc->ram[AP_MC146818_REGISTER_D] & AP_MC146818_D_VRT);
  default:
    return rtc->ram[address];
  }
}

void ap_mc146818_write(ap_mc146818_t *rtc, uint8_t address, uint8_t value) {
  address &= (uint8_t)(AP_MC146818_BYTES - 1u);

  switch (address) {
  case AP_MC146818_SECONDS:
    rtc->now.second = from_format(rtc, value);
    return;
  case AP_MC146818_MINUTES:
    rtc->now.minute = from_format(rtc, value);
    return;
  case AP_MC146818_HOURS:
    rtc->now.hour = hours_from_byte(rtc, value);
    return;
  case AP_MC146818_DAY_OF_WEEK:
    rtc->now.day_of_week = from_format(rtc, value);
    return;
  case AP_MC146818_DAY_OF_MONTH:
    rtc->now.day = from_format(rtc, value);
    return;
  case AP_MC146818_MONTH:
    rtc->now.month = from_format(rtc, value);
    return;
  case AP_MC146818_YEAR:
    /* The two-digit year replaces the low two digits and leaves the century
     * where it was, so writing `88` to a clock in 1987 gives 1988 and not
     * year 88. The part cannot express the century and this core will not
     * invent a windowing rule for it. */
    rtc->now.year = (rtc->now.year / 100u) * 100u + from_format(rtc, value);
    return;
  case AP_MC146818_REGISTER_A:
    /* "Read/Write Register except UIP". */
    rtc->ram[AP_MC146818_REGISTER_A] = (uint8_t)(value & ~AP_MC146818_A_UIP);
    return;
  case AP_MC146818_REGISTER_C:
  case AP_MC146818_REGISTER_D:
    /* Both are read-only. */
    return;
  default:
    rtc->ram[address] = value;
    return;
  }
}

/* Motorola MC146818A, `[146818]`, register definitions read from the page
 * images -- the scan's OCR layer is too degraded to quote from. */

#include "unity.h"

#include <string.h>

#include "device/ap_mc146818.h"

void setUp(void) {}
void tearDown(void) {}

/* A fixed instant, chosen because the oracle's own calendar was dumped reading
 * this date and it exercises a month end. Fixed, not the host's: see below. */
static const ap_mc146818_time_t START = {
    .year = 1987u,
    .month = 7u,
    .day = 31u,
    .day_of_week = 6u,
    .hour = 21u,
    .minute = 9u,
    .second = 21u,
};

static void init(ap_mc146818_t *rtc) {
  TEST_ASSERT_TRUE(ap_mc146818_reset(rtc, &START));
}

/* Twenty-four hour, BCD -- register B's 24/12 set, DM clear. */
static void set_24h(ap_mc146818_t *rtc) {
  ap_mc146818_write(rtc, AP_MC146818_REGISTER_B, AP_MC146818_B_24HOUR);
}

static ap_time_t seconds(uint64_t n) { return AP_TIME_BASE_HZ * n; }


/* `SQWE` drives the SQW pin at the rate the same selector picks for the
 * periodic interrupt, and holds it low when clear. The pin was declined on the
 * grounds that nothing on this board is wired to it -- which is a fact about
 * the board and not about the part, and left a stored control bit
 * indistinguishable from an implemented one. */
static void test_the_square_wave_pin_follows_sqwe_and_the_rate_select(void) {
  ap_mc146818_t rtc;
  static const ap_mc146818_time_t start = {
      .year = 1988u, .month = 1u, .day = 1u, .day_of_week = 6u};
  TEST_ASSERT_TRUE(ap_mc146818_reset(&rtc, &start));

  /* A representable rate: 2 Hz, `[146818]` Table 5's slowest. */
  ap_mc146818_write(&rtc, AP_MC146818_REGISTER_A, 0x0Fu);
  TEST_ASSERT_TRUE(ap_mc146818_rate_supported(&rtc));
  const uint32_t hz = ap_mc146818_periodic_hz(&rtc);
  TEST_ASSERT_TRUE(hz > 0u);

  /* Held low with SQWE clear, whatever the selector says. */
  TEST_ASSERT_EQUAL_UINT32(0u, ap_mc146818_square_wave_hz(&rtc));

  ap_mc146818_write(&rtc, AP_MC146818_REGISTER_B, AP_MC146818_B_SQWE);
  TEST_ASSERT_EQUAL_UINT32(hz, ap_mc146818_square_wave_hz(&rtc));

  /* "None" selects no rate at all, so the pin is not driven even enabled. */
  ap_mc146818_write(&rtc, AP_MC146818_REGISTER_A, 0x00u);
  TEST_ASSERT_EQUAL_UINT32(0u, ap_mc146818_square_wave_hz(&rtc));

  /* And a rate this core cannot represent exactly is not claimed either --
   * a pin reported at a rounded frequency is indistinguishable from a correct
   * one, which is the whole reason `rate_supported` exists. */
  ap_mc146818_write(&rtc, AP_MC146818_REGISTER_A, 0x01u);
  if (!ap_mc146818_rate_supported(&rtc)) {
    TEST_ASSERT_EQUAL_UINT32(0u, ap_mc146818_square_wave_hz(&rtc));
  }
}

static void test_the_clock_starts_where_the_caller_puts_it(void) {
  ap_mc146818_t rtc;
  init(&rtc);
  set_24h(&rtc);

  /* The property this device exists to preserve. The oracle seeds its calendar
   * from the host clock -- a dump of `010900` returns today's date -- and a
   * core that did the same would hash differently on every run and rot its
   * goldens overnight. `CLAUDE.md` requires the headless frontend to have "no
   * wall clock"; a battery-backed clock is exactly the device that smuggles one
   * in. */
  TEST_ASSERT_EQUAL_HEX8(0x21, ap_mc146818_read(&rtc, AP_MC146818_SECONDS));
  TEST_ASSERT_EQUAL_HEX8(0x09, ap_mc146818_read(&rtc, AP_MC146818_MINUTES));
  TEST_ASSERT_EQUAL_HEX8(0x21, ap_mc146818_read(&rtc, AP_MC146818_HOURS));
  TEST_ASSERT_EQUAL_HEX8(0x31, ap_mc146818_read(&rtc, AP_MC146818_DAY_OF_MONTH));
  TEST_ASSERT_EQUAL_HEX8(0x07, ap_mc146818_read(&rtc, AP_MC146818_MONTH));
  TEST_ASSERT_EQUAL_HEX8(0x87, ap_mc146818_read(&rtc, AP_MC146818_YEAR));
}

static void test_two_clocks_started_alike_stay_identical(void) {
  ap_mc146818_t a;
  ap_mc146818_t b;
  memset(&a, 0xAA, sizeof a);
  memset(&b, 0x55, sizeof b);
  init(&a);
  init(&b);
  TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof a);

  ap_mc146818_advance(&a, seconds(100000u));
  ap_mc146818_advance(&b, seconds(100000u));
  TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof a);
}

static void test_the_clock_reads_bcd_unless_told_otherwise(void) {
  ap_mc146818_t rtc;
  init(&rtc);

  /* "A '1' in DM signifies binary data, while a '0' in DM specifies
   * binary-coded-decimal (BCD) data." Register B resets to zero, so BCD. */
  TEST_ASSERT_EQUAL_HEX8(0x09, ap_mc146818_read(&rtc, AP_MC146818_MINUTES));

  ap_mc146818_write(&rtc, AP_MC146818_REGISTER_B,
                    AP_MC146818_B_DM | AP_MC146818_B_24HOUR);
  TEST_ASSERT_EQUAL_HEX8(9, ap_mc146818_read(&rtc, AP_MC146818_MINUTES));
  TEST_ASSERT_EQUAL_HEX8(21, ap_mc146818_read(&rtc, AP_MC146818_HOURS));
}

static void test_changing_the_format_does_not_move_the_clock(void) {
  ap_mc146818_t rtc;
  init(&rtc);
  set_24h(&rtc);

  ap_mc146818_time_t before = ap_mc146818_now(&rtc);
  ap_mc146818_write(&rtc, AP_MC146818_REGISTER_B,
                    AP_MC146818_B_DM | AP_MC146818_B_24HOUR);
  ap_mc146818_time_t after = ap_mc146818_now(&rtc);

  /* The reason the clock is kept as numbers rather than in the register bytes.
   * If the bytes were the truth, flipping DM would reinterpret `0x21` as 33
   * decimal and silently move the clock twelve seconds. */
  TEST_ASSERT_EQUAL_UINT(before.second, after.second);
  TEST_ASSERT_EQUAL_UINT(before.hour, after.hour);
  TEST_ASSERT_EQUAL_HEX8(21, ap_mc146818_read(&rtc, AP_MC146818_HOURS));
}

static void test_twelve_hour_mode_marks_the_afternoon(void) {
  ap_mc146818_t rtc;
  init(&rtc);

  /* Register B resets with 24/12 clear, so twelve-hour mode, and 21:09 reads as
   * 9 with the high bit set. That is exactly what the oracle's calendar was
   * dumped showing -- `89` in the hours byte -- which is how twelve-hour mode
   * was identified there in the first place. */
  TEST_ASSERT_EQUAL_HEX8(0x89, ap_mc146818_read(&rtc, AP_MC146818_HOURS));

  /* And midnight is 12 AM, not 0. */
  ap_mc146818_write(&rtc, AP_MC146818_HOURS, 0x12);
  TEST_ASSERT_EQUAL_UINT(0u, ap_mc146818_now(&rtc).hour);
  TEST_ASSERT_EQUAL_HEX8(0x12, ap_mc146818_read(&rtc, AP_MC146818_HOURS));

  /* Noon is 12 PM. */
  ap_mc146818_write(&rtc, AP_MC146818_HOURS, 0x92);
  TEST_ASSERT_EQUAL_UINT(12u, ap_mc146818_now(&rtc).hour);
}

static void test_a_second_passes_once_a_second(void) {
  ap_mc146818_t rtc;
  init(&rtc);
  set_24h(&rtc);

  ap_mc146818_advance(&rtc, seconds(1u) - 1u);
  TEST_ASSERT_EQUAL_HEX8(0x21, ap_mc146818_read(&rtc, AP_MC146818_SECONDS));

  ap_mc146818_advance(&rtc, seconds(1u));
  TEST_ASSERT_EQUAL_HEX8(0x22, ap_mc146818_read(&rtc, AP_MC146818_SECONDS));
}

static void test_the_calendar_carries_across_a_month_end(void) {
  ap_mc146818_t rtc;
  init(&rtc);
  set_24h(&rtc);

  /* From 21:09:21 on 31 July to just past midnight: 2 hours 50 minutes 39
   * seconds. July has 31 days, so the next day is 1 August. */
  ap_mc146818_advance(&rtc, seconds((2u * 3600u) + (50u * 60u) + 39u));

  TEST_ASSERT_EQUAL_HEX8(0x00, ap_mc146818_read(&rtc, AP_MC146818_HOURS));
  TEST_ASSERT_EQUAL_HEX8(0x01, ap_mc146818_read(&rtc, AP_MC146818_DAY_OF_MONTH));
  TEST_ASSERT_EQUAL_HEX8(0x08, ap_mc146818_read(&rtc, AP_MC146818_MONTH));
  /* "Sunday is 1", so Friday 6 becomes Saturday 7. */
  TEST_ASSERT_EQUAL_HEX8(0x07, ap_mc146818_read(&rtc, AP_MC146818_DAY_OF_WEEK));
}

static void test_the_day_of_week_wraps_from_seven_to_one(void) {
  ap_mc146818_t rtc;
  init(&rtc);
  set_24h(&rtc);
  ap_mc146818_write(&rtc, AP_MC146818_DAY_OF_WEEK, 0x07);

  ap_mc146818_advance(&rtc, seconds(24u * 3600u));
  /* Not 8. A counter that ran to 8 would be wrong once a week, which is the
   * sort of period over which nobody notices. */
  TEST_ASSERT_EQUAL_HEX8(0x01, ap_mc146818_read(&rtc, AP_MC146818_DAY_OF_WEEK));
}

static void test_february_has_twenty_nine_days_in_a_leap_year(void) {
  ap_mc146818_t rtc;
  ap_mc146818_time_t start = START;
  start.year = 1988u;
  start.month = 2u;
  start.day = 28u;
  start.hour = 23u;
  start.minute = 59u;
  start.second = 59u;
  TEST_ASSERT_TRUE(ap_mc146818_reset(&rtc, &start));
  set_24h(&rtc);

  ap_mc146818_advance(&rtc, seconds(1u));
  TEST_ASSERT_EQUAL_HEX8(0x29, ap_mc146818_read(&rtc, AP_MC146818_DAY_OF_MONTH));
  TEST_ASSERT_EQUAL_HEX8(0x02, ap_mc146818_read(&rtc, AP_MC146818_MONTH));
}

static void test_a_century_year_is_not_a_leap_year_unless_it_divides_by_four_hundred(
    void) {
  /* The part carries two digits and `[146818]` calls it "a 100 year calendar",
   * so a core holding only `00` cannot tell 1900 from 2000 -- and they take
   * opposite leap rules. Keeping the full year is what makes this answerable at
   * all, and it costs nothing. */
  static const struct {
    unsigned year;
    uint8_t expected_day;
    uint8_t expected_month;
  } cases[] = {
      {1900u, 0x01, 0x03}, /* not a leap year: 28 Feb -> 1 March */
      {2000u, 0x29, 0x02}, /* a leap year: 28 Feb -> 29 Feb */
      {1988u, 0x29, 0x02},
      {1987u, 0x01, 0x03},
  };

  for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    ap_mc146818_t rtc;
    ap_mc146818_time_t start = START;
    start.year = cases[i].year;
    start.month = 2u;
    start.day = 28u;
    start.hour = 23u;
    start.minute = 59u;
    start.second = 59u;
    TEST_ASSERT_TRUE(ap_mc146818_reset(&rtc, &start));
    set_24h(&rtc);
    ap_mc146818_advance(&rtc, seconds(1u));

    TEST_ASSERT_EQUAL_HEX8(cases[i].expected_day,
                           ap_mc146818_read(&rtc, AP_MC146818_DAY_OF_MONTH));
    TEST_ASSERT_EQUAL_HEX8(cases[i].expected_month,
                           ap_mc146818_read(&rtc, AP_MC146818_MONTH));
  }
}

static void test_writing_the_year_keeps_the_century(void) {
  ap_mc146818_t rtc;
  init(&rtc);
  set_24h(&rtc);

  /* The part has no century byte, so a two-digit write must land inside the
   * century the clock is already in. Inventing a windowing rule -- "under 70
   * means 20xx" -- would be a guess with a thirty-year error in it. */
  ap_mc146818_write(&rtc, AP_MC146818_YEAR, 0x88);
  TEST_ASSERT_EQUAL_UINT(1988u, ap_mc146818_now(&rtc).year);
  TEST_ASSERT_EQUAL_HEX8(0x88, ap_mc146818_read(&rtc, AP_MC146818_YEAR));
}

static void test_setting_the_set_bit_stops_the_clock(void) {
  ap_mc146818_t rtc;
  init(&rtc);
  ap_mc146818_write(&rtc, AP_MC146818_REGISTER_B,
                    AP_MC146818_B_24HOUR | AP_MC146818_B_SET);

  /* "When the SET bit is written to a '1', any update cycle in progress is
   * aborted and the program may initialize the time and calendar bytes without
   * an update occurring in the midst of initializing." */
  ap_mc146818_advance(&rtc, seconds(100u));
  TEST_ASSERT_EQUAL_HEX8(0x21, ap_mc146818_read(&rtc, AP_MC146818_SECONDS));
}

static void test_a_held_clock_does_not_catch_up_when_released(void) {
  ap_mc146818_t rtc;
  init(&rtc);
  ap_mc146818_write(&rtc, AP_MC146818_REGISTER_B,
                    AP_MC146818_B_24HOUR | AP_MC146818_B_SET);
  ap_mc146818_advance(&rtc, seconds(100u));

  /* The half that is easy to get wrong. If the cursor did not advance while
   * held, releasing SET would deliver a hundred update cycles at once -- a
   * clock that catches up is not a clock that was stopped, and software that
   * set the time would watch it jump forward by however long it took. */
  ap_mc146818_write(&rtc, AP_MC146818_REGISTER_B, AP_MC146818_B_24HOUR);
  ap_mc146818_advance(&rtc, seconds(101u));

  TEST_ASSERT_EQUAL_HEX8(0x22, ap_mc146818_read(&rtc, AP_MC146818_SECONDS));
}

static void test_an_update_sets_its_flag_whether_or_not_it_interrupts(void) {
  ap_mc146818_t rtc;
  init(&rtc);
  set_24h(&rtc);

  /* "Each of the three interrupt sources have separate flag bits in Register C,
   * which are set independent of the state of the corresponding enable bits in
   * Register B." So a polling driver sees UF with UIE clear. */
  ap_mc146818_advance(&rtc, seconds(1u));
  TEST_ASSERT_FALSE(ap_mc146818_irq(&rtc));

  uint8_t c = ap_mc146818_read(&rtc, AP_MC146818_REGISTER_C);
  TEST_ASSERT_EQUAL_HEX8(AP_MC146818_C_UF, c & AP_MC146818_C_UF);
  TEST_ASSERT_EQUAL_HEX8(0, c & AP_MC146818_C_IRQF);
}

static void test_reading_the_flag_register_clears_it(void) {
  ap_mc146818_t rtc;
  init(&rtc);
  set_24h(&rtc);
  ap_mc146818_advance(&rtc, seconds(1u));

  /* "All bits which are high when read by the program are cleared." */
  TEST_ASSERT_NOT_EQUAL(0, ap_mc146818_read(&rtc, AP_MC146818_REGISTER_C));
  TEST_ASSERT_EQUAL_HEX8(0, ap_mc146818_read(&rtc, AP_MC146818_REGISTER_C));
}

static void test_an_enabled_update_flag_drives_the_interrupt(void) {
  ap_mc146818_t rtc;
  init(&rtc);
  ap_mc146818_write(&rtc, AP_MC146818_REGISTER_B,
                    AP_MC146818_B_24HOUR | AP_MC146818_B_UIE);

  ap_mc146818_advance(&rtc, seconds(1u));
  TEST_ASSERT_TRUE(ap_mc146818_irq(&rtc));

  uint8_t c = ap_mc146818_read(&rtc, AP_MC146818_REGISTER_C);
  TEST_ASSERT_EQUAL_HEX8(AP_MC146818_C_IRQF, c & AP_MC146818_C_IRQF);
  /* And reading it drops the pin. */
  TEST_ASSERT_FALSE(ap_mc146818_irq(&rtc));
}

static void test_the_alarm_fires_when_the_three_bytes_match(void) {
  ap_mc146818_t rtc;
  init(&rtc);
  ap_mc146818_write(&rtc, AP_MC146818_REGISTER_B,
                    AP_MC146818_B_24HOUR | AP_MC146818_B_AIE);

  /* Two seconds ahead: 21:09:23. */
  ap_mc146818_write(&rtc, AP_MC146818_SECONDS_ALARM, 0x23);
  ap_mc146818_write(&rtc, AP_MC146818_MINUTES_ALARM, 0x09);
  ap_mc146818_write(&rtc, AP_MC146818_HOURS_ALARM, 0x21);

  ap_mc146818_advance(&rtc, seconds(1u));
  TEST_ASSERT_EQUAL_HEX8(0, ap_mc146818_read(&rtc, AP_MC146818_REGISTER_C) &
                                AP_MC146818_C_AF);

  ap_mc146818_advance(&rtc, seconds(2u));
  TEST_ASSERT_EQUAL_HEX8(AP_MC146818_C_AF,
                         ap_mc146818_read(&rtc, AP_MC146818_REGISTER_C) &
                             AP_MC146818_C_AF);
}

static void test_a_dont_care_alarm_byte_matches_anything(void) {
  ap_mc146818_t rtc;
  init(&rtc);
  ap_mc146818_write(&rtc, AP_MC146818_REGISTER_B,
                    AP_MC146818_B_24HOUR | AP_MC146818_B_AIE);

  /* "including a 'don't care' alarm code of binary 11XXXXXX". Don't-care hours
   * and minutes with a fixed second gives the once-per-minute alarm.
   *
   * The test is on the raw byte, not the decoded value: `C0` is not valid BCD
   * at all, so a model that decoded before comparing would turn the wildcard
   * into a nonsense number matching nothing. */
  ap_mc146818_write(&rtc, AP_MC146818_SECONDS_ALARM, 0x25);
  ap_mc146818_write(&rtc, AP_MC146818_MINUTES_ALARM, 0xC0);
  ap_mc146818_write(&rtc, AP_MC146818_HOURS_ALARM, 0xFF);

  ap_mc146818_advance(&rtc, seconds(4u));
  TEST_ASSERT_EQUAL_HEX8(AP_MC146818_C_AF,
                         ap_mc146818_read(&rtc, AP_MC146818_REGISTER_C) &
                             AP_MC146818_C_AF);
}

static void test_the_fifty_ram_bytes_are_ordinary_storage(void) {
  ap_mc146818_t rtc;
  init(&rtc);

  /* `008778-03` §3.6: "50 bytes of low-power static RAM. The RAM is used to
   * store configuration information." Addresses `0E` to `3F`. */
  for (unsigned a = AP_MC146818_RAM_BASE; a < AP_MC146818_BYTES; a++) {
    ap_mc146818_write(&rtc, (uint8_t)a, (uint8_t)(a ^ 0x5Au));
  }
  for (unsigned a = AP_MC146818_RAM_BASE; a < AP_MC146818_BYTES; a++) {
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(a ^ 0x5Au),
                           ap_mc146818_read(&rtc, (uint8_t)a));
  }
  TEST_ASSERT_EQUAL_UINT(50u, AP_MC146818_BYTES - AP_MC146818_RAM_BASE);
}

static void test_the_read_only_registers_refuse_writes(void) {
  ap_mc146818_t rtc;
  init(&rtc);

  /* Register D: "VRT ... b6 TO b0 - The remaining bits of Register D are
   * unused. They cannot be written, but are always read as 0's." */
  ap_mc146818_write(&rtc, AP_MC146818_REGISTER_D, 0x00);
  TEST_ASSERT_EQUAL_HEX8(AP_MC146818_D_VRT,
                         ap_mc146818_read(&rtc, AP_MC146818_REGISTER_D));

  /* Register A is "Read/Write Register except UIP". */
  ap_mc146818_write(&rtc, AP_MC146818_REGISTER_A, 0xFF);
  TEST_ASSERT_EQUAL_HEX8(0, ap_mc146818_read(&rtc, AP_MC146818_REGISTER_A) &
                                AP_MC146818_A_UIP);
  TEST_ASSERT_EQUAL_HEX8(0x7F, ap_mc146818_read(&rtc, AP_MC146818_REGISTER_A));
}

static void test_the_rate_table_is_table_fives(void) {
  ap_mc146818_t rtc;
  init(&rtc);

  /* `[146818]` Table 5, the 4.194304/1.048576 MHz column. Each interval in the
   * table is the reciprocal of its square-wave frequency, so one number serves
   * for both -- checked here at the two ends and at the boundary that matters. */
  static const uint32_t expected[16] = {0,   32768, 16384, 8192, 4096, 2048,
                                        1024, 512,  256,   128,  64,   32,
                                        16,   8,    4,     2};
  for (unsigned rs = 0; rs < 16u; rs++) {
    ap_mc146818_write(&rtc, AP_MC146818_REGISTER_A, (uint8_t)rs);
    TEST_ASSERT_EQUAL_UINT32(expected[rs], ap_mc146818_periodic_hz(&rtc));
  }
}

static void test_the_six_fastest_rates_are_refused_not_rounded(void) {
  ap_mc146818_t rtc;
  init(&rtc);

  /* Table 5's rates are 32768/2^n Hz, and `AP_TIME_BASE_HZ` factors as
   * 2^9 * 3 * 5^8 * 11 -- so it carries 2^9 and the six fastest are not exactly
   * representable. `CLAUDE.md`'s rule is to reject rather than round, and the
   * cost of changing the base instead is recorded in the header: 64x for these
   * six, and 8192x to represent the crystal itself, which would leave under
   * four days of span in a 64-bit counter.
   *
   * A rounded periodic interrupt is indistinguishable from a correct one, which
   * is exactly why it must be refused where it cannot be exact. */
  for (unsigned rs = 1u; rs <= 6u; rs++) {
    ap_mc146818_write(&rtc, AP_MC146818_REGISTER_A, (uint8_t)rs);
    TEST_ASSERT_FALSE(ap_mc146818_rate_supported(&rtc));

    ap_mc146818_advance(&rtc, seconds(10u * rs));
    TEST_ASSERT_EQUAL_HEX8(0, ap_mc146818_read(&rtc, AP_MC146818_REGISTER_C) &
                                  AP_MC146818_C_PF);
  }

  /* And the nine slower ones are exact, 512 Hz down to 2 Hz. */
  for (unsigned rs = 7u; rs < 16u; rs++) {
    ap_mc146818_write(&rtc, AP_MC146818_REGISTER_A, (uint8_t)rs);
    TEST_ASSERT_TRUE(ap_mc146818_rate_supported(&rtc));
  }

  /* "None" is honoured exactly by doing nothing. */
  ap_mc146818_write(&rtc, AP_MC146818_REGISTER_A, 0x00);
  TEST_ASSERT_TRUE(ap_mc146818_rate_supported(&rtc));
}

static void test_a_representable_rate_sets_its_flag_on_time(void) {
  ap_mc146818_t rtc;
  init(&rtc);

  /* RS = 1111 is 500 ms, the slowest and the one an operating system is most
   * likely to use for a scheduler tick. */
  ap_mc146818_write(&rtc, AP_MC146818_REGISTER_A, 0x0F);
  ap_mc146818_write(&rtc, AP_MC146818_REGISTER_B, AP_MC146818_B_24HOUR);

  ap_mc146818_advance(&rtc, (AP_TIME_BASE_HZ / 2u) - 1u);
  TEST_ASSERT_EQUAL_HEX8(0, ap_mc146818_read(&rtc, AP_MC146818_REGISTER_C) &
                                AP_MC146818_C_PF);

  ap_mc146818_advance(&rtc, AP_TIME_BASE_HZ / 2u);
  TEST_ASSERT_EQUAL_HEX8(AP_MC146818_C_PF,
                         ap_mc146818_read(&rtc, AP_MC146818_REGISTER_C) &
                             AP_MC146818_C_PF);
}

static void test_the_periodic_flag_drives_the_interrupt_when_enabled(void) {
  ap_mc146818_t rtc;
  init(&rtc);
  ap_mc146818_write(&rtc, AP_MC146818_REGISTER_A, 0x0F); /* 2 Hz */
  ap_mc146818_write(&rtc, AP_MC146818_REGISTER_B,
                    AP_MC146818_B_24HOUR | AP_MC146818_B_PIE);

  /* "IRQF = PF*PIE + AF*AIE + UF*UIE" -- the first term, which was absent while
   * the periodic interrupt was declined. */
  ap_mc146818_advance(&rtc, AP_TIME_BASE_HZ / 2u);
  TEST_ASSERT_TRUE(ap_mc146818_irq(&rtc));
  (void)ap_mc146818_read(&rtc, AP_MC146818_REGISTER_C);
  TEST_ASSERT_FALSE(ap_mc146818_irq(&rtc));
}

static void test_the_periodic_interrupt_keeps_running_while_the_clock_is_held(
    void) {
  ap_mc146818_t rtc;
  init(&rtc);
  ap_mc146818_write(&rtc, AP_MC146818_REGISTER_A, 0x0F);
  ap_mc146818_write(&rtc, AP_MC146818_REGISTER_B,
                    AP_MC146818_B_24HOUR | AP_MC146818_B_SET);

  /* `[146818]` ties SET to the *update cycle*, not to the divider chain, and
   * Figure 15 shows the periodic flag continuing across an update. So setting
   * the time does not silence the scheduler tick -- which is the behaviour an
   * operating system depends on while it is setting the clock. */
  ap_mc146818_advance(&rtc, AP_TIME_BASE_HZ);
  TEST_ASSERT_EQUAL_HEX8(0x21, ap_mc146818_read(&rtc, AP_MC146818_SECONDS));
  TEST_ASSERT_EQUAL_HEX8(AP_MC146818_C_PF,
                         ap_mc146818_read(&rtc, AP_MC146818_REGISTER_C) &
                             AP_MC146818_C_PF);
}

static void test_selecting_a_rate_late_delivers_no_backlog(void) {
  ap_mc146818_t rtc;
  init(&rtc);
  ap_mc146818_write(&rtc, AP_MC146818_REGISTER_B, AP_MC146818_B_24HOUR);

  /* No rate selected for a while. */
  ap_mc146818_advance(&rtc, seconds(100u));
  ap_mc146818_write(&rtc, AP_MC146818_REGISTER_A, 0x0F);

  /* Less than one period later, so nothing yet -- rather than a flag for every
   * period that would have elapsed had a rate been selected all along. */
  ap_mc146818_advance(&rtc, seconds(100u) + (AP_TIME_BASE_HZ / 4u));
  TEST_ASSERT_EQUAL_HEX8(0, ap_mc146818_read(&rtc, AP_MC146818_REGISTER_C) &
                                AP_MC146818_C_PF);
}

/* ---------------------------------------------------------------------------
 * A long interval, carried the whole way
 *
 * This item's verification line asked for "the 14-day calendar interval hazard
 * noted in the MAME driver" to be reproduced or explained. There is no such
 * note: the pinned `ext/mame` has nothing about days, weeks or intervals
 * anywhere in `src/mame/apollo/`, and no commit in its history touches "14
 * days" under `src/mame` at all. The claim dates from this project's first
 * scaffolding commit, written before the driver had been read.
 *
 * What is left when the citation goes is the substance behind it, and that is
 * worth having on its own account: a calendar advanced across a long interval
 * has to carry correctly at every boundary it crosses, and the part cannot be
 * asked to -- `[146818]`'s update cycle advances one second at a time, so 14
 * days is 1,209,600 carries and any one of them can be the wrong one.
 * ------------------------------------------------------------------------- */

/* Fourteen days from a date chosen so the interval crosses a month end: 31 July
 * plus 14 days is 14 August, so the July/August boundary and the 31-day month
 * are both inside it. */
static void test_fourteen_days_of_carries_land_on_the_right_date(void) {
  static const ap_mc146818_time_t start = {
      .year = 1987u, .month = 7u, .day = 31u, .day_of_week = 6u,
      .hour = 21u, .minute = 9u, .second = 21u,
  };
  ap_mc146818_t rtc;
  TEST_ASSERT_TRUE(ap_mc146818_reset(&rtc, &start));

  ap_mc146818_advance(&rtc, seconds(14u * 24u * 60u * 60u));
  const ap_mc146818_time_t now = ap_mc146818_now(&rtc);

  TEST_ASSERT_EQUAL_UINT(1987u, now.year);
  TEST_ASSERT_EQUAL_UINT(8u, now.month);
  TEST_ASSERT_EQUAL_UINT(14u, now.day);
  /* Time of day untouched: 14 days is a whole number of days, so a carry that
   * drifted by a second would show here and nowhere else. */
  TEST_ASSERT_EQUAL_UINT(21u, now.hour);
  TEST_ASSERT_EQUAL_UINT(9u, now.minute);
  TEST_ASSERT_EQUAL_UINT(21u, now.second);
  /* And the day of the week advanced by exactly two weeks, which is to say not
   * at all -- the one field that a 14-day interval leaves invariant, and so the
   * one a miscount cannot hide in. */
  TEST_ASSERT_EQUAL_UINT(start.day_of_week, now.day_of_week);
}

/* The boundaries a fortnight can straddle, each crossed on its own so a failure
 * names itself rather than arriving as "the date is wrong". */
static void test_a_fortnight_across_every_boundary_it_can_cross(void) {
  static const struct {
    ap_mc146818_time_t start;
    unsigned year, month, day;
    const char *what;
  } cases[] = {
      /* February in a common year: 28 days, so 20 Feb + 14 is 6 March. */
      {{.year = 1987u, .month = 2u, .day = 20u, .day_of_week = 6u,
        .hour = 0u, .minute = 0u, .second = 0u}, 1987u, 3u, 6u,
       "February, common year"},
      /* And in a leap year, where the same sum is 5 March. 1988 is divisible
       * by four and not by a hundred. */
      {{.year = 1988u, .month = 2u, .day = 20u, .day_of_week = 0u,
        .hour = 0u, .minute = 0u, .second = 0u}, 1988u, 3u, 5u,
       "February, leap year"},
      /* The year end, which carries the year as well as the month. */
      {{.year = 1987u, .month = 12u, .day = 25u, .day_of_week = 5u,
        .hour = 23u, .minute = 59u, .second = 0u}, 1988u, 1u, 8u,
       "year end"},
      /* 2000 is a leap year -- divisible by 400 -- and the part's two-digit
       * year cannot tell it from 1900, which is not. The century rule this
       * core carries is what decides it, and a fortnight from 20 February is
       * where the two answers differ by a day. */
      {{.year = 2000u, .month = 2u, .day = 20u, .day_of_week = 0u,
        .hour = 0u, .minute = 0u, .second = 0u}, 2000u, 3u, 5u,
       "the 400-year rule"},
  };

  for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    ap_mc146818_t rtc;
    TEST_ASSERT_TRUE(ap_mc146818_reset(&rtc, &cases[i].start));
    ap_mc146818_advance(&rtc, seconds(14u * 24u * 60u * 60u));
    const ap_mc146818_time_t now = ap_mc146818_now(&rtc);

    TEST_ASSERT_EQUAL_UINT_MESSAGE(cases[i].year, now.year, cases[i].what);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(cases[i].month, now.month, cases[i].what);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(cases[i].day, now.day, cases[i].what);
  }
}

/* And the interval is a *sum*, not a jump: advancing in many small steps must
 * reach the same instant as advancing in one. `ap_mc146818_advance` takes an
 * absolute time and carries the remainder, so a caller polling often and one
 * polling rarely must not disagree -- which is the property a real driver, and
 * a fast mode, both depend on. */
static void test_the_same_fortnight_reached_in_steps_agrees(void) {
  static const ap_mc146818_time_t start = {
      .year = 1987u, .month = 7u, .day = 31u, .day_of_week = 6u,
      .hour = 21u, .minute = 9u, .second = 21u,
  };
  const uint64_t total = 14u * 24u * 60u * 60u;

  ap_mc146818_t one_go;
  TEST_ASSERT_TRUE(ap_mc146818_reset(&one_go, &start));
  ap_mc146818_advance(&one_go, seconds(total));

  ap_mc146818_t stepped;
  TEST_ASSERT_TRUE(ap_mc146818_reset(&stepped, &start));
  /* Steps that are not a whole second, so the remainder is exercised rather
   * than avoided: 1,209,600 seconds in 999-second-and-a-third pieces. */
  const ap_time_t step = seconds(999u) + AP_TIME_BASE_HZ / 3u;
  for (ap_time_t t = step; t <= seconds(total); t += step) {
    ap_mc146818_advance(&stepped, t);
  }
  ap_mc146818_advance(&stepped, seconds(total));

  const ap_mc146818_time_t a = ap_mc146818_now(&one_go);
  const ap_mc146818_time_t b = ap_mc146818_now(&stepped);
  TEST_ASSERT_EQUAL_UINT(a.year, b.year);
  TEST_ASSERT_EQUAL_UINT(a.month, b.month);
  TEST_ASSERT_EQUAL_UINT(a.day, b.day);
  TEST_ASSERT_EQUAL_UINT(a.hour, b.hour);
  TEST_ASSERT_EQUAL_UINT(a.minute, b.minute);
  TEST_ASSERT_EQUAL_UINT(a.second, b.second);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_square_wave_pin_follows_sqwe_and_the_rate_select);
  RUN_TEST(test_fourteen_days_of_carries_land_on_the_right_date);
  RUN_TEST(test_a_fortnight_across_every_boundary_it_can_cross);
  RUN_TEST(test_the_same_fortnight_reached_in_steps_agrees);
  RUN_TEST(test_the_clock_starts_where_the_caller_puts_it);
  RUN_TEST(test_two_clocks_started_alike_stay_identical);
  RUN_TEST(test_the_clock_reads_bcd_unless_told_otherwise);
  RUN_TEST(test_changing_the_format_does_not_move_the_clock);
  RUN_TEST(test_twelve_hour_mode_marks_the_afternoon);
  RUN_TEST(test_a_second_passes_once_a_second);
  RUN_TEST(test_the_calendar_carries_across_a_month_end);
  RUN_TEST(test_the_day_of_week_wraps_from_seven_to_one);
  RUN_TEST(test_february_has_twenty_nine_days_in_a_leap_year);
  RUN_TEST(
      test_a_century_year_is_not_a_leap_year_unless_it_divides_by_four_hundred);
  RUN_TEST(test_writing_the_year_keeps_the_century);
  RUN_TEST(test_setting_the_set_bit_stops_the_clock);
  RUN_TEST(test_a_held_clock_does_not_catch_up_when_released);
  RUN_TEST(test_an_update_sets_its_flag_whether_or_not_it_interrupts);
  RUN_TEST(test_reading_the_flag_register_clears_it);
  RUN_TEST(test_an_enabled_update_flag_drives_the_interrupt);
  RUN_TEST(test_the_alarm_fires_when_the_three_bytes_match);
  RUN_TEST(test_a_dont_care_alarm_byte_matches_anything);
  RUN_TEST(test_the_fifty_ram_bytes_are_ordinary_storage);
  RUN_TEST(test_the_read_only_registers_refuse_writes);
  RUN_TEST(test_the_rate_table_is_table_fives);
  RUN_TEST(test_the_six_fastest_rates_are_refused_not_rounded);
  RUN_TEST(test_a_representable_rate_sets_its_flag_on_time);
  RUN_TEST(test_the_periodic_flag_drives_the_interrupt_when_enabled);
  RUN_TEST(test_the_periodic_interrupt_keeps_running_while_the_clock_is_held);
  RUN_TEST(test_selecting_a_rate_late_delivers_no_backlog);
  return UNITY_END();
}

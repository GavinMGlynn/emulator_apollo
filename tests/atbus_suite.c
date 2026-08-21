/* The AT-compatible bus's published cycle times, `008778-03` Appendix A and B,
 * and the wait states a processor pays for them.
 *
 * The transcription is checked against itself: the two appendices describe the
 * same bus at two clock rates, so almost every figure must reduce to the same
 * number of bus clocks in both. A digit wrong in either column fails that.
 */

#include "unity.h"

#include "board/ap_atbus.h"
#include "board/ap_board.h"
#include "device/ap_mc146818.h"
#include "cpu/m68030/ap_m68030_access.h"
#include "machine/ap_machine.h"

void setUp(void) {}
void tearDown(void) {}

/* ---------------------------------------------------------------------------
 * The tables, and what they agree about
 * ------------------------------------------------------------------------- */

/* `#26` "BUS CLOCK Cycle Time": 166 ns and 125 ns, so 6 MHz and 8 MHz, and
 * `#25` gives the CPU/motherboard clock as twice each. Figures B-3 and B-7
 * label both "Internal signal on the CPU/Motherboard. Not available on the
 * Bus." */
static void test_each_appendix_runs_its_bus_at_half_its_clock(void) {
  const ap_atbus_timing_t *a = ap_atbus_timing(AP_ATBUS_SERIES_3000);
  const ap_atbus_timing_t *b = ap_atbus_timing(AP_ATBUS_SERIES_4000);
  TEST_ASSERT_NOT_NULL(a);
  TEST_ASSERT_NOT_NULL(b);

  TEST_ASSERT_EQUAL_UINT32(12000000u, a->clock_hz);
  TEST_ASSERT_EQUAL_UINT32(6000000u, a->bus_clock_hz);
  TEST_ASSERT_EQUAL_UINT32(16000000u, b->clock_hz);
  TEST_ASSERT_EQUAL_UINT32(8000000u, b->bus_clock_hz);
}

/* The check the transcription is worth having: three of the four figures are
 * the same number of bus clocks in both appendices, printed as different
 * nanosecond counts only because the bus clocks differ. Tolerated to 0.05 of a
 * clock, which is the rounding the tables force by printing 166 ns for a
 * 166.67 ns period -- nothing looser. */
static void test_the_two_appendices_agree_in_bus_clocks(void) {
  const ap_atbus_timing_t *a = ap_atbus_timing(AP_ATBUS_SERIES_3000);
  const ap_atbus_timing_t *b = ap_atbus_timing(AP_ATBUS_SERIES_4000);

  const struct {
    ap_atbus_cycle_t cycle;
    bool read;
    unsigned expected; /* hundredths of a bus clock */
    const char *what;
  } same[] = {
      {AP_ATBUS_CYCLE_MEMORY, false, 300u, "#30 memory write cycle"},
      {AP_ATBUS_CYCLE_IO_16, true, 150u, "#37 16-bit I/O command"},
      {AP_ATBUS_CYCLE_IO_8, true, 450u, "#48 8-bit I/O command"},
  };

  for (unsigned i = 0; i < sizeof same / sizeof same[0]; i++) {
    const unsigned ca = ap_atbus_centiclocks(a, same[i].cycle, same[i].read);
    const unsigned cb = ap_atbus_centiclocks(b, same[i].cycle, same[i].read);
    TEST_ASSERT_UINT_WITHIN_MESSAGE(5u, same[i].expected, ca, same[i].what);
    TEST_ASSERT_UINT_WITHIN_MESSAGE(5u, same[i].expected, cb, same[i].what);
  }
}

/* And the row that genuinely differs. Four bus clocks on the slower board and
 * three on the faster one is a real wait state, not the rounding above: the gap
 * is a whole clock where the others are two hundredths. */
static void test_the_memory_read_cycle_is_the_one_row_that_differs(void) {
  const ap_atbus_timing_t *a = ap_atbus_timing(AP_ATBUS_SERIES_3000);
  const ap_atbus_timing_t *b = ap_atbus_timing(AP_ATBUS_SERIES_4000);

  TEST_ASSERT_UINT_WITHIN(5u, 400u,
                          ap_atbus_centiclocks(a, AP_ATBUS_CYCLE_MEMORY, true));
  TEST_ASSERT_UINT_WITHIN(5u, 300u,
                          ap_atbus_centiclocks(b, AP_ATBUS_CYCLE_MEMORY, true));

  /* On the Series 4000 a read and a write cost the same; on the Series 3000
   * they do not, which is what makes the direction worth carrying. */
  TEST_ASSERT_EQUAL_UINT(ap_atbus_centiclocks(b, AP_ATBUS_CYCLE_MEMORY, true),
                         ap_atbus_centiclocks(b, AP_ATBUS_CYCLE_MEMORY, false));
  TEST_ASSERT_TRUE(ap_atbus_centiclocks(a, AP_ATBUS_CYCLE_MEMORY, true) >
                   ap_atbus_centiclocks(a, AP_ATBUS_CYCLE_MEMORY, false));
}

/* The durations come out in `AP_TIME_BASE_HZ` units and not in anybody's
 * clocks, which is this machine's standing rule. An implementation that
 * divided before multiplying would come out nine per cent short.
 *
 * **1000 ns, not 750.** This asserted the Series 3000's `#48` *command width*
 * for as long as that was the best figure in hand; §3.4 publishes the
 * **cycle** -- "1 microsecond for 8-bit transfers to 8-bit devices" -- and
 * §2.4.2 gives it independently as four wait states on a two-clock base, which
 * is six of this board's 166.67 ns bus clocks. The header records why the
 * count rather than the nanoseconds is what travels between the families. */
static void test_a_cycle_time_is_a_duration_in_base_units(void) {
  const ap_atbus_timing_t *a = ap_atbus_timing(AP_ATBUS_SERIES_3000);
  const ap_time_t io = ap_atbus_access_time(a, AP_ATBUS_CYCLE_IO_8, true);

  TEST_ASSERT_EQUAL_UINT64(1000ull * AP_TIME_BASE_HZ / 1000000000ull, io);
}

/* **The cycle is six bus clocks for 8-bit I/O and three for 16-bit, on both
 * boards** -- which is what makes it a derivation rather than a transcription
 * of one family's nanoseconds. §3.4's printed 500 ns and 1 us fall out of the
 * Series 3000's clock; the Series 4000 runs the same counts faster. */
static void test_an_io_cycle_is_a_fixed_number_of_bus_clocks(void) {
  for (unsigned i = 0; i < 2u; i++) {
    const ap_atbus_timing_t *t =
        ap_atbus_timing(i == 0u ? AP_ATBUS_SERIES_3000 : AP_ATBUS_SERIES_4000);
    TEST_ASSERT_NOT_NULL(t);
    /* Rounded to the nearest hundredth of a clock, as `centiclocks` does, so
     * the Series 3000's 166.67 ns period does not read as a mismatch. */
    const uint64_t eight =
        ((uint64_t)t->io_8_cycle_ns * t->bus_clock_hz * 100u + 500000000u) /
        1000000000u;
    const uint64_t sixteen =
        ((uint64_t)t->io_16_cycle_ns * t->bus_clock_hz * 100u + 500000000u) /
        1000000000u;
    TEST_ASSERT_EQUAL_UINT64(600u, eight);
    TEST_ASSERT_EQUAL_UINT64(300u, sixteen);
  }
}

/* And a 16-bit transfer to an 8-bit device needs no figure of its own: §2.4.1
 * says it "is converted to two 8-bit transfers", and §3.4's 2 us is twice its
 * own 1 us. Asserted so the absence of a third cycle kind is a decision on
 * record rather than an omission. */
static void test_a_wide_transfer_to_a_narrow_device_is_two_cycles(void) {
  const ap_atbus_timing_t *a = ap_atbus_timing(AP_ATBUS_SERIES_3000);
  TEST_ASSERT_EQUAL_UINT32(2000u, 2u * a->io_8_cycle_ns);
}

/* ---------------------------------------------------------------------------
 * What the board declares
 * ------------------------------------------------------------------------- */

#define RAM_BYTES 0x00010000u
static uint8_t ram[RAM_BYTES];
static ap_board_t board;

static const ap_mc146818_time_t epoch = {
    .year = 1987u, .month = 7u, .day = 31u, .day_of_week = 6u,
    .hour = 21u, .minute = 9u, .second = 21u,
};

static void build_board(void) {
  TEST_ASSERT_TRUE(ap_board_init(&board, ram, RAM_BYTES, &epoch, 0x012345u));
}

/* ## `RESET` drives the board's reset line
 *
 * `002398-04` p. 12-8: "Neither RSA **nor the reset instruction** reset the SIO
 * lines" — one sentence saying the two have the same effect and naming the same
 * exclusion. `rsa` has called `ap_board_reset_devices` since it was
 * implemented; the instruction could not, because the processor knows nothing
 * about a board. `ap_machine` holds both halves and is where the wire goes.
 *
 * This suite rather than `machine_suite` because it is the one that already
 * builds a machine with a board attached, which is the whole condition being
 * tested.
 *
 * **Measured before it was made**: the identity boot is byte-identical either
 * way — state hash `717289781987BD4A` and the same console — so the reference
 * path does not depend on it. */
static void test_the_reset_instruction_resets_the_board_but_not_the_sio(void) {
  build_board();

  ap_machine_t m;
  ap_machine_init_model(&m, ram, RAM_BYTES, AP_MODEL_DN3500);
  ap_machine_set_board(&m, &board);
  ap_machine_reset(&m, AP_BOARD_RAM_BASE, AP_BOARD_RAM_BASE + 0x8000u);

  /* `RESET` is privileged, and `ap_machine_reset` starts in supervisor state.
   * `4E70` then a `NOP` to land on. */
  bool ok = false;
  ap_board_write(&board, AP_BOARD_RAM_BASE + 0u, 0x4Eu, &ok);
  TEST_ASSERT_TRUE(ok);
  ap_board_write(&board, AP_BOARD_RAM_BASE + 1u, 0x70u, &ok);
  ap_board_write(&board, AP_BOARD_RAM_BASE + 2u, 0x4Eu, &ok);
  ap_board_write(&board, AP_BOARD_RAM_BASE + 3u, 0x71u, &ok);

  board.interrupts.master.imr = 0x5Au;
  board.dma_page.page[0] = 0xA5u;
  board.calendar.rtc.ram[AP_CALENDAR_CONFIG_NODEID] = 0x7Bu;
  board.sio.port[0].channel[0].mr[0] = 0x13u;
  const unsigned before = m.cpu.external_resets;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_machine_step(&m).status);

  /* The processor saw the instruction, and the board saw the line. */
  TEST_ASSERT_EQUAL_UINT(before + 1u, m.cpu.external_resets);
  TEST_ASSERT_EQUAL_HEX8(0xFFu, board.interrupts.master.imr);
  TEST_ASSERT_EQUAL_HEX8(0x00u, board.dma_page.page[0]);

  /* And the two exclusions hold for the instruction exactly as they do for
   * `rsa`, which is what makes the page's one sentence true of both. */
  TEST_ASSERT_EQUAL_HEX8(0x7Bu,
                         board.calendar.rtc.ram[AP_CALENDAR_CONFIG_NODEID]);
  TEST_ASSERT_EQUAL_HEX8(0x13u, board.sio.port[0].channel[0].mr[0]);
}

/* "Decided by address, not by device." Everything in the I/O window takes the
 * I/O figure, which is how the disk, the tape and an empty slot get one without
 * anyone deciding device by device which is an AT card. */
static void test_the_at_windows_declare_a_figure_and_nothing_else_does(void) {
  build_board();
  const ap_atbus_timing_t *a = ap_atbus_timing(AP_ATBUS_SERIES_3000);
  const ap_time_t io = ap_atbus_access_time(a, AP_ATBUS_CYCLE_IO_8, true);
  const ap_time_t mem = ap_atbus_access_time(a, AP_ATBUS_CYCLE_MEMORY, true);

  /* The window itself, and three devices inside it. */
  TEST_ASSERT_EQUAL_UINT64(io, ap_board_access_time(&board, 0x040000u, true));
  /* The **floppy**, `05F800`, not the Winchester: §5.4.2 makes the fixed disk
   * a 16-bit device and the floppy an 8-bit one, so `io` -- the 8-bit figure --
   * is the floppy's. Asserting it at the Winchester was right only while every
   * card was charged the same width. */
  TEST_ASSERT_EQUAL_UINT64(io,
                           ap_board_access_time(&board, AP_DISK_FLOPPY_ADDR,
                                                true));
  /* And the Winchester is half of it: three bus clocks against six. */
  TEST_ASSERT_EQUAL_UINT64(io / 2u,
                           ap_board_access_time(&board, AP_DISK_FIXED_ADDR,
                                                true));
  TEST_ASSERT_EQUAL_UINT64(io, ap_board_access_time(&board, 0x050000u, true));
  TEST_ASSERT_EQUAL_UINT64(io, ap_board_access_time(&board, 0x05FFFFu, true));

  TEST_ASSERT_EQUAL_UINT64(mem, ap_board_access_time(&board, 0x080000u, true));

  /* And the board's own side of the machine, where no figure is published. Zero
   * means "the caller's minimum", not "instant". */
  TEST_ASSERT_EQUAL_UINT64(0u, ap_board_access_time(&board, 0x000000u, true));
  TEST_ASSERT_EQUAL_UINT64(0u, ap_board_access_time(&board, 0x010400u, true));
  TEST_ASSERT_EQUAL_UINT64(0u, ap_board_access_time(&board, 0x017000u, true));
  TEST_ASSERT_EQUAL_UINT64(
      0u, ap_board_access_time(&board, AP_BOARD_RAM_BASE, true));
}

/* `#18` against `#30`: an AT memory read costs more than a write on this board,
 * which is the only place in the tables where the direction changes a figure --
 * and the reason the callback takes a direction at all. */
static void test_an_at_memory_read_costs_more_than_a_write(void) {
  build_board();
  TEST_ASSERT_TRUE(ap_board_access_time(&board, 0x080000u, true) >
                   ap_board_access_time(&board, 0x080000u, false));
}

/* ---------------------------------------------------------------------------
 * What the processor pays for it
 * ------------------------------------------------------------------------- */

/* The division of labour: the board answers a duration, the machine turns it
 * into its own clocks. Nobody writes a wait-state count down, and the count is
 * different on two models from one published nanosecond figure.
 *
 * A DN3500 runs at 25 MHz (40 ns) and a DN3000 at 12 MHz (83.3 ns). A 750 ns
 * 8-bit I/O cycle is 19 clocks on the first and 9 on the second, less the two
 * the bus charges anyway. */
static void test_a_faster_processor_waits_more_clocks_for_the_same_card(void) {
  build_board();

  ap_machine_t fast;
  ap_machine_init_model(&fast, ram, RAM_BYTES, AP_MODEL_DN3500);
  ap_machine_set_board(&fast, &board);

  ap_machine_t slow;
  ap_machine_init_model(&slow, ram, RAM_BYTES, AP_MODEL_DN3000);
  ap_machine_set_board(&slow, &board);

  const ap_time_t cycle =
      ap_board_access_time(&board, AP_DISK_FLOPPY_ADDR, true);
  TEST_ASSERT_TRUE(cycle > 0u);

  /* Computed here the way the machine must: rounded up, because a device that
   * needs part of a clock holds the bus for all of it. */
  const uint64_t fast_clocks =
      (cycle + fast.cpu_clock.period - 1u) / fast.cpu_clock.period;
  const uint64_t slow_clocks =
      (cycle + slow.cpu_clock.period - 1u) / slow.cpu_clock.period;

  /* 25 and 12, up from 19 and 9: the cycle is the published one now. */
  TEST_ASSERT_EQUAL_UINT64(25u, fast_clocks);
  TEST_ASSERT_EQUAL_UINT64(12u, slow_clocks);
  TEST_ASSERT_TRUE(fast_clocks > slow_clocks);
}

/* The whole point, measured on a real access rather than computed: reading a
 * device in the AT I/O window costs a machine more clocks than reading main
 * memory, and the difference is the wait states the board declared.
 *
 * Before this, every region answered at the minimum and the two were equal --
 * "contention emergent in *how long* only in principle". */
static void test_an_at_device_read_costs_a_machine_more_than_memory(void) {
  build_board();

  ap_machine_t m;
  ap_machine_init_model(&m, ram, RAM_BYTES, AP_MODEL_DN3500);
  ap_machine_set_board(&m, &board);
  ap_machine_reset(&m, AP_BOARD_RAM_BASE, AP_BOARD_RAM_BASE + 0x8000u);

  /* Caches off: a second read of a cached line runs no bus cycle at all, and
   * what is being compared here is bus time. */
  m.instruction_access.cache_enabled = false;
  m.data_access.cache_enabled = false;

  ap_m68030_access_result_t r = ap_m68030_access_read(
      &m.data_access, AP_BOARD_RAM_BASE + 0x100u,
      AP_M68030_FC_SUPERVISOR_DATA);
  TEST_ASSERT_FALSE(r.fault);
  const uint32_t ram_clocks = r.clocks;

  /* The floppy: an 8-bit card, so this is the four-wait-state cycle. */
  r = ap_m68030_access_read(&m.data_access, AP_DISK_FLOPPY_ADDR,
                            AP_M68030_FC_SUPERVISOR_DATA);
  TEST_ASSERT_FALSE(r.fault);
  const uint32_t device_clocks = r.clocks;
  TEST_ASSERT_EQUAL_UINT(AP_M68030_MIN_BUS_CLOCKS, ram_clocks);
  /* **25 processor clocks, up from 19.** A DN3500's 8-bit AT I/O cycle is
   * §3.4's published 1 microsecond -- six bus clocks -- where this asserted
   * `#48`'s 750 ns command width while that was the best figure in hand. The
   * relationship the test exists for is unchanged and stronger: a device read
   * still costs far more than a RAM read, and now by the documented amount.
   *
   * **And the Winchester is a 16-bit card**, so it costs half again -- which is
   * `008778-03` §5.4.2's word-versus-byte transfer format made observable. */
  TEST_ASSERT_EQUAL_UINT(25u, device_clocks);
  TEST_ASSERT_TRUE(device_clocks > ram_clocks);

  r = ap_m68030_access_read(&m.data_access, AP_DISK_FIXED_ADDR,
                            AP_M68030_FC_SUPERVISOR_DATA);
  TEST_ASSERT_FALSE(r.fault);
  TEST_ASSERT_EQUAL_UINT(13u, r.clocks);
  TEST_ASSERT_TRUE(r.clocks < device_clocks);
}


/* `008778-03` §3.3's DRAM figures and §2.3.2's `IO_CH_RDY` ceiling, each exact
 * on the time base. A rounded constant here would drift against every other
 * clock in the machine, which is the property `AP_TIME_BASE_HZ` exists to give
 * and the reason it is asserted rather than assumed. */
static void test_the_dram_figures_land_exactly_on_the_time_base(void) {
  TEST_ASSERT_EQUAL_UINT64((ap_time_t)2585088u, AP_ATBUS_DRAM_RAS_TICKS);
  TEST_ASSERT_EQUAL_UINT64((ap_time_t)1292544u, AP_ATBUS_DRAM_CAS_TICKS);
  TEST_ASSERT_EQUAL_UINT64((ap_time_t)86169600000u,
                           AP_ATBUS_DRAM_REFRESH_PERIOD);
  TEST_ASSERT_EQUAL_UINT64((ap_time_t)53856000u, AP_ATBUS_IO_CH_RDY_MAX);

  /* Exact, not merely close: the base divides by each figure's denominator. */
  TEST_ASSERT_EQUAL_UINT64(0u, (ap_time_t)AP_TIME_BASE_HZ * 120u % 1000000000u);
  TEST_ASSERT_EQUAL_UINT64(0u, (ap_time_t)AP_TIME_BASE_HZ * 60u % 1000000000u);
  TEST_ASSERT_EQUAL_UINT64(0u, (ap_time_t)AP_TIME_BASE_HZ * 4u % 1000u);
  TEST_ASSERT_EQUAL_UINT64(0u, (ap_time_t)AP_TIME_BASE_HZ * 25u % 10000000u);

  /* RAS is twice CAS, which is the one relation between them the figures
   * state implicitly and a transposed pair would break. */
  TEST_ASSERT_EQUAL_UINT64(AP_ATBUS_DRAM_CAS_TICKS * 2u,
                           AP_ATBUS_DRAM_RAS_TICKS);
}

/* The per-row refresh interval each family's figures imply.
 *
 * The DS3000's comes out at 15.625 us, which is §2.4.6's "approximately 15
 * microseconds" and the fixed 15 us square wave this core models on the 2681's
 * OP3 — so the refresh source, which was modelled from §3.9 alone, is
 * confirmed to be the right interval for the memory behind it.
 *
 * The DS4000's comes out at 4 us, four times faster, which that source cannot
 * supply. Asserted so the discrepancy is a fact under test rather than a
 * sentence in a comment: it is `PROVISIONAL` and named on the plan. */
static void test_the_two_families_imply_different_refresh_intervals(void) {
  /* In nanoseconds, to keep the arithmetic readable. */
  const ap_time_t ns = (ap_time_t)AP_TIME_BASE_HZ / 1000000000u;

  const ap_time_t ds3000 =
      AP_ATBUS_DRAM_REFRESH_PERIOD / AP_ATBUS_DRAM_ROWS_DS3000 / ns;
  const ap_time_t ds4000 =
      AP_ATBUS_DRAM_REFRESH_PERIOD / AP_ATBUS_DRAM_ROWS_DS4000 / ns;

  TEST_ASSERT_EQUAL_UINT64(15625u, ds3000); /* 15.625 us */
  TEST_ASSERT_EQUAL_UINT64(4000u, ds4000);  /* 4.000 us */

  /* The DS3000 sits within a few percent of the modelled 15 us source. The
   * DS4000 is out by the row ratio, 1000/256 = 3.906 -- near four but not
   * four, so it is asserted as the ratio rather than rounded. */
  TEST_ASSERT_TRUE(ds3000 > 15000u && ds3000 < 16000u);
  TEST_ASSERT_EQUAL_UINT64(AP_ATBUS_DRAM_ROWS_DS4000 * ds4000,
                           AP_ATBUS_DRAM_ROWS_DS3000 * ds3000);
  TEST_ASSERT_TRUE(ds3000 * 1000u / ds4000 == 3906u);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_each_appendix_runs_its_bus_at_half_its_clock);
  RUN_TEST(test_the_two_appendices_agree_in_bus_clocks);
  RUN_TEST(test_the_memory_read_cycle_is_the_one_row_that_differs);
  RUN_TEST(test_a_cycle_time_is_a_duration_in_base_units);
  RUN_TEST(test_an_io_cycle_is_a_fixed_number_of_bus_clocks);
  RUN_TEST(test_a_wide_transfer_to_a_narrow_device_is_two_cycles);
  RUN_TEST(test_the_reset_instruction_resets_the_board_but_not_the_sio);
  RUN_TEST(test_the_at_windows_declare_a_figure_and_nothing_else_does);
  RUN_TEST(test_an_at_memory_read_costs_more_than_a_write);
  RUN_TEST(test_a_faster_processor_waits_more_clocks_for_the_same_card);
  RUN_TEST(test_an_at_device_read_costs_a_machine_more_than_memory);
  RUN_TEST(test_the_dram_figures_land_exactly_on_the_time_base);
  RUN_TEST(test_the_two_families_imply_different_refresh_intervals);
  return UNITY_END();
}

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
 * clocks, which is this machine's standing rule. 750 ns is 4950 base units
 * exactly -- 6.6 per nanosecond -- and an implementation that divided before
 * multiplying would return 4500, nine per cent short. */
static void test_a_cycle_time_is_a_duration_in_base_units(void) {
  const ap_atbus_timing_t *a = ap_atbus_timing(AP_ATBUS_SERIES_3000);
  const ap_time_t io = ap_atbus_access_time(a, AP_ATBUS_CYCLE_IO_8, true);

  TEST_ASSERT_EQUAL_UINT64(750ull * AP_TIME_BASE_HZ / 1000000000ull, io);
  TEST_ASSERT_EQUAL_UINT64(14850u, io);
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
  TEST_ASSERT_EQUAL_UINT64(io, ap_board_access_time(&board, 0x04D000u, true));
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

  const ap_time_t cycle = ap_board_access_time(&board, 0x04D000u, true);
  TEST_ASSERT_TRUE(cycle > 0u);

  /* Computed here the way the machine must: rounded up, because a device that
   * needs part of a clock holds the bus for all of it. */
  const uint64_t fast_clocks =
      (cycle + fast.cpu_clock.period - 1u) / fast.cpu_clock.period;
  const uint64_t slow_clocks =
      (cycle + slow.cpu_clock.period - 1u) / slow.cpu_clock.period;

  TEST_ASSERT_EQUAL_UINT64(19u, fast_clocks);
  TEST_ASSERT_EQUAL_UINT64(9u, slow_clocks);
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

  r = ap_m68030_access_read(&m.data_access, 0x04D000u,
                            AP_M68030_FC_SUPERVISOR_DATA);
  TEST_ASSERT_FALSE(r.fault);
  const uint32_t device_clocks = r.clocks;
  TEST_ASSERT_EQUAL_UINT(AP_M68030_MIN_BUS_CLOCKS, ram_clocks);
  TEST_ASSERT_EQUAL_UINT(19u, device_clocks);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_each_appendix_runs_its_bus_at_half_its_clock);
  RUN_TEST(test_the_two_appendices_agree_in_bus_clocks);
  RUN_TEST(test_the_memory_read_cycle_is_the_one_row_that_differs);
  RUN_TEST(test_a_cycle_time_is_a_duration_in_base_units);
  RUN_TEST(test_the_at_windows_declare_a_figure_and_nothing_else_does);
  RUN_TEST(test_an_at_memory_read_costs_more_than_a_write);
  RUN_TEST(test_a_faster_processor_waits_more_clocks_for_the_same_card);
  RUN_TEST(test_an_at_device_read_costs_a_machine_more_than_memory);
  return UNITY_END();
}

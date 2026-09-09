/* `008778-03` Table A-1 and Table B-1, transcribed. See ap_atbus.h. */

#include "board/ap_atbus.h"

/* Read from the page images, not from a text extraction: the figures that
 * matter here sit in table cells and in annotations inside timing diagrams, and
 * both are what OCR mangles first. */
static const ap_atbus_timing_t TIMING[] = {
    [AP_ATBUS_SERIES_3000] =
        {
            .series = AP_ATBUS_SERIES_3000,
            .name = "Series 3000",
            /* `#25` "12 MHz CLOCK Cycle Time ... 83", `#26` "BUS CLOCK Cycle
             * Time ... 166". */
            .clock_hz = 12000000u,
            .bus_clock_hz = 6000000u,
            .memory_read_ns = 666u,
            .memory_write_ns = 500u,
            .io_16_ns = 250u,
            .io_8_ns = 750u,
            /* Figure A-9's internal `* 3MHz`, half the 6 MHz bus clock. */
            .dma_clock_hz = 3000000u,
            /* 3 and 6 bus clocks at 166.67 ns -- §3.4's printed 500 ns and
             * 1 us exactly. */
            .io_16_cycle_ns = 500u,
            .io_8_cycle_ns = 1000u,
        },
    [AP_ATBUS_SERIES_4000] =
        {
            .series = AP_ATBUS_SERIES_4000,
            .name = "Series 4000",
            /* `#25` "16Mhz CLOCK Cycle Time ... 62", `#26` "BUS CLOCK Cycle
             * Time ... 125". */
            .clock_hz = 16000000u,
            .bus_clock_hz = 8000000u,
            .memory_read_ns = 375u,
            .memory_write_ns = 375u,
            .io_16_ns = 185u,
            .io_8_ns = 560u,
            /* Figure B-9's internal `* 4MHz`, half the 8 MHz bus clock. */
            .dma_clock_hz = 4000000u,
            /* The same 3 and 6 bus clocks at 125 ns. See the header on why the
             * count travels between the families and the nanoseconds do not. */
            .io_16_cycle_ns = 375u,
            .io_8_cycle_ns = 750u,
        },
};

const ap_atbus_timing_t *ap_atbus_timing(ap_atbus_series_t series) {
  if ((unsigned)series >= sizeof TIMING / sizeof TIMING[0]) {
    return nullptr;
  }
  return &TIMING[series];
}

static uint32_t nanoseconds(const ap_atbus_timing_t *timing,
                            ap_atbus_cycle_t cycle, bool read) {
  switch (cycle) {
  case AP_ATBUS_CYCLE_MEMORY:
    /* The one row where the direction matters, and on the Series 3000 it
     * matters by a whole bus clock. */
    return read ? timing->memory_read_ns : timing->memory_write_ns;
  case AP_ATBUS_CYCLE_IO_8:
    return timing->io_8_ns;
  case AP_ATBUS_CYCLE_IO_16:
    return timing->io_16_ns;
  }
  return 0u;
}

/* What a whole cycle costs, as against `nanoseconds()`'s printed figure.
 *
 * The two are the same for memory, where `#18` and `#30` *are* cycle times.
 * They differ for I/O, where the appendices give only the command width: §3.4
 * publishes the cycle and §2.4.2 gives it as a wait-state count. See the
 * header.
 *
 * Kept apart from `nanoseconds()` on purpose. That function feeds
 * `ap_atbus_centiclocks`, whose whole job is checking the two appendices
 * against each other **through the figures they print** -- folding a derived
 * cycle into it would make that check compare a derivation with itself. */
static uint32_t cycle_nanoseconds(const ap_atbus_timing_t *timing,
                                  ap_atbus_cycle_t cycle, bool read) {
  switch (cycle) {
  case AP_ATBUS_CYCLE_MEMORY:
    return read ? timing->memory_read_ns : timing->memory_write_ns;
  case AP_ATBUS_CYCLE_IO_8:
    return timing->io_8_cycle_ns;
  case AP_ATBUS_CYCLE_IO_16:
    return timing->io_16_cycle_ns;
  }
  return 0u;
}

ap_time_t ap_atbus_access_time(const ap_atbus_timing_t *timing,
                               ap_atbus_cycle_t cycle, bool read) {
  if (timing == nullptr) {
    return 0u;
  }
  /* Nanoseconds to base units: 6.6 of them per nanosecond. Multiplied before
   * dividing -- the other order truncates the .6 and loses 9% of every figure
   * here -- so the only rounding left is the one the table already did when it
   * printed 166 ns for a 166.67 ns period. */
  return (ap_time_t)cycle_nanoseconds(timing, cycle, read) * AP_TIME_BASE_HZ /
         1000000000u;
}

unsigned ap_atbus_centiclocks(const ap_atbus_timing_t *timing,
                              ap_atbus_cycle_t cycle, bool read) {
  if (timing == nullptr) {
    return 0u;
  }
  /* ns * Hz / 1e9, times 100. Done in one expression at 64 bits so neither the
   * scaling nor the division loses the halves the table is full of. */
  const uint64_t ns = nanoseconds(timing, cycle, read);
  return (unsigned)((ns * (uint64_t)timing->bus_clock_hz * 100u + 500000000u) /
                    1000000000u);
}

uint32_t ap_atbus_dma_transfer_ticks(ap_atbus_series_t series,
                                     uint32_t cpu_hz) {
  const ap_atbus_timing_t *timing = ap_atbus_timing(series);
  if (timing == nullptr || timing->dma_clock_hz == 0u || cpu_hz == 0u) {
    return 0u;
  }
  /* Four controller states expressed in processor clocks, as
   * `cpu_hz * states / dma_clock_hz`. Ordered so the multiply happens first:
   * 25,000,000 x 4 / 3,000,000 is 33 and (25,000,000 / 3,000,000) x 4 is 32,
   * and the second is wrong by a state and a third.
   *
   * It does not divide evenly on every model -- 4 states at 3 MHz is 1.333 us,
   * which is 33.33 clocks at 25 MHz and 26.67 at 20 MHz. Truncation is the
   * approximation, stated: an accumulator would carry the remainder and is
   * what this wants if a boot ever shows the drift, which at a third of a clock
   * per transfer would take some millions of them to reach one. */
  return (uint32_t)(((uint64_t)cpu_hz * AP_ATBUS_DMA_STATES) /
                    (uint64_t)timing->dma_clock_hz);
}

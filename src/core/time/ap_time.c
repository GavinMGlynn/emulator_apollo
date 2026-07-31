#include "time/ap_time.h"

bool ap_time_base_divides(uint32_t hz) {
  return hz != 0u && (AP_TIME_BASE_HZ % (uint64_t)hz) == 0u;
}

bool ap_clock_init(ap_clock_t *clk, uint32_t hz) {
  clk->hz = 0u;
  clk->period = 0u;
  if (!ap_time_base_divides(hz)) {
    return false;
  }
  clk->hz = hz;
  clk->period = AP_TIME_BASE_HZ / (uint64_t)hz;
  return true;
}

ap_time_t ap_clock_duration(const ap_clock_t *clk, uint64_t cycles) {
  return cycles * clk->period;
}

uint64_t ap_clock_cycles_in(const ap_clock_t *clk, ap_time_t duration) {
  return duration / clk->period;
}

ap_time_t ap_clock_align_up(const ap_clock_t *clk, ap_time_t t) {
  uint64_t rem = t % clk->period;
  return rem == 0u ? t : t + (clk->period - rem);
}

ap_time_t ap_clock_align_down(const ap_clock_t *clk, ap_time_t t) {
  return t - (t % clk->period);
}

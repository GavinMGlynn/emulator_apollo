/* Global time base for the multi-node machine.
 *
 * Why a common time base rather than CPU cycles: this emulator runs several
 * nodes of *different models* on one Apollo Token Ring. A DN3000 runs a 12 MHz
 * 68020, a DN3500 a 25 MHz 68030, a DN4500 a 33 MHz 68030 -- while the ring
 * itself is a fixed 12 Mbit/s domain shared by all of them. One ring bit time
 * is 83.333... ns, which is 2.0833... cycles of a 25 MHz CPU and 2.75 of a
 * 33 MHz CPU. No CPU's cycle is a legal unit of account for the machine.
 *
 * So time is counted in units of AP_TIME_BASE_HZ, chosen as the least common
 * multiple of every clock frequency in the machine. Every clock's period is
 * then an exact integer number of units and nothing accumulates rounding
 * error:
 *
 *     12 MHz -> 275 units      20 MHz -> 165 units
 *     25 MHz -> 132 units      33 MHz -> 100 units
 *     12 Mbit/s ring bit -> 275 units
 *
 * Discipline: AP_TIME_BASE_HZ is a *derived* constant. When a new clock domain
 * is added (a video dot clock, say) whose frequency does not divide it, the
 * base is recomputed as the new LCM. That changes the unit but not one bit of
 * emulated behaviour, because every period is derived from it rather than
 * written down. ap_clock_init() refuses a frequency the base does not divide
 * exactly, so an unrepresentable clock is a loud failure at construction time
 * and never a silent drift at run time.
 *
 * At 3.3 GHz a uint64_t spans ~177 years of emulated time; wrap is not a
 * concern the model needs to handle.
 */

#ifndef APOLLO_TIME_AP_TIME_H
#define APOLLO_TIME_AP_TIME_H

#include <stdbool.h>
#include <stdint.h>

/* LCM(12, 20, 25, 33 MHz) = 2^2 * 3 * 5^2 * 11 MHz = 3.3 GHz.
 * See docs/PROJECT_STATUS.md for which model clocks are confirmed and which
 * are still PROVISIONAL. */
#define AP_TIME_BASE_HZ UINT64_C(3300000000)

/* Absolute time, in AP_TIME_BASE_HZ units, since machine reset. */
typedef uint64_t ap_time_t;

/* Returned by a subsystem with no scheduled externally observable action.
 * Chosen so that a naive min() over next_event() values does the right thing. */
#define AP_TIME_NEVER UINT64_MAX

/* A clock domain: one frequency, and its exact period in base units. */
typedef struct {
  uint32_t hz;
  uint64_t period; /* base units per cycle; exact by construction */
} ap_clock_t;

/* True when AP_TIME_BASE_HZ is an exact integer multiple of hz, i.e. when hz
 * can be represented without drift. */
[[nodiscard]] bool ap_time_base_divides(uint32_t hz);

/* Initialise a clock domain. Returns false (leaving *clk zeroed) when hz is
 * zero or is not exactly divided by the time base -- the caller must treat
 * that as a configuration error, not round it away. */
[[nodiscard]] bool ap_clock_init(ap_clock_t *clk, uint32_t hz);

/* Duration of `cycles` cycles of this clock, in base units. */
[[nodiscard]] ap_time_t ap_clock_duration(const ap_clock_t *clk, uint64_t cycles);

/* Number of whole cycles of this clock that fit in `duration` base units.
 * Truncates: a partial cycle has not completed. */
[[nodiscard]] uint64_t ap_clock_cycles_in(const ap_clock_t *clk, ap_time_t duration);

/* First cycle boundary of this clock at or after absolute time `t`.
 *
 * This is the primitive that keeps mixed clock domains honest. A node may only
 * execute on its own cycle boundaries, so when the scheduler advances global
 * time to a ring event the node runs up to -- not past -- the last boundary
 * that has actually elapsed, and resumes at the boundary this returns. Nodes
 * are assumed to share a common reset instant at t = 0. */
[[nodiscard]] ap_time_t ap_clock_align_up(const ap_clock_t *clk, ap_time_t t);

/* Last cycle boundary of this clock at or before absolute time `t`. */
[[nodiscard]] ap_time_t ap_clock_align_down(const ap_clock_t *clk, ap_time_t t);

#endif /* APOLLO_TIME_AP_TIME_H */

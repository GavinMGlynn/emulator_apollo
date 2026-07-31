/* Global time base for the multi-node machine.
 *
 * Why a common time base rather than CPU cycles: this emulator runs several
 * nodes of *different models* on one Apollo Token Ring. A DN3000 runs a 12 MHz
 * 68020, a DN3500 a 25 MHz 68030, a DN4500 a 33 MHz 68030 -- while the ring
 * itself is a fixed 12 Mbit/s domain shared by all of them. One ring bit cell
 * is 83.33 ns, which is 2.0833... cycles of a 25 MHz CPU and 2.75 of a 33 MHz
 * CPU. No CPU's cycle is a legal unit of account for the machine.
 *
 * The ring contributes two clocks, not one. Its data rate is 12 Mbit/s, but the
 * physical layer is bi-phase encoded: "In the time it takes to transmit one bit
 * (this is a bit cell, or 83.33 nsec), two windows exist: the clock window and
 * the data window", and each node's phase-lock loops run at "the 24-MHz clock"
 * -- 010005-00 (Apollo Token Ring MAC and Physical Layer Protocols) section 3.2,
 * p.3-3. So the ring's line clock is 24 MHz and its data clock 12 MHz, and both
 * must be exactly representable.
 *
 * Time is therefore counted in units of AP_TIME_BASE_HZ, the least common
 * multiple of every clock frequency in the machine:
 *
 *     LCM(12, 20, 24, 25, 33 MHz) = 2^3 * 3 * 5^2 * 11 MHz = 6.6 GHz
 *
 *     12 MHz -> 550 units      20 MHz -> 330 units
 *     24 MHz -> 275 units      25 MHz -> 264 units
 *     33 MHz -> 200 units
 *     ring bit cell (12 Mbit/s) -> 550 units, of two 275-unit windows
 *
 * Discipline: AP_TIME_BASE_HZ is a *derived* constant. When a new clock domain
 * is added whose frequency does not divide it -- as the ring's 24 MHz line clock
 * did not divide the original 3.3 GHz base -- the base is recomputed as the new
 * LCM. That changes the unit but not one bit of emulated behaviour, because
 * every period is derived from it rather than written down. ap_clock_init()
 * refuses a frequency the base does not divide exactly, so an unrepresentable
 * clock is a loud failure at construction time and never a silent drift at run
 * time. A video dot clock is the next candidate to force a recomputation.
 *
 * At 6.6 GHz a uint64_t spans ~88 years of emulated time; wrap is not a concern
 * the model needs to handle.
 */

#ifndef APOLLO_TIME_AP_TIME_H
#define APOLLO_TIME_AP_TIME_H

#include <stdbool.h>
#include <stdint.h>

/* LCM(12, 20, 24, 25, 33 MHz) = 2^3 * 3 * 5^2 * 11 MHz = 6.6 GHz.
 * See docs/PROJECT_STATUS.md for which model clocks are confirmed and which
 * are still PROVISIONAL. */
#define AP_TIME_BASE_HZ UINT64_C(6600000000)

/* The ring's two clock domains, from 010005-00 section 3.2 p.3-3. Declared here
 * because the time base exists to represent them exactly, and a change to
 * either is a change to the base. */
#define AP_RING_DATA_HZ 12000000u /* NRZ data rate: one bit per 83.33 ns cell */
#define AP_RING_LINE_HZ 24000000u /* bi-phase window clock: two per bit cell */

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

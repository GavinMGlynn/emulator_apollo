/* Apollo interval timer: the MC6840 as the board wires it.
 *
 * `008778-03` §3.8 "Interval Time Clocks (MC6840)", at `010800` in the 64 MB
 * physical allocation of Table 2-8. The part is `device/ap_mc6840.h` and knows
 * nothing about Apollo; this is the three clock rates, the address decode, and
 * which interrupt line it raises.
 *
 * ## Where it sits, and why that was measured rather than assumed
 *
 * The boot PROM never touches the timer — a write trace over `010800`-`0108FF`
 * for six emulated seconds records nothing at all — so there was no firmware to
 * watch, as there had been for the interrupt controllers. What settled it was a
 * clean read of the region: `00 00 00 00 00 FF 00 FF ...`, even bytes zero and
 * odd bytes `00 00` then six `FF`s.
 *
 * That is the part on **odd addresses at stride 2**, `RS n` at
 * `010800 + 1 + 2n`: RS0 is "no operation" and reads zero, RS1 is the status
 * register with nothing pending and reads zero, and the remaining six are
 * counters and buffers reading `FF` because `[6840]` §4.1 says the latches
 * "default to $FFFF" when unwritten. A latch default from one manual showing
 * through a dump is a stronger check than any convention.
 *
 * Odd-byte placement is also the ordinary way a byte-wide peripheral lands on a
 * 68000 bus, so two independent things agree. Contrast a known-unmapped address,
 * which reads `FF` throughout (`FINDINGS.md` C10) — the zeros are what prove a
 * device answers here at all.
 *
 * ## The three rates
 *
 * `008778-03` §3.8 gives each timer its own input:
 *
 *   Timer 1  250 kHz    (4 us)
 *   Timer 2  125 kHz    (8 us)
 *   Timer 3  62.5 kHz   (16 us), prescalable by the part to 128 us
 *
 * All three divide `AP_TIME_BASE_HZ` exactly — 26400, 52800 and 105600 base
 * units per pulse — so this device needs no change to the time base. That is
 * checked at reset rather than assumed: `ap_timer_reset` refuses a rate the
 * base cannot represent instead of rounding it, which is `CLAUDE.md`'s rule for
 * a clock domain.
 */

#ifndef APOLLO_BOARD_AP_TIMER_H
#define APOLLO_BOARD_AP_TIMER_H

#include <stdbool.h>
#include <stdint.h>

#include "device/ap_mc6840.h"
#include "time/ap_time.h"

/* `008778-03` Table 2-8: "010800 - 0108FF  INTERVAL TIMER". */
#define AP_TIMER_ADDR 0x010800u
#define AP_TIMER_RANGE 0x100u

/* `008778-03` Table 2-3: "IRQO ... MC6840 Timer", priority 1 — the highest in
 * the machine. Confirmed by measurement: with the timer armed, the master
 * controller's in-service register reads `01`. `FINDINGS.md` C12. */
#define AP_TIMER_IRQ 0u

/* §3.8's three input rates. */
#define AP_TIMER1_HZ 250000u
#define AP_TIMER2_HZ 125000u
#define AP_TIMER3_HZ 62500u

typedef struct {
  ap_mc6840_t ptm;
  ap_clock_t clock[AP_MC6840_TIMERS];
  /* Absolute time each timer has been clocked up to. Kept per timer because the
   * three rates are different and a single cursor could only serve one. */
  ap_time_t clocked_to[AP_MC6840_TIMERS];
} ap_timer_t;

/* Reset the part and set up the three clock domains.
 *
 * Returns false if any rate is not exactly representable in the time base,
 * leaving the timer unusable rather than running at a rounded rate. That cannot
 * happen with the rates above and the current base; it is checked because the
 * base is a derived constant and a future clock could change it. */
[[nodiscard]] bool ap_timer_reset(ap_timer_t *timer);

/* Whether an address decodes to the timer, and to which register.
 *
 * False for an even address inside the range: the part is on the odd bytes, and
 * an even one reads zero from the other lane rather than aliasing a register.
 * Folding the two together would let a driver's word access land on a register
 * it never named. */
[[nodiscard]] bool ap_timer_decode(uint32_t address, ap_mc6840_rs_t *rs);

[[nodiscard]] uint8_t ap_timer_read(ap_timer_t *timer, uint32_t address);
void ap_timer_write(ap_timer_t *timer, uint32_t address, uint8_t value);

/* Advance every timer to absolute time `now`, issuing one clock pulse per
 * elapsed period of that timer's own rate.
 *
 * Idempotent for a `now` already reached, and monotonic: going backwards is
 * ignored rather than wrapping. Time is counted in base units throughout, never
 * in pulses, so a timer whose rate does not divide another's still lands on its
 * own boundaries exactly. */
void ap_timer_advance(ap_timer_t *timer, ap_time_t now);

/* The IRQ pin, to be wired to `AP_TIMER_IRQ` on the interrupt controllers. The
 * board does that wiring, not this module — the timer has no business knowing
 * what is listening. */
[[nodiscard]] bool ap_timer_irq(const ap_timer_t *timer);

#endif /* APOLLO_BOARD_AP_TIMER_H */

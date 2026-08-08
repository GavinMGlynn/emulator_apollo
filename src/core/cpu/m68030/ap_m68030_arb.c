#include "cpu/m68030/ap_m68030_arb.h"

#include <string.h>

/* Two clocks, §7.7.4's published maximum. See the header on why the maximum and
 * not some point inside the range. */
#define SYNC_DEPTH 2u
#define SYNC_MASK ((uint8_t)((1u << SYNC_DEPTH) - 1u))

void ap_m68030_arb_init(ap_m68030_arb_t *arb) {
  memset(arb, 0, sizeof *arb);
  arb->state = AP_M68030_ARB_STATE_0;
  arb->rmc = AP_M68030_RMC_NONE;
}

void ap_m68030_arb_set_request(ap_m68030_arb_t *arb, bool asserted) {
  arb->br = asserted;
}

void ap_m68030_arb_set_acknowledge(ap_m68030_arb_t *arb, bool asserted) {
  arb->bgack = asserted;
}

void ap_m68030_arb_set_rmc(ap_m68030_arb_t *arb, ap_m68030_rmc_t rmc) {
  arb->rmc = rmc;
}

void ap_m68030_arb_set_cycle_committed(ap_m68030_arb_t *arb, bool committed) {
  arb->cycle_committed = committed;
}

/* Whether a bus request may be acted on at all.
 *
 * §7.7.4: an indivisible sequence "causes the bus arbitration state machine to
 * ignore bus requests (assertions of BR) that occur after the first read cycle
 * of the read-modify-write sequence by not issuing bus grants". Not a delayed
 * grant -- the request is ignored, and if it is gone by the time the lock
 * clears there is nothing left to grant.
 *
 * §7.7.2 adds the other deferral, which is a deferral rather than an ignore:
 * "In the case an internal decision to execute another bus cycle, BG is
 * deferred until the bus cycle has begun." A request held across it is still
 * granted, because `br` is a level and not an edge. */
static bool may_grant(const ap_m68030_arb_t *arb) {
  return arb->rmc != AP_M68030_RMC_LOCKED && !arb->cycle_committed;
}

static ap_m68030_arb_state_t next_state(const ap_m68030_arb_t *arb) {
  switch (arb->state) {
  case AP_M68030_ARB_STATE_0:
    /* "Request R and acknowledge A keep the arbiter in state 0 as long as they
     * are both negated. When a request R is received, both grant G and signal T
     * are asserted (in state 1 at the top left)." */
    if (arb->r && may_grant(arb)) {
      return AP_M68030_ARB_STATE_1;
    }
    /* "As shown by the path from state 0 to state 4, BGACK alone can be used to
     * place the processor's external bus buffers in the high-impedance state,
     * providing single-wire arbitration capability."
     *
     * Note this path does not consult `may_grant`: it issues no grant. A device
     * that takes the bus by asserting BGACK alone is not asking permission, and
     * the manual describes it as a capability rather than a violation. */
    if (arb->a) {
      return AP_M68030_ARB_STATE_4;
    }
    return AP_M68030_ARB_STATE_0;

  case AP_M68030_ARB_STATE_1:
    /* "The next clock causes a change to state 2" -- unconditional. */
    return AP_M68030_ARB_STATE_2;

  case AP_M68030_ARB_STATE_2:
    /* "The bus arbiter remains in that state until acknowledge A is asserted or
     * request R is negated. Once either occurs, the arbiter changes to the
     * center state, state 3, and negates grant G." */
    if (arb->a || !arb->r) {
      return AP_M68030_ARB_STATE_3;
    }
    return AP_M68030_ARB_STATE_2;

  case AP_M68030_ARB_STATE_3:
    /* "The next clock takes the arbiter to state 4" -- unconditional. */
    return AP_M68030_ARB_STATE_4;

  case AP_M68030_ARB_STATE_4:
    /* "With acknowledge A asserted, the arbiter remains in state 4 until A is
     * negated or request R is again asserted. When A is negated, the arbiter
     * returns to the original state, state 0, and negates signal T."
     *
     * A negated is tested first because that is the one edge the prose names a
     * target for. */
    if (!arb->a) {
      return AP_M68030_ARB_STATE_0;
    }
    /* INFERRED. The prose says state 4 is left when "request R is again
     * asserted" but does not say where to, and Figure 7-61 did not survive the
     * scan. State 1 is the only state that reasserts G, and §7.7.3 says a grant
     * is what should happen: "If a BR is still pending after the assertion of
     * BGACK, another BG is asserted within a few clocks of the negation of BG".
     * Two passages agreeing on the output pin a third leaves unstated is as
     * close to transcription as this edge can get; it is marked so that a
     * legible copy of the figure can confirm or correct it. */
    if (arb->r && may_grant(arb)) {
      return AP_M68030_ARB_STATE_1;
    }
    return AP_M68030_ARB_STATE_4;
  }
  return AP_M68030_ARB_STATE_0;
}

/* G and T by state, from the prose that walks Figure 7-61. */
static void drive_outputs(ap_m68030_arb_t *arb) {
  switch (arb->state) {
  case AP_M68030_ARB_STATE_0:
    arb->bg = false;
    arb->three_state = false;
    break;
  case AP_M68030_ARB_STATE_1:
  case AP_M68030_ARB_STATE_2:
    arb->bg = true;
    arb->three_state = true;
    break;
  case AP_M68030_ARB_STATE_3:
  case AP_M68030_ARB_STATE_4:
    arb->bg = false;
    arb->three_state = true;
    break;
  }
}

static bool sync_advance(uint8_t *shift, bool pin) {
  /* Sample the pin into the register, then read the oldest bit of what is now
   * there. A level asserted before clock 1 therefore emerges at the end of
   * clock SYNC_DEPTH, and the state machine acts on it on clock SYNC_DEPTH + 1
   * -- two clocks of synchronisation and the edge after, exactly as §7.7.4
   * splits them.
   *
   * Reading the register *before* the shift instead costs one clock more than
   * the part takes, because the sample being read is then one older than the
   * register is deep. That is a whole clock of arbitration latency on every
   * request, and nothing but a test counting individual clocks finds it. */
  *shift = (uint8_t)((((unsigned)*shift << 1) | (pin ? 1u : 0u)) & SYNC_MASK);
  return (*shift & (1u << (SYNC_DEPTH - 1u))) != 0u;
}

void ap_m68030_arb_tick(ap_m68030_arb_t *arb) {
  /* ## The idle state, skipped because it is provably a no-op
   *
   * This runs on every bus tick, and a real boot arbitrates almost never -- a
   * whole Domain/OS boot reports **8 requests and 2 holds against 1.6 billion
   * ticks**. `perf` still put it at 9.6% of the run, second only to the bus
   * tick itself, because it is the cheapest possible work done the largest
   * possible number of times.
   *
   * With the machine in state 0, both pins low and both synchronisers empty,
   * every line below writes back what is already there:
   *
   *   - `r` and `a` are false, because each was taken from the top bit of a
   *     register that is zero -- so `next_state` returns state 0 again;
   *   - state 0's outputs are `bg = false` and `three_state = false`, which is
   *     what `drive_outputs` last wrote and what `ap_m68030_arb_init`'s
   *     `memset` leaves, so an arbiter in state 0 always already has them;
   *   - `sync_advance` shifts a zero into a zero register and returns false.
   *
   * So this is an idle-skip guard in the plan's sense: it names the subsystem's
   * no-op state rather than guessing that a tick "probably" does nothing.
   * Anything less than provable belongs nowhere near the reference core, and
   * the boot state hash is the check. */
  if (arb->state == AP_M68030_ARB_STATE_0 && !arb->br && !arb->bgack &&
      arb->br_sync == 0u && arb->bgack_sync == 0u) {
    return;
  }

  /* Transition on R and A as they already stood: "State changes occur on the
   * next rising edge of the clock after the internal signal is valid." */
  arb->state = next_state(arb);
  drive_outputs(arb);

  arb->r = sync_advance(&arb->br_sync, arb->br);
  arb->a = sync_advance(&arb->bgack_sync, arb->bgack);
}

bool ap_m68030_arb_bus_grant(const ap_m68030_arb_t *arb) {
  /* Figure 7-61's note, which is the part of the figure that did survive: "The
   * BG output will not be asserted while RMC is asserted."
   *
   * Applied at the pin rather than in the state machine, because that is where
   * the note applies it -- and because the distinction matters for the first
   * read cycle, during which the manual both asserts RMC and allows normal
   * arbitration. The machine may sit in state 1 with G internally asserted; the
   * pin still reads negated. */
  return arb->bg && arb->rmc == AP_M68030_RMC_NONE;
}

bool ap_m68030_arb_three_state(const ap_m68030_arb_t *arb) {
  return arb->three_state;
}

bool ap_m68030_arb_processor_is_master(const ap_m68030_arb_t *arb) {
  return !arb->three_state;
}

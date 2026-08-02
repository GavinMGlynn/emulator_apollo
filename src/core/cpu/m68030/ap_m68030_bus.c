/* MC68030 bus cycle state machine. See ap_m68030_bus.h for the citations. */

#include "cpu/m68030/ap_m68030_bus.h"

/* Signal levels for a given state. Read cycles follow `[030]` 7.3.1 pp. 7-31
 * ff., write cycles 7.3.2 pp. 7-38 ff.
 *
 * The two differ in three places, and none of them is cosmetic -- a device
 * decodes a transfer from these strobes:
 *
 *   read                                  write
 *   S1  AS and DS asserted                S1  AS and *DBEN* asserted
 *   S2  DBEN asserted                     S2  data driven; DSACK sampled at end
 *   S3  DSACK sampled at start            S3  *DS* asserted -- "indicating that
 *                                             the data is stable on the data bus"
 *   S5  AS, DS and DBEN all negated       S5  AS and DS negated, but "R/W,
 *                                             SIZ0-SIZ1, FC0-FC2, and DBEN also
 *                                             remain valid throughout S5"
 *
 * So DS moves from S1 to S3 and DBEN from S2 to S1, and DBEN outlives the cycle
 * on a write. The first version of this file asserted the read timing for both
 * and recorded the gap as a tail rather than pretending it was verified; this
 * closes it from 7.3.2 rather than by inference from 7.3.4's "same signals, in
 * the same sequence", which is about termination and not about direction.
 *
 * These are the levels *at the end of* the named state, which is what a device
 * sampling on a clock edge sees. The address, R/W, SIZ and FC stay valid
 * through S5 on both kinds and are not modelled as signals here, because they
 * are fields of the request rather than strobes. */
static void apply_signals(ap_m68030_bus_t *bus) {
  const bool read = bus->read;

  switch (bus->state) {
  case AP_M68030_S_IDLE:
    bus->ecs = bus->ocs = bus->as = bus->ds = bus->dben = false;
    break;
  case AP_M68030_S0:
    bus->ecs = true;
    bus->as = bus->ds = bus->dben = false;
    break;
  case AP_M68030_S1:
    bus->ecs = bus->ocs = false; /* negated during S1, both kinds */
    bus->as = true;
    bus->ds = read;    /* write asserts DS in S3 */
    bus->dben = !read; /* read asserts DBEN in S2 */
    break;
  case AP_M68030_S2:
    bus->as = true;
    bus->ds = read;
    bus->dben = true;
    break;
  case AP_M68030_S3:
  case AP_M68030_S4:
    bus->as = bus->ds = bus->dben = true;
    break;
  case AP_M68030_S5:
    bus->as = false;
    bus->ds = false;
    bus->dben = !read; /* held valid throughout S5 on a write */
    break;
  }
}

void ap_m68030_bus_set_rmc(ap_m68030_bus_t *bus, bool asserted) {
  bus->rmc = asserted;
}

void ap_m68030_bus_begin(ap_m68030_bus_t *bus, uint32_t address,
                         uint8_t function_code, ap_m68030_size_t size, bool read,
                         bool first_operand) {
  bus->address = address;
  bus->function_code = function_code;
  bus->size = size;
  bus->read = read;

  bus->state = AP_M68030_S_IDLE;
  bus->termination = AP_M68030_TERM_NONE;
  bus->clocks = 0;
  bus->wait_states = 0;
  bus->active = true;
  bus->complete = false;
  bus->advancing_to_s4 = false;
  bus->cbreq = false;
  bus->cback = false;
  bus->bursting = false;
  bus->burst_beats = 0;

  /* OCS accompanies ECS on the first external cycle of an operand operation
   * only; it is asserted in S0 along with ECS, so it is set here and cleared
   * by S1 like ECS. */
  bus->ocs = first_operand;
  bus->ecs = false;
  bus->as = bus->ds = bus->dben = false;
  /* RMC is deliberately *not* cleared here: it spans the read and the write
   * of one indivisible operation, so a cycle beginning inside one must not
   * drop it. `ap_m68030_bus_set_rmc` is what ends it. */
}

void ap_m68030_bus_request_burst(ap_m68030_bus_t *bus) {
  /* "Although the operation is synchronous, the burst mode is never used during
   * read-modify-write cycles" (§7.3.6). Refused here rather than at the
   * acceptance test below, so the request is never even made -- CBREQ is a pin,
   * and a model that asserted it and then ignored CBACK would be describing a
   * processor that changes its mind. */
  if (bus->rmc) {
    return;
  }
  bus->cbreq = true;
}

void ap_m68030_bus_acknowledge_burst(ap_m68030_bus_t *bus, bool acknowledged) {
  bus->cback = acknowledged;
}

void ap_m68030_bus_terminate(ap_m68030_bus_t *bus, ap_m68030_term_t term) {
  if (bus->active) {
    bus->termination = term;
  }
}

bool ap_m68030_bus_active(const ap_m68030_bus_t *bus) { return bus->active; }

bool ap_m68030_bus_tick(ap_m68030_bus_t *bus) {
  if (!bus->active) {
    return false;
  }

  bus->clocks++;

  /* One tick is one clock, which is the two states that clock contains. The
   * states are stepped in order rather than collapsed, because the manual
   * specifies the cycle in states and a translation layer between its
   * granularity and ours is where an off-by-a-half-clock would hide. */

  if (bus->state == AP_M68030_S_IDLE) {
    /* First clock of the cycle: S0 then S1. */
    bus->state = AP_M68030_S0;
    apply_signals(bus);
    bus->state = AP_M68030_S1;
    apply_signals(bus);
    return false;
  }

  if (bus->advancing_to_s4) {
    /* Final clock: S4 then S5. CIIN is sampled at the beginning of S4 and data
     * latched at its end; the negations happen in S5. */
    bus->state = AP_M68030_S4;
    apply_signals(bus);
    bus->state = AP_M68030_S5;
    apply_signals(bus);

    /* Left in S5 rather than reset to idle: S5's levels are what a device sees
     * as the cycle ends, and on a write DBEN is still asserted there. Jumping
     * straight to idle would erase that difference. begin() clears it. */
    bus->active = false;
    bus->complete = true;
    return true;
  }

  /* A burst under way transfers one long word per clock: "The processor
   * continues to accept data on every clock during which STERM is asserted
   * until the burst is complete or an abnormal termination occurs." AS, DS,
   * R/W, the address, the function code and the size are all "maintain[ed] ...
   * in their current state throughout the burst operation", so the strobes stay
   * as S3 left them rather than being re-driven from S0. */
  if (bus->bursting) {
    if (bus->termination == AP_M68030_TERM_BERR) {
      /* "or an abnormal termination occurs" -- the line fill stops short. */
      bus->state = AP_M68030_S5;
      apply_signals(bus);
      bus->active = false;
      bus->complete = true;
      bus->bursting = false;
      return true;
    }
    if (bus->termination != AP_M68030_TERM_STERM) {
      /* STERM absent this clock: the device is not ready, so this clock is a
       * wait state and the burst stands still rather than advancing. */
      bus->wait_states++;
      return false;
    }

    bus->burst_beats++;

    /* "If the MC68030 executes a full burst operation and fetches four long
     * words, CBREQ is negated after STERM is asserted for the third cycle,
     * indicating that the MC68030 only requests one more long word." */
    if (bus->burst_beats + 1 >= AP_M68030_BURST_BEATS) {
      bus->cbreq = false;
    }

    if (bus->burst_beats >= AP_M68030_BURST_BEATS) {
      bus->state = AP_M68030_S5;
      apply_signals(bus);
      bus->active = false;
      bus->complete = true;
      bus->bursting = false;
      return true;
    }
    return false;
  }

  /* Second clock, and every wait clock after it: S2 then S3.
   *
   * "If DSACKx is not recognized by the start of state 3 (S3), the processor
   * inserts wait states instead of proceeding to states 4 and 5." The
   * termination is therefore sampled between this clock's two states: present
   * means advance, absent means this whole clock was a wait state and S2/S3
   * repeat on the next one. */
  bus->state = AP_M68030_S2;
  apply_signals(bus);

  if (bus->termination == AP_M68030_TERM_NONE) {
    bus->wait_states++;
    bus->state = AP_M68030_S3;
    apply_signals(bus);
    return false;
  }

  bus->state = AP_M68030_S3;
  apply_signals(bus);

  /* STERM terminates here, giving the documented two-clock minimum cycle for a
   * 32-bit port (7.3.4 p. 7-48). DSACK carries on to S4/S5, which is why the
   * asynchronous minimum is three clocks. A bus error ends the cycle
   * immediately, like STERM, rather than transferring data. */
  /* "burst mode is only initiated if both of these signals are asserted for a
   * synchronous cycle" -- CBREQ and CBACK together, and only under STERM. The
   * first long word has just transferred, so the cycle stays open for up to
   * three more, one per clock. */
  if (bus->termination == AP_M68030_TERM_STERM && bus->cbreq && bus->cback) {
    bus->burst_beats = 1;
    bus->bursting = true;
    return false;
  }

  if (bus->termination == AP_M68030_TERM_STERM ||
      bus->termination == AP_M68030_TERM_BERR) {
    /* The cycle ends here, so report the post-cycle strobe levels rather than
     * leaving S3's asserted ones standing. */
    bus->state = AP_M68030_S5;
    apply_signals(bus);
    bus->active = false;
    bus->complete = true;
    return true;
  }

  bus->advancing_to_s4 = true;
  return false;
}

/* MC68030 bus cycle state machine. See ap_m68030_bus.h for the citations. */

#include "cpu/m68030/ap_m68030_bus.h"

/* Signal levels for a given state, `[030]` 7.3.1 pp. 7-31 ff.
 *
 * ECS is asserted in S0 only -- "for one-half clock" in the flowchart of
 * Figure 7-19 -- and negated during S1. AS and DS are asserted in S1 and held
 * until they are negated during S5. DBEN follows in S2.
 *
 * These are the levels *at the end of* the named state, which is what a device
 * sampling on a clock edge sees. S5 negates its three signals during the state,
 * so it leaves them low. */
static void apply_signals(ap_m68030_bus_t *bus) {
  switch (bus->state) {
  case AP_M68030_S_IDLE:
    bus->ecs = bus->ocs = bus->as = bus->ds = bus->dben = false;
    break;
  case AP_M68030_S0:
    bus->ecs = true;
    bus->as = bus->ds = bus->dben = false;
    break;
  case AP_M68030_S1:
    bus->ecs = bus->ocs = false; /* negated during S1 */
    bus->as = true;
    bus->ds = true;
    bus->dben = false;
    break;
  case AP_M68030_S2:
  case AP_M68030_S3:
    bus->as = bus->ds = bus->dben = true;
    break;
  case AP_M68030_S4:
    bus->as = bus->ds = bus->dben = true;
    break;
  case AP_M68030_S5:
    /* "The processor negates AS, DS, and DBEN during state 5 (S5)." The
     * address, R/W, SIZ and FC stay valid through S5 and are not modelled as
     * signals here because they are fields of the request, not strobes. */
    bus->as = bus->ds = bus->dben = false;
    break;
  }
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

  /* OCS accompanies ECS on the first external cycle of an operand operation
   * only; it is asserted in S0 along with ECS, so it is set here and cleared
   * by S1 like ECS. */
  bus->ocs = first_operand;
  bus->ecs = false;
  bus->as = bus->ds = bus->dben = false;
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

    bus->state = AP_M68030_S_IDLE;
    apply_signals(bus);
    bus->active = false;
    bus->complete = true;
    return true;
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
  if (bus->termination == AP_M68030_TERM_STERM ||
      bus->termination == AP_M68030_TERM_BERR) {
    bus->state = AP_M68030_S_IDLE;
    apply_signals(bus);
    bus->active = false;
    bus->complete = true;
    return true;
  }

  bus->advancing_to_s4 = true;
  return false;
}

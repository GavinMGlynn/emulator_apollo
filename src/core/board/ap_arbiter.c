#include "board/ap_arbiter.h"

#include <string.h>

void ap_arbiter_reset(ap_arbiter_t *arbiter) {
  memset(arbiter, 0, sizeof *arbiter);
  ap_m68030_arb_init(&arbiter->cpu);
  arbiter->selected = AP_ARBITER_PROCESSOR;
  arbiter->master = AP_ARBITER_PROCESSOR;
}

void ap_arbiter_request(ap_arbiter_t *arbiter, unsigned drq, bool asserted) {
  if (drq >= AP_ARBITER_REQUESTERS) {
    return;
  }
  uint8_t bit = (uint8_t)(1u << drq);
  if (asserted) {
    arbiter->request |= bit;
  } else {
    arbiter->request = (uint8_t)(arbiter->request & ~bit);
  }
}

int ap_arbiter_highest_requester(const ap_arbiter_t *arbiter) {
  /* "DRQO having the highest priority and DRQ7 having the lowest". */
  for (unsigned i = 0; i < AP_ARBITER_REQUESTERS; i++) {
    if ((arbiter->request & (1u << i)) != 0u) {
      return (int)i;
    }
  }
  return AP_ARBITER_PROCESSOR;
}

int ap_arbiter_master(const ap_arbiter_t *arbiter) { return arbiter->master; }

bool ap_arbiter_processor_may_run(const ap_arbiter_t *arbiter) {
  /* Two conditions, and they are not the same one. The processor must still be
   * driving the bus -- §7.7.4's T signal, which asserts as soon as a grant is
   * issued and not when it is taken up -- and no device may hold mastership.
   *
   * Checking only the second would let the processor run in the window between
   * grant and acknowledgement, which is precisely the window the manual spends
   * §7.7.3 describing. */
  return ap_m68030_arb_processor_is_master(&arbiter->cpu) &&
         arbiter->master == AP_ARBITER_PROCESSOR;
}

void ap_arbiter_tick(ap_arbiter_t *arbiter) {
  /* Step 1 of §7.7's sequence: "An external device asserts the bus request
   * signal." Wire-ORed, as §7.7.1 has it -- "This can be a wire-ORed signal ...
   * that indicates to the processor that some external device requires control
   * of the bus" -- so the processor sees one line however many devices ask. */
  int asking = ap_arbiter_highest_requester(arbiter);
  ap_m68030_arb_set_request(&arbiter->cpu, asking != AP_ARBITER_PROCESSOR);

  /* Step 3, and the part that belongs to the board: "1) EXTERNAL ARBITRATION
   * DETERMINES NEXT BUS MASTER, 2) NEXT BUS MASTER WAITS FOR CURRENT CYCLE TO
   * COMPLETE, 3) NEXT BUS MASTER ASSERTS BUS GRANT ACKNOWLEDGE (BGACK) TO
   * BECOME NEW MASTER."
   *
   * The device may only take the bus once the processor has granted it. */
  if (arbiter->master == AP_ARBITER_PROCESSOR) {
    if (ap_m68030_arb_bus_grant(&arbiter->cpu) &&
        asking != AP_ARBITER_PROCESSOR) {
      arbiter->selected = asking;
      arbiter->master = asking;
      ap_m68030_arb_set_acknowledge(&arbiter->cpu, true);
    }
  } else if ((arbiter->request & (1u << (unsigned)arbiter->master)) == 0u) {
    /* "BGACK should not be negated until all bus cycles required by the
     * alternate bus master are completed. Bus mastership terminates at the
     * negation of BGACK." The device dropping its request is what ends it. */
    arbiter->master = AP_ARBITER_PROCESSOR;
    arbiter->selected = AP_ARBITER_PROCESSOR;
    ap_m68030_arb_set_acknowledge(&arbiter->cpu, false);
  }

  ap_m68030_arb_tick(&arbiter->cpu);
}

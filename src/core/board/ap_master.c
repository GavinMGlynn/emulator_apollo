/* An I/O adapter's route to the arbiter. See ap_master.h for §2.4.7 in full. */

#include "board/ap_master.h"

#include <string.h>

void ap_master_init(ap_master_t *port, unsigned unit, unsigned channel,
                    unsigned drq) {
  memset(port, 0, sizeof *port);
  port->unit = unit;
  port->channel = channel;
  port->drq = drq;
  port->state = AP_MASTER_IDLE;
}

void ap_master_set_request(ap_master_t *port, bool asserted) {
  port->request = asserted;
}

void ap_master_set_master_l(ap_master_t *port, bool asserted) {
  port->master_l = asserted;
}

static bool in_cascade(const ap_master_t *port, const ap_i8237_t *dma) {
  return ap_i8237_mode_of(dma, port->channel) == AP_I8237_MODE_CASCADE;
}

void ap_master_tick(ap_master_t *port, ap_i8237_t *dma,
                    ap_arbiter_t *arbiter) {
  /* The adapter's DRQ is a pin on the controller, not a wire to the arbiter.
   * Driven every clock rather than on the edge, because it is a level. */
  ap_i8237_set_request_pin(dma, port->channel, port->request);

  /* "until it releases the DRQx and MASTER.L signals" -- both. Once the board
   * has acknowledged, the adapter's claim on the bus outlives its DRQ line, so
   * the request the arbiter sees is held here rather than passed through. */
  const bool holding = port->state == AP_MASTER_ACKNOWLEDGED ||
                       port->state == AP_MASTER_OWNS;
  const bool claiming = holding ? (port->request || port->master_l) : false;

  /* Asking the part rather than the pin: `ap_i8237_service_pending` honours the
   * mask register and the controller-disable bit, so a masked channel does not
   * reach the arbiter however hard the adapter pulls its DRQ line. */
  const bool selected =
      ap_i8237_service_pending(dma) == (int)port->channel;

  ap_arbiter_request(arbiter, port->drq,
                     claiming || (port->request && selected));

  switch (port->state) {
  case AP_MASTER_IDLE:
    if (port->request) {
      port->state = AP_MASTER_REQUESTING;
    }
    break;

  case AP_MASTER_REQUESTING:
    if (!port->request) {
      port->state = AP_MASTER_IDLE;
      break;
    }
    /* "the system board asserts DACKx.L". The board can only acknowledge a
     * channel it has actually won the bus for, which is the arbiter's answer
     * and not this module's: `ap_arbiter_master` is the device that has
     * asserted BGACK, so DACK follows mastership rather than preceding it. */
    if (ap_arbiter_master(arbiter) == (int)port->drq && selected) {
      port->state = AP_MASTER_ACKNOWLEDGED;
    }
    break;

  case AP_MASTER_ACKNOWLEDGED:
    /* "and then asserting the MASTER.L signal". Cascade mode is what makes the
     * bus the adapter's rather than the controller's, so ownership needs it;
     * without it the acknowledgement is an ordinary DMA cycle's, and the
     * transfer that follows is the controller's business. */
    if (port->master_l && in_cascade(port, dma)) {
      port->state = AP_MASTER_OWNS;
      break;
    }
    if (!port->request && !port->master_l) {
      port->state = AP_MASTER_IDLE;
    }
    break;

  case AP_MASTER_OWNS:
    /* "has full ownership of the bus until it releases the DRQx and MASTER.L
     * signals" -- so one of the two still asserted is still ownership. */
    if (!port->request && !port->master_l) {
      port->state = AP_MASTER_IDLE;
    }
    break;
  }
}

ap_master_state_t ap_master_state(const ap_master_t *port) {
  return port->state;
}

bool ap_master_acknowledged(const ap_master_t *port) {
  return port->state == AP_MASTER_ACKNOWLEDGED || port->state == AP_MASTER_OWNS;
}

bool ap_master_owns_bus(const ap_master_t *port) {
  return port->state == AP_MASTER_OWNS;
}

bool ap_master_aen_inhibited(const ap_master_t *port) {
  /* The signal, not the state: MASTER.L asserted is what inhibits AEN, and the
   * manual gives no other condition. */
  return port->master_l;
}

bool ap_master_controllers_may_drive(const ap_master_t *port,
                                     const ap_i8237_t *dma) {
  return !in_cascade(port, dma);
}

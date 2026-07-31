/* The shared arbitration point: one bus, several masters, one priority.
 *
 * This is what makes contention *emergent* rather than modelled. Nothing here
 * computes a delay or adds a penalty; masters ask, one wins, and the losers --
 * including the processor -- simply cannot run a cycle until it releases. Any
 * timing difference that follows is a consequence of that and not a figure
 * anyone chose.
 *
 * ## Why the processor is a claimant and not the owner
 *
 * `[030]` §7.7: "the bus controller in the MC68030 manages the bus arbitration
 * signals so that the processor has the lowest priority". A model where the CPU
 * runs and devices interrupt it cannot express contention at all; one where the
 * CPU is the lowest-priority claimant of a shared resource gets it for free.
 *
 * The processor's own side of the protocol is `cpu/m68030/ap_m68030_arb.h` --
 * the BR/BG/BGACK state machine of §7.7.4. This module is the *external*
 * circuitry that machine expects, and §7.7 says outright that it is external:
 * "Systems having several devices that can become bus master require external
 * circuitry to assign priorities to the device so that, when two or more
 * external devices attempt to become bus master at the same time, the one
 * having the highest priority becomes bus master first."
 *
 * ## The priority
 *
 * `008778-03` §2.4.6 on the AT bus request lines: "They are prioritized, with
 * DRQO having the highest priority and DRQ7 having the lowest priority." So the
 * order is DRQ0 through DRQ7, and then the processor beneath all of them.
 *
 * ## What is not asserted here
 *
 * `008778-03` §2.4.7 describes how an I/O adapter takes the bus -- a DMA channel
 * programmed in cascade mode, then MASTER.L asserted after DACK -- and the
 * Series 4000's Master Request Register alternative. Neither route is modelled:
 * both need a transfer to be running, and the register's "particular bit" is
 * unnamed in the manual and unreachable in the oracle (`FINDINGS.md` C10 found
 * the Master Request Register absent). What is here is the arbitration itself,
 * which both routes end in.
 */

#ifndef APOLLO_BOARD_AP_ARBITER_H
#define APOLLO_BOARD_AP_ARBITER_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_arb.h"

/* DRQ0 through DRQ7. */
#define AP_ARBITER_REQUESTERS 8u

/* The processor, as a master. Numerically below every device because it is
 * below every device in priority. */
#define AP_ARBITER_PROCESSOR (-1)

typedef struct {
  /* The processor's own arbitration state machine. Driven, not reimplemented:
   * the board asserts BR and BGACK on it and reads BG back, which is exactly
   * the three-wire protocol §7.7 describes. */
  ap_m68030_arb_t cpu;

  /* The request lines, one bit per DRQ. */
  uint8_t request;

  /* Which device the external priority encoder has selected, or
   * `AP_ARBITER_PROCESSOR` when none is claiming. This is the "external
   * arbitration determines next bus master" step of Figure 7-59. */
  int selected;

  /* Which device actually holds the bus, having asserted BGACK. Separate from
   * `selected` because §7.7.3 puts real time between them: a grant is offered,
   * and mastership begins only when it is acknowledged. Collapsing the two
   * would make every handover instantaneous and delete the contention this
   * module exists to produce. */
  int master;
} ap_arbiter_t;

void ap_arbiter_reset(ap_arbiter_t *arbiter);

/* Drive one DRQ line. */
void ap_arbiter_request(ap_arbiter_t *arbiter, unsigned drq, bool asserted);

/* One bus clock. Runs the processor's arbitration machine and the board's own
 * handshake around it. */
void ap_arbiter_tick(ap_arbiter_t *arbiter);

/* Who holds the bus: `AP_ARBITER_PROCESSOR`, or a DRQ index. */
[[nodiscard]] int ap_arbiter_master(const ap_arbiter_t *arbiter);

/* Whether the processor may run a bus cycle this clock.
 *
 * This is the whole point of the module, and it is a *question the CPU asks*
 * rather than a stall anyone imposes. A processor that cannot run is not being
 * penalised; it is losing an arbitration. */
[[nodiscard]] bool ap_arbiter_processor_may_run(const ap_arbiter_t *arbiter);

/* The highest-priority device currently asking, or `AP_ARBITER_PROCESSOR`. */
[[nodiscard]] int ap_arbiter_highest_requester(const ap_arbiter_t *arbiter);

#endif /* APOLLO_BOARD_AP_ARBITER_H */

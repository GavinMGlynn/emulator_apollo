/* MC68030 bus arbitration control unit: `[030]` §7.7.
 *
 * The protocol by which something other than the processor becomes bus master.
 * Three signals and a five-state machine:
 *
 *   1. An external device asserts bus request (BR).
 *   2. The processor asserts bus grant (BG), promising the bus at the end of
 *      the current cycle.
 *   3. The device asserts bus grant acknowledge (BGACK) and *is* the master
 *      until it negates it.
 *
 * §7.7: "the bus controller in the MC68030 manages the bus arbitration signals
 * so that the processor has the lowest priority". That inversion is the whole
 * point of the module. A model where the CPU runs and devices interrupt it
 * cannot express contention; one where the CPU is the lowest-priority claimant
 * of a shared resource gets contention for free, which is what this project
 * means by emergent.
 *
 * ## Where Figure 7-61 went
 *
 * The state diagram is a figure, and the figure did not survive the scan --
 * §7.7.4's page carries its legend (R, A, G, T, and the RMC note) and nothing
 * else. The states below are transcribed from the *prose* that walks the same
 * diagram over the following page, which names all five states, both outputs in
 * each, and every edge but one. This is the same recovery `docs/references/`
 * records for Figures 9-9 and 9-35: prose describing a lost figure is still the
 * manual, and is a far better source than a guess at what the figure showed.
 *
 * The one edge the prose does not give a target for is marked `INFERRED` at its
 * site, with the two passages it is inferred from.
 *
 * ## R and A are not BR and BGACK
 *
 * §7.7.4: "input signals labeled R and A are internally synchronized versions of
 * the BR and BGACK signals". The pins are asynchronous; the state machine sees
 * them only after synchronisation, and "State changes occur on the next rising
 * edge of the clock after the internal signal is valid". So a request costs
 * synchronisation *plus* an edge before any grant appears, and a caller that
 * expects BG in the same clock as BR has mismodelled the part.
 *
 * All signals here are asserted-true, as the manual's own figure legend has
 * them: "All signals are shown in positive logic (active high), regardless of
 * their true active voltage level." Inverting at the pin is the board's job.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_ARB_H
#define APOLLO_CPU_M68030_AP_M68030_ARB_H

#include <stdbool.h>
#include <stdint.h>

/* §7.7.4, walked in prose. G is BG and T is the three-state control: "If T is
 * true, the address, data, and control buses are placed in the high-impedance
 * state after the next rising edge following the negation of AS and RMC."
 *
 * Numbered as the manual numbers them so a state here can be found in the text
 * without a translation table. */
typedef enum {
  /* "State 0, at the top center of the diagram, in which G and T are both
   * negated, is the state of the bus arbiter while the processor is bus
   * master." */
  AP_M68030_ARB_STATE_0 = 0,
  /* "When a request R is received, both grant G and signal T are asserted (in
   * state 1 at the top left)." */
  AP_M68030_ARB_STATE_1,
  /* "The next clock causes a change to state 2, at the lower left, in which G
   * and T are held." */
  AP_M68030_ARB_STATE_2,
  /* "the arbiter changes to the center state, state 3, and negates grant G." */
  AP_M68030_ARB_STATE_3,
  /* "The next clock takes the arbiter to state 4, at the upper right, in which
   * grant G remains negated and signal T remains asserted." */
  AP_M68030_ARB_STATE_4,
} ap_m68030_arb_state_t;

/* How far into an indivisible read-modify-write the processor is.
 *
 * Three values rather than a flag because §7.7.4 draws the distinction itself,
 * and it is not one a bool can carry. The RMC pin is asserted for the *whole*
 * sequence -- it is what tells the board the bus is locked, and BG stays
 * negated throughout, per Figure 7-61's note. But the *state machine's* inhibit
 * is narrower: it "ignore[s] bus requests (assertions of BR) that occur after
 * the first read cycle of the read-modify-write sequence".
 *
 * So the two halves of a locked sequence differ in what they leave behind. A
 * request arriving during the first read still walks the machine to its grant
 * states, and BG appears the moment RMC negates. One arriving after is not
 * acted on at all. Flattening the two would make every locked sequence behave
 * like whichever half was chosen.
 *
 * The first read cycle is also where the documented escape lives, and it is not
 * a bus grant: "One way for an alternate bus master to force the MC68030 to
 * release the bus applies only to the first read cycle ... a normal relinquish
 * and retry operation (asserting BERR, HALT, and BR at the same time) is used."
 * That path is the bus controller's, not this module's, and is why the note on
 * BG can hold without exception. */
typedef enum {
  AP_M68030_RMC_NONE = 0,   /* no read-modify-write in progress */
  AP_M68030_RMC_FIRST_READ, /* RMC asserted; arbitration still allowed */
  AP_M68030_RMC_LOCKED,     /* past the first read: bus requests ignored */
} ap_m68030_rmc_t;

typedef struct {
  ap_m68030_arb_state_t state;

  /* The pins, as the board drives them. Asynchronous: written at any time, and
   * seen by the state machine only through the synchroniser. */
  bool br;
  bool bgack;

  /* §7.7.4: "all asynchronous inputs to the MC68030 are internally synchronized
   * in a maximum of two cycles of the processor clock". Shift registers, one
   * bit per clock, least-significant bit is the oldest sample.
   *
   * PROVISIONAL: two clocks is the published *maximum*, not the delay any
   * particular edge sees -- where the edge falls relative to the clock decides
   * it, and the manual publishes only the bound. `CLAUDE.md`'s rule for a
   * quantity published as a range is to model the documented value, say so
   * here, and name it in `docs/PROJECT_STATUS.md`. A point number invented to
   * split the range would be worse than the bound, because it would look
   * measured. */
  uint8_t br_sync;
  uint8_t bgack_sync;

  /* R and A: the synchronised versions the state machine actually sees. */
  bool r;
  bool a;

  /* Outputs, valid after the tick that produced them. */
  bool bg;
  bool three_state;

  /* Not pins: the processor's own state, which gates when a grant may be
   * issued. */
  ap_m68030_rmc_t rmc;

  /* §7.7.2: "In the case an internal decision to execute another bus cycle, BG
   * is deferred until the bus cycle has begun." True between the decision and
   * the cycle actually starting. */
  bool cycle_committed;
} ap_m68030_arb_t;

void ap_m68030_arb_init(ap_m68030_arb_t *arb);

/* Drive the pins. Asynchronous -- these may be called at any point, and take
 * effect only once the synchroniser has passed them through. */
void ap_m68030_arb_set_request(ap_m68030_arb_t *arb, bool asserted);
void ap_m68030_arb_set_acknowledge(ap_m68030_arb_t *arb, bool asserted);

void ap_m68030_arb_set_rmc(ap_m68030_arb_t *arb, ap_m68030_rmc_t rmc);

/* §7.7.2's deferral: set between the processor deciding to run a bus cycle and
 * that cycle reaching S0. */
void ap_m68030_arb_set_cycle_committed(ap_m68030_arb_t *arb, bool committed);

/* One processor clock: the state machine transitions on the synchronised inputs
 * as they stood, and then the synchroniser advances. That order is §7.7.4's
 * "State changes occur on the next rising edge of the clock after the internal
 * signal is valid" -- valid first, acted on next. */
void ap_m68030_arb_tick(ap_m68030_arb_t *arb);

/* BG, the pin. Negated whenever the bus is locked, whatever the state:
 * Figure 7-61's own note is "The BG output will not be asserted while RMC is
 * asserted." */
[[nodiscard]] bool ap_m68030_arb_bus_grant(const ap_m68030_arb_t *arb);

/* T: the buses go high-impedance. */
[[nodiscard]] bool ap_m68030_arb_three_state(const ap_m68030_arb_t *arb);

/* Whether the processor may run a bus cycle of its own. False from the moment T
 * asserts, which is state 1 -- the processor stops driving the bus when it
 * grants, not when the grant is acknowledged. */
[[nodiscard]] bool ap_m68030_arb_processor_is_master(const ap_m68030_arb_t *arb);

#endif /* APOLLO_CPU_M68030_AP_M68030_ARB_H */

/* The ring medium: what joins node cores into a ring.
 *
 * ## Why this interface is as narrow as it is
 *
 * The plan requires it to be "narrow enough that a process-separated transport
 * can be added later without touching node cores". That constrains the shape
 * more than it looks: everything crossing this boundary must be *per bit clock*
 * and *by value*. A node hands the medium the cell it is driving and asks what
 * arrived; nothing else passes, and no node holds a pointer to another. A
 * transport that carried cells between processes would then need to carry one
 * small value per node per bit time and nothing else.
 *
 * That is also why `advance` is a single call over the whole ring rather than
 * per node. The ring is one clock domain -- `[MAC]` §3.1 calls the network
 * "continuously synchronous", with every node "responsible for maintaining
 * clock and bit synchronization" -- so a per-node step would invite callers to
 * advance nodes at different rates, which is not a thing this network can do.
 * One call, all nodes, one bit time.
 *
 * ## Topology
 *
 * Nodes occupy slots `0..count-1` and the signal flows from slot `i` to slot
 * `i+1`, wrapping. Slot order *is* the physical cable order, which matters
 * because `[MAC]` §3.3.1's PLL relationship is between adjacent nodes: "a
 * node's receive phase-lock loop will always be synchronized to the preceding
 * node's transmit phase-lock loop".
 *
 * ## Bypass
 *
 * A bypassed node is not skipped by rerouting the ring around it -- it stays in
 * the slot order and its relays pass the signal through, which is what `[MAC]`
 * §3.5 describes. It also receives its own transmission, since the same relays
 * join its transmit output to its receive input. Both halves matter: the first
 * keeps the ring intact, the second is what makes a loopback self-test
 * possible, and the ring firmware's self-test is this controller's first real
 * test.
 */

#ifndef APOLLO_RING_AP_RING_MEDIUM_H
#define APOLLO_RING_AP_RING_MEDIUM_H

#include <stdbool.h>
#include <stdint.h>

#include "ring/ap_ring_phy.h"

/* Enough for the ring sizes this project runs. `[PLAN]` records real rings of
 * "well over a hundred nodes", and nothing here scales with the bound, but a
 * fixed array keeps the core allocation-free -- which it is throughout. */
#define AP_RING_MAX_NODES 64u

/* Longest cable a link may model, in bit times. `[MAC]` §3.4 puts the maximum
 * between nodes at 1 km.
 *
 * **That is 51 bit times at 12 Mbit/s, not the 60 this said** (corrected
 * 2026-08-21 from Table A-1, which nothing here had read). The cable's own
 * specification gives **velocity of propagation 78%** and **delay 1.3 nsec/ft
 * maximum**, and those agree with each other -- 78% implies 1.30 ns/ft. A
 * kilometre is then 4.27 us, which is 51.2 bit times at 83.33 ns each.
 *
 * The old figure came from assuming a *typical* coaxial velocity factor rather
 * than looking one up: 60 bit times needs 0.667c, which is ordinary RG-59 and
 * not what Apollo specifies. The lesson is the cheap one -- the number was in
 * Appendix A of a manual this project cites constantly, and was estimated
 * instead.
 *
 * 64 still covers the documented maximum, and with more margin than the
 * comment used to claim: 51 rather than 60 against a bound of 64. No behaviour
 * changes; the constant was already large enough. */
#define AP_RING_MAX_CABLE_BITS 64u

typedef struct {
  bool attached;
  ap_ring_bypass_t bypass;
  /* What this node is driving onto its output this bit time. */
  ap_ring_cell_t driving;
  /* What arrived at its input, settled by the last `advance`. */
  ap_ring_cell_t received;

  /* The cable *leaving* this slot, as a delay line. `[MAC]` §3.3 counts "cable
   * plant" among the static elements contributing ring delay, alongside the
   * connected nodes -- and on real hardware the cable dominates. Without it a
   * ring of fewer than nine stations is shorter than its own nine-bit token,
   * which is not a limit any physical ring has.
   *
   * **The hardware agrees that a short ring is a real case, and gives the
   * controller a switch for it.** `002398-04` p. 8-41 documents a `DELAY` bit in
   * the transmit command register: "Enable an additional **7 bit delay** into
   * the length of the network. This may be required to support the recirculation
   * of the token, **which is 9 bits**." So the token's length is confirmed from
   * a second source, and the remedy this field models by hand -- lengthening the
   * ring until the token fits -- is one a real controller could apply in a
   * single bit. That controller is the DN4xx's; whether the DS3000's gate array
   * has the same switch is not established here.
   *
   * Zero by default, so a ring that does not care about cable length behaves
   * exactly as it did before this existed. */
  unsigned cable_bits;
  ap_ring_cell_t line[AP_RING_MAX_CABLE_BITS];
  unsigned line_head;
} ap_ring_node_t;

typedef struct {
  ap_ring_node_t node[AP_RING_MAX_NODES];
  unsigned slots;   /* how many slots are in the cable order */
  uint64_t bit_time; /* bit clocks since reset, for probes and traces */
} ap_ring_medium_t;

void ap_ring_medium_init(ap_ring_medium_t *m);

/* Attach a node at the next free slot; returns its slot or -1 when full.
 * Slots are handed out in order and a detached slot is reused, so a ring that
 * loses and regains a node keeps its cable order. */
[[nodiscard]] int ap_ring_medium_attach(ap_ring_medium_t *m);

void ap_ring_medium_detach(ap_ring_medium_t *m, int slot);

/* Whether a slot currently holds a node. */
[[nodiscard]] bool ap_ring_medium_attached(const ap_ring_medium_t *m, int slot);

/* The lowest attached slot, or -1 on an empty segment. Its use is to name one
 * node as the cable's stepper when several share a medium inside one process:
 * a shared medium advanced by every node on it would advance once per node per
 * bit time. Not a hardware concept -- real cable steps itself -- which is why
 * it is a query here rather than state on the medium. */
[[nodiscard]] int ap_ring_medium_first_slot(const ap_ring_medium_t *m);

/* Take a node in or out of the ring, `[MAC]` §3.5. */
void ap_ring_medium_set_bypass(ap_ring_medium_t *m, int slot, bool bypassed);

/* The cell this node drives during the next bit time. */
void ap_ring_medium_transmit(ap_ring_medium_t *m, int slot,
                             ap_ring_cell_t cell);

/* What arrived at this node's receiver, as of the last `advance`. */
[[nodiscard]] ap_ring_cell_t ap_ring_medium_receive(const ap_ring_medium_t *m,
                                                    int slot);

/* Set the length of the cable leaving `slot`, in bit times. Refused above
 * `AP_RING_MAX_CABLE_BITS`; a caller asking for more has misread the medium
 * rather than found a longer cable. */
void ap_ring_medium_set_cable_bits(ap_ring_medium_t *m, int slot,
                                   unsigned bits);

/* One bit clock for the whole ring. Every node's driven cell moves to the
 * next attached slot's receiver; a bypassed node passes its input through to
 * its output instead of driving, and hears its own transmission. */
void ap_ring_medium_advance(ap_ring_medium_t *m);

/* The ring's total delay in hundredths of a bit: each participating node
 * contributes its elastic-store delay. `[MAC]` §3.3 requires the total to be
 * "exactly an integral ... number of bit-times", so a caller building a ring
 * can ask whether the one it built is stable. Bypassed nodes contribute
 * nothing: their relays are a piece of cable, not a retiming element. */
[[nodiscard]] int ap_ring_medium_delay_centibits(const ap_ring_medium_t *m,
                                                 int per_node_centibits);

/* The ring's circumference in whole bit times: every participating node's own
 * bit of delay plus every cable it drives. This is the figure that must exceed
 * a token's width for the ring to carry one. */
[[nodiscard]] unsigned ap_ring_medium_circumference_bits(
    const ap_ring_medium_t *m);

/* Whether that total is a whole number of bit-times, which is `[MAC]` §3.3's
 * stability condition stated directly. */
[[nodiscard]] bool ap_ring_medium_stable(const ap_ring_medium_t *m,
                                         int per_node_centibits);

#endif /* APOLLO_RING_AP_RING_MEDIUM_H */

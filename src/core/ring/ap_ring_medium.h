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

typedef struct {
  bool attached;
  ap_ring_bypass_t bypass;
  /* What this node is driving onto its output this bit time. */
  ap_ring_cell_t driving;
  /* What arrived at its input, settled by the last `advance`. */
  ap_ring_cell_t received;
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

/* Take a node in or out of the ring, `[MAC]` §3.5. */
void ap_ring_medium_set_bypass(ap_ring_medium_t *m, int slot, bool bypassed);

/* The cell this node drives during the next bit time. */
void ap_ring_medium_transmit(ap_ring_medium_t *m, int slot,
                             ap_ring_cell_t cell);

/* What arrived at this node's receiver, as of the last `advance`. */
[[nodiscard]] ap_ring_cell_t ap_ring_medium_receive(const ap_ring_medium_t *m,
                                                    int slot);

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

/* Whether that total is a whole number of bit-times, which is `[MAC]` §3.3's
 * stability condition stated directly. */
[[nodiscard]] bool ap_ring_medium_stable(const ap_ring_medium_t *m,
                                         int per_node_centibits);

#endif /* APOLLO_RING_AP_RING_MEDIUM_H */

/* The ring medium. See `ap_ring_medium.h` for why the interface is this
 * narrow and why `advance` covers the whole ring at once. */

#include "ring/ap_ring_medium.h"

#include <stddef.h>

void ap_ring_medium_init(ap_ring_medium_t *m) {
  for (unsigned i = 0; i < AP_RING_MAX_NODES; i++) {
    m->node[i] = (ap_ring_node_t){0};
  }
  m->slots = 0u;
  m->bit_time = 0u;
}

static bool valid(const ap_ring_medium_t *m, int slot) {
  return slot >= 0 && (unsigned)slot < m->slots;
}

int ap_ring_medium_attach(ap_ring_medium_t *m) {
  /* A freed slot first, so the cable order survives a node leaving and
   * returning -- which is what the plan's node insertion and removal item will
   * exercise, and the order matters because the PLL relationship is between
   * *adjacent* nodes. */
  for (unsigned i = 0; i < m->slots; i++) {
    if (!m->node[i].attached) {
      m->node[i] = (ap_ring_node_t){.attached = true};
      return (int)i;
    }
  }
  if (m->slots >= AP_RING_MAX_NODES) {
    return -1;
  }
  const unsigned slot = m->slots++;
  m->node[slot] = (ap_ring_node_t){.attached = true};
  return (int)slot;
}

void ap_ring_medium_detach(ap_ring_medium_t *m, int slot) {
  if (!valid(m, slot)) {
    return;
  }
  /* The slot stays in the cable order and stops carrying anything. It is not
   * removed: renumbering the ring underneath its nodes would silently change
   * who each node's upstream neighbour is. */
  m->node[slot] = (ap_ring_node_t){0};
}

int ap_ring_medium_first_slot(const ap_ring_medium_t *m) {
  if (m == NULL) {
    return -1;
  }
  for (unsigned i = 0; i < AP_RING_MAX_NODES; i++) {
    if (m->node[i].attached) {
      return (int)i;
    }
  }
  return -1;
}

bool ap_ring_medium_attached(const ap_ring_medium_t *m, int slot) {
  return valid(m, slot) && m->node[slot].attached;
}

void ap_ring_medium_set_cable_bits(ap_ring_medium_t *m, int slot,
                                   unsigned bits) {
  if (!valid(m, slot) || bits > AP_RING_MAX_CABLE_BITS) {
    return;
  }
  m->node[slot].cable_bits = bits;
  m->node[slot].line_head = 0u;
  /* Filled with *idle*, not with zeros.
   *
   * `[MAC]` §3.2 requires a transition in every clock window, so a live ring
   * always carries clock even when it is carrying no data. A delay line of
   * all-zero cells has no transitions at all -- that is a **dead** line, and a
   * receiver decoding it reports a bi-phase error on every bit, correctly. A
   * test asserting a quiet ring is error-free caught exactly that.
   *
   * Idle is a run of encoded Zero bits, which alternate the line level, so the
   * cells alternate too. A caller modelling a cable that is genuinely dead can
   * still drive dead cells into it; this is only what a cable holds before
   * anything has been sent down it. */
  bool level = false;
  for (unsigned i = 0; i < AP_RING_MAX_CABLE_BITS; i++) {
    const ap_ring_cell_t idle = ap_ring_biphase_encode(false, level);
    m->node[slot].line[i] = idle;
    level = ap_ring_cell_trailing_level(idle);
  }
}

void ap_ring_medium_set_bypass(ap_ring_medium_t *m, int slot, bool bypassed) {
  if (!valid(m, slot)) {
    return;
  }
  m->node[slot].bypass.bypassed = bypassed;
}

void ap_ring_medium_transmit(ap_ring_medium_t *m, int slot,
                             ap_ring_cell_t cell) {
  if (!valid(m, slot)) {
    return;
  }
  m->node[slot].driving = cell;
}

ap_ring_cell_t ap_ring_medium_receive(const ap_ring_medium_t *m, int slot) {
  if (!valid(m, slot)) {
    return (ap_ring_cell_t){0};
  }
  return m->node[slot].received;
}

/* The nearest slot upstream of `slot` that actually *drives* the cable:
 * attached and in the ring.
 *
 * Both kinds of skipped slot are skipped for the same reason -- neither is a
 * retiming element. A detached slot is a gap in the cable. A bypassed one is a
 * pair of relays joining input coax to output coax (`[MAC]` §3.5), which is
 * also just cable: it adds no bit delay, which is why it contributes none to
 * `ap_ring_medium_delay_centibits` either. So the signal crosses any run of
 * them within one bit time and the search walks past them.
 *
 * The walk runs a full lap, so a ring with exactly one driving node finds that
 * node as its own upstream -- which is right: its signal goes round the
 * bypassed nodes and comes back. */
static int driver_upstream_of(const ap_ring_medium_t *m, unsigned slot) {
  for (unsigned step = 1u; step <= m->slots; step++) {
    const unsigned i = (slot + m->slots - step) % m->slots;
    if (m->node[i].attached && ap_ring_node_in_ring(m->node[i].bypass)) {
      return (int)i;
    }
  }
  return -1;
}

/* Put `in` onto the cable leaving `slot` and return what emerges at its far
 * end this bit time. With no cable the two are the same cell, which is what
 * makes a zero-length link behave exactly as it did before cables existed. */
static ap_ring_cell_t cable_shift(ap_ring_node_t *n, ap_ring_cell_t in) {
  if (n->cable_bits == 0u) {
    return in;
  }
  const unsigned head = n->line_head % n->cable_bits;
  const ap_ring_cell_t out = n->line[head];
  n->line[head] = in;
  n->line_head = (head + 1u) % n->cable_bits;
  return out;
}

void ap_ring_medium_advance(ap_ring_medium_t *m) {
  if (m->slots == 0u) {
    m->bit_time++;
    return;
  }

  /* Every receiver is settled from what emerges from a cable, and every cable
   * is fed from a *driven* cell placed before this call. Nothing reads a
   * receiver, so there is no order dependence between slots and no way for a
   * cell to cross two driving nodes in one bit time -- the ring's whole timing
   * argument rests on exactly one hop per clock between retiming elements.
   *
   * §3.5's two relay connections are independent and both are modelled here:
   * the input-coax-to-output-coax path is what `driver_upstream_of` walks
   * across, and the transmit-to-receive path is the loopback below. Making the
   * second *replace* the first was the bug the pass-through test caught -- a
   * bypassed node then swallowed the ring instead of passing it on.
   *
   * The cables are shifted for every slot before anything is delivered, so a
   * cable's contents cannot depend on the order slots are visited in. */
  ap_ring_cell_t from_cable[AP_RING_MAX_NODES];
  for (unsigned i = 0; i < m->slots; i++) {
    if (!m->node[i].attached || !ap_ring_node_in_ring(m->node[i].bypass)) {
      continue;
    }
    from_cable[i] = cable_shift(&m->node[i], m->node[i].driving);
  }

  for (unsigned i = 0; i < m->slots; i++) {
    if (!m->node[i].attached) {
      continue;
    }
    if (ap_ring_node_loopback(m->node[i].bypass)) {
      /* "these relays connect the node's transmit output to its receive
       * input" -- so a bypassed node hears itself, while the ring's signal
       * goes past it untouched. */
      m->node[i].received = m->node[i].driving;
      continue;
    }
    const int up = driver_upstream_of(m, i);
    m->node[i].received =
        (up >= 0) ? from_cable[(unsigned)up] : (ap_ring_cell_t){0};
  }

  m->bit_time++;
}

int ap_ring_medium_delay_centibits(const ap_ring_medium_t *m,
                                   int per_node_centibits) {
  int total = 0;
  for (unsigned i = 0; i < m->slots; i++) {
    /* Only nodes actually in the ring retime the signal. A bypassed node's
     * relays are a piece of cable. */
    if (m->node[i].attached && ap_ring_node_in_ring(m->node[i].bypass)) {
      total += per_node_centibits;
    }
  }
  return total;
}

unsigned ap_ring_medium_circumference_bits(const ap_ring_medium_t *m) {
  unsigned total = 0u;
  for (unsigned i = 0; i < m->slots; i++) {
    if (!m->node[i].attached || !ap_ring_node_in_ring(m->node[i].bypass)) {
      continue;
    }
    /* One bit for the station's own retiming, plus the cable it drives. A
     * bypassed node contributes neither, for the same reason it contributes no
     * delay: its relays are cable with no length of their own here. */
    total += 1u + m->node[i].cable_bits;
  }
  return total;
}

bool ap_ring_medium_stable(const ap_ring_medium_t *m, int per_node_centibits) {
  const int total = ap_ring_medium_delay_centibits(m, per_node_centibits);
  /* "the total delay around the network must be exactly an integral -- rather
   * than a fractional -- number of bit-times" (`[MAC]` §3.3). One bit is 100
   * centibits, so the condition is that the total divides by 100. */
  return (total % 100) == 0;
}

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

/* The nearest attached slot upstream of `slot`, whether or not it is in the
 * ring.
 *
 * **A bypassed slot is not skipped, and that is a correction.** This used to
 * walk past both kinds of skipped slot for one stated reason -- neither is a
 * retiming element -- and walking past a bypassed node took its *cable* with
 * it, so bypassing a node shortened the ring. `[MAC]` §3.5's own sentence is
 * that the relays "connect a node's input coaxial cable to its output coaxial
 * cable": both cables stay in the loop and no relay has ever shortened a cable
 * plant. What a bypassed node contributes nothing to is the *delay* -- its
 * relays add no bit time, which is why it still counts for nothing in
 * `ap_ring_medium_delay_centibits` -- and its cable is a separate thing from
 * its relays.
 *
 * A **detached** slot is still skipped, and for a different reason: it is a gap
 * in the cable, a slot with no node and no coax in it at all.
 *
 * The walk runs a full lap, so a ring with exactly one attached slot finds that
 * slot as its own upstream -- which is right: its signal goes round and comes
 * back.
 *
 * Measured consequence of the old behaviour, kept because it is what found
 * this: on a two-node segment where the padded node was the bypassed one, the
 * live circumference was **one bit**, which cannot carry a nine-bit token, so
 * the connected node forced a token where it should have claimed a circulating
 * one (`FINDINGS.md` C251). */
static unsigned attached_upstream_of(const ap_ring_medium_t *m, unsigned slot) {
  for (unsigned step = 1u; step <= m->slots; step++) {
    const unsigned i = (slot + m->slots - step) % m->slots;
    if (m->node[i].attached) {
      return i;
    }
  }
  return slot;
}

/* What is emerging from the cable leaving `slot` this bit time, read before
 * anything is written into any cable. A zero-length link has nothing stored, so
 * its output is whatever is put in and it is resolved during the walk instead.
 *
 * Split from the write half so that every delay line's *output* is taken from
 * its old contents: a bypassed node's cable is fed from its upstream
 * neighbour's cable within the same bit time, and without this split what a
 * cable delivered would depend on the order slots were visited in. */
static ap_ring_cell_t cable_head(const ap_ring_node_t *n) {
  return n->line[n->line_head % n->cable_bits];
}

/* Put `in` onto the cable leaving `slot`, having already taken its output. */
static void cable_push(ap_ring_node_t *n, ap_ring_cell_t in) {
  const unsigned head = n->line_head % n->cable_bits;
  n->line[head] = in;
  n->line_head = (head + 1u) % n->cable_bits;
}

void ap_ring_medium_advance(ap_ring_medium_t *m) {
  if (m->slots == 0u) {
    m->bit_time++;
    return;
  }

  /* **Pass one: every delay line's output, before any input is written.**
   *
   * Nothing reads a receiver here, and every cable's output is its own old
   * contents, so there is no order dependence between slots and no way for a
   * cell to cross two *driving* nodes in one bit time -- the ring's whole
   * timing argument rests on exactly one hop per clock between retiming
   * elements. A run of bypassed nodes is crossed within one bit time plus
   * whatever their cables hold, which is what §3.5's relays do. */
  ap_ring_cell_t from_cable[AP_RING_MAX_NODES];
  for (unsigned i = 0; i < m->slots; i++) {
    from_cable[i] = (ap_ring_cell_t){0};
    if (m->node[i].attached && m->node[i].cable_bits > 0u) {
      from_cable[i] = cable_head(&m->node[i]);
    }
  }

  /* **Pass two: walk the cable in order, starting at a node that drives it.**
   *
   * A slot in the ring puts its own `driving` onto its cable; a bypassed one
   * puts through whatever arrived, §3.5's input coax joined to output coax. The
   * walk has to start somewhere its input is known, which is any node still in
   * the ring. A segment with none is pure cable with no source: it circulates
   * what its lines already hold, and `carried` is seeded from the upstream
   * line so a lap of stored cells still comes round. */
  int first = -1;
  for (unsigned i = 0; i < m->slots; i++) {
    if (m->node[i].attached && ap_ring_node_in_ring(m->node[i].bypass)) {
      first = (int)i;
      break;
    }
  }
  if (first < 0) {
    for (unsigned i = 0; i < m->slots; i++) {
      if (m->node[i].attached) {
        first = (int)i;
        break;
      }
    }
  }
  if (first < 0) {
    m->bit_time++;
    return;
  }
  ap_ring_cell_t carried = from_cable[attached_upstream_of(m, (unsigned)first)];
  for (unsigned step = 0; step < m->slots; step++) {
    const unsigned i = ((unsigned)first + step) % m->slots;
    if (!m->node[i].attached) {
      continue;
    }
    const ap_ring_cell_t in = ap_ring_node_in_ring(m->node[i].bypass)
                                  ? m->node[i].driving
                                  : carried;
    if (m->node[i].cable_bits > 0u) {
      cable_push(&m->node[i], in);
    } else {
      /* A zero-length link is a wire, not a register: what goes in comes out
       * the same bit time, which is what makes it behave exactly as it did
       * before cables existed. */
      from_cable[i] = in;
    }
    carried = from_cable[i];
  }

  for (unsigned i = 0; i < m->slots; i++) {
    if (!m->node[i].attached) {
      continue;
    }
    if (ap_ring_node_loopback(m->node[i].bypass)) {
      /* "these relays connect the node's transmit output to its receive
       * input" -- so a bypassed node hears itself, while the ring's signal
       * goes past it untouched. Both halves of §3.5 are independent and both
       * are modelled: the pass-through is the walk above, and making this one
       * *replace* it was the bug the pass-through test caught. */
      m->node[i].received = m->node[i].driving;
      continue;
    }
    m->node[i].received = from_cable[attached_upstream_of(m, i)];
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
    if (!m->node[i].attached) {
      continue;
    }
    /* The cable always, the retiming bit only when the node is in the ring.
     * A bypassed node's **relays** have no length of their own, which is why
     * it contributes no delay -- but the coax they are spliced into is still
     * there, which is why its cable counts. Corrected 2026-09-08; this used to
     * skip a bypassed slot entirely, so bypassing a node shortened the ring.
     * `attached_upstream_of` carries the reasoning and the measured cost. */
    total += m->node[i].cable_bits;
    if (ap_ring_node_in_ring(m->node[i].bypass)) {
      total += 1u;
    }
  }
  return total;
}

unsigned ap_ring_medium_plant_bits(const ap_ring_medium_t *m) {
  unsigned total = 0u;
  for (unsigned i = 0; i < m->slots; i++) {
    /* Bypass is not consulted: a relay does not shorten the cable it is
     * spliced into. See the header for the one question this answers. */
    if (!m->node[i].attached) {
      continue;
    }
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

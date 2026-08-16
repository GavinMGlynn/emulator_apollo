/* A ring segment carried between processes: two emulators, one cable.
 *
 * `ap_ring_medium` is a segment inside one address space. Two *instances* of
 * this emulator cannot share one, and that is the last thing standing between
 * `RING.md` 112 and a ring whose nodes are separate programs -- possibly on
 * separate hosts. This is the carrier.
 *
 * ## It lives in the frontend, and that is not an accident
 *
 * `src/core` has zero frontend dependencies and does no file I/O. A socket is
 * host I/O, so it cannot go there, and it does not need to: a remote node is
 * modelled as an ordinary **slot on the local medium whose cell is supplied by
 * the link instead of by a station**. `ap_ring_medium_transmit(m, slot, cell)`
 * already accepts a cell for any slot, so the core needs no change at all.
 *
 * ## The link IS a cable, and its length is the batch size
 *
 * A socket round trip per bit time at 12 Mbit/s is twelve million round trips a
 * second, which is not an implementation. The way out is not a heuristic: it is
 * `[MAC]` §3.4. The manual puts the maximum cable between two nodes at 1 km,
 * which `ap_ring_medium.h` counts as **64 bit times** of delay, and a bit
 * driven now *cannot reach the next node* for that many bit times whatever the
 * carrier does. So exchanging `n` bit times in one message is
 * indistinguishable from exchanging them one at a time, for any `n` no greater
 * than the modelled cable length -- the cells are in flight either way.
 *
 * The batch is therefore a **physical parameter, not a tuning knob**: it is how
 * long the cable between the two processes is, in bit times, and both ends must
 * agree on it or they are modelling different cables.
 *
 * ## Determinism, which is the property that had to survive
 *
 * The headless frontend is deterministic -- no wall clock, no host input, no
 * threads -- and a socket is host input. What is preserved here is the property
 * that actually matters: **content**, not timing. The exchange is strict
 * lock-step, every batch the same size in both directions, no timeouts and no
 * partial reads, so the sequence of cells each side sees is a function of the
 * two programs and nothing else. Two runs produce identical rings; they do not
 * produce identical wall-clock schedules, and nothing here claims they do.
 *
 * A run using this is therefore reproducible in the sense the ring state hash
 * checks, and is *not* hermetic in the sense `--boot-limit` runs are. Same
 * distinction `mdsession.py` carries and for the same reason.
 */

#ifndef APOLLO_FRONTEND_AP_RING_LINK_H
#define APOLLO_FRONTEND_AP_RING_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ring/ap_ring_medium.h"

/* A cell is two windows, so a batch packs one byte per bit time. Deliberately
 * not bit-packed: the saving is a factor of four on a link whose cost is
 * round trips rather than bytes, and a byte per cell keeps the wire format
 * readable in a capture, which is worth more when a ring will not form. */
typedef struct {
  int fd;              /* borrowed; the caller opens and closes it */
  unsigned cable_bits; /* batch size == modelled cable length, in bit times */
  bool failed;         /* the peer went away or the format broke */
  uint64_t batches;    /* exchanges completed, for reporting */
} ap_ring_link_t;

/* Wire an already-connected descriptor. `cable_bits` must be 1..64 and the same
 * at both ends; a mismatch is a different cable, not a tuning difference, and
 * shows up as a short read rather than as drift. */
[[nodiscard]] bool ap_ring_link_init(ap_ring_link_t *link, int fd,
                                     unsigned cable_bits);

/* Exchange one cable's worth of bit times.
 *
 * `local` is what this process's slots drove, oldest first; `remote` receives
 * what the peer's drove over the same bit times. Both arrays are
 * `cable_bits` long. Strict lock-step: this writes its whole batch and then
 * reads a whole batch, so neither side can run ahead.
 *
 * False once the link has failed, and it stays false -- a ring that has lost a
 * node does not silently continue as a shorter one. */
[[nodiscard]] bool ap_ring_link_exchange(ap_ring_link_t *link,
                                         const ap_ring_cell_t *local,
                                         ap_ring_cell_t *remote);

/* The two halves of it, separately.
 *
 * `exchange` writes its whole batch and *then* blocks reading, which is right
 * for two processes running concurrently and **deadlocks a single thread
 * driving both ends** -- both ends must send before either receives. That is
 * what two processes do naturally and what one cannot, so the split exists to
 * make the lock-step testable in one process; it also lets a caller overlap
 * the halves with its own work. Same bytes either way. */
[[nodiscard]] bool ap_ring_link_send(ap_ring_link_t *link,
                                     const ap_ring_cell_t *local);
[[nodiscard]] bool ap_ring_link_recv(ap_ring_link_t *link,
                                     ap_ring_cell_t *remote);

#endif /* APOLLO_FRONTEND_AP_RING_LINK_H */

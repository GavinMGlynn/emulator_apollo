/* A ring station: the MAC-level behaviour of a node on the medium.
 *
 * This is *not* the ring controller device. The controller is the board with
 * registers and a dual-ported RAM buffer, and its verification is the ring
 * ROM's own self-test. A station is the behaviour that board implements, at the
 * level `[MAC]` ch. 2 describes it, and it exists so the ring's timing can be
 * probed before any register map is known.
 *
 * ## Transceiving, and where the one-bit delay comes from
 *
 * `[MAC]` §3.2 p. 3-3: "Nodes in the Apollo token ring transceive. This means
 * that they receive data in, and then immediately send it out (alternatively,
 * they strip it from the ring)."
 *
 * "Immediately" is not zero: a station drives on bit time *n+1* what it
 * received on bit time *n*, which is exactly the elastic-store buffer's
 * nominal one-bit delay (§3.3.2). So a ring of N stations has a round-trip of
 * N bit times, and §3.3's requirement that the total delay be "an integral
 * number of bit-times" is satisfied by construction here -- every station
 * contributes exactly one.
 *
 * ## Claiming happens to a symbol in flight
 *
 * §2.2.1.1: a station takes the ring "by changing the state of the character's
 * last bit" of a free token as it passes. That is the one operation with no
 * store-and-forward equivalent -- the station is not sending a claimed token,
 * it is altering the free one on its way through, and the bit it alters is the
 * one it is driving that very bit time.
 *
 * Modelled by watching a nine-bit window of the received bit stream: when the
 * first eight bits of a free token have arrived and the station wants the ring,
 * the ninth bit it drives is a One rather than the Zero it received.
 */

#ifndef APOLLO_RING_AP_RING_STATION_H
#define APOLLO_RING_AP_RING_STATION_H

#include <stdbool.h>
#include <stdint.h>

#include "ring/ap_ring_mac.h"
#include "ring/ap_ring_medium.h"

typedef struct {
  int slot; /* where on the medium */

  /* The last nine received bits, most recent in bit 0. Nine because that is an
   * out-of-band character's width; anything narrower cannot recognise one. */
  uint16_t window;
  unsigned bits_seen;

  /* What this station drives next bit time -- the bit it received, unless it
   * is claiming. */
  bool pending_bit;
  bool pending_valid;

  /* Line level, carried across cells because the encoding is differential. */
  bool tx_level;
  bool rx_level;
  bool rx_level_valid;

  bool wants_ring;   /* the host has something to send */
  bool holds_ring;   /* this station claimed the token */
  bool saw_biphase_error;

  /* A symbol this station is sourcing rather than forwarding, and how many of
   * its bits are still to go. Held here and not in a table keyed by slot: two
   * rings in one process share slot numbers, so anything keyed that way is
   * shared state between rings that have nothing to do with each other. */
  uint16_t originate_symbol;
  uint8_t originate_left;

  /* Counters, which are what the probes read. */
  uint64_t tokens_seen;
  uint64_t claims_made;
  uint64_t bits_forwarded;
} ap_ring_station_t;

void ap_ring_station_init(ap_ring_station_t *s, int slot);

/* Put a free token onto the ring from this station, nine bits, one per call to
 * `ap_ring_station_drive` until it is done. Used to start a ring: on real
 * hardware "any node that wants to transmit can generate a claimed token
 * (after a specified timeout) in order to force transmission" (§2.2.1.1), and a
 * ring with no token has to get one from somewhere. */
void ap_ring_station_originate_token(ap_ring_station_t *s, uint16_t symbol);

/* Drive this bit time's cell onto the medium. Call before `advance`. */
void ap_ring_station_drive(ap_ring_station_t *s, ap_ring_medium_t *m);

/* Take this bit time's cell from the medium. Call after `advance`. */
void ap_ring_station_receive(ap_ring_station_t *s, const ap_ring_medium_t *m);

/* Whether the station's window currently ends on a well-formed out-of-band
 * character, and which. */
[[nodiscard]] bool ap_ring_station_at_symbol(const ap_ring_station_t *s,
                                             uint16_t *symbol);

#endif /* APOLLO_RING_AP_RING_STATION_H */

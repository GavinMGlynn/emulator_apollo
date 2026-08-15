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

#include "ring/ap_ring_frame.h"
#include "ring/ap_ring_framer.h"
#include "ring/ap_ring_mac.h"
#include "ring/ap_ring_medium.h"

/* ## Stripping, and the one timeout `[MAC]` puts a number on
 *
 * §2.1 step 3: once a node has the ring it "breaks ring recirculation and
 * begins to transmit its packet. Concurrently, it begins to discard received
 * data (including its own packet, which will eventually come back around the
 * ring). The process of discarding received data is called stripping."
 *
 * Step 7 bounds it: "The transmitting node continues to strip all data from the
 * ring until it finishes receiving its own frame, or until a 10.9 msec (2^14
 * byte) timeout occurs. This timeout prevents a node from stripping bits
 * forever (for example, if its frame has gotten lost on the ring)."
 *
 * The two figures agree and each checks the other: 2^14 bytes is 131,072 bits,
 * and at 12 Mbit/s that is 10.923 ms. Worth stating because the exponent does
 * not survive a text extraction -- `pdftotext` renders it as "214 byte", which
 * reads as a plausible and entirely wrong number. Read from the page image.
 *
 * Step 8: "When a node stops stripping, recirculation resumes around the ring."
 */
#define AP_RING_STRIP_TIMEOUT_BYTES 16384u
#define AP_RING_STRIP_TIMEOUT_BITS (AP_RING_STRIP_TIMEOUT_BYTES * 8u)

/* ## The other timeout, which `[MAC]` does not put a number on
 *
 * §2.2.1.1: "If no token exists on the network (for example, if the ring has
 * broken), any node that wants to transmit can generate a claimed token (after
 * a specified timeout) in order to force transmission."
 *
 * The manual specifies *that* there is a timeout and never says what it is.
 * `PROVISIONAL`: this core uses the stripping timeout, which is the only
 * documented figure of the right order and is at least anchored to something
 * the manual states rather than invented. `RING.md` question E carries it, and
 * patent 4,716,575 is where a real figure would come from.
 *
 * Recorded as its own constant and not spelled `AP_RING_STRIP_TIMEOUT_BITS` at
 * its use site, so that when a real figure turns up, changing it is a one-line
 * edit and every probe that depends on it moves together. */
#define AP_RING_TOKEN_LOSS_TIMEOUT_BITS AP_RING_STRIP_TIMEOUT_BITS

typedef struct {
  int slot; /* where on the medium */

  /* ## The transmit path, `[MAC]` §2.1 steps 3, 6 and 7
   *
   * An audit against chapter 2 (`RING.md` 85-85e) found these three absent:
   * the station claimed the ring, stripped, and never transmitted anything,
   * because nothing ever called `ap_ring_frame_emit`. Step 3 is "breaks ring
   * recirculation and **begins to transmit its packet**", step 6 is "sends out
   * a **new free token** to follow the frame", and step 7 bounds stripping by
   * "until it **finishes receiving its own frame**, or until a 10.9 msec
   * timeout".
   *
   * The frame is assembled once into a caller-lent buffer and then driven one
   * bit per bit time, which is what a transceiving node does -- `src/core`
   * allocates nothing, the same rule the screen's memories follow. */
  uint8_t *tx_bits;
  size_t tx_capacity;   /* bytes lent */
  size_t tx_bit_count;  /* bits the assembled frame occupies */
  size_t tx_bit_pos;    /* bits driven so far */
  bool tx_armed;        /* a frame is assembled and waiting for the ring */

  /* Step 7's other arm. Once the station has seen its own frame start come
   * back it counts the frame's own length before it stops stripping, which is
   * "finishes receiving its own frame" without needing to know the ring's
   * circumference. */
  bool tx_seen_own_frame_start;
  size_t tx_stripped_own;

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
  bool stripping;    /* §2.1 step 3: discarding received data */
  bool saw_biphase_error;

  /* Bit times since this station last saw a token go by, and since it began
   * stripping. Both are compared against the timeouts above. */
  uint64_t bits_since_token;
  uint64_t bits_stripping;
  uint64_t strip_timeouts;
  uint64_t forced_tokens;

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

/* Lend the station a buffer to assemble frames in. Without one it cannot
 * transmit, which is the state every station was in before `RING.md` 85. */
void ap_ring_station_attach_tx(ap_ring_station_t *s, uint8_t *bytes,
                               size_t capacity);

/* Assemble a frame and ask for the ring. False when `[MAC]` §2.2.2's length
 * rules refuse it or the lent buffer is too small -- checked by
 * `ap_ring_frame_emit` before anything is written, so a refused frame leaves
 * the station idle rather than half-armed. */
[[nodiscard]] bool ap_ring_station_queue_frame(
    ap_ring_station_t *s, const ap_ring_frame_fields_t *fields);

/* Whether a queued frame has been fully driven onto the ring. */
[[nodiscard]] bool ap_ring_station_transmitted(const ap_ring_station_t *s);

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

/* A ring station. See `ap_ring_station.h` for why the one-bit delay is the
 * elastic store's and why claiming alters a symbol in flight. */

#include "ring/ap_ring_station.h"

#include "ring/ap_ring_phy.h"

void ap_ring_station_init(ap_ring_station_t *s, int slot) {
  *s = (ap_ring_station_t){0};
  s->slot = slot;
}

void ap_ring_station_originate_token(ap_ring_station_t *s, uint16_t symbol) {
  /* Sourcing is kept apart from forwarding because the two have different
   * timing: a forwarded bit was received a bit time ago, a sourced one was
   * never received at all. */
  s->originate_symbol = symbol;
  s->originate_left = (uint8_t)AP_RING_OOB_BITS;
}

/* Whether a whole free token has just arrived -- so the bit about to be driven
 * is that token's last one.
 *
 * The alignment took two attempts and is worth stating. `receive` shifts the
 * incoming bit into the window *and* records it as the bit to forward, so at
 * the moment of driving, the window's low bit **is** `pending_bit`. Checking
 * an eight-bit prefix therefore fires a bit early, on the token's
 * second-to-last bit, which forwards an unmodified token and claims nothing. */
static bool window_holds_free_token(const ap_ring_station_t *s) {
  return s->bits_seen >= AP_RING_OOB_BITS &&
         s->window == (uint16_t)AP_RING_OOB_FREE_TOKEN;
}

void ap_ring_station_drive(ap_ring_station_t *s, ap_ring_medium_t *m) {
  bool bit = false;
  /* Whether the claim happened on *this* bit. Stripping starts with the claim
   * (§2.1 step 3) but must not swallow the very bit that carries it: the
   * claimed token's last bit is the One this station is driving, and stripping
   * it back to a Zero turns the claimed token into a free one again. The next
   * station downstream then claims a ring that is already taken -- which is
   * exactly what the contention test caught, for the second time and by a
   * different route than the first. */
  bool claimed_now = false;

  if (s->originate_left > 0u) {
    const unsigned shift = (unsigned)s->originate_left - 1u;
    bit = ((s->originate_symbol >> shift) & 1u) != 0u;
    s->originate_left--;
  } else if (s->pending_valid) {
    bit = s->pending_bit;
    /* The claim: the free token's last bit is the one being driven now, and a
     * station taking the ring drives a One where it received a Zero. The
     * symbol is altered as it passes rather than replaced, which is what
     * §2.2.1.1 describes and what leaves the rest of the character intact.
     *
     * Matching the *whole* token is what distinguishes free from claimed: the
     * two differ only in the last bit, so any test on a prefix accepts both,
     * and a station downstream of one that had already claimed would claim
     * again. The contention probe caught exactly that -- two stations holding
     * one ring -- which is what that probe is for. */
    if (s->wants_ring && !s->holds_ring && window_holds_free_token(s)) {
      bit = true;
      s->holds_ring = true;
      s->wants_ring = false;
      s->claims_made++;
      /* §2.1 step 3: acquiring the ring "breaks ring recirculation" and
       * stripping begins in the same breath. The two are one event, not two,
       * which is why they are set together here. */
      s->stripping = true;
      s->bits_stripping = 0u;
      claimed_now = true;
    }
    s->bits_forwarded++;
  }

  if (s->stripping && !claimed_now) {
    /* "pads the bit serial stream with Zeros" (§2.1 step 4). Whatever arrived
     * is discarded; a Zero goes out in its place. */
    bit = false;
    s->bits_stripping++;
    if (s->bits_stripping >= AP_RING_STRIP_TIMEOUT_BITS) {
      /* "or until a 10.9 msec (2^14 byte) timeout occurs. This timeout
       * prevents a node from stripping bits forever." Step 8: recirculation
       * resumes when it stops. */
      s->stripping = false;
      s->holds_ring = false;
      s->strip_timeouts++;
    }
  }

  /* §2.2.1.1: with no token on the ring, a node that wants to transmit "can
   * generate a claimed token (after a specified timeout) in order to force
   * transmission". The timeout is PROVISIONAL -- see the header. */
  if (s->wants_ring && !s->holds_ring && s->originate_left == 0u &&
      s->bits_since_token >= AP_RING_TOKEN_LOSS_TIMEOUT_BITS) {
    ap_ring_station_originate_token(s, AP_RING_OOB_CLAIMED_TOKEN);
    s->holds_ring = true;
    s->wants_ring = false;
    s->forced_tokens++;
    s->bits_since_token = 0u;
  }

  const ap_ring_cell_t cell = ap_ring_biphase_encode(bit, s->tx_level);
  s->tx_level = ap_ring_cell_trailing_level(cell);
  ap_ring_medium_transmit(m, s->slot, cell);
}

void ap_ring_station_receive(ap_ring_station_t *s, const ap_ring_medium_t *m) {
  const ap_ring_cell_t cell = ap_ring_medium_receive(m, s->slot);
  if (!s->rx_level_valid) {
    /* The first cell has nothing before it to transition against. Seeding from
     * the cell's own clock window means the first bit decodes as if the line
     * had been idle at the opposite level, which is what a receiver's PLL
     * settles to; it costs one bit of startup and no ambiguity afterwards. */
    s->rx_level = !cell.clock_window;
    s->rx_level_valid = true;
  }

  bool error = false;
  const bool bit = ap_ring_biphase_decode(cell, s->rx_level, &error);
  if (error) {
    s->saw_biphase_error = true;
  }
  s->rx_level = ap_ring_cell_trailing_level(cell);

  s->window = (uint16_t)(((unsigned)s->window << 1) | (bit ? 1u : 0u));
  s->window &= 0x1FFu;
  if (s->bits_seen < AP_RING_OOB_BITS) {
    s->bits_seen++;
  }

  uint16_t symbol = 0u;
  s->bits_since_token++;
  if (ap_ring_station_at_symbol(s, &symbol) &&
      (symbol == AP_RING_OOB_FREE_TOKEN ||
       symbol == AP_RING_OOB_CLAIMED_TOKEN)) {
    /* Either token resets the loss timer: §2.2.1.1's condition is that *no*
     * token exists on the ring, and a claimed one is a token. A station that
     * only watched for free tokens would force a second token onto a ring that
     * was merely busy, which is how a token ring acquires two. */
    s->bits_since_token = 0u;
    if (symbol == AP_RING_OOB_FREE_TOKEN) {
      s->tokens_seen++;
    }
  }

  /* Forwarded next bit time: "receive data in, and then immediately send it
   * out" -- one bit later, which is the elastic store's nominal delay. */
  s->pending_bit = bit;
  s->pending_valid = true;
}

bool ap_ring_station_at_symbol(const ap_ring_station_t *s, uint16_t *symbol) {
  if (s->bits_seen < AP_RING_OOB_BITS) {
    return false;
  }
  if (!ap_ring_oob_well_formed(s->window)) {
    return false;
  }
  *symbol = s->window;
  return true;
}

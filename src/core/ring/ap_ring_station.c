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
    }
    s->bits_forwarded++;
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
  if (ap_ring_station_at_symbol(s, &symbol) &&
      symbol == AP_RING_OOB_FREE_TOKEN) {
    s->tokens_seen++;
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

/* A ring station. See `ap_ring_station.h` for why the one-bit delay is the
 * elastic store's and why claiming alters a symbol in flight. */

#include "ring/ap_ring_station.h"

#include "ring/ap_ring_phy.h"

void ap_ring_station_init(ap_ring_station_t *s, int slot) {
  *s = (ap_ring_station_t){0};
  s->slot = slot;
}

void ap_ring_station_set_address(ap_ring_station_t *s, uint32_t address) {
  s->address = address;
}

/* Where the receive-side header capture has got to. */
enum { RX_IDLE = 0, RX_AWAIT_SEPARATOR, RX_HEADER, RX_DONE };

void ap_ring_station_attach_tx(ap_ring_station_t *s, uint8_t *bytes,
                               size_t capacity) {
  s->tx_bits = bytes;
  s->tx_capacity = capacity;
  s->tx_bit_count = 0u;
  s->tx_bit_pos = 0u;
  s->tx_armed = false;
}

bool ap_ring_station_queue_frame(ap_ring_station_t *s,
                                 const ap_ring_frame_fields_t *fields) {
  if (s == NULL || fields == NULL || s->tx_bits == NULL) {
    return false;
  }
  /* Assembled once, here, rather than a field at a time while driving: the
   * framer refuses a frame whose lengths §2.2.2 does not permit *before*
   * writing anything, and a station that discovered that mid-transmission
   * would already have put a malformed frame on the ring. */
  ap_ring_bitwriter_t w;
  ap_ring_bitwriter_init(&w, s->tx_bits, s->tx_capacity);
  if (!ap_ring_frame_emit(&w, fields)) {
    return false;
  }
  s->tx_bit_count = w.bit_count;
  s->tx_bit_pos = 0u;
  s->tx_armed = true;
  s->tx_seen_own_frame_start = false;
  s->tx_stripped_own = 0u;
  /* Step 1: "A node generates a packet and enables its transmitter." */
  s->wants_ring = true;
  return true;
}

bool ap_ring_station_transmitted(const ap_ring_station_t *s) {
  return s != NULL && !s->tx_armed && s->tx_bit_count > 0u &&
         s->tx_bit_pos >= s->tx_bit_count;
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
  /* Whether this bit is one this station is *sourcing* rather than forwarding.
   * Stripping must not overwrite it: step 6's new free token is emitted while
   * the station is still stripping its own frame, and a Zero written over it
   * turns the token back into padding -- the ring is then never released and
   * no other station can ever transmit. The same hazard the claim bit has,
   * one sentence further down §2.1. */
  bool originated_now = false;

  if (s->originate_left > 0u) {
    const unsigned shift = (unsigned)s->originate_left - 1u;
    bit = ((s->originate_symbol >> shift) & 1u) != 0u;
    s->originate_left--;
    originated_now = true;
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

  if (s->stripping && !claimed_now && !originated_now) {
    /* **Step 3: "begins to transmit its packet".** While a queued frame has
     * bits left they are what this station drives; the received bit is still
     * discarded, which is what makes transmitting and stripping concurrent
     * rather than sequential -- §2.1 puts them in one sentence. */
    if (s->tx_armed && s->tx_bit_pos < s->tx_bit_count) {
      const size_t i = s->tx_bit_pos;
      bit = ((s->tx_bits[i >> 3] >> (7u - (i & 7u))) & 1u) != 0u;
      s->tx_bit_pos++;
      if (s->tx_bit_pos >= s->tx_bit_count) {
        /* **Step 6: "sends out a new free token to follow the frame."**
         * Queued to originate from the next bit time, so it follows the
         * frame's last bit rather than replacing it. Without this a station
         * that claimed the ring held it until the strip timeout, which is what
         * the audit found. */
        s->tx_armed = false;
        ap_ring_station_originate_token(s, AP_RING_OOB_FREE_TOKEN);
      }
      s->bits_stripping++;
      if (s->bits_stripping >= AP_RING_STRIP_TIMEOUT_BITS) {
        s->stripping = false;
        s->holds_ring = false;
        s->strip_timeouts++;
      }
      const ap_ring_cell_t txcell = ap_ring_biphase_encode(bit, s->tx_level);
      s->tx_level = ap_ring_cell_trailing_level(txcell);
      ap_ring_medium_transmit(m, s->slot, txcell);
      return;
    }
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

  /* **Step 7's first arm: "until it finishes receiving its own frame".**
   * Only the 10.9 ms timeout existed before, so every transmission stripped
   * the full 131,072 bits (`RING.md` 85a). A station cannot know the ring's
   * circumference, but it does know its own frame's length: once the frame
   * start comes back it counts that many bits and stops. */
  /* Watched from the **claim**, not from the end of transmission: a ring is
   * only a few bit times around and a frame is a thousand bits long, so a
   * station's own frame start comes back long before its last bit has gone
   * out. Gating this on the frame being finished missed it every time -- the
   * first version of this test caught that, which is what it is for. */
  if (s->stripping && s->tx_bit_count > 0u) {
    if (!s->tx_seen_own_frame_start) {
      uint16_t symbol = 0u;
      if (ap_ring_station_at_symbol(s, &symbol) &&
          symbol == (uint16_t)AP_RING_OOB_FRAME_START) {
        s->tx_seen_own_frame_start = true;
        s->tx_stripped_own = 0u;
      }
    } else if (++s->tx_stripped_own >= s->tx_bit_count) {
      /* Step 8: "When a node stops stripping, recirculation resumes." */
      s->stripping = false;
      s->holds_ring = false;
      s->tx_seen_own_frame_start = false;
    }
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

  bool forward_bit = bit;

  /* **§2.2.2.2's decision, taken from the passing stream.**
   *
   * The frame start sequence is the frame start character, a null separator
   * and a separator character (§2.2.2.1), so the header begins after the
   * separator -- not after the frame start. Waiting for the wrong one puts the
   * null separator's eight zeros into the destination address.
   *
   * Bit stuffing is undone here rather than by `ap_ring_bitreader`, because a
   * station sees one bit at a time and cannot rewind: a zero arriving on a run
   * of five ones was inserted by the transmitter and is dropped (§2.2.1). */
  {
    uint16_t sym = 0u;
    const bool oob = ap_ring_station_at_symbol(s, &sym);
    if (oob && sym == (uint16_t)AP_RING_OOB_FRAME_START) {
      s->rx_state = RX_AWAIT_SEPARATOR;
      s->rx_addressed = false;
      s->rx_flipped_parity = false;
      s->frames_seen++;
    } else if (s->rx_state == RX_AWAIT_SEPARATOR && oob &&
               sym == (uint16_t)AP_RING_OOB_SEPARATOR) {
      s->rx_state = RX_HEADER;
      s->rx_ones_run = 0u;
      s->rx_bit_count = 0u;
      s->rx_byte = 0u;
      s->rx_header_len = 0u;
      s->rx_header_bits = 0u;
    } else if (s->rx_state == RX_HEADER) {
      const bool stuffed = (s->rx_ones_run >= 5u) && !bit;
      s->rx_ones_run = bit ? s->rx_ones_run + 1u : 0u;
      if (!stuffed) {
        /* **§2.2.2.2's early acknowledge, modified as it passes.** "A node's
         * ring transmitter inserts an early acknowledge field; another node's
         * receiver modifies it", and Figure 2-7 attaches intend-to-copy to "an
         * **addressed** receiver". The field is header byte **7** and the
         * decision is taken at byte 6, so it is always known in time.
         *
         * Altering a bit *in flight* is the same mechanism claiming a token
         * uses: the bit is changed on its way through, on the bit time it is
         * forwarded. It needs no CRC recalculation because "ring hardware
         * treats this field as a string of Zeros in its CRC calculation"
         * (§2.2.2.2), and it cannot disturb the bit stuffing: Figure 2-5 puts
         * a byte of zeros at `+6` immediately before it, and the field's own
         * legal bits are 3 and 1, so the longest run of ones it can carry is
         * one. */
        const unsigned data_bit = s->rx_header_bits;
        s->rx_header_bits++;
        if (s->rx_addressed && data_bit >= 56u && data_bit < 64u) {
          const unsigned in_byte = data_bit - 56u;
          if (in_byte == 4u) { /* bit 3, most-significant-first */
            if (!bit) {
              forward_bit = true;
              s->rx_flipped_parity = true;
            }
          } else if (in_byte == 6u && s->rx_flipped_parity) {
            /* "This bit is used for odd parity." Setting intend-to-copy adds a
             * One, so the parity bit must flip to keep the count odd. */
            forward_bit = !bit;
          }
        }
        s->rx_byte = (uint8_t)(((unsigned)s->rx_byte << 1) | (bit ? 1u : 0u));
        if (++s->rx_bit_count == 8u) {
          if (s->rx_header_len < sizeof s->rx_header) {
            s->rx_header[s->rx_header_len++] = s->rx_byte;
          }
          s->rx_bit_count = 0u;
          s->rx_byte = 0u;
          if (s->rx_header_len == 6u) {
            const uint32_t dest = ((uint32_t)s->rx_header[0] << 24) |
                                  ((uint32_t)s->rx_header[1] << 16) |
                                  ((uint32_t)s->rx_header[2] << 8) |
                                  (uint32_t)s->rx_header[3];
            const uint16_t type = (uint16_t)(((uint16_t)s->rx_header[4] << 8) |
                                             s->rx_header[5]);
            /* "receivers ignore the destination address field" when the
             * broadcast bit is set -- so it is checked first and the address
             * comparison is not reached. */
            s->rx_addressed = ((type & AP_RING_TYPE_BROADCAST) != 0u) ||
                              (dest == s->address);
            if (s->rx_addressed) {
              s->frames_addressed++;
            }
            /* Capture continues past the decision: the early acknowledge is
             * a byte further on and has to be reached. */
          }
        }
      }
    }
  }

  /* Forwarded next bit time: "receive data in, and then immediately send it
   * out" -- one bit later, which is the elastic store's nominal delay. */
  s->pending_bit = forward_bit;
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

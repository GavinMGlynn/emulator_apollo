/* Apollo Token Ring physical layer. See `ap_ring_phy.h` for the citations and
 * for what §3.4's analogue figures deliberately do not become code. */

#include "ring/ap_ring_phy.h"

ap_ring_cell_t ap_ring_biphase_encode(bool bit, bool previous) {
  ap_ring_cell_t cell;
  /* "In each clock window, a transition ... must always be present". */
  cell.clock_window = !previous;
  /* "A transition within the data window indicates a bit value of One; no
   * transition within the data window signals a bit value of Zero." */
  cell.data_window = bit ? !cell.clock_window : cell.clock_window;
  return cell;
}

bool ap_ring_cell_trailing_level(ap_ring_cell_t cell) {
  return cell.data_window;
}

bool ap_ring_biphase_decode(ap_ring_cell_t cell, bool previous, bool *error) {
  const bool clock_transition = cell.clock_window != previous;
  if (!clock_transition) {
    /* "a bi-phase error will occur and the corresponding data will be
     * interpreted as having a bit value of Zero" -- a bit is still produced,
     * so a single bad cell costs a bit and not the byte framing. */
    *error = true;
    return false;
  }
  *error = false;
  return cell.data_window != cell.clock_window;
}

ap_ring_esb_status_t ap_ring_esb_classify(int centibits) {
  /* Inclusive failures: "underflow will occur if ... out-of-phase by 0.5
   * bit-times or less", "overflow ... by 1.5 bit-times or more". */
  if (centibits <= AP_RING_ESB_MIN_CENTIBITS) {
    return AP_RING_ESB_UNDERFLOW;
  }
  if (centibits >= AP_RING_ESB_MAX_CENTIBITS) {
    return AP_RING_ESB_OVERFLOW;
  }
  return AP_RING_ESB_OK;
}

int ap_ring_pll_phase_offset_centibits(int deviation_hz) {
  /* Linear from (centre - 3 kHz, 50) to (centre + 3 kHz, 150): a span of
   * 100 centibits over 6 kHz, so the offset at the centre is 100 -- the
   * nominal one-bit delay §3.3.2 gives for loops in phase. The two statements
   * agreeing is the only cross-check either has. */
  if (deviation_hz <= -AP_RING_PLL_DEVIATION_HZ) {
    return AP_RING_ESB_MIN_CENTIBITS;
  }
  if (deviation_hz >= AP_RING_PLL_DEVIATION_HZ) {
    return AP_RING_ESB_MAX_CENTIBITS;
  }
  /* Integer arithmetic throughout, and the span divides exactly: 100
   * centibits over 6000 Hz. Rounding toward zero here would bias the offset
   * downward on one side of centre only, so the multiply comes first. */
  const int span = AP_RING_ESB_MAX_CENTIBITS - AP_RING_ESB_MIN_CENTIBITS;
  return AP_RING_ESB_NOMINAL_CENTIBITS +
         (deviation_hz * span) / (2 * AP_RING_PLL_DEVIATION_HZ);
}

bool ap_ring_node_in_ring(ap_ring_bypass_t state) { return !state.bypassed; }

bool ap_ring_node_loopback(ap_ring_bypass_t state) { return state.bypassed; }

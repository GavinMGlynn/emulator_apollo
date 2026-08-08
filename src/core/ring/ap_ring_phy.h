/* Apollo Token Ring — physical layer, `[MAC]` ch. 3.
 *
 * The MAC layer above hands this one bits; this is what the wire does with
 * them. As with the rest of Phase 6 there is no runnable oracle, so every
 * figure cites `[MAC]` and the ones this core cannot model are named rather
 * than approximated.
 *
 * ## Bi-phase: two windows per bit cell
 *
 * `[MAC]` §3.2 p. 3-3: "In the time it takes to transmit one bit (this is a bit
 * cell, or 83.33 nsec), two windows exist: the clock window and the data
 * window. In each clock window, a transition (i.e., a clock signal) must always
 * be present or a bi-phase error will occur and the corresponding data will be
 * interpreted as having a bit value of Zero. In each data window, transitions
 * (or the lack of them) signal bit values. A transition within the data window
 * indicates a bit value of One; no transition within the data window signals a
 * bit value of Zero."
 *
 * So a bit cell is modelled as **two half-cell levels**: the clock window
 * always inverts the line, and the data window inverts it again only for a one.
 * That is a differential encoding -- the *absolute* level carries nothing, only
 * the transitions do -- which is why a node can be inserted anywhere in the
 * ring without agreeing on polarity with anyone.
 *
 * Note what the manual makes of a missing clock transition: not a dropped cell
 * but a **zero**. The receiver reports the error and still produces a bit,
 * because the frame check downstream is what is supposed to catch the damage.
 * A decoder that returned "no bit" would desynchronise the byte framing on a
 * single glitch and turn one bad cell into a lost frame.
 *
 * ## What is deliberately not modelled
 *
 * §3.4's analogue characteristics -- an 18 MHz driver cutoff, a -20 dBm
 * receiver sensitivity, 1 km between nodes -- describe a cable, and a
 * bit-accurate ring reproduces none of it. They are recorded in
 * `docs/references/RING.md` and carry no code.
 *
 * `[MAC]` §3.4 also contains an internal inconsistency, verified against the
 * page image rather than a text extraction: it gives transmitted power as
 * "18 dBm into 75 ohms (typically, 2.5 V peak-to-peak)", and 2.5 V
 * peak-to-peak into 75 ohms is about 10 dBm as a sine or 13 dBm as a square
 * wave, not 18. Recorded because a later reader will find the same arithmetic
 * and should not have to wonder whether it was a transcription error here. It
 * is in the manual.
 */

#ifndef APOLLO_RING_AP_RING_PHY_H
#define APOLLO_RING_AP_RING_PHY_H

#include <stdbool.h>
#include <stdint.h>

#include "time/ap_time.h"

/* The two clock domains, `[MAC]` §3.2 p. 3-3 and finding 10a in `RING.md`.
 * 12 Mbit/s is the *data* rate; the line runs at 24 MHz because each bit cell
 * carries two windows. Both must divide `AP_TIME_BASE_HZ` exactly, which is
 * what forced the base to its present value. */
#define AP_RING_DATA_HZ 12000000u
#define AP_RING_LINE_HZ 24000000u

/* One bit cell, in time-base units. `[MAC]` calls it 83.33 nsec; the exact
 * figure is 1/12 MHz, and this is that rather than the manual's rounding --
 * a rounded period would drift against every other clock in the machine. */
#define AP_RING_BIT_CELL_TICKS (AP_TIME_BASE_HZ / AP_RING_DATA_HZ)

/* And one window, which is half a cell. */
#define AP_RING_WINDOW_TICKS (AP_TIME_BASE_HZ / AP_RING_LINE_HZ)

/* A bit cell on the wire: the level during the clock window and during the
 * data window. Levels are relative -- only transitions carry meaning. */
typedef struct {
  bool clock_window;
  bool data_window;
} ap_ring_cell_t;

/* Encode one bit, given the line level the previous cell left behind.
 * The clock window always inverts; the data window inverts again for a one. */
[[nodiscard]] ap_ring_cell_t ap_ring_biphase_encode(bool bit, bool previous);

/* The level a cell leaves behind, which is the next cell's `previous`. */
[[nodiscard]] bool ap_ring_cell_trailing_level(ap_ring_cell_t cell);

/* Decode one cell. `*error` is set when the clock window carried no transition
 * -- a bi-phase error -- and the returned bit is then Zero, as `[MAC]` §3.2
 * requires. */
[[nodiscard]] bool ap_ring_biphase_decode(ap_ring_cell_t cell, bool previous,
                                          bool *error);

/* ## The elastic-store buffer, `[MAC]` §3.3.2 p. 3-4
 *
 * "The elastic-store buffer introduces a variable delay into the NRZ serial
 * data stream. Essentially, the elastic-store buffer holds the phase offset
 * between each node's transmit and receive phase-lock loops. Nominally, when a
 * node's transmit and receive phase-lock loops are in phase, the elastic-store
 * buffer introduces a 1-bit delay."
 *
 * Its range is `0.5 bits <= ESB delay <= 1.5 bits`, and outside that the buffer
 * underflows or overflows and "the network is forced to re-initialize at a new
 * operating frequency (24 MHz)".
 *
 * This is the mechanism by which the ring stays stable: §3.3 requires the total
 * delay around the network to be "exactly an integral -- rather than a
 * fractional -- number of bit-times", and each node's buffer contributes the
 * fractional part that makes the sum come out whole.
 *
 * Delay is held in **hundredths of a bit** rather than as a float: the core is
 * deterministic across platforms and a float would put the compiler's rounding
 * into a state hash. */
#define AP_RING_ESB_NOMINAL_CENTIBITS 100
#define AP_RING_ESB_MIN_CENTIBITS 50
#define AP_RING_ESB_MAX_CENTIBITS 150

typedef enum {
  AP_RING_ESB_OK,
  AP_RING_ESB_UNDERFLOW, /* out of phase by 0.5 bit-times or less */
  AP_RING_ESB_OVERFLOW   /* out of phase by 1.5 bit-times or more */
} ap_ring_esb_status_t;

/* Classify a phase offset. The bounds are *inclusive* failures: §3.3.2 says
 * underflow occurs at "0.5 bit-times or less" and overflow at "1.5 bit-times or
 * more", so the legal interval is open at both ends and a node sitting exactly
 * on a bound has already failed. Modelled that way because the alternative --
 * treating the bounds as legal -- would let a ring appear stable in precisely
 * the condition the manual calls an error. */
[[nodiscard]] ap_ring_esb_status_t ap_ring_esb_classify(int centibits);

/* The nominal PLL centre and the deviation at which the phase offset reaches
 * each bound. `[MAC]` §3.3.1 p. 3-4: offset is "at its minimum value (0.5
 * bit-times or less) at 24 MHz -3 kHz" and "increases linearly to its maximum
 * value (1.5 bit-times or more) at 24 MHz +3 kHz". */
#define AP_RING_PLL_CENTRE_HZ AP_RING_LINE_HZ
#define AP_RING_PLL_DEVIATION_HZ 3000

/* Phase offset for a receive frequency, in hundredths of a bit, by the linear
 * relation §3.3.1 states. Clamped at the bounds, since the manual gives the
 * relation only across that interval and says "or less"/"or more" outside it.
 *
 * `PROVISIONAL`: the manual gives two endpoints and the word "linearly", so
 * the interpolation between them is documented rather than measured, and no
 * oracle exists to check it. See `RING.md` question D. */
[[nodiscard]] int ap_ring_pll_phase_offset_centibits(int deviation_hz);

/* ## Passive network bypass, `[MAC]` §3.5 p. 3-5
 *
 * "When powered off or under command of the controller, relays connect a node's
 * input coaxial cable to its output coaxial cable. At the same time, these
 * relays connect the node's transmit output to its receive input."
 *
 * Both halves happen together, which is the part worth modelling: a bypassed
 * node is simultaneously *invisible to the ring* and *looped back on itself*,
 * and the second half is what lets it run loopback self-tests while out of the
 * ring. A model with only the first half would make the ring firmware's own
 * self-test -- the first real test this controller has -- impossible to run. */
typedef struct {
  bool bypassed;
} ap_ring_bypass_t;

/* Whether the ring's signal passes through this node's transmitter, i.e.
 * whether the node participates. */
[[nodiscard]] bool ap_ring_node_in_ring(ap_ring_bypass_t state);

/* Whether the node's own transmit output reaches its own receive input. */
[[nodiscard]] bool ap_ring_node_loopback(ap_ring_bypass_t state);

#endif /* APOLLO_RING_AP_RING_PHY_H */

/* Apollo serial ports: two 2681 DUARTs as the board wires them.
 *
 * `008778-03` §3.9 and Table 2-8: "010400 - 0104FF  SIO1", "010500 - 0105FF
 * SIO2". The part is `device/ap_mc68681.h`.
 *
 * ## Stride 2, measured
 *
 * Sixteen registers over thirty-two bytes, both bytes of each word selecting
 * the same register, aliased through the 256-byte range. A dump of `010400`
 * reads `07 07 0C 0C FF FF 00 00 ...` -- every value paired, which is the
 * stride showing itself. `FINDINGS.md` C14.
 *
 * The one unpaired position in that dump is the finding worth carrying: offsets
 * 8 and 9 read `10 00`, because both address the input port change register and
 * *reading it clears it*. A register sweep of this part is an experiment rather
 * than an observation -- it starts counters, pops FIFOs and clears status.
 *
 * ## What the ports are for
 *
 * §3.9: "SIO line 0 is used for the keyboard and supports full-duplex operation
 * for bidirectional keyboards ... SIO line 1 in the DS3000 and lines 1, 2, and
 * 3 in the DS4000 interface to all other asynchronous devices." So the console
 * and the keyboard are both here, which is why Phase 3's verification for this
 * item is a console byte stream identical to the oracle's.
 *
 * And the part does one job that has nothing to do with serial lines at all:
 * "The counter/timer on the SIO chip is used for the refresh count. This is set
 * up in the timer mode to produce a square wave output on output OP3. The
 * period of the output is 15 microseconds." Dynamic memory refresh runs off
 * this DUART's timer.
 *
 * That period is exactly 99000 base units, which is worth stating because its
 * *frequency* is not an integer -- 66666.67 Hz. A model counting in hertz could
 * not represent this board's refresh clock at all; counting in `AP_TIME_BASE_HZ`
 * units represents it exactly. It is the second such case, after the interval
 * timer's prescaled 7812.5 Hz.
 */

#ifndef APOLLO_BOARD_AP_SIO_H
#define APOLLO_BOARD_AP_SIO_H

#include <stdbool.h>
#include <stdint.h>

#include "device/ap_mc68681.h"
#include "time/ap_time.h"

#define AP_SIO1_ADDR 0x010400u
#define AP_SIO2_ADDR 0x010500u
#define AP_SIO_RANGE 0x100u

/* `008778-03` Table 2-3: "IRQ1 ... 2681 SIO Port 1", priority 2 -- second only
 * to the interval timer. */
#define AP_SIO_IRQ 1u

/* §3.9's refresh square wave, in base units. Exact; see the header. */
#define AP_SIO_REFRESH_PERIOD 99000u

/* §3.9: "SIO_O is used for the keyboard". Channel A of the first part. */
#define AP_SIO_KEYBOARD_PORT 0u
#define AP_SIO_KEYBOARD_CHANNEL 0u

typedef struct {
  ap_mc68681_t port[2];

  /* Writes per port and register. The per-region counts said the firmware wrote
   * to serial 11839 times and said nothing about *which* registers, and a
   * transmit that never happened looks exactly like one that was dropped at the
   * register. This is the level at which those two separate. */
  unsigned register_writes[2][AP_MC68681_REGISTERS];

  /* Reads too, and they carry more than writes do on this part: several of its
   * registers *act* when read. Reading register 14 starts the counter and
   * register 15 stops it, so a read count is the only way to see a timer being
   * driven -- a write count cannot show it at all. */
  unsigned register_reads[2][AP_MC68681_REGISTERS];
} ap_sio_t;

void ap_sio_reset(ap_sio_t *sio);

[[nodiscard]] bool ap_sio_decode(uint32_t address, unsigned *unit,
                                 unsigned *reg);

[[nodiscard]] uint8_t ap_sio_read(ap_sio_t *sio, uint32_t address);
void ap_sio_write(ap_sio_t *sio, uint32_t address, uint8_t value);

/* The IRQ line the two parts share. */
[[nodiscard]] bool ap_sio_irq(const ap_sio_t *sio);

/* Deliver a byte to a port's receiver, as a terminal on the other end of the
 * wire would. The board has no host input of its own and must not acquire any:
 * a deterministic core cannot have a device reaching for a keyboard. The bytes
 * come from a caller that decided them in advance, which is what keeps a run
 * reproducible. */
void ap_sio_receive(ap_sio_t *sio, unsigned unit, unsigned channel,
                    uint8_t byte);

/* Deliver a byte sent at a stated rate. `sender_csr` is the clock-select value
 * the device on the other end of the wire is using; a mismatch against the
 * port's own leaves the byte in the FIFO with a framing error, which is what
 * the boot PROM's console autobaud is waiting to see. Prefer this to
 * `ap_sio_receive` for anything modelling a real device: the rate-less form
 * says "assume the wire agrees", which is a claim rather than a default. */
void ap_sio_receive_at(ap_sio_t *sio, unsigned unit, unsigned channel,
                       uint8_t byte, uint8_t sender_csr);

/* Deliver a byte whose sender states its whole framing — rate in `sender_csr`,
 * parity in `sender_mr1`. A disagreement on either leaves the byte in the FIFO
 * with the matching error bit set, which is what a driver reads to discover a
 * mis-cabled link.
 *
 * This is the form a modelled *device* should use: a keyboard or a terminal has
 * its own configuration, and saying so lets the DUART decide whether the link
 * works rather than assuming it does. */
void ap_sio_receive_framed(ap_sio_t *sio, unsigned unit, unsigned channel,
                           uint8_t byte, uint8_t sender_csr,
                           uint8_t sender_mr1);

/* Whether a port's receiver already holds a byte the program has not taken.
 * A caller feeding a script uses this to deliver the next byte only when the
 * previous one has been read, which is what a real terminal's flow looks like
 * and what stops a script from overrunning the FIFO. */
[[nodiscard]] bool ap_sio_receiver_ready(ap_sio_t *sio, unsigned unit,
                                         unsigned channel);

/* Take a byte the port's transmitter is holding, if any. The other end of the
 * same wire as `ap_sio_receive`: the board never decides where output goes, it
 * only makes it available to a caller that does.
 *
 * This is what "verify on the real output" needs -- a console byte stream from
 * the machine itself, rather than a proxy for one. */
[[nodiscard]] bool ap_sio_transmit(ap_sio_t *sio, unsigned unit,
                                   unsigned channel, uint8_t *byte);

/* A port's current receiver clock select, for a device that needs to send at
 * the rate the port is listening on. */
[[nodiscard]] uint8_t ap_sio_clock_select(ap_sio_t *sio, unsigned unit,
                                          unsigned channel);

#endif /* APOLLO_BOARD_AP_SIO_H */

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
#include "model/ap_model.h"
#include "time/ap_time.h"

#define AP_SIO1_ADDR 0x010400u
#define AP_SIO2_ADDR 0x010500u
#define AP_SIO_RANGE 0x100u

/* `008778-03` Table 2-3: "IRQ1 ... 2681 SIO Port 1", priority 2 -- second only
 * to the interval timer. */
#define AP_SIO_IRQ 1u

/* §3.9's refresh square wave, in base units. Exact; see the header. */
/* 15 microseconds, derived rather than written as a unit count -- it was
 * `297000u`, which was right only for a 19.8 GHz base. `ap_time.h`: "every
 * period is derived from it rather than written down". */
#define AP_SIO_REFRESH_PERIOD ((AP_TIME_BASE_HZ * 15u) / 1000000u)

/* The DUART's X1 crystal, which clocks the counter/timer.
 *
 * **Derived, not transcribed.** No manual here states it. What is stated is
 * §3.9's output period -- 15 microseconds -- and what is measured is the
 * firmware's own programming, read out of this core after a boot of
 * `3500_BOOT_12191_7`: serial 1's `ACR` is `E0` and its counter preload is 27.
 * `ACR[6:4]` of `110` is "Timer, clock source X1/CLK", and §3 makes the square
 * wave two terminal counts to a period, so 54 counter clocks span 15 us and
 * X1 is 3.6 MHz exactly.
 *
 * Two things make that worth more than an arithmetic identity. It is
 * *self-consistent*: 3.6 MHz with a preload of 27 gives exactly 15 us, where
 * the part's conventional 3.6864 MHz crystal would give 14.65 and force
 * §3.9's figure to be a rounding. And it is *checked* -- `sio_suite` asserts
 * that the firmware's own preload at this rate produces `AP_SIO_REFRESH_PERIOD`,
 * so the derivation cannot drift from either of the two facts it rests on.
 *
 * It also cost the time base a recomputation: 3.6 MHz does not divide 6.6 GHz,
 * so `AP_TIME_BASE_HZ` is now 19.8 GHz. `time/ap_time.h` has the discipline. */
#define AP_SIO_X1_HZ 3600000u

/* What the firmware programs, and the only preload this core has ever seen.
 * Not a constant the model depends on -- the counter runs from whatever a
 * driver loads -- but the one the derivation above is checked against. */
#define AP_SIO_MEASURED_REFRESH_PRELOAD 27u

/* §3.9: "SIO_O is used for the keyboard". Channel A of the first part. */
#define AP_SIO_KEYBOARD_PORT 0u
#define AP_SIO_KEYBOARD_CHANNEL 0u

typedef struct {
  ap_mc68681_t port[2];
  /* One clock domain per part, at `AP_SIO_X1_HZ`, with the instant each has
   * been clocked up to. Per part rather than shared because the two are
   * separate chips and a board could clock them differently -- this one does
   * not, and modelling one cursor would make that an assumption instead of an
   * observation. */
  ap_clock_t x1[2];
  ap_time_t clocked_to[2];

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

[[nodiscard]] bool ap_sio_reset(ap_sio_t *sio);

[[nodiscard]] bool ap_sio_decode(uint32_t address, unsigned *unit,
                                 unsigned *reg);

[[nodiscard]] uint8_t ap_sio_read(ap_sio_t *sio, uint32_t address);
void ap_sio_write(ap_sio_t *sio, uint32_t address, uint8_t value);

/* The IRQ line the two parts share. */
[[nodiscard]] bool ap_sio_irq(const ap_sio_t *sio);

/* Advance both parts' counter/timers to absolute time `now`, issuing one clock
 * pulse per elapsed X1 period.
 *
 * This is what makes the memory refresh a real thing rather than a register
 * value: §3.9 has the counter "set up in the timer mode to produce a square
 * wave output on output OP3", and until something advanced it the square wave
 * had no period at all. Idempotent for a `now` already reached, and monotonic,
 * as every other advance here is. */
void ap_sio_advance(ap_sio_t *sio, ap_time_t now);

/* The refresh square wave itself: serial 1's counter output, which §3.9 puts on
 * OP3. Named rather than left to a caller reading the part's field, because
 * *which* part and *which* output carry the refresh is board wiring. */
[[nodiscard]] bool ap_sio_refresh_output(const ap_sio_t *sio);

/* ## Serial 1's OP7 is IRQ13, and it exists so diagnostics can test the PICs
 *
 * `008778-03` §2.5, in the paragraph before Table 2-3: "Note that IRQ13 is not
 * available on the bus. In the DS3000, **it is connected to Output Port Bit 7
 * of the 2681 SIO chip** and is used by **diagnostics to verify the integrity
 * of the interrupt controllers**." Table 2-3 gives IRQ13 priority `4+6` on
 * controller 2 and its Domain System function as "Used During Diagnostic
 * Tests".
 *
 * So this is a wire with no device on either end -- an interrupt line the
 * machine can raise by hand, for no purpose but to see whether the controllers
 * report it. The loaded `SELF_TEST` diagnostic is the program the note is
 * about: at `01002792` it sets `OPCR` to `04`, sets `OPR[7]` and requires
 * controller 2's IR5 to be **clear**, then clears `OPR[7]` and requires it to
 * be **set**.
 *
 * **The pin is the complement of the register bit.** `[68681]`: OP7 is "either
 * the complement of `OPR[7]` or the channel B transmitter interrupt output",
 * chosen by `OPCR[7]`. So *setting* the bit drives the pin low and the command
 * that clears it raises the line -- which is the direction §2.5 wants, since an
 * interrupt "is generated when an IRQ line is raised from low to high".
 *
 * The alternate source is not modelled: `OPCR[7]` set means OP7 carries channel
 * B's transmitter interrupt instead, and nothing in any firmware here selects
 * it. A board asking for it gets no diagnostic interrupt rather than a guess. */
#define AP_SIO_DIAGNOSTIC_IRQ 13u
#define AP_SIO_OPCR_OP7_IS_TXRDYB 0x80u
#define AP_SIO_OPR_DIAGNOSTIC 0x80u
[[nodiscard]] bool ap_sio_diagnostic_interrupt(const ap_sio_t *sio);

/* How many bits a channel's link currently carries, from its `MR1`.
 *
 * Exposed because a *scripted* sender has to wait for it. `MR1` resets to a
 * five-bit link, so a keyboard scan code delivered before the firmware has
 * programmed eight arrives with its top three bits missing -- and a release
 * code, which is the make code with bit 7 set, cannot arrive at all. A script
 * that sent as soon as the receiver was free would be sending into that
 * window, and the byte would be lost silently rather than refused. */
/* How long one character takes on this channel's wire, from its own mode
 * registers and the rate its clock select names. Zero for a channel whose rate
 * is not a fixed one, or an out-of-range unit or channel. */
[[nodiscard]] ap_time_t ap_sio_character_time(const ap_sio_t *sio, unsigned unit,
                                              unsigned channel, unsigned baud);

[[nodiscard]] unsigned ap_sio_character_bits(const ap_sio_t *sio, unsigned unit,
                                             unsigned channel);

/* Whether a channel's receiver is enabled. A disabled receiver **drops** what
 * arrives -- it never sampled the character -- so a scripted sender that did
 * not wait for this would deliver into a port that is not listening and see
 * exactly what a machine ignoring the device looks like. */
[[nodiscard]] bool ap_sio_receiver_enabled(const ap_sio_t *sio, unsigned unit,
                                           unsigned channel);

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

/* ## Serial 1's input port carries the RAM configuration
 *
 * `IP0`-`IP6` of the first DUART are not serial handshake lines at all. They are
 * strapped to a **RAM configuration byte** describing which of the four memory
 * banks are populated and how large they are, and the boot PROM reads them to
 * size memory before it does anything else. A machine whose input port answers
 * zero is a machine with no memory fitted, and the firmware polls that register
 * rather than proceeding -- 9,982,874 times in one 30,000,000 instruction run.
 *
 * ### What is known about the encoding, and what is not
 *
 * Four points, from the oracle's own table, with its bank comments:
 *
 *     64   "4-4-0-0"    DN3500,  8 MB
 *     60   "4-4-4-4"    DN3500, 16 MB
 *     20   "8-8-8-8"    DN3500, 32 MB   -- and DN3000 "2-2-2-2", 8 MB
 *     14   "8-8-0-0"    DN5500, 16 MB
 *
 * The *scheme* is not derivable from four points and no manual in
 * `docs/references/` describes it: `20` means "8-8-8-8" on one machine and
 * "2-2-2-2" on another, so the field is not a plain per-bank size and depends
 * on the model. So this is a **table**, model by model, and not an encoder --
 * a function computing a byte from a size would be inventing the rule that
 * makes it work, and would be wrong for every configuration not in the table.
 * `FINDINGS.md` C115. */
#define AP_SIO_RAM_CONFIG_UNIT 0u

/* The byte for a model at a RAM size, or false when the pair is not one of the
 * four the oracle records. Refused rather than approximated: a wrong
 * configuration byte is a machine that sizes memory it does not have. */
[[nodiscard]] bool ap_sio_ram_config_byte(ap_model_id_t model,
                                          uint32_t ram_bytes, uint8_t *out);

/* ## The keyboard's own framing
 *
 * Measured, not assumed: `apollo_kbd_device::device_reset` says "keyboard comms
 * is at 8E1, 1200 baud" and sets `set_data_frame(1, 8, PARITY_EVEN,
 * STOP_BITS_1)` at 1200 in both directions.
 *
 * A keyboard has *one* framing and does not follow the port. Delivering at the
 * port's own rate -- which this board did -- makes every keypress arrive
 * cleanly whatever the firmware programmed, which is a machine where the cable
 * always agrees; a real one shows a framing or parity error when the driver
 * gets it wrong.
 *
 * `66` is 1200 baud in `[68681]`'s set one. `MR1` `03` is eight bits with
 * parity enabled -- the enable bit is **clear for parity**, which is the trap
 * in that field -- and a type of zero, which is even. */
#define AP_SIO_KEYBOARD_CSR 0x66u
#define AP_SIO_KEYBOARD_MR1 0x03u

/* Strap the configuration onto serial 1's input port. */
void ap_sio_set_ram_config(ap_sio_t *sio, uint8_t config);

#endif /* APOLLO_BOARD_AP_SIO_H */

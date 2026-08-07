/* Motorola MC6840 Programmable Timer Module.
 *
 * `[6840]` *MC6840 Programmable Timer Module (PTM)*, Motorola, MC6840UM. Cited
 * by section throughout; the manual is a scan with no text layer, so quotations
 * were read from the page images rather than grepped.
 *
 * The Apollo interval timer, `008778-03` §3.8 "Interval Time Clocks (MC6840)":
 * "Three 16-bit timing elements are contained within an LSI Timer device." At
 * `010800` in the 64 MB allocation.
 *
 * This is the part. What the board feeds it is separate and belongs to
 * `board/`, but is worth recording here because it is what makes the part
 * useful and it lands exactly on this project's time base:
 *
 *   Timer 1  250 kHz    (4 us period)
 *   Timer 2  125 kHz    (8 us period)
 *   Timer 3  62.5 kHz   (16 us period), "may be prescaled to make the
 *                        effective input signal have a 128-microsecond period"
 *
 * That last line is a cross-check rather than a coincidence: 16 us prescaled to
 * 128 us is a division by eight, and `[6840]` §3.6.1's control register 3 bit 0
 * selects exactly "CLK ÷1" or "CLK ÷8". Two manuals agreeing on the divisor.
 *
 * All three inputs divide `AP_TIME_BASE_HZ` exactly (26400, 52800 and 105600
 * base units per pulse), so adding this device needs no change to the time
 * base. The prescaled rate is the case that justifies counting in base units at
 * all: 7812.5 Hz is not an integer frequency, but its period is exactly 844800
 * base units.
 *
 * ## What is modelled
 *
 * Both counting modes -- continuous and single shot -- in both sixteen-bit and
 * dual eight-bit operation, and the whole register and interrupt model around
 * them: both control register aliases, the write and read buffering, the status
 * register, the prescaler, and every one of `[6840]`'s five ways of clearing an
 * interrupt.
 *
 * ## The mode encoding, which is not three contiguous bits
 *
 * It is tempting to read bits 5, 4 and 3 as one mode field. They are not:
 * `[6840]` §§3.7-3.10 give each bit a separate job, and the job changes
 * depending on bit 3.
 *
 *   bit 3 = 0   the counting modes
 *               bit 5   0 continuous, 1 single shot
 *               bit 4   0 a latch write also reinitialises the counter, 1 not
 *               bit 2   0 sixteen-bit, 1 dual eight-bit
 *   bit 3 = 1   the measurement modes
 *               bit 4   0 period measurement, 1 pulse-width measurement
 *               bit 5   0 interrupt if shorter than the time out, 1 if longer
 *
 * Reading it as a single field is how this module first went wrong: it
 * required bits 5-3 to equal `010` for continuous, and so **declined
 * `XX0000XX`** — which is equally 16-bit continuous, differing only in whether a
 * latch write reinitialises. Half of continuous mode was refused, and nothing
 * caught it because the half that worked was the half the tests used.
 *
 * ## What is declined
 *
 * The two **measurement modes**, and now for a reason about the machine rather
 * than about this module. Both time a digital signal presented on a timer's
 * gate pin — "The digital signal to be measured is applied to the individual
 * gate pin that is assigned to each timer" — and on this board the three gates
 * have nothing connected to them; the timers take fixed clocks from `008778-03`
 * §3.8. There is no signal to measure, so the modes are decoded, reported, and
 * refused rather than approximated.
 *
 * A timer in a declined mode does not count at all. That is deliberate over
 * "count as if continuous": a timer silently behaving like a different mode
 * produces plausible interrupts at the wrong rate, which survives a boot and
 * corrupts every timing measurement built on it.
 */

#ifndef APOLLO_DEVICE_AP_MC6840_H
#define APOLLO_DEVICE_AP_MC6840_H

#include <stdbool.h>
#include <stdint.h>

#define AP_MC6840_TIMERS 3u

/* `[6840]` Figure 2-6's register select decode. Write and read differ, so both
 * are named; RS is the three address lines RS2/RS1/RS0. */
typedef enum {
  /* Write: control register 3 or 1, chosen by CR2 bit 0. Read: no operation. */
  AP_MC6840_RS_CONTROL_1_OR_3 = 0,
  /* Write: control register 2. Read: the status register. */
  AP_MC6840_RS_CONTROL_2_OR_STATUS = 1,
  AP_MC6840_RS_TIMER1_MSB = 2, /* write MSB buffer / read timer 1 counter */
  AP_MC6840_RS_TIMER1_LSB = 3, /* write timer 1 latches / read LSB buffer */
  AP_MC6840_RS_TIMER2_MSB = 4,
  AP_MC6840_RS_TIMER2_LSB = 5,
  AP_MC6840_RS_TIMER3_MSB = 6,
  AP_MC6840_RS_TIMER3_LSB = 7,
} ap_mc6840_rs_t;

/* Control register bits, `[6840]` §3.6.1. Bits 1 through 7 mean the same in all
 * three registers; bit 0 is unique to each. */
#define AP_MC6840_CR_INTERNAL_CLOCK 0x02u /* 0 external (Cx), 1 internal (E) */
#define AP_MC6840_CR_DUAL_8BIT 0x04u      /* 0 sixteen-bit, 1 dual eight-bit */
#define AP_MC6840_CR_MEASUREMENT 0x08u    /* bit 3: 1 selects a measurement mode */
#define AP_MC6840_CR_BIT4 0x10u           /* see the header: two different jobs */
#define AP_MC6840_CR_BIT5 0x20u           /* see the header: two different jobs */
#define AP_MC6840_CR_IRQ_ENABLE 0x40u     /* bit 6 */
#define AP_MC6840_CR_OUTPUT_ENABLE 0x80u  /* bit 7 */

/* The four modes `[6840]` §§3.7-3.10 describe. */
typedef enum {
  AP_MC6840_MODE_CONTINUOUS = 0,
  AP_MC6840_MODE_SINGLE_SHOT,
  AP_MC6840_MODE_PERIOD_MEASUREMENT,
  AP_MC6840_MODE_PULSE_WIDTH_MEASUREMENT,
} ap_mc6840_mode_t;

/* Bit 0, by register. */
#define AP_MC6840_CR1_TIMERS_PRESET 0x01u /* 0 all operate, 1 all preset */
#define AP_MC6840_CR2_SELECT_CR1 0x01u    /* 0 CR3 at RS 0, 1 CR1 at RS 0 */
#define AP_MC6840_CR3_PRESCALE 0x01u      /* 0 CLK/1, 1 CLK/8 */

/* Status register, `[6840]` §3.11. Bits 3-6 read zero: "Not Used". */
#define AP_MC6840_STATUS_TIMER1 0x01u
#define AP_MC6840_STATUS_TIMER2 0x02u
#define AP_MC6840_STATUS_TIMER3 0x04u
#define AP_MC6840_STATUS_COMPOSITE 0x80u

typedef struct {
  uint16_t latch;
  uint16_t counter;
  uint8_t control;
  bool interrupt_flag;
  bool output;
  /* The gate pin. `[6840]` §3.7's counter-enable condition is "Reset clear"
   * and "Gate pin is low", so a high gate holds the timer. Modelled as the pin
   * rather than as an enable, because the manual's clearing rules refer to the
   * gate *going* low and a level cannot express an edge. */
  bool gate;
  /* Dual eight-bit operation counts two bytes rather than one word, so the
   * low half needs its own reload. `[6840]` §3.7.2: the period is "the count in
   * the LSB latch +1, times the count in the MSB latch +1, times the period of
   * the clock", which is two nested counters and not a wider one. */
  uint8_t lsb_counter;

  /* Single shot puts out "only one pulse ... for each Counter Initialization",
   * although "internally, the count recycling is continuous as if in the
   * Continuous Mode". So the interrupts keep coming and the output does not. */
  bool single_shot_fired;

  /* Prescaler state, timer 3 only. `[6840]` §3.6.1: "Bit 0 in control register
   * #3 is a clock prescalar and is available only in control register #3." */
  uint8_t prescale_count;
} ap_mc6840_timer_t;

typedef struct {
  ap_mc6840_timer_t timer[AP_MC6840_TIMERS];

  /* `[6840]` §3.5: a 16-bit write goes MSB to a buffer and then LSB to the
   * latch, which takes the buffer with it; a 16-bit read takes the counter's MS
   * byte and puts the LS byte in a buffer for the next read. "The contents of
   * the MSB Buffer and LSB Buffer are changed only by two methods: Being
   * written over OR A hardware Reset occurs." */
  uint8_t msb_buffer;
  uint8_t lsb_buffer;

  uint8_t control2; /* only bit 0 is used, and it selects CR1 or CR3 at RS 0 */

  /* `[6840]` §3.11's interrupt clearing rule needs memory: "Read the status
   * register (RS), then read the timer (RT) causing the interrupt. (An
   * interrupt that occurs between RS and RT will *not* be cleared.)"
   *
   * So a flag raised after the status read survives the timer read, and only a
   * snapshot taken at the status read can tell the two apart. */
  uint8_t status_snapshot;
  bool status_was_read;
} ap_mc6840_t;

/* Hardware reset. `[6840]` §4.1: latches "default to $FFFF" if not written, and
 * "bit 0 of control register 2 defaults to zero (control register #3 is
 * selected address zero)". */
void ap_mc6840_reset(ap_mc6840_t *ptm);

void ap_mc6840_write(ap_mc6840_t *ptm, ap_mc6840_rs_t rs, uint8_t value);
[[nodiscard]] uint8_t ap_mc6840_read(ap_mc6840_t *ptm, ap_mc6840_rs_t rs);

/* Drive one timer's gate pin. Low enables counting. */
void ap_mc6840_set_gate(ap_mc6840_t *ptm, unsigned index, bool high);

/* One pulse of a timer's clock input. The board decides the rate; the part only
 * counts what it is given. */
void ap_mc6840_clock(ap_mc6840_t *ptm, unsigned index);

/* The IRQ pin. `[6840]` §3.11: "A composite interrupt is caused by a timer
 * interrupt *and* that timer's interrupt flag enabled (CRX6 = 1). A composite
 * interrupt causes IRQ to be asserted." */
/* Whether a timer counts the **internal** clock or the external `Cx` input.
 *
 * `[6840]`: the counter "divides either the internal or external clock", and
 * control-register bit 1 is which. This core read the bit nowhere, so every
 * timer counted the internal clock whatever a driver selected -- and a driver
 * that switched to an external source would see no change at all, which is the
 * failure that looks like the timer working.
 *
 * The external *input* is not modelled: nothing on this board drives `Cx`, and
 * a rate invented for it would be a claim about a wire that is not there. So
 * this reports the selection and `ap_mc6840_clock` still advances only the
 * internal one -- the part says what it was asked to do, and the board's
 * silence is the board's.
 */
[[nodiscard]] bool ap_mc6840_uses_internal_clock(const ap_mc6840_t *ptm,
                                                 unsigned index);
[[nodiscard]] bool ap_mc6840_irq(const ap_mc6840_t *ptm);

/* The mode a timer is programmed for, decoded per the header. */
[[nodiscard]] ap_mc6840_mode_t ap_mc6840_mode(const ap_mc6840_t *ptm,
                                              unsigned index);

/* Whether that mode is one this module implements. False for the two
 * measurement modes; see the header. A caller must check rather than assume,
 * because a timer counting in the wrong mode produces plausible interrupts at
 * the wrong rate. */
[[nodiscard]] bool ap_mc6840_mode_supported(const ap_mc6840_t *ptm,
                                            unsigned index);

/* A timer's output pin, valid only when the mode is supported. */
[[nodiscard]] bool ap_mc6840_output(const ap_mc6840_t *ptm, unsigned index);

#endif /* APOLLO_DEVICE_AP_MC6840_H */

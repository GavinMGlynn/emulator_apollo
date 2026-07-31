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
 * The 16-bit continuous mode, which is what a periodic interval timer uses, and
 * the whole register and interrupt model around it: both control register
 * aliases, the write and read buffering, the status register, and every one of
 * `[6840]`'s five ways of clearing an interrupt.
 *
 * ## What is declined
 *
 * Dual 8-bit operation, single-shot, period measurement and pulse-width
 * measurement. Not because they are hard but because their timing rules have
 * not been transcribed from `[6840]` §§3.7.2-3.10, and the two measurement
 * modes additionally time an external gate signal that no part of this machine
 * currently drives.
 *
 * `ap_mc6840_mode_supported` reports it, and a timer left in a declined mode
 * simply does not count. That is a deliberate choice over "count as if
 * continuous": a timer that silently behaved like a different mode would
 * produce plausible interrupts at the wrong rate, which is the failure that
 * survives a boot and corrupts every timing measurement downstream.
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
#define AP_MC6840_CR_MODE_MASK 0x38u      /* bits 5,4,3 select the mode */
#define AP_MC6840_CR_IRQ_ENABLE 0x40u     /* bit 6 */
#define AP_MC6840_CR_OUTPUT_ENABLE 0x80u  /* bit 7 */

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
[[nodiscard]] bool ap_mc6840_irq(const ap_mc6840_t *ptm);

/* Whether a timer's programmed mode is one this module implements. False for
 * the declined modes; see the header. A caller must check rather than assume,
 * because a timer counting in the wrong mode produces plausible interrupts at
 * the wrong rate. */
[[nodiscard]] bool ap_mc6840_mode_supported(const ap_mc6840_t *ptm,
                                            unsigned index);

/* A timer's output pin, valid only when the mode is supported. */
[[nodiscard]] bool ap_mc6840_output(const ap_mc6840_t *ptm, unsigned index);

#endif /* APOLLO_DEVICE_AP_MC6840_H */

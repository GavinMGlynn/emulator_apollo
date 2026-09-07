/* Motorola MC146818A Real-Time Clock plus RAM.
 *
 * `[146818]` *MC146818A Real-Time Clock Plus RAM (RTC)*, Motorola. The scan
 * carries an OCR layer of poor quality, so the register definitions were read
 * from the page images; quotations below are transcribed from those.
 *
 * The Apollo calendar, `008778-03` §3.6: "The calendar chip used on the
 * CPU/Motherboard is the MC146818. This device combines three features: a
 * time-of-day clock with an alarm and a 100-year calendar, a programmable
 * periodic interrupt and square-wave generator, and 50 bytes of low-power
 * static RAM. The RAM is used to store configuration information." At `010900`,
 * and battery-backed.
 *
 * ## Time comes from the caller, never from the host
 *
 * This is the one place a device could quietly destroy the project's central
 * property. The oracle seeds its calendar from the **host clock** — a dump of
 * `010900` returns whatever today is — and a core that did the same would
 * produce a different state hash on every run, and goldens that rot overnight.
 *
 * So this module has no notion of the wall clock at all. It is advanced by
 * `ap_mc146818_advance` in time-base units like every other device, and the
 * date it starts from is whatever the caller sets. `CLAUDE.md` requires the
 * headless frontend to have "no wall clock"; a battery-backed clock is exactly
 * the device that tempts one in through the back door.
 *
 * ## What is modelled
 *
 * The ten time and calendar bytes, the four registers, the 50 RAM bytes, the
 * once-per-second update cycle with its carry into a real calendar, the alarm
 * with `[146818]`'s don't-care codes, the update-ended and alarm interrupt
 * flags, and Register C's read-to-clear.
 *
 * ## The periodic rates, and the base that carries them
 *
 * **All fifteen are implemented.** This section used to decline the six fastest
 * and the paragraphs below record why, because the reason was a real one and
 * the fix has a real cost. `[146818]` Table 5 gives
 * the rates as 32768/2^n Hz, and `AP_TIME_BASE_HZ` = 336,600,000,000 factors as
 *
 *     2^9 * 3^2 * 5^8 * 11 * 17
 *
 * — so it carries only 2^9 where 32.768 kHz needs 2^15, and the rates from
 * 1.024 kHz up to 32.768 kHz are **not exactly representable**. `CLAUDE.md`'s
 * rule is that such a clock means recomputing the base, and the cost of doing
 * so is measured rather than guessed at. With `ap_time_t` a `uint64_t` the
 * representable span is `2^64 / base`:
 *
 *   as it stands             336.6 GHz              span 634 days
 *   including 32.768 kHz     base * 64 = 21.5 THz   span 9.9 days
 *   including the 4.194304
 *   MHz crystal itself       base * 8192 = 2.76 PHz span 1 hour 52 minutes
 *
 * The crystal can therefore never be a clock domain in a 64-bit base at all,
 * which is worth knowing on its own. The six fast rates could be, at a cost of
 * 64x the representable span, and that trade has not been made because nothing
 * has been observed using them — the Apollo firmware never writes the calendar.
 *
 * **These figures were wrong until the video clock item's recomputation was
 * carried through to them**, and the way they were wrong is the point: the
 * factorisation was written as `2^9 * 3 * 5^8 * 11`, which is neither the
 * current base nor the one before it, and the spans (88.6 years, 505 days,
 * 3.95 days) belonged to a base two recomputations old. A derived constant has
 * derived consequences, and nothing checked these when it moved. The
 * *conclusion* survived unchanged, because only the power of two decides it —
 * which is exactly why the error could sit there.
 *
 * **The trade was made.** `AP_TIME_BASE_HZ` is the LCM times 2^6, so it carries
 * 2^15 and every one of Table 5's rates divides it exactly -- 32.768 kHz down
 * to 2 Hz. The span falls from 634 days to 9.9 days of emulated time, which is
 * four orders of magnitude beyond the longest run this project makes and was
 * the price of a device able to honour its own rate table.
 *
 * It cost two tests their length: the calendar's date-carry tests spanned a
 * fortnight and now span nine days, which still crosses a month end, both
 * February lengths and a year end. That is a genuine reduction in what they
 * exercise and is recorded rather than glossed.
 *
 * `ap_mc146818_rate_supported` still reports which case a rate falls in. It now
 * answers true for all fifteen, and it is kept rather than deleted because the
 * *crystal* remains unrepresentable and a future clock could move the base
 * again.
 *
 * The **square-wave output** pin is driven: `ap_mc146818_square_wave_hz`
 * reports its frequency, which shares the same selector and the same table as
 * the periodic interrupt, gated by Register B's `SQWE` and held at zero for a
 * rate this core cannot represent exactly. Nothing on this board is wired to
 * the pin, which is a fact about the board rather than the part, and is no
 * reason for the part to be unable to say what it is driving.
 *
 * ## `UIP`, and the 492 microseconds it is high for
 *
 * **Modelled.** This module used to answer `UIP` as a constant zero on the
 * grounds that its own update cycle is instantaneous, and named the blocker:
 * "modelling the 248 microsecond window would need the rate tables that are
 * declined". The 2026-08-22 walk read those tables -- `[146818]` Table 6 and
 * Figure 15 -- so the blocker is gone and the bit now pulses.
 *
 * The update cycle here is still instantaneous; what is modelled is the
 * *window around it*. `ap_mc146818_update_in_progress` reports the bit high for
 * the `tBUC + tUC` that ends at the one-second boundary the update lands on,
 * which is where Figure 15 puts it: `UF` is set as `UIP` falls.
 *
 * That is a real behavioural difference and not a cosmetic one. A driver that
 * polls `UIP` and waits for it to clear before reading the time now waits, and
 * one that reads the clock bytes while `UIP` is high is reading what the part
 * calls invalid data. This core still hands it valid data -- the bytes are
 * never in transition here, because the update is atomic -- which is
 * permissive rather than wrong, and is the one part of `[146818]` p. 14 left
 * unmodelled: "the MC146818A protects the program from reading transitional
 * data ... by switching the time, calendar, and alarm portion of the RAM off
 * the microprocessor bus during the entire update cycle".
 *
 * The `DSE` bit's two special updates are **applied**: last Sunday in April
 * 1:59:59 -> 3:00:00, last Sunday in October 1:59:59 -> 1:00:00, the second
 * only the *first* time the hour comes round. That "first" is the whole
 * difficulty -- the hour repeats, and a model that shifted on both passes would
 * hold the clock at one o'clock for ever.
 */

#ifndef APOLLO_DEVICE_AP_MC146818_H
#define APOLLO_DEVICE_AP_MC146818_H

#include <stdbool.h>
#include <stdint.h>

#include "time/ap_time.h"

/* `[146818]`'s address space: ten clock bytes, four registers, then RAM. */
#define AP_MC146818_SECONDS 0x00u
#define AP_MC146818_SECONDS_ALARM 0x01u
#define AP_MC146818_MINUTES 0x02u
#define AP_MC146818_MINUTES_ALARM 0x03u
#define AP_MC146818_HOURS 0x04u
#define AP_MC146818_HOURS_ALARM 0x05u
#define AP_MC146818_DAY_OF_WEEK 0x06u
#define AP_MC146818_DAY_OF_MONTH 0x07u
#define AP_MC146818_MONTH 0x08u
#define AP_MC146818_YEAR 0x09u
#define AP_MC146818_REGISTER_A 0x0Au
#define AP_MC146818_REGISTER_B 0x0Bu
#define AP_MC146818_REGISTER_C 0x0Cu
#define AP_MC146818_REGISTER_D 0x0Du
#define AP_MC146818_RAM_BASE 0x0Eu
#define AP_MC146818_BYTES 0x40u

/* Register A. "UIP ... is a status flag that may be monitored by the program",
 * read-only; DV2-DV0 select the divider; RS3-RS0 the rate. */
#define AP_MC146818_A_UIP 0x80u
/* `[146818]` Table 6, *Update Cycle Times*, in microseconds.
 *
 * `tBUC` is the lead `UIP` gives before the update cycle begins, and the table
 * prints 244 against all three time bases; p. 15 says the same in words --
 * "when UIP is a '0', the update cycle is not in progress and will not be for
 * at least 244 us (**for all time bases**)". `tUC` is the cycle itself, and it
 * is the one quantity the time base changes: 248 us on either fast crystal,
 * 1984 us on the watch crystal.
 *
 * `UIP` is high across both spans. Figure 15 draws them end to end beneath one
 * pulse, and p. 14 states the sum outright: "periodic interrupts that occur at
 * a rate of greater than tBUC + tUC allow valid time and date information to be
 * read at each occurrence of the periodic interrupt."
 *
 * (p. 11 prints the slow figure as **1948** us. Table 6 and p. 14 both say
 * 1984, Table 6 carries it beside the fast figure, and 1984 us is 2^16/33 ms to
 * within rounding -- the shape a divider chain produces. A transposition, and
 * `docs/references/MC146818A_WALK.md` records it.) */
#define AP_MC146818_TBUC_US 244u
#define AP_MC146818_TUC_US 248u
#define AP_MC146818_TUC_SLOW_US 1984u
/* `DV2-DV0`, Register A bits 6-4. The datasheet's Table 4 gives them "three
 * uses": select one of three operating time bases -- 4.194304 MHz, 1.048576
 * MHz or 32.768 kHz -- or **hold the divider chain in reset**, which "prevents
 * interrupts or SQW output from operating" and is how a driver sets the time
 * precisely without the clock moving under it.
 *
 * This core read the field nowhere, so the seconds advanced whatever was
 * written there and a driver holding the dividers in reset watched the time
 * change while it was setting it. */
#define AP_MC146818_A_DIVIDER 0x70u
/* `11X`: the two codes that hold the chain in reset. */
#define AP_MC146818_A_DIVIDER_RESET 0x60u
#define AP_MC146818_A_RATE 0x0Fu

/* Register B, all read/write. */
#define AP_MC146818_B_SET 0x80u   /* inhibits the update cycle */
#define AP_MC146818_B_PIE 0x40u   /* periodic interrupt enable */
#define AP_MC146818_B_AIE 0x20u   /* alarm interrupt enable */
#define AP_MC146818_B_UIE 0x10u   /* update-ended interrupt enable */
#define AP_MC146818_B_SQWE 0x08u  /* square-wave enable */
#define AP_MC146818_B_DM 0x04u    /* 1 binary, 0 BCD */
#define AP_MC146818_B_24HOUR 0x02u /* 1 twenty-four hour, 0 twelve */
#define AP_MC146818_B_DSE 0x01u   /* daylight savings enable */

/* Register C, read-only. "IRQF = PF*PIE + AF*AIE + UF*UIE". */
#define AP_MC146818_C_IRQF 0x80u
#define AP_MC146818_C_PF 0x40u
#define AP_MC146818_C_AF 0x20u
#define AP_MC146818_C_UF 0x10u

/* Register D, read-only. "VRT ... b6 TO b0 - The remaining bits of Register D
 * are unused. They cannot be written, but are always read as 0's." */
#define AP_MC146818_D_VRT 0x80u

/* A wall-clock instant, supplied by the caller. Not a host time: the caller
 * decides, and a deterministic frontend supplies a fixed one. */
typedef struct {
  unsigned year;        /* full year, e.g. 1987 */
  unsigned month;       /* 1-12 */
  unsigned day;         /* 1-31 */
  /* 1-7, `[146818]` numbers Sunday as 1.
   *
   * **Domain/OS never reads this register**, which is worth knowing before a
   * boot is taken as evidence about it: `007196-01`'s `CAL_$TIMEDATE_REC_T` is
   * year, month, day, hour, minute and second with **no weekday field**, and
   * `CAL_$WEEKDAY` "computes the day of the week for any Gregorian date" from
   * the other three. So this field is modelled and tested from the datasheet
   * alone, and always will be. *The operating system's own enumeration differs
   * by one* -- `CAL_$SUN` through `CAL_$SAT` have "ordinal values 0 through 6"
   * -- so a driver that did read the register would subtract. */
  unsigned day_of_week;
  unsigned hour;        /* 0-23, always twenty-four hour here */
  unsigned minute;      /* 0-59 */
  unsigned second;      /* 0-59 */
} ap_mc146818_time_t;

typedef struct {
  uint8_t ram[AP_MC146818_BYTES];
  /* The clock kept as plain numbers rather than in the register bytes, because
   * the register format depends on DM and 24/12, which software may change at
   * any moment. Keeping the truth separate means a format change re-presents
   * the same instant instead of reinterpreting the old bytes as the new
   * format -- which would silently move the clock. */
  ap_mc146818_time_t now;
  /* Base-unit time the last one-second update happened at. */
  ap_time_t updated_to;
  ap_clock_t second_clock;
  /* The instant `ap_mc146818_advance` was last called with. `updated_to` moves
   * only in whole seconds, so it cannot say *where inside* the current second
   * the clock stands -- and that is exactly the question `UIP` answers.
   *
   * Deliberately absent from `ap_board_hash_calendar`: it is a copy of the
   * caller's own argument, equal to the machine's absolute time on every call,
   * so hashing it would hash the clock the harness already hashes. */
  ap_time_t stepped_to;

  /* The periodic interrupt, kept separate because it runs at its own rate and
   * must not be quantised to the one-second update. */
  ap_time_t periodic_to;
  ap_clock_t periodic_clock;

  /* Whether the divider chain was held in reset at the last advance. The
   * release is what needs remembering, not the hold: `[146818]` p. 13 and p. 15
   * both give the first update cycle after a release as one-half second later,
   * and a level test cannot tell the instant the chain restarted from every
   * instant since. */
  bool divider_held;

  /* Whether `DSE`'s special update has already been taken in the hour it
   * applies to. October's rule is "when the time **first** reaches 1:59:59 AM",
   * and the hour repeats -- without this the clock would be pushed back to one
   * o'clock for ever. Cleared by any hour that is not two. */
  bool dst_shifted;
  /* How many update cycles have run: a diagnostic, not machine state. A clock
   * that is not advancing and a clock whose seconds happen to read alike are
   * the same value and different faults. */
  unsigned update_cycles;

} ap_mc146818_t;

/* Reset, and set the clock to `start`. Returns false if the one-second tick is
 * not representable in the time base, which cannot happen but is checked
 * because the base is derived. */
[[nodiscard]] bool ap_mc146818_reset(ap_mc146818_t *rtc,
                                     const ap_mc146818_time_t *start);

[[nodiscard]] uint8_t ap_mc146818_read(ap_mc146818_t *rtc, uint8_t address);
void ap_mc146818_write(ap_mc146818_t *rtc, uint8_t address, uint8_t value);

/* Advance to absolute time `now`, running one update cycle per elapsed second.
 * Monotonic and by whole seconds, with the remainder carried. */
void ap_mc146818_advance(ap_mc146818_t *rtc, ap_time_t now);

/* The IRQ pin: `[146818]`'s "IRQF = PF*PIE + AF*AIE + UF*UIE". */
/* The earliest instant this part's line could change by time alone -- the
 * conservative lower bound `board/ap_sio.h` states the rule for. Two clocks can
 * raise a flag: the periodic interrupt and the one-second update, each carrying
 * its own remainder, so the bound is whichever is due first. A clock with no
 * period is stopped and cannot raise anything. */
[[nodiscard]] ap_time_t
ap_mc146818_interrupt_next_change(const ap_mc146818_t *rtc);

[[nodiscard]] bool ap_mc146818_irq(const ap_mc146818_t *rtc);

/* The periodic interrupt frequency selected by Register A's RS3-RS0, in hertz,
 * or zero for `[146818]` Table 5's "None" row.
 *
 * The table publishes an interval and a square-wave frequency side by side --
 * 30.517 us against 32.768 kHz, 500 ms against 2 Hz -- and the interval is the
 * reciprocal of the frequency throughout, so one number serves for both. */
[[nodiscard]] uint32_t ap_mc146818_periodic_hz(const ap_mc146818_t *rtc);

/* False when the selected rate is one this core cannot represent exactly. See
 * the header: the six fastest rates need 2^15 in a time base that carries 2^9.
 * A caller must check rather than assume, because a periodic interrupt running
 * at a rounded rate is indistinguishable from a correct one and would drift
 * whatever is built on it. */
[[nodiscard]] bool ap_mc146818_rate_supported(const ap_mc146818_t *rtc);

/* The **square-wave output pin**, in hertz, or zero when it is not being
 * driven.
 *
 * `[146818]` Register B's `SQWE`: "When this bit is set to a 1, a square-wave
 * signal at the frequency set by the rate-selection bits RS3-RS0 is driven out
 * on the SQW pin. When the SQWE bit is set to zero, the SQW pin is held low."
 * So the pin is the same selector and the same table as the periodic interrupt
 * -- which is why `ap_mc146818_periodic_hz` serves both -- gated by one bit.
 *
 * This was declined on the grounds that nothing on this board is wired to the
 * pin. That is a fact about the *board*, not about the part, and it is not a
 * reason for the part to be unable to say what it is driving: a stored-but-inert
 * control bit is indistinguishable from an implemented one, which is exactly
 * the confusion this function removes. A caller with nothing wired to it simply
 * does not ask.
 *
 * `ap_mc146818_rate_supported` still governs: a rate this core cannot represent
 * exactly is not one it should claim to be driving. */
[[nodiscard]] uint32_t ap_mc146818_square_wave_hz(const ap_mc146818_t *rtc);

/* Whether the divider chain is running. False while `DV2-DV0` hold it in
 * reset, when neither the update cycle nor the square wave operates. */
[[nodiscard]] bool ap_mc146818_divider_running(const ap_mc146818_t *rtc);

/* The operating time base `DV2-DV0` selects, in hertz: `[146818]` Table 4's
 * 4.194304 MHz (`000`), 1.048576 MHz (`001`) or 32.768 kHz (`010`). Zero for
 * every other code.
 *
 * This and `ap_mc146818_divider_running` disagree about `011`, `100` and `101`,
 * and the disagreement is Table 4's: the table lists five rows and its note says
 * "other combinations of divider bits are used for **test purposes only**", so
 * those three neither hold the chain in reset nor name a crystal. The chain
 * runs -- which is what `ap_mc146818_divider_running` reports -- at a rate the
 * table declines to state.
 *
 * Nothing on this board selects one: `008778-03` section 3.6 and the
 * datasheet's own interface figures both show the 4.194304 MHz crystal. */
[[nodiscard]] uint32_t ap_mc146818_time_base_hz(const ap_mc146818_t *rtc);

/* Register A's `UIP`, which `ap_mc146818_read` returns in bit 7.
 *
 * `[146818]` p. 15: "When UIP is a '1', the update cycle is in progress or will
 * soon begin. When UIP is a '0', the update cycle is not in progress and will
 * not be for at least 244 us (for all time bases) ... Writing the SET bit in
 * Register B to a '1' inhibits any update cycle and then clears the UIP status
 * bit."
 *
 * So two things hold it low wherever the second stands -- `SET`, and a divider
 * chain held in reset -- which are two of the three conditions p. 14 opens the
 * update cycle with. */
[[nodiscard]] bool ap_mc146818_update_in_progress(const ap_mc146818_t *rtc);

/* The clock as numbers, for tests and for a state hash that must not depend on
 * the register format software happens to have selected. */
[[nodiscard]] ap_mc146818_time_t ap_mc146818_now(const ap_mc146818_t *rtc);

#endif /* APOLLO_DEVICE_AP_MC146818_H */

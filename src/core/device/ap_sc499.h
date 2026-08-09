/* Archive SC-499 cartridge tape controller.
 *
 * `[SC499]` *Archive Corporation SC-499 Tape Controller Information Guide*,
 * §1.9. The Apollo cartridge tape controller: `008778-03` Table 2-9 places the
 * drive at `050000`-`050F80`, AT `218`-`21F`.
 *
 * ## Four addresses, two of them write-only
 *
 * `[SC499]`: "Only four of the address locations are used by the SC-499."
 *
 *   BASE+0  data/command, read or write
 *   BASE+1  control register on write, status register on read
 *   BASE+2  start DMA -- "Any write to this register will cause DMAGO to be
 *           active", whatever the value written
 *   BASE+3  reset DMA, likewise
 *
 * A read sweep of this part finds only the first two, because the other two are
 * write-triggered commands with nothing behind them. That is how it was first
 * measured here (`FINDINGS.md` C17) and the manual is what completed the
 * picture (C18) -- neither source alone gives the part.
 *
 * ## The status register's bit numbers, and the polarity column the OCR ate
 *
 * This used to say the bit numbers "come from the measurement", because
 * `[SC499]`'s *text layer* loses them. **The page image does not.** PDF page 15
 * carries a two-column table the extraction flattens into prose, and reading
 * the image gives both columns:
 *
 *     BIT 7   0 = IRQF    interrupt request flag
 *     BIT 6   0 = RDY     ready, from LSI chip
 *     BIT 5   0 = EXC     exception, from LSI chip
 *     BIT 4   1 = DONE    done, from DMA logic
 *     BIT 3   1 = DIRC    direction, controller to host
 *
 * The positions confirm what was inferred. **The polarity was never inferred at
 * all, and was wrong**: RDY and EXC are *active low*, so an idle-and-ready
 * controller reads bit 6 as **0**, not 1. This core had all five active high
 * and set `ready` at reset to reproduce the measured `40` -- two errors that
 * cancelled into the right byte with the opposite meaning. `40` means **not
 * ready**, which is what a controller that has just been reset is.
 *
 * Two independent implementations agree with the image, which is what makes
 * this safe to change rather than merely differently guessed: Linux's
 * `tpqic02.h` defines `QIC_STAT_READY 0x40` as active low, and the oracle's own
 * `sc499.cpp` defines `SC499_STAT_RDY 0x40` as active low.
 *
 * Bit 7 was the one dissent, and **the dissent was wrong**. The image prints
 * `0 = IRQF`; both other implementations call it active *high*, and this core
 * followed them on the strength of a measured `0` at reset -- but that
 * measurement was a *dump of the oracle*, which is to say MAME's model of the
 * bit rather than the hardware's behaviour. The argument against the image was
 * that an active-low flag would read as asserted while `IEN` is clear and the
 * IRQ line tri-stated; that conflates the status *bit* with the *line*. §1.10
 * separates them: "Each interrupt source bit, RDY, EXC, and DONE ... can be
 * read through the Status Register regardless of the state of the interrupt
 * masks", while "the IRQ line is tri-stated when IEN is cleared" --
 * `ap_sc499_irq` gates the line on `IEN` and always did.
 *
 * The guest settles it, independently of either emulator. Domain/OS's tape
 * reset waits for the status register to read exactly `F7` and then exactly
 * `57`, and with IRQF active low both fall out of the manual's own combinational
 * rule with nothing left over:
 *
 *     idle, nothing asserted   IRQF inactive -> bit 7 = 1    F7
 *     exception asserted       IRQF active   -> bit 7 = 0    57
 *
 * Modelled active low, as the page image prints it. This core disagrees with
 * both `sc499.cpp` and Linux's `tpqic02.h` here, which is the case `CLAUDE.md`
 * anticipates in saying to expect to out-accurate the oracle -- and the
 * out-accuracy is checkable: with the bit active high the driver's first
 * comparison can never match.
 *
 * ## What is modelled
 *
 * The register model and the reset semantics. What is not is the QIC-02 command
 * set itself -- the tape motion, the block protocol, the drive behind the
 * controller. Those need a tape image and a drive; this is the interface a
 * driver programs.
 */

#ifndef APOLLO_DEVICE_AP_SC499_H
#define APOLLO_DEVICE_AP_SC499_H

#include <stdbool.h>
#include <stdint.h>

#include "time/ap_time.h"

#define AP_SC499_REGISTERS 4u

typedef enum {
  AP_SC499_DATA = 0u,           /* read or write */
  AP_SC499_CONTROL_STATUS = 1u, /* write control, read status */
  AP_SC499_DMAGO = 2u,          /* any write starts DMA */
  AP_SC499_RSTDMA = 3u,         /* any write resets the DMA logic */
} ap_sc499_reg_t;

/* Control register, `[SC499]` §1.9, write only. Bits 0-3 unused. */
#define AP_SC499_CTL_RESET 0x80u  /* "Reset controller microprocessor" */
#define AP_SC499_CTL_REQUEST 0x40u /* "Request to LSI chip" */
#define AP_SC499_CTL_IEN 0x20u     /* "Enables interrupts; IEN = 0, masks" */
#define AP_SC499_CTL_DNIEN 0x10u   /* "Enables DONE interrupt" */

/* Status register, read only. Positions and polarity both from `[SC499]`'s page
 * image; see the header. **Two of these are asserted low**, which is why they
 * are not simply OR'd together on a read. */
#define AP_SC499_ST_IRQ 0x80u /* **active low**: ORing of RDY and EXC, DONE if DNIEN */
#define AP_SC499_ST_RDY 0x40u /* **active low**: "Ready, from LSI chip" */
#define AP_SC499_ST_EXC 0x20u /* **active low**: "Exception, from LSI chip" */
#define AP_SC499_ST_DONE 0x10u /* active high: "Done, from DMA logic" */
#define AP_SC499_ST_DIR 0x08u  /* active high: controller to host */

/* `[SC499]` p. 12: "(BITS 0-2 Not Used)".
 *
 * **Not used is not zero.** Nothing on the controller drives those three lines,
 * so a read of the status register leaves them to the bus, and an undriven ISA
 * data line reads as one -- which is the same reasoning `ap_board.c` already
 * applies to the AT window at large, where "the pull-ups answer" `FF`. This
 * core read them as zero, so its idle status was `70` where the hardware's is
 * `77`.
 *
 * That was not a harmless cosmetic difference. Domain/OS's tape reset waits for
 * the status register to read exactly `F7` and then exactly `57` -- `CMPI.W
 * #$00F7` at `3C459F5A` and `#$0057` at `3C459F82` -- and both constants have
 * these three bits set. With them clear the comparison can never match, the
 * bounded retry times out, and the driver takes its error path. Detail in
 * `PROJECT_STATUS.md`.
 *
 * Corroborated rather than assumed: the oracle's `sc499.cpp` carries `0x07` in
 * its idle status too, measured as `m_status=77` on a booting machine. */
#define AP_SC499_ST_UNUSED 0x07u

/* The bits that read 1 when nothing is asserted. Useful as the base a status
 * read starts from, and as the thing a test can name rather than spelling `C0`
 * and inviting the reader to work out why. */
#define AP_SC499_ST_ACTIVE_LOW \
  (AP_SC499_ST_IRQ | AP_SC499_ST_RDY | AP_SC499_ST_EXC | AP_SC499_ST_UNUSED)


/* Whether a register is driven on a read. The two DMA command addresses are
 * write-only, and the measured dump shows them floating to `FF` rather than
 * reading zero -- so a board must answer for them, and the part must say it is
 * not answering. */
[[nodiscard]] bool ap_sc499_readable(unsigned reg);

/* Which of `[SC499]` §1.13.2's three command-transfer figures applies, chosen by
 * the device's state when the command is issued.
 *
 * The handshake is one protocol with three entry conditions, not three
 * protocols: Figure 1-7 when the device is ready, Figure 1-8 when it is in
 * exception, Figure 1-9 when it still holds the bus. Each figure looks like the
 * whole thing until the next is read -- which is how this core's first attempt
 * came to violate the READY/EXCEPTION rule, having been written from 1-7 alone. */
typedef enum {
  AP_SC499_ENTRY_READY = 0,   /* Figure 1-7 */
  AP_SC499_ENTRY_EXCEPTION,   /* Figure 1-8 */
  AP_SC499_ENTRY_DIRECTION,   /* Figure 1-9 */
  /* Not a command entry at all: Figure 1-5's gap between one data block and the
   * next, which the data path needs and the three above do not describe. */
  AP_SC499_ENTRY_DATA_BLOCK,  /* Figure 1-5, T14->T15 */
} ap_sc499_entry_t;

typedef struct {
  uint8_t control;
  uint8_t data;
  bool ready;
  bool exception;
  bool done;
  bool direction;
  bool dma_active;

  /* The handshake's clock. The device carries its own cursor rather than being
   * handed the time at each command, because `ap_board_write` has no `now` to
   * give it and threading one through every register write to reach this device
   * would put a timestamp on paths that have nothing to do with time. The
   * cursor is current to within one tick, which is the granularity of a
   * cycle-stepped core and so the finest anything here can mean. */
  ap_time_t now;
  /* When the device will assert READY, and whether it is working towards it.
   * A separate flag rather than a sentinel deadline: zero is a legitimate time
   * and `now` starts there. */
  bool executing;
  ap_time_t ready_at;
  /* Which figure the command in flight entered by, kept so the completion knows
   * what to undo -- 1-8 lifts an exception, 1-9 hands back the bus. */
  ap_sc499_entry_t entry;
} ap_sc499_t;

/* Power-on reset. `[SC499]` defines RSTDMA as performing the same functions, so
 * the two share an implementation and a test. */
/* The command handshake's edge-to-edge times, in `AP_TIME_BASE_HZ` units.
 *
 * **`PROVISIONAL`, every one of them.** `[SC499]` §1.13.2 publishes *bounds*,
 * not values: "0 us < T3->T4 < 150 us" says the device hands the bus back within
 * 150 microseconds and says nothing about when. Modelled at the documented
 * bound, which is `CLAUDE.md`'s rule for a quantity published as a range --
 * model the documented value, mark it here, name it in `PROJECT_STATUS.md`. The
 * same treatment the 68030's two-clock input synchroniser carries.
 *
 * Taking the bound means every handshake runs at its slowest permitted speed.
 * That is wrong in a knowable direction and by a knowable amount, which is the
 * property that makes a provisional figure safe to hold: closing it needs a
 * measurement, not a decision.
 *
 * All nine land exactly on the time base -- 40 ns is 264 units, 500 ms is
 * 3,300,000,000 -- so none of them is rounded on top of being provisional. */
#define AP_SC499_T_DATA_TO_REQUEST 0u          /* "0 us <", no lower bound */
/* Derived from the base, never written down as a unit count.
 *
 * These were literals -- `19800u` and the rest -- and the figures were right for
 * a 19.8 GHz base and silently wrong for any other. Recomputing the base for
 * the video dot clock turned every one of them into a wrong *duration*, which
 * is the failure mode `ap_time.h` says a derived constant exists to prevent:
 * "every period is derived from it rather than written down". They now say the
 * microseconds the figures actually are. */
#define AP_SC499_US(n) ((ap_time_t)(AP_TIME_BASE_HZ / 1000000u) * (n))

#define AP_SC499_T_REQUEST_TO_NOT_READY AP_SC499_US(1)   /* < 1 us */
#define AP_SC499_T_EXCEPTION_TO_READY AP_SC499_US(10)    /* 10 us <, Figure 1-8 T3->T4 */
#define AP_SC499_T_DIRECTION_RELEASE AP_SC499_US(150)    /* < 150 us, Figure 1-9 T3->T4 */
#define AP_SC499_T_DIRECTION_TO_READY AP_SC499_US(500) /* < 500 us, Figure 1-9 T4->T6 */
#define AP_SC499_T_COMMAND_EXECUTION AP_SC499_US(500000) /* < 500 ms, Figure 1-7 T4->T5 */

/* Figure 1-5, Data Transfer Write Operation, T14->T15: "Device Asserts READY
 * (Device READY For Next Data Block)", timed `100 us. < T14--->T15`.
 *
 * **`PROVISIONAL`, and the direction is the reason.** This is a *minimum* --
 * the device must wait at least this long -- so the figure constrains the delay
 * without fixing it, and any value at or above it is legal. The bound itself is
 * taken, which models the fastest drive the specification permits; a real one
 * may be slower and nothing here would notice. Closes with a measurement of a
 * drive, not with another reading. */
#define AP_SC499_T_BLOCK_TO_READY ((AP_TIME_BASE_HZ * 100u) / 1000000u)
#define AP_SC499_T_CLOSE_MIN AP_SC499_US(20)           /* 20 us <, T6->T8 */
#define AP_SC499_T_CLOSE_MAX AP_SC499_US(100)           /* < 100 us, T6->T8 */

void ap_sc499_reset(ap_sc499_t *tape);

/* Which figure a command issued now would follow. */
[[nodiscard]] ap_sc499_entry_t ap_sc499_command_entry(const ap_sc499_t *tape);

/* Begin the handshake the selected figure prescribes, on the device accepting a
 * command. **Ordering and timing both**, now that the device has a clock.
 *
 * READY is deasserted here, at once. That is the one edge not taken at its
 * documented bound: `T_REQUEST_TO_NOT_READY` is "< 1 us", and holding READY up
 * for that microsecond would show a driver a device that looks *finished* with
 * a command it has only just been handed. Every other bound is taken, so the
 * handshake errs slow; this one errs early, because the two directions are not
 * equally safe.
 *
 * The remaining edges land when `ap_sc499_advance` reaches the deadline, so a
 * caller that never advances the device leaves READY down forever -- which is a
 * hang rather than a wrong answer, and the honest consequence of a device that
 * takes time in a machine that is not running. */
void ap_sc499_command_accepted(ap_sc499_t *tape);

/* Carry the handshake to `now`, asserting READY and undoing the entry
 * condition when its deadline has passed. Idempotent, and refuses to go
 * backwards. */
void ap_sc499_advance(ap_sc499_t *tape, ap_time_t now);

/* Whether a command is still executing -- READY down, deadline not reached.
 * Exposed so a test can assert the interval rather than infer it from the
 * status bit it is supposed to explain. */
[[nodiscard]] bool ap_sc499_executing(const ap_sc499_t *tape);

/* How long the figure entered by `entry` takes to reach READY, in base units.
 * Named so the three figures' totals can be checked against `[SC499]`'s bounds
 * directly, rather than only through a device that has run. */
[[nodiscard]] ap_time_t ap_sc499_handshake_duration(ap_sc499_entry_t entry);

/* The device has begun a data block: READY goes down and comes back up when it
 * is ready for the next one. `[SC499]` §1.13.1 and Figure 1-5. */
void ap_sc499_block_boundary(ap_sc499_t *tape);

/* Raise or clear the exception condition, keeping it exclusive of ready.
 *
 * `[SC499]` Figure 1-6: "READY shall not be asserted for an EXCEPTION
 * condition." Figure 1-8 shows the order the other way round -- on a command
 * issued while EXCEPTION is up, the device deasserts EXCEPTION first and only
 * then, at least 10 us later, asserts READY. The two are never both up, by
 * specification rather than convention.
 *
 * So this is a single call rather than two fields a caller sets independently:
 * an invariant that can be broken by forgetting one line is not an invariant.
 * `FINDINGS.md` C26 records the join getting exactly that wrong before the
 * figures were read. */
void ap_sc499_set_exception(ap_sc499_t *tape, bool asserted);

[[nodiscard]] uint8_t ap_sc499_read(ap_sc499_t *tape, unsigned reg);
void ap_sc499_write(ap_sc499_t *tape, unsigned reg, uint8_t value);

/* The IRQ pin. `[SC499]`: "The IRQ line is tri-stated when IEN is cleared", so
 * a masked controller drives nothing at all rather than driving low. */
[[nodiscard]] bool ap_sc499_irq(const ap_sc499_t *tape);

#endif /* APOLLO_DEVICE_AP_SC499_H */

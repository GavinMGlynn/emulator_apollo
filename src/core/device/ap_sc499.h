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

  /* The post-reset exception, armed when the host releases RSTSAC. Kept apart
   * from `executing`/`ready_at` because it is not a command handshake and must
   * not be cancelled by one. */
  bool reset_arming;
  bool reset_pending;
  ap_time_t exception_at;
  /* How long RSTSAC has been held. §1.12 makes 25 us a *requirement* on the
   * host -- "RSTSAC must be set, held for more than 25 usec, then cleared" --
   * so a narrower pulse is not a reset and must not arm the exception. Dated at
   * the next advance for the same reason `exception_at` is: setting the bit runs
   * `ap_sc499_reset`, which clears `now` along with everything else, so there is
   * no instant to read at the write itself. */
  bool hold_dating;
  bool hold_dated;
  ap_time_t held_since;
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

/* **PROVISIONAL.** How long after the host releases RSTSAC the controller
 * asserts EXCEPTION to report the power-on-reset condition.
 *
 * *That* it asserts one is protocol, not guesswork: Linux's `tpqic02.h` defines
 * `TP_POR` ("Power on or reset occurred") as a bit in the drive's status bytes,
 * and a host obtains those with READ STATUS, which is what it issues **in
 * response to an exception**. Domain/OS corroborates -- its tape reset waits
 * for status `57`, which is EXCEPTION asserted, and cannot proceed without it.
 *
 * *How long* is **bounded** by the guide and not fixed by it, which is the
 * correction to what this comment used to say ("in neither document"). §1.8.1,
 * Power-On Confidence Test: "A POC test occurs automatically when power is
 * applied **or when a reset command is issued**... Successful completion of the
 * above tests are reported to the host by the assertion of EXC- **within five
 * seconds**. If EXC- is not asserted within this time a failure is indicated."
 *
 * So the reset response is the POC test, its ceiling is 5 s, and 5 s is a
 * *failure* threshold rather than a typical -- the page gives no typical, and a
 * part that ran four sub-tests including a 16K RAM check plainly does not take
 * the same time every unit. Any value below 5 s is legal hardware, which is what
 * makes this figure provisional rather than wrong: 200 ms is inside the legal
 * range, it is the oracle's, and the oracle's is itself unsourced --
 * `sc499.cpp` arms `attotime::from_msec(200)` with no citation.
 *
 * §1.12 specifies the host's side exactly ("held for more than 25 usec"), which
 * is modelled -- see `AP_SC499_T_RESET_MIN_HOLD`. §1.13.2's "Exception Asserted"
 * figure is a *command* transfer, not a reset; `tpqic02.h`'s timeouts are all
 * per-command.
 *
 * **And Apollo's own specification agrees on the five seconds.** `08845 Apollo
 * Specification for QIC-36 Tape Controller`, §12.3 "QIC-02 Command Maximum
 * Timings ... before time-out conditions are generated": Reset Command **5
 * Sec**. That is a second, independent source for the same ceiling, from the
 * vendor of *this machine* rather than of the controller, and it was found by
 * following `[SC499]` p. 14's own instruction to consult the QIC-02 standard --
 * which led to bitsavers' `archive/` directory, where both that standard and
 * this Apollo document sit beside the guide this file already cited.
 *
 * §12.3's other three are bounds on commands rather than on the reset, and they
 * are recorded in `AP_SC499_MAX_*` below because a modelled duration that
 * exceeded one would be a command the host had already given up on.
 *
 * Adopted rather than invented, and marked so it cannot be mistaken for a
 * measurement: the value must exceed nothing in particular and only has to fall
 * inside the driver's window (after its first poll sees `F7`, before its second
 * poll's 131 M-iteration timeout), which is far too wide to pin a number.
 * Replace it with a figure from the QIC-02 standard or from hardware; a named
 * plan item carries it. */
#define AP_SC499_T_RESET_TO_EXCEPTION AP_SC499_US(200000) /* PROVISIONAL */

/* `[SC499]` §1.12, RSTSAC: "must be set, **held for more than 25 usec**, then
 * cleared by either writing a 0 to Control Register Bit 7 or by a RSTDMA."
 *
 * This is a requirement on the *host*, and modelling it means a narrower pulse
 * does not reset the controller -- which is the only reading that gives the
 * sentence force. The guide does not say what a shorter pulse does, so "not a
 * reset" is a choice among undefined behaviours, taken because the alternative
 * makes the stated minimum unobservable and therefore untestable.
 *
 * It costs this machine nothing: Domain/OS holds RSTSAC for 1395.5 us, 55.8x
 * the minimum, measured over a 450 M-instruction boot in which the driver pulses
 * it exactly once. `PROJECT_STATUS.md` records the measurement. */
#define AP_SC499_T_RESET_MIN_HOLD AP_SC499_US(25)

/* `08845` §12.3's maxima: what the *host* waits before declaring a time-out,
 * not what the drive takes. They bound this module's figures rather than
 * supplying them -- a modelled duration at or beyond one of these would be a
 * command the driver had already abandoned -- and `sc499_suite` asserts that
 * every modelled figure sits inside its bound. */
#define AP_SC499_MAX_RESET AP_SC499_US(5000000)          /* 5 s */
#define AP_SC499_MAX_BOT AP_SC499_US(80000000)           /* 1 min 20 s */
#define AP_SC499_MAX_RETENSION AP_SC499_US(241000000)    /* 600 ft, worst case */
#define AP_SC499_MAX_ERASE AP_SC499_US(240000000)        /* 4 min */

/* ## The drive's own figures, which `008778-03` chapter 9 supplies
 *
 * Everything above this point is the *controller's*: `[SC499]`'s handshake
 * bounds and `08845` §12.3's host time-outs. That block's own comment says the
 * maxima "bound this module's figures rather than supplying them", and until
 * chapter 9 was walked nothing supplied them. Table 9-1, *Cartridge Tape Drive
 * Performance Specifications*, is Apollo specifying the drive:
 *
 *     Data capacity (minimum)     45 MB (450-ft tape), 60 MB (600-ft tape)
 *     Number of tracks             9
 *     Recording density        10,000 flux changes/inch, 8000 bits/inch (GCR)
 *     Transfer rate (nominal)     90 KB/second
 *     Tape speed (long term)      90 inches/second +/- 3%
 *     Start/stop time (max)      300 milliseconds
 *     Rewind time (max)           85 seconds (600 ft), 65 seconds (450 ft)
 *     Track selection time (max) 600 milliseconds
 *     Power-up initialization     10 seconds at BOT, 90 seconds at EOT
 *
 * **Only one of those is adoptable as a duration, and the table says which by
 * its own qualifiers.** Every other timing row is a *maximum* -- an acceptance
 * limit a drive must not exceed -- and the transfer rate alone is marked
 * **nominal**. Taking a maximum as a duration is what this file already refuses
 * to do with §12.3's time-outs.
 *
 * **And one of the maxima proves the point, because it is unusable.** Table
 * 9-1's rewind maximum for a 600-ft cartridge is **85 seconds**, while `08845`
 * §12.3 times a host's BOT command out at **80 seconds** -- `AP_SC499_MAX_BOT`
 * below. A drive rewinding at its published maximum would be abandoned by the
 * host before it arrived. So the 85 s is a worst-case limit for acceptance and
 * not a figure any model may adopt, exactly as the 5-second POC ceiling is "a
 * *failure* threshold rather than a typical". Two Apollo documents, and the
 * conflict between them is what tells you which kind of number each is.
 *
 * Not modelled, recorded here so a later reader does not re-derive them: the
 * 300 ms start/stop, the 600 ms track selection -- this core models no tape
 * *track*, the serpentine turnaround being below the QIC-02 command interface --
 * and the power-up initialization, which is the drive's rather than the
 * controller's POC and which no command in this model waits on.
 *
 * ### The transfer rate, and why it is safe where the maxima are not
 *
 * 90 KB/second is **nominal**, and the table's other rows derive it: 90 inches
 * per second at 8000 bits per inch is 720,000 bits/s, which is 90,000 bytes/s
 * exactly. So "KB" here is 1000 bytes and the figure is cross-checked inside
 * its own table rather than taken on trust -- the same check the floppy's 94 ms
 * average access got in `device/ap_omti.h`.
 */
#define AP_SC499_DRIVE_BYTES_PER_SEC 90000u

/* Figure 1-5, Data Transfer Write Operation, T14->T15: "Device Asserts READY
 * (Device READY For Next Data Block)", timed `100 us. < T14--->T15`.
 *
 * **That 100 us is a *minimum*, and it was being used as the whole interval.**
 * The device must wait at least that long; any value at or above it is legal,
 * so the bound modelled the fastest drive the specification permits and a real
 * one would be slower with nothing here noticing. This comment said so, and
 * said it "closes with a measurement of a drive, not with another reading".
 *
 * `008778-03` Table 9-1 is not another reading of the *controller* -- it is
 * Apollo's specification of the **drive**, which is the thing the 100 us was
 * standing in for. A 512-byte QIC-02 block at the nominal 90,000 bytes/second
 * takes **5.69 ms**, fifty-seven times the interface minimum, and that is what
 * a block now costs. The Figure 1-5 bound is kept as a floor: a block small
 * enough to cross the head faster than the interface can turn round still waits
 * for the interface.
 *
 * `image/ap_ct.h`'s `AP_CT_BLOCK_SIZE` is deliberately not reached for here:
 * that constant is a property of the *image format*, and this one is a property
 * of the QIC-02 *interface*. They are both 512 and they are different facts, so
 * `sc499_suite` asserts the two agree rather than one being defined from the
 * other -- the same rule this core keeps for every pair of numbers that happen
 * to coincide. */
#define AP_SC499_T_BLOCK_TO_READY_MIN ((AP_TIME_BASE_HZ * 100u) / 1000000u)

/* QIC-02 Rev D: the fixed block. `archive/QIC-02_Rev_D_Specification_Sep82.pdf`
 * is on the shelf and §4.2's data block is 512 bytes. */
#define AP_SC499_BLOCK_BYTES 512u

/* How long a block of `bytes` takes to cross the head at the drive's nominal
 * rate, floored at the interface's own turnaround. */
[[nodiscard]] ap_time_t ap_sc499_block_duration(unsigned bytes);
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
/* The earliest instant this controller's line could change by time alone -- the
 * conservative lower bound `board/ap_sio.h` states the rule for. Nothing here
 * free-runs: the handshake completes at an instant the command scheduled, so
 * the bound is whichever deadline is outstanding, and an idle controller cannot
 * change without a bus access. */
[[nodiscard]] ap_time_t ap_sc499_interrupt_next_change(const ap_sc499_t *tape);

[[nodiscard]] bool ap_sc499_irq(const ap_sc499_t *tape);

#endif /* APOLLO_DEVICE_AP_SC499_H */

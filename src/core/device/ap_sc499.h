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
 * The one dissent is bit 7. The image prints `0 = IRQF`; both implementations
 * call it active *high*, and so does the machine -- at reset `IEN` is clear, the
 * IRQ line is tri-stated, and the measured bit 7 is `0`. Under the image's
 * polarity that would mean an interrupt asserted by a controller that cannot
 * drive the line. Modelled active high, with the disagreement recorded rather
 * than tidied away.
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
#define AP_SC499_ST_IRQ 0x80u /* active high: ORing of RDY and EXC, DONE if DNIEN */
#define AP_SC499_ST_RDY 0x40u /* **active low**: "Ready, from LSI chip" */
#define AP_SC499_ST_EXC 0x20u /* **active low**: "Exception, from LSI chip" */
#define AP_SC499_ST_DONE 0x10u /* active high: "Done, from DMA logic" */
#define AP_SC499_ST_DIR 0x08u  /* active high: controller to host */

/* The bits that read 1 when nothing is asserted. Useful as the base a status
 * read starts from, and as the thing a test can name rather than spelling `C0`
 * and inviting the reader to work out why. */
#define AP_SC499_ST_ACTIVE_LOW (AP_SC499_ST_RDY | AP_SC499_ST_EXC)


/* Whether a register is driven on a read. The two DMA command addresses are
 * write-only, and the measured dump shows them floating to `FF` rather than
 * reading zero -- so a board must answer for them, and the part must say it is
 * not answering. */
[[nodiscard]] bool ap_sc499_readable(unsigned reg);

typedef struct {
  uint8_t control;
  uint8_t data;
  bool ready;
  bool exception;
  bool done;
  bool direction;
  bool dma_active;
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
#define AP_SC499_T_REQUEST_TO_NOT_READY 19800u  /* < 1 us */
#define AP_SC499_T_EXCEPTION_TO_READY 198000u   /* 10 us <, Figure 1-8 T3->T4 */
#define AP_SC499_T_DIRECTION_RELEASE 2970000u   /* < 150 us, Figure 1-9 T3->T4 */
#define AP_SC499_T_DIRECTION_TO_READY 9900000u /* < 500 us, Figure 1-9 T4->T6 */
#define AP_SC499_T_COMMAND_EXECUTION 9900000000u /* < 500 ms, Figure 1-7 T4->T5 */
#define AP_SC499_T_CLOSE_MIN 396000u           /* 20 us <, T6->T8 */
#define AP_SC499_T_CLOSE_MAX 1980000u           /* < 100 us, T6->T8 */

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
} ap_sc499_entry_t;

void ap_sc499_reset(ap_sc499_t *tape);

/* Which figure a command issued now would follow. */
[[nodiscard]] ap_sc499_entry_t ap_sc499_command_entry(const ap_sc499_t *tape);

/* Apply the effects the selected figure prescribes, on the device accepting a
 * command. The *ordering* is modelled and the timings are not: every figure in
 * §1.13.2 publishes bounds rather than values -- `T3->T4 < 150 us`,
 * `T4->T6 < 500 us` -- and `CLAUDE.md`'s rule for a range is to model the
 * documented value and mark it `PROVISIONAL`, which is work this has not done.
 *
 * The ordering alone is right about everything a polling driver can observe,
 * which is what the join needs today. A driver watching for the edges
 * themselves is what would need the timings. */
void ap_sc499_command_accepted(ap_sc499_t *tape);

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

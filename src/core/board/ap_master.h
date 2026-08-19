/* How an I/O adapter *reaches* the arbiter: `008778-03` §2.4.7, "Bus
 * Mastership Arbitration".
 *
 * `board/ap_arbiter.h` is the arbitration itself -- who gets the bus when
 * several ask. This is the route an AT-bus card takes to become one of those
 * askers, and it is not simply "assert a request line": the card requests
 * through a *DMA channel*, and the bus it ends up with is only the card's
 * because that channel was programmed a particular way.
 *
 * The paragraph in full, because every rule below is a clause of it:
 *
 *   "An I/O adapter obtains mastership of the bus by asserting its DMA Request
 *   signal (DRQx) to a DMA channel that has been programmed in cascade mode,
 *   and then asserting the MASTER.L signal after its DMA Acknowledge is
 *   received (the system board asserts DACKx.L). At this point, the system
 *   processor relinquishes ownership of the bus. The MASTER.L signal prevents
 *   assertion of the AEN signal, allowing the bus Master to comunicate with the
 *   I/O devices. Programming a DMA channel into cascade mode prevents the DMA
 *   controllers from driving the address and control bus, making the bus
 *   available to the bus Master. The I/O adapter now has full ownership of the
 *   bus until it releases the DRQx and MASTER.L signals."
 *
 * ## Which card ever does this, and why nothing here exercises it
 *
 * §2.4.7 describes the mechanism without naming a user, and for most of this
 * manual the answer is *none*: §10.4 says the graphics controller "is a slave
 * device on the bus", §14.2 says the same of the 802.3 controller, and §15.2 of
 * the Serial/Parallel controller. Three cards, three statements, no masters --
 * which is why no card appears in `board/ap_arbiter.h`'s priority list and why
 * that absence is a design fact rather than an omission.
 *
 * **§16.2 is the exception, and it is the only one in the document.** The PC
 * Coprocessor "acts as a Slave device on the bus. **However, when directing the
 * actions of another PC-type adapter board (8- or 16-bit), the PC Coprocessor
 * acts as a Master on the bus.**" So the card this handshake exists for is the
 * PC Coprocessor at `C00000` (Table 2-6, Figure 16-2), and it is not fitted
 * here and not modelled -- `ap_board.c` maps neither it nor its `D00000`
 * alternate.
 *
 * That is worth stating rather than leaving implicit. This module is complete
 * against §2.4.7 and **structurally unexercised**: nothing in a machine this
 * core builds can assert `MASTER.L`, so the path is reachable only from a test.
 * A reader who finds it quiet should know that is the configuration and not a
 * defect, and a reader who fits a PC Coprocessor one day should know this is
 * the module that wakes up.
 *
 * ## Why cascade mode is the load-bearing part
 *
 * The same DRQ line, on a channel in any other mode, is an ordinary DMA
 * request: the controller wins the arbitration and drives the bus itself, and
 * the card that asked gets a transfer rather than the bus. Cascade mode is what
 * makes the controller stand aside. So this module gates *ownership* on the
 * channel's mode and reports `ap_master_controllers_may_drive` for the other
 * case, which is the DMA controllers' own item rather than this one.
 *
 * A masked cascade channel therefore never reaches the arbiter at all -- the
 * request is decided by `ap_i8237_service_pending`, which honours the mask and
 * the controller-disable bit, so software that has masked the channel has
 * closed the route. That falls out of asking the part rather than the pin, and
 * is the reason this module asks the part.
 *
 * ## Which DRQ line, and which channel, is not decided here
 *
 * The caller supplies both. `board/ap_dma.h` refuses to claim the AT's
 * "controller 1 cascades onto channel 0 of controller 2" wiring for this board
 * -- the equivalent assumption about the interrupt controllers was wrong here
 * (`FINDINGS.md` C11), and the DMA cascade is to be measured once transfers
 * exist. A module that hard-wired a channel would make that measurement
 * unnecessary-looking and be wrong in the same way.
 *
 * ## What is deliberately not modelled: the Series 4000 route
 *
 * §2.4.7's second paragraph gives an alternative: "In the Series 4000, an
 * alternate method of bus arbitration exists that implements a Master Request
 * Register. By setting a particular bit in this register, an external processor
 * asserts its DMA Request signal to the system processor." Which bit is not
 * stated.
 *
 * The **register itself is modelled** -- `board/ap_boardreg.h` stores `011600`
 * as the byte-wide storage Table 2-8 says exists, and
 * `ap_boardreg_master_request` reports it -- so what is missing here is not the
 * register but the *meaning of a bit in it*. Modelling the route would mean
 * choosing a bit number, which no source here supplies, and a wrong choice
 * would assert an external master at moments the hardware does not. That is a
 * recorded gap with a bounded closing route, not an omission: the byte is
 * already available to whatever consumes it once a source names the bit.
 *
 * **The search is exhausted rather than untried**, which is a different
 * statement and the one worth recording. Every reference on disk was read:
 * `008778-03` §2.4.7 is the paragraph above and never names the bit;
 * `019411-A00`'s address map lists "MASTER REQUEST REGISTER" at `011600`-
 * `0116FF` and gives it no contents; and the Domain Engineering Handbook, which
 * *does* print register-level detail and settled the DMA page mapping
 * (`board/ap_dmapage.h`), stops at the DN3000 in every revision here -- Rev 1
 * (1983), Rev 3 (1985) and Rev 4 (1987) all predate the Series 4000 boards this
 * paragraph is about. The web was searched and returns only `008778-03` itself.
 * So the closing route is unchanged and now bounded: a Series 4000 hardware
 * reference, or a runnable DN4500 oracle.
 *
 *
 * ## The three timings, now numbered -- and what enforcing them would cost
 *
 * §2.3.2 states three constraints on `MASTER.L` in prose, and **Appendix A's
 * Table A-1 gives all three as figures** for the Series 3000's 6-MHz bus:
 *
 *     #75  Bus Driven from MASTER.L Asserted          166 ns minimum
 *     #76  MEMCMD, IOCMD Asserted from Master.L       333 ns minimum
 *     #77  Master Width Asserted                       12 us maximum
 *
 * The first two are §2.3.2's prose in units: the adapter "must wait one system
 * clock period before driving the address and data lines, and two clock periods
 * before issuing a Read or Write command", and one 6-MHz bus clock is 166 ns.
 * Table A-1 #26 gives that clock a 166 ns cycle time, so the three numbers are
 * one fact counted two ways and they agree.
 *
 * **The third resolves an apparent contradiction rather than restating it.**
 * §2.3.2 warns that holding `MASTER.L` "for more than **15 microseconds** ...
 * the system memory may be lost because no refresh is performed", and §2.4.6
 * puts the refresh interval at "approximately 15 microseconds" -- the same
 * number, which is why the walk recorded the warning as a documented failure
 * mode. Table A-1 caps `Master Width Asserted` at **12 us**, which is *below*
 * the interval by 3 us. So 15 us is the point at which memory is lost and 12 us
 * is the limit the bus specification enforces to keep clear of it: a failure
 * threshold and a design margin, not two versions of one figure.
 *
 * **Recorded, not enforced, and the reason is structural.** This module holds no
 * clock -- `ap_master_t` has no `ap_time_t` in it -- so enforcing any of the
 * three means giving it one and a caller to drive it. It has neither, because
 * nothing in a machine this core builds can assert `MASTER.L` at all (see
 * above). Adding a timestamp would put unexercised state into the identity hash
 * and buy no behaviour. The figures are here so that whoever fits a PC
 * Coprocessor does not have to find them again, and the plan item says exactly
 * that rather than claiming the timings are modelled.
 */
#define AP_MASTER_T_BUS_DRIVEN_NS 166u
#define AP_MASTER_T_COMMAND_NS 333u
/* §2.4.6's interval, above which §2.3.2 says memory is lost. Kept beside the
 * limit because the pair is the finding: `WIDTH_MAX` is deliberately under it. */
#define AP_MASTER_T_REFRESH_INTERVAL_NS 15000u

/* ## The Series 4000's figures, and the one that does not move
 *
 * Appendix B's **Table B-1** is Table A-1's eighty-four rows recomputed for the
 * 8-MHz bus, and comparing the two is what turns the reading above from
 * plausible into checked:
 *
 *                                          Series 3000   Series 4000
 *     #26  BUS CLOCK Cycle Time                166 ns        125 ns
 *     #75  Bus Driven from MASTER.L            166 ns        125 ns
 *     #76  MEMCMD, IOCMD from MASTER.L         333 ns        250 ns
 *     #77  Master Width Asserted                12 us         12 us
 *
 * **#75 and #76 scale with the bus clock and #77 does not.** One and two clocks
 * at 125 ns are 125 and 250, exactly as they were one and two clocks at 166 ns
 * -- so those two are §2.3.2's "one system clock period ... two clock periods"
 * on both families, and they move because the clock moves. #77 stays at 12 us
 * across a bus that got 33% faster.
 *
 * That is the discriminator for what #77 *is*. A limit derived from the bus
 * would have scaled to 9 us; this one is bounded by the **refresh interval**,
 * which does not scale because it is not a bus quantity at all -- §3.9 sources
 * it from the 2681's counter/timer on `OP3` at a fixed 15 us, and `ap_sio.h`
 * carries that period. So `MASTER_T_WIDTH_MAX` is one number on both machines
 * for the same reason `AP_SIO_REFRESH_PERIOD` is: the thing it protects is a
 * DUART's square wave, not a bus clock.
 *
 * Only the Series 3000's #75 and #76 are given constants here, because that is
 * the family this core builds. The Series 4000 pair is recorded rather than
 * defined so that a second set of constants does not appear before a machine
 * that would use them. */
#define AP_MASTER_T_WIDTH_MAX_NS 12000u

/*
 * ## Ownership ends when *both* signals are released
 *
 * "until it releases the DRQx and MASTER.L signals" -- both, which is stronger
 * than it looks. It means an adapter that drops DRQ while still holding MASTER.L
 * keeps the bus, so this module holds the arbiter's request line asserted on the
 * adapter's behalf once acknowledged. Letting the arbiter see the bare DRQ line
 * would hand the bus back mid-transaction, with MASTER.L still asserted and the
 * card still driving.
 */

#ifndef APOLLO_BOARD_AP_MASTER_H
#define APOLLO_BOARD_AP_MASTER_H

#include <stdbool.h>

#include "board/ap_arbiter.h"
#include "device/ap_i8237.h"

/* The acquisition sequence, as its own states rather than as a set of flags.
 * The manual gives an order -- request, acknowledge, MASTER.L -- and a machine
 * whose stages are independent booleans cannot express an order. */
typedef enum {
  /* Nothing asserted. */
  AP_MASTER_IDLE,
  /* "asserting its DMA Request signal (DRQx) to a DMA channel": asked, and
   * waiting for the board. */
  AP_MASTER_REQUESTING,
  /* "after its DMA Acknowledge is received (the system board asserts
   * DACKx.L)": the bus is the adapter's to take, and it has not taken it. */
  AP_MASTER_ACKNOWLEDGED,
  /* "The I/O adapter now has full ownership of the bus." */
  AP_MASTER_OWNS,
} ap_master_state_t;

typedef struct {
  /* Which controller and channel the adapter requests through, and which of the
   * arbiter's eight lines it appears on. Supplied, not assumed: see above. */
  unsigned unit;
  unsigned channel;
  unsigned drq;

  /* The adapter's two output signals. */
  bool request;
  bool master_l;

  ap_master_state_t state;
} ap_master_t;

/* `unit` is only carried for the caller's benefit -- this module is handed the
 * controller it belongs to on every tick, so a port cannot be pointed at one
 * controller and ticked against another without the caller doing it. */
void ap_master_init(ap_master_t *port, unsigned unit, unsigned channel,
                    unsigned drq);

/* The adapter's DRQx line. */
void ap_master_set_request(ap_master_t *port, bool asserted);

/* The adapter's MASTER.L line. Asserting it before DACK has been received is
 * not refused here and does not take the bus either: ownership needs both, and
 * which of the two arrives first is a property of the adapter rather than of
 * the board. The manual's order is what a working card does; nothing in it says
 * what the hardware makes of the other order, so nothing here claims to. */
void ap_master_set_master_l(ap_master_t *port, bool asserted);

/* One bus clock of the acquisition, against the controller the adapter requests
 * through and the arbiter it is competing in. Does *not* tick the arbiter: the
 * board owns that clock, and a module that ticked it would run it once per
 * attached adapter. */
void ap_master_tick(ap_master_t *port, ap_i8237_t *dma, ap_arbiter_t *arbiter);

[[nodiscard]] ap_master_state_t ap_master_state(const ap_master_t *port);

/* DACKx.L, an output of the system board rather than of the adapter. True from
 * the moment the arbitration is won, and still true while the adapter owns the
 * bus -- it is not a pulse. */
[[nodiscard]] bool ap_master_acknowledged(const ap_master_t *port);

/* "The I/O adapter now has full ownership of the bus." */
[[nodiscard]] bool ap_master_owns_bus(const ap_master_t *port);

/* "The MASTER.L signal prevents assertion of the AEN signal, allowing the bus
 * Master to comunicate with the I/O devices." AEN asserted means the address on
 * the bus is a DMA controller's and an I/O card must ignore it; suppressing it
 * is what lets the new master address those cards at all. */
[[nodiscard]] bool ap_master_aen_inhibited(const ap_master_t *port);

/* "Programming a DMA channel into cascade mode prevents the DMA controllers
 * from driving the address and control bus." False while this port's channel is
 * in cascade mode, whatever else is happening. */
[[nodiscard]] bool ap_master_controllers_may_drive(const ap_master_t *port,
                                                   const ap_i8237_t *dma);

#endif /* APOLLO_BOARD_AP_MASTER_H */

/* Apollo cascaded interrupt controllers and the vector scheme.
 *
 * `008778-03` §2.4.8: "Interrupts are implemented using two cascaded Intel
 * 8259A Programmable Interrupt Controller (PIC) chips. Of the 16 possible
 * interrupts, one is used for cascading, four are used internally by the system
 * processor, and 11 are available on the AT-compatible bus."
 *
 * The parts themselves are `device/ap_i8259.h` and know nothing about Apollo.
 * This is the wiring: which line carries the cascade, where the two sit in the
 * address space, and how an acknowledge is routed between them.
 *
 * ## The cascade is on IR3, and that is measured
 *
 * Not IR2. The AT convention is IR2 and it is wrong here, which is the sort of
 * error that survives review because it is imported from a *neighbouring*
 * system rather than invented.
 *
 * The 8259A's initialization words cannot be read back — priority, triggering
 * and vectoring are all write-only — so this was settled by watching the Apollo
 * boot PROM program the parts, with `tools/mame-oracle/writetrace.lua`. The
 * master's ICW3 is `08`, setting bit 3; the slave answers ICW3 `03`, its own ID.
 * The two agree, which is what makes it a wiring fact rather than a stray
 * write. `FINDINGS.md` C11 has the full transcript.
 *
 * This also dissolves an anomaly `008778-03` Table 2-3 appeared to carry. The
 * table gives IRQ3 priority 3 and the slave group 4+1 through 4+8, so the slave
 * looked to be outranked by IRQ3 — impossible on a stock AT. With the cascade on
 * IR3 it is plain fixed priority and nothing is odd: IR0, IR1, IR2, the slave,
 * then IR4 to IR7.
 *
 * ## The vector scheme
 *
 * `008778-03` §3.2 tabulates the vector byte as `T7 T6 T5 T4 T3` followed by
 * the interrupt level in the low three bits, which is exactly what `[8259]`
 * describes for 8086 mode. The `T` bits are ICW2, and the measured bases are
 * `A0` for the master and `A8` for the slave — so master IR0 vectors to `A0`
 * and slave IR0 (the machine's IRQ8) to `A8`, giving the sixteen levels the
 * contiguous range `A0`-`AF`.
 *
 * Those bases are what the firmware programs, not constants this module
 * imposes: `ap_intr_reset` leaves both parts uninitialised, exactly as a
 * powered-on board does, and everything above arrives by ordinary writes.
 */

#ifndef APOLLO_BOARD_AP_INTR_H
#define APOLLO_BOARD_AP_INTR_H

#include <stdbool.h>
#include <stdint.h>

#include "device/ap_i8259.h"

/* `008778-03` §2.5 and §3.2: "009400 Interrupt Controller 1", "009500
 * Interrupt Controller 2" on the Series 3000; `011000` and `011100` in the
 * 64 MB allocation this machine uses. Each is a 256-byte range, and A0 is
 * address bit 0 — confirmed by the trace, whose ICW1 lands on `011000` and
 * whose ICW2 onward land on `011001`. */
#define AP_INTR_MASTER_ADDR 0x011000u
#define AP_INTR_SLAVE_ADDR 0x011100u
#define AP_INTR_RANGE 0x100u

/* Measured from the boot PROM's own ICW3 pair. See the header. */
#define AP_INTR_CASCADE_LINE 3u

/* The 68030 interrupt level the master's INT output drives.
 *
 * Measured, because neither `008778-03` nor `019411-A00` states it. The route
 * was: start the interval timer by hand so that something actually requests,
 * then sweep a single write of the CPU's interrupt mask. A mask of 6 permits
 * only level 7 and blocks it; a mask of 5 permits 6 and 7 and lets it through.
 * Confirmed from the other side by the master's ISR, which reads `01` exactly
 * when the interrupt is taken. `FINDINGS.md` C12.
 *
 * The control experiment is the part worth remembering: with nothing able to
 * interrupt, a forced `SR` stays where it is put, so the firmware never touches
 * it. Without that, the mask seen at an acknowledge could equally have been the
 * exception raising it or a loop restoring it, and the two readings give
 * different levels. */
#define AP_INTR_CPU_LEVEL 6u

/* The machine's sixteen interrupt request lines. `008778-03` Table 2-3 numbers
 * them IRQ0-IRQ15, the second eight being the slave's. */
#define AP_INTR_LINES 16u

typedef struct {
  ap_i8259_t master;
  ap_i8259_t slave;
} ap_intr_t;

/* Power-on: both parts uninitialised, as a board is before its PROM runs.
 * Deliberately not pre-programmed with the measured ICW values — those are
 * what the firmware writes, and a core that assumed them would boot a machine
 * whose PROM had not run yet. */
void ap_intr_reset(ap_intr_t *intr);

/* Whether an address decodes to either controller. */
[[nodiscard]] bool ap_intr_decode(uint32_t address, bool *is_slave, bool *a0);

[[nodiscard]] uint8_t ap_intr_read(ap_intr_t *intr, uint32_t address);
void ap_intr_write(ap_intr_t *intr, uint32_t address, uint8_t value);

/* Drive one of the machine's sixteen request lines. Lines 8-15 are the slave's
 * 0-7; the cascade between them is this module's business and not the
 * caller's. */
void ap_intr_set_request(ap_intr_t *intr, unsigned irq, bool asserted);

/* Whether the CPU should see an interrupt. */
[[nodiscard]] bool ap_intr_pending(const ap_intr_t *intr);

/* Run a full acknowledge and answer the vector byte.
 *
 * When the master acknowledges its cascade line, the acknowledge is forwarded
 * to the slave and the *slave's* vector is returned — `[8259]`: "As a master,
 * the 8259A sends the ID of the interrupting slave device onto the CAS0-2
 * lines. The slave thus selected will send its preprogrammed subroutine address
 * onto the Data Bus." Both parts end with a bit in service, and software owes
 * an EOI to each: "An EOI command must be issued twice if in the Cascade mode,
 * once for the master and once for the corresponding slave." */
[[nodiscard]] uint8_t ap_intr_acknowledge(ap_intr_t *intr);

#endif /* APOLLO_BOARD_AP_INTR_H */

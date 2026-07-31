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
 * ## The status register's bit numbers come from the measurement
 *
 * `[SC499]`'s scan loses them, listing five sources in order: the interrupt
 * request flag, Ready, Exception, Done, Direction. The oracle's own controller
 * reads `40` from that register at reset -- bit 6 -- and Ready is what an idle
 * controller asserts and is second in the list. So the list runs downward from
 * bit 7, which is recorded as a *joint* conclusion: the manual supplied the
 * order and the probe supplied the offset.
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

/* Status register, read only. Bit positions from the measurement; see the
 * header. */
#define AP_SC499_ST_IRQ 0x80u /* ORing of RDY and EXC, and DONE if DNIEN */
#define AP_SC499_ST_RDY 0x40u /* "Ready, from LSI chip" */
#define AP_SC499_ST_EXC 0x20u /* "Exception, from LSI chip" */
#define AP_SC499_ST_DONE 0x10u /* "Done, from DMA logic" */
#define AP_SC499_ST_DIR 0x08u  /* bus direction, controller to host */

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
void ap_sc499_reset(ap_sc499_t *tape);

[[nodiscard]] uint8_t ap_sc499_read(ap_sc499_t *tape, unsigned reg);
void ap_sc499_write(ap_sc499_t *tape, unsigned reg, uint8_t value);

/* The IRQ pin. `[SC499]`: "The IRQ line is tri-stated when IEN is cleared", so
 * a masked controller drives nothing at all rather than driving low. */
[[nodiscard]] bool ap_sc499_irq(const ap_sc499_t *tape);

#endif /* APOLLO_DEVICE_AP_SC499_H */

/* OMTI 862X ESDI/floppy controller.
 *
 * `[OMTI]` *OMTI IBM PC AT Controller Series Reference Manual*, Scientific Micro
 * Systems, January 1987, publication 3001483. §4.1 addresses the **862X**
 * family, so it covers the DN3500's 8621 although the title page lists only the
 * 8620 and 8627.
 *
 * ## One card, two controllers
 *
 * §4.1: "the OMTI 862X controller looks like two independent controllers - one
 * controller for the floppy disk, and one controller for the fixed disk. The
 * host communicates with the OMTI 8000 series through two independent sets of
 * registers." §3.4 says the hardware matches that view: "This allows full
 * concurrent operations between these two sections. For example, DMA data
 * transfer could be occurring at the same time as programmed Input/Output data
 * transfers are occurring on the fixed disk."
 *
 * So this is modelled as two register sets that share nothing, not as one
 * controller with a mode bit. They are even placed 74 KB apart in Apollo's
 * address space -- measured, `FINDINGS.md` C22.
 *
 * ## What is modelled and what is not
 *
 * The two register sets and their documented read/write asymmetries. **Not** the
 * command sets: `[OMTI]` §5 (fixed disk) and §6 (floppy) describe Command
 * Descriptor Blocks and their protocols, and those want a drive and a disk image
 * behind them. This is the interface a driver programs, in the same sense as the
 * 8237A's register model.
 *
 * ## The trap
 *
 * §4.2's data register is not a fixed width: "This is an 8 or 16 bit register
 * depending on the state of the controller (determined by the C/D bit in the
 * STATUS register) ... When the C/D bit is 1, only bits 0-7 are valid. When C/D
 * is 0 all 16 bits are valid." A model with a fixed-width data register would
 * carry commands correctly and corrupt every data word, or the reverse.
 */

#ifndef APOLLO_DEVICE_AP_OMTI_H
#define APOLLO_DEVICE_AP_OMTI_H

#include <stdbool.h>
#include <stdint.h>

/* `[OMTI]` Table 4-1, four ports, different meanings read and written. */
#define AP_OMTI_DISK_REGISTERS 4u
typedef enum {
  AP_OMTI_DISK_DATA = 0u,   /* read DATA IN, write DATA OUT */
  AP_OMTI_DISK_STATUS = 1u, /* read STATUS, write RESET (a function) */
  AP_OMTI_DISK_CONFIG = 2u, /* read CONFIGURATION, write SELECT (a function) */
  AP_OMTI_DISK_MASK = 3u,   /* read N/A, write MASK */
} ap_omti_disk_reg_t;

/* `[OMTI]` Table 4-2, the fixed-disk status register. */
#define AP_OMTI_ST_FIXED 0xC0u /* bits 7 and 6, "Not Used (Set to 1)" */
#define AP_OMTI_ST_IREQ 0x20u  /* 1 = Command Complete */
#define AP_OMTI_ST_DREQ 0x10u  /* 1 = DMA Cycle Requested */
#define AP_OMTI_ST_BSY 0x08u   /* 1 = Controller Selected */
#define AP_OMTI_ST_CD 0x04u    /* 1 = byte is a command or status byte */

/* `[OMTI]` Table 4-3, the floppy half: five registers within an eight-address
 * block based at AT `3F0`, so the offsets are 2 and 4 through 7. */
#define AP_OMTI_FLOPPY_REGISTERS 8u
typedef enum {
  AP_OMTI_FDC_DOR = 2u,      /* write Digital Output; read N/A */
  AP_OMTI_FDC_MSR = 4u,      /* read Main Status; write N/A */
  AP_OMTI_FDC_DATA = 5u,     /* read and write Data */
  AP_OMTI_FDC_CONTROL = 6u,  /* write Additional Control; read N/A */
  AP_OMTI_FDC_DIR = 7u,      /* read Digital Input; write Diskette Control */
} ap_omti_fdc_reg_t;

/* Digital Output Register, `[OMTI]` Table 4-3. "All bits are cleared when a
 * channel reset occurs." */
#define AP_OMTI_DOR_DRIVE_B_MOTOR 0x20u
#define AP_OMTI_DOR_DRIVE_A_MOTOR 0x10u
#define AP_OMTI_DOR_INT_DMA 0x08u
/* Bit 2 runs the opposite way to every other control bit here: "Reset floppy
 * disk function when 0. The floppy disk function comes out of reset when this
 * bit is set to 1." So clearing the register to stop the motors also asserts
 * reset. */
#define AP_OMTI_DOR_NOT_RESET 0x04u
#define AP_OMTI_DOR_SELECT_B 0x01u /* "A 0 selects drive A, A 1 selects drive B" */

/* Digital Input Register: "Bit 7 ... is received from pin 34 of the floppy disk
 * control cable and is normally used for diskette change status. Bits 0 through
 * 6 are Reserved." */
#define AP_OMTI_DIR_DISK_CHANGE 0x80u

typedef struct {
  /* Fixed disk. */
  uint16_t data;
  uint8_t status;
  uint8_t configuration;
  uint8_t mask;

  /* Floppy, entirely separate. */
  uint8_t dor;
  uint8_t fdc_status;
  uint8_t fdc_data;
  uint8_t fdc_control;
  bool disk_change;
} ap_omti_t;

/* Power-on: both halves. */
void ap_omti_reset(ap_omti_t *omti);

/* The fixed disk's own reset, reached by writing its status port. It must not
 * touch the floppy half -- `[OMTI]` §4.1 has them independent and §3.4 has them
 * running concurrently, so a disk reset that stopped the drive motors would be
 * a fault with no register to explain it. The floppy has its own reset in
 * Digital Output bit 2. */
void ap_omti_disk_reset(ap_omti_t *omti);

/* The fixed-disk half. */
[[nodiscard]] uint8_t ap_omti_disk_read(ap_omti_t *omti, unsigned reg);
void ap_omti_disk_write(ap_omti_t *omti, unsigned reg, uint8_t value);

/* The floppy half. `reg` is the offset within the eight-address block. */
[[nodiscard]] uint8_t ap_omti_fdc_read(ap_omti_t *omti, unsigned reg);
void ap_omti_fdc_write(ap_omti_t *omti, unsigned reg, uint8_t value);

/* Whether the floppy side is held in reset -- Digital Output bit 2 clear. */
[[nodiscard]] bool ap_omti_fdc_in_reset(const ap_omti_t *omti);

/* Whether the data register is byte-wide this moment, per the status C/D bit. */
[[nodiscard]] bool ap_omti_data_is_byte(const ap_omti_t *omti);

#endif /* APOLLO_DEVICE_AP_OMTI_H */

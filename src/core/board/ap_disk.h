/* Apollo disk and floppy: the OMTI 862X as the board wires it.
 *
 * The part is `device/ap_omti.h`. What is here is the two placements, and they
 * are 74 KB apart -- one card, two halves, nowhere near each other.
 *
 * ## Two blocks, both measured
 *
 *   `04D000`-`04D3FF`  the fixed disk, four registers on consecutive bytes,
 *                      aliased every eight through 1 KB
 *   `05F800`-`05FBFF`  the floppy, an eight-address block based at AT `3F0`,
 *                      so its five registers sit at offsets 2 and 4 to 7
 *
 * Both were found by scanning the whole AT I/O window with the card fitted and
 * with `isa1` emptied, and both vanish entirely without it (`FINDINGS.md` C22).
 * The distance between them is not arbitrary: `008778-03`'s AT I/O window maps
 * `Apollo = 0x040000 + AT x 0x80`, and `3F0 - 1A0` multiplied by 128 is 74 KB
 * (C23). Within each block the AT addresses then run as consecutive bytes.
 *
 * That rule is worth stating here because it is the first thing that would let
 * a future device's address be *predicted* and then confirmed, rather than
 * hunted for -- every placement on this board so far has cost a differential
 * scan.
 *
 * ## Interrupts
 *
 * `008778-03` Table 2-3: "IRQ14 ... Winchester Drive or User Device" at
 * priority 4+7, and "IRQ6 ... Floppy Drive or User Device" at priority 7. So
 * the two halves interrupt separately, on lines eight apart, and the Winchester
 * -- being on the cascaded controller -- outranks the floppy despite the higher
 * number.
 */

#ifndef APOLLO_BOARD_AP_DISK_H
#define APOLLO_BOARD_AP_DISK_H

#include <stdbool.h>
#include <stdint.h>

#include "device/ap_omti.h"

#define AP_DISK_FIXED_ADDR 0x04D000u
#define AP_DISK_FIXED_SIZE 0x400u
#define AP_DISK_FLOPPY_ADDR 0x05F800u
#define AP_DISK_FLOPPY_SIZE 0x400u

/* `008778-03` Table 2-3. */
#define AP_DISK_FIXED_IRQ 14u
#define AP_DISK_FLOPPY_IRQ 6u

typedef struct {
  ap_omti_t controller;

  /* ## Which registers a run touched, and how often
   *
   * Instrumentation about a *run*, so it lives with the board's placement of
   * the part rather than in the part: `ap_omti_t` is state, and `omti_suite`
   * asserts that a RESET makes one controller byte-identical to a fresh one --
   * a claim these counters would break, and rightly, since a firmware-issued
   * RESET must not erase the evidence a run has gathered.
   *
   * A region total says the firmware talked to the controller and cannot say
   * *what it asked*. Six million reads against seven writes is a poll, and
   * which register is polled is the difference between a controller that is not
   * answering and a driver asking somewhere else -- which three readings of the
   * disassembly could not settle, because the base `a0` holds is set far from
   * the loop. */
  unsigned disk_reads[AP_OMTI_DISK_REGISTERS];
  unsigned disk_writes[AP_OMTI_DISK_REGISTERS];
  unsigned floppy_reads[AP_OMTI_FLOPPY_REGISTERS];
  unsigned floppy_writes[AP_OMTI_FLOPPY_REGISTERS];
} ap_disk_t;

void ap_disk_reset(ap_disk_t *disk);

/* Which half an address falls in, and which register of it. `is_floppy`
 * distinguishes the two blocks; they share a controller but not a register
 * set. */
[[nodiscard]] bool ap_disk_decode(uint32_t address, bool *is_floppy,
                                  unsigned *reg);

/* ---------------------------------------------------------------------------
 * The DMA side
 *
 * `008778-03` Table 2-4 puts the **floppy on DRQ2** -- controller 1, channel 2
 * -- and reserves **DRQ7 for the Winchester**, which is controller 2, channel 3
 * and a 16-bit line. As with every DMA path, the device is selected by `DACK`
 * and no address reaches it, so these name the half rather than a register: it
 * is the data port either way, `[OMTI]` §4.1's two independent register sets.
 *
 * **There is no request line here, and that is not an omission of wiring.**
 * `device/ap_omti.h` models the two register sets and *not* the command sets --
 * §5 and §6's Command Descriptor Blocks want a drive and a disk image behind
 * them -- so nothing in this controller knows a transfer is in progress and
 * there is no condition from which a `DRQ` could honestly be derived. A driver
 * starts these channels with the 8237's software request, which is what the
 * request register is for. It gains a line when the command sets do.
 * ------------------------------------------------------------------------- */

[[nodiscard]] uint8_t ap_disk_dma_read(ap_disk_t *disk, bool is_floppy);
void ap_disk_dma_write(ap_disk_t *disk, bool is_floppy, uint8_t value);

[[nodiscard]] uint8_t ap_disk_read(ap_disk_t *disk, uint32_t address);
void ap_disk_write(ap_disk_t *disk, uint32_t address, uint8_t value);

#endif /* APOLLO_BOARD_AP_DISK_H */

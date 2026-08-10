/* Apollo Token Ring controller: the register interface.
 *
 * Sources are `docs/references/RING.md`, whose numbered findings are cited
 * throughout rather than restated. The register map came out of the ring
 * option ROM's own disassembly -- `[ROM3500]`, Apollo part 10666 -- because no
 * register-level document for this board exists on disk or on the web.
 *
 * ## A unit is two windows, not one
 *
 * Finding 38: the firmware's `$CA0` maps a unit number to **two** base
 * pointers. Unit 0 gets `a1 = $51000` (AT I/O `0x220`) *and* `a2 = $59000` (AT
 * `0x320`); unit 1 gets `$52000`/`$5A000`. Anything else returns zero and the
 * caller reports `ring: init error`. So `[S3K]` Table 2-9's "a second
 * controller sits at AT 0x320" is how the manual lists the ranges, and the
 * firmware drives the pair as one unit.
 *
 * Whether that is one board with two decodes or two boards driven as a pair is
 * **not settled** by the ROM, and this module does not decide it: it models a
 * unit with two windows and leaves the question where the evidence leaves it.
 *
 * ## The four banks
 *
 * Finding 12: four banks at `+000`, `+400`, `+800` and `+C00`, each with slots
 * at `+0`, `+2`, `+4`, `+6`. Fifteen distinct offsets are touched on the DN3500
 * and DN4500 ROMs and only the first bank's four on the DN3000's, so the bank
 * structure is the later board's.
 *
 * ## What each bank is, and what is still unknown
 *
 *   `+000`  an **ID register**. Finding 39: init reads a *byte* from `(a2)`,
 *           after clearing the host instruction cache and with three
 *           `and.w #$ffff,d0` as a delay, and accepts only `$36` or `$37` --
 *           ASCII `'6'` and `'7'`. Anything else is `ring: init error`.
 *   `+400`  a **status word whose bit 15 is a presence gate**. Findings 13 and
 *           40: init reads it, masks `$8000`, and if clear returns success
 *           having touched nothing else -- an empty slot is not an error. With
 *           it set the firmware clears `(a2)`, `+402`, `+404`, `+400` in that
 *           order, and later writes `$800` (bit 11) to `+400`.
 *   `+800`  an **Intel 8254**, counters at `+0`/`+2`/`+4` and control at `+6`.
 *   `+C00`  a second 8254, driven with the same pattern immediately after.
 *
 * Finding 41 establishes the timers on four independent points, and 41a records
 * that this corrects an earlier reading of the same offsets as buffer.
 *
 * **Unknown, and modelled as storage rather than guessed at**: the meaning of
 * `+402`, `+404` and `+406`, and of `+400`'s bits other than 15 and 11. The
 * firmware writes them and this module keeps them, so a driver reads back what
 * it wrote; nothing here acts on any of them. Question A in `RING.md`.
 *
 * The `a1` window is the same shape and its `+2`, `+4`, `+6`, `+400` and `+402`
 * are cleared at init with no other use in the ROM, so it is storage here for
 * the same reason and with the same caveat.
 *
 * ## What this is not
 *
 * The dual-ported RAM buffer, the transmit and receive logic, and the bypass
 * relays are not here. Finding 42 is why the first of those cannot be: the ring
 * ROM never touches AT *memory* space -- every absolute long address in it is
 * one of the four I/O windows -- so the buffer is not reachable from this
 * firmware and its layout must come from `[S3K]`'s AT memory table and a
 * Domain/OS driver. The MAC layer itself already exists in `src/core/ring/`;
 * what is missing is the path between it and these registers.
 */

#ifndef APOLLO_DEVICE_AP_RING_CTL_H
#define APOLLO_DEVICE_AP_RING_CTL_H

#include <stdbool.h>
#include <stdint.h>

#include "device/ap_i8254.h"

/* Finding 38's four windows, as Apollo physical addresses. */
#define AP_RING_CTL_UNIT0_A1 0x051000u
#define AP_RING_CTL_UNIT0_A2 0x059000u
#define AP_RING_CTL_UNIT1_A1 0x052000u
#define AP_RING_CTL_UNIT1_A2 0x05A000u
#define AP_RING_CTL_WINDOW 0x1000u

/* Finding 12's banks. */
#define AP_RING_CTL_BANK_ID 0x000u
#define AP_RING_CTL_BANK_STATUS 0x400u
#define AP_RING_CTL_BANK_TIMER_A 0x800u
#define AP_RING_CTL_BANK_TIMER_B 0xC00u
#define AP_RING_CTL_BANK_MASK 0xC00u
#define AP_RING_CTL_SLOT_MASK 0x006u

/* Finding 40. Bit 15 is read by init as a presence gate; bit 11 is written
 * afterwards and its purpose is not established. */
#define AP_RING_CTL_STATUS_PRESENT 0x8000u
#define AP_RING_CTL_STATUS_BIT11 0x0800u

/* Finding 39: the only two values init accepts. ASCII `'6'` and `'7'`, which
 * with `[ROM3500]`'s revision string ` 3.6` and `[ROM4500]`'s ` 4.0` looks like
 * a board revision -- but the ROM only ever compares, so that reading is not
 * evidenced and nothing here depends on it. */
#define AP_RING_CTL_ID_6 0x36u
#define AP_RING_CTL_ID_7 0x37u

/* One window's worth of state. */
typedef struct {
  /* `+000`, read as a byte by init. */
  uint8_t id;
  /* `+400` and the three slots beside it, kept because the firmware writes them
   * and reads are otherwise unexplainable -- not because their meaning is
   * known. */
  uint16_t status;
  uint16_t slot_402;
  uint16_t slot_404;
  uint16_t slot_406;
  ap_i8254_t timer_a;
  ap_i8254_t timer_b;
} ap_ring_ctl_window_t;

typedef struct {
  /* A unit is the pair, finding 38. */
  ap_ring_ctl_window_t a1;
  ap_ring_ctl_window_t a2;
  /* Whether a board is fitted. Drives `+400` bit 15, which is the whole of what
   * the firmware uses to tell an empty slot from a populated one. */
  bool present;
} ap_ring_ctl_t;

/* Power-on. `present` chooses whether the unit answers as a fitted board. */
void ap_ring_ctl_reset(ap_ring_ctl_t *ctl, bool present);

/* Whether an address falls in one of the four windows, and which half of which
 * unit. */
[[nodiscard]] bool ap_ring_ctl_decode(uint32_t address, unsigned *unit,
                                      bool *second_window, uint32_t *offset);

/* Byte access. The firmware reads the ID as a byte and the timers are
 * byte-wide, so a byte entry is the primitive and the word entries build on it.
 */
[[nodiscard]] uint8_t ap_ring_ctl_read8(ap_ring_ctl_t *ctl, bool second_window,
                                        uint32_t offset);
void ap_ring_ctl_write8(ap_ring_ctl_t *ctl, bool second_window,
                        uint32_t offset, uint8_t value);

/* Word access, big-endian as the bus is. */
[[nodiscard]] uint16_t ap_ring_ctl_read16(ap_ring_ctl_t *ctl,
                                          bool second_window, uint32_t offset);
void ap_ring_ctl_write16(ap_ring_ctl_t *ctl, bool second_window,
                         uint32_t offset, uint16_t value);

/* One clock pulse to both of a window's timers. */
void ap_ring_ctl_clock(ap_ring_ctl_t *ctl, bool second_window);

#endif /* APOLLO_DEVICE_AP_RING_CTL_H */

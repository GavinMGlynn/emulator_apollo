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
 * ## The buffer is a port, not a window
 *
 * Findings 46, 46a and 47, from the self-test at `ENTRY_05`. The dual-ported RAM
 * `[S3K]` §1.5.4 promises is **not memory-mapped**: it is reached through an
 * auto-incrementing data port at `+406`, whose pointer is `+006`. The firmware
 * walks `$7FFF + 1` words -- **64 KB** -- in four patterns, resetting `+006` to
 * zero before each pass.
 *
 * This corrects finding 42, in the way 41a corrected 15. That scan was right
 * that no absolute long operand in the ROM lies in AT *memory* space; its
 * conclusion that the buffer was therefore unreachable assumed a dual-ported
 * buffer had to be memory-mapped, and it is not.
 *
 * **The read port is pipelined by one word, and that is not a detail.** Both
 * readers discard a `move.w (a1),d1` before their loop and then read `d3 + 1`
 * words. A model that returned the addressed word immediately would be one word
 * out for the whole 64 KB and fail every pattern the firmware tries.
 *
 * `+006` is modelled as a plain word pointer, which is what the memory test
 * makes observable and what the transmit and receive paths corroborate:
 * transmit sets it to `0` and writes a four-word header there, receive sets it
 * to `$10` and reads the frame back from word 16 -- a coherent layout with the
 * transmit area at the base. The other observed values are `$600` and `$610`,
 * both inside the tested extent. Whether its upper bits also carry a *mode* is
 * **not settled**: no access distinguishes that reading from a pointer, so the
 * simpler one is modelled and the ambiguity recorded rather than decided.
 *
 * ## What is still unknown, and modelled as storage rather than guessed at
 *
 * `+400`'s bits. Finding 45 shows five of them live -- 15, 13, 11, 2 and 1 --
 * each polled by its own helper with its own timeout and an expected polarity,
 * and *none* of them explained. `+402` and `+404` are byte-wide command
 * registers taking `$1`, `$2`, `$6` and `$8`, `$0`, and carry status as well;
 * their bits are equally unexplained. All of it is kept so a driver reads back
 * what it wrote, and nothing here acts on any of it. Question A in `RING.md`.
 *
 * The `a1` window is **write-only to this firmware** -- finding 50a is an
 * exhaustive scan finding no read of any `(a3)` offset anywhere in the ROM -- so
 * its registers cannot be characterised further from this source, and it is
 * storage here for that reason rather than for want of looking.
 *
 * ## What this is not
 *
 * The transmit and receive logic and the bypass relays. Finding 50 gives their
 * shape -- the self-test is a **loopback** test, transmitting a frame and
 * expecting to receive its own, which is what §3.5's relay arrangement exists to
 * permit -- but the status bits that sequence it are the unknown above. The MAC
 * layer already exists in `src/core/ring/`; what is missing is the meaning of
 * the handshake between it and these registers.
 */

#ifndef APOLLO_DEVICE_AP_RING_CTL_H
#define APOLLO_DEVICE_AP_RING_CTL_H

#include <stdbool.h>
#include <stdint.h>

#include "device/ap_i8254.h"
#include "ring/ap_ring_phy.h"
#include "time/ap_time.h"

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

/* `+400`'s live bits, findings 40 and 45. Named by number because that is all
 * that is established: each is polled, with its own timeout and an expected
 * polarity, and none is explained. Bit 15 alone has a meaning -- the presence
 * gate -- because init's use of it settles one. */
#define AP_RING_CTL_STATUS_PRESENT 0x8000u

/* What `+400` reads on an idle, just-reset board, asserted by the firmware's
 * own subtest 01 -- `(+400) & $F806 == $F806`. Bits 15, 14, 13, 12, 11, 2 and
 * 1. See `ap_ring_ctl.c`'s reset for why the ROM is the authority here. */
#define AP_RING_CTL_STATUS_IDLE 0xF806u

/* The low lane of `+402`, which finding 48 says carries status beside the
 * command byte. Subtest 13 requires `(+402) & $F0 == $F0` after the firmware
 * has written only the high lane, so these four bits read set on a healthy
 * board; the rest are unasserted and stay clear. */
#define AP_RING_CTL_COMMAND_STATUS_IDLE 0x00F0u

/* And `+404`'s, from subtest 15: `(+404) & $F8 == $E0`, so bits 7-5 set with
 * bits 4 and 3 clear. */
#define AP_RING_CTL_COMMAND2_STATUS_IDLE 0x00E0u
#define AP_RING_CTL_STATUS_BIT13 0x2000u
#define AP_RING_CTL_STATUS_BIT11 0x0800u
#define AP_RING_CTL_STATUS_BIT2 0x0004u
#define AP_RING_CTL_STATUS_BIT1 0x0002u
/* Two more the machine's own differing-bits line named (`RING.md` 68c): bit 14
 * is set at reset and at subtest 16 and must be **clear** at 26, so the command
 * clears it and it does not return; bit 3 is clear at 16 and **set** at 26, so
 * completion sets it. */
#define AP_RING_CTL_STATUS_BIT14 0x4000u
#define AP_RING_CTL_STATUS_BIT3 0x0008u

/* The dual-ported RAM, finding 46: `$7FFF + 1` words, which is 64 KB. The
 * figure is the firmware's own -- the extent it tests -- and not a datasheet's,
 * because there is no datasheet. */
#define AP_RING_CTL_BUFFER_WORDS 0x8000u

/* Finding 39: the only two values init accepts. ASCII `'6'` and `'7'`, which
 * with `[ROM3500]`'s revision string ` 3.6` and `[ROM4500]`'s ` 4.0` looks like
 * a board revision -- but the ROM only ever compares, so that reading is not
 * evidenced and nothing here depends on it. */
#define AP_RING_CTL_ID_6 0x36u
#define AP_RING_CTL_ID_7 0x37u

/* One window's worth of state. */
typedef struct {
  /* `+000`, read as a byte by init and cleared as a word by the self-test. */
  uint8_t id;
  /* `+002` and `+004`, written by the self-test and by `$944`/`$976`. Storage:
   * `+004` is read once, at `000944`, and its value is discarded. */
  uint16_t slot_002;
  uint16_t slot_004;
  /* `+006`, the buffer pointer -- finding 46. In **words**, which is the unit
   * `+406` advances by. */
  uint16_t pointer;
  /* `+400` and the two command registers beside it, kept because the firmware
   * writes them and reads them back -- not because their bits are known. */
  uint16_t status;
  uint16_t command_402;

  /* `+402`'s low lane, which is status rather than the constant finding 63
   * first modelled: subtest 13 requires `F0` on an idle register and subtest 23
   * requires `B0` once a `$6` command has been taken, so bit 6 goes with the
   * operation. Held per window because it changes. */
  uint16_t command_402_status;
  /* `+404`'s low lane, the same shape and cleared by the same event. */
  uint16_t command_404_status;
  uint16_t command_404;

  /* Whether a `$6` command is awaiting its acknowledge. An acknowledge with
   * nothing outstanding must do nothing: subtest 16 runs after a `$2` command
   * and *requires bit 3 clear*, so a model that set it on every write to the
   * first window's `+2` fails there -- measured, not reasoned. `RING.md` 75. */
  bool operation_pending;

  /* `+406` on the `a1` window, which finding 50a shows is never read. The `a2`
   * window's `+406` is the buffer port and does not use this. */
  uint16_t slot_406;

  /* **A 16-bit port reached over a byte bus needs a half-word latch.** The
   * board delivers a `move.w` as two byte accesses, and `+406` advances its
   * pointer on every access -- so reading it twice for one word consumes two
   * words of the buffer and hands the firmware halves of different ones. The
   * 8254s above already guard against exactly this; the data port did not.
   * The even half performs the access and latches, the odd half is served from
   * the latch. */
  uint16_t port_latch;
  uint16_t port_write_high;
  /* The one-word read-ahead latch of finding 46a. Its power-on content is
   * whatever the port last fetched, which is why the firmware discards its
   * first read rather than trusting it. */
  uint16_t read_ahead;
  ap_i8254_t timer_a;
  ap_i8254_t timer_b;
} ap_ring_ctl_window_t;

typedef struct {
  /* A unit is the pair, finding 38. */
  ap_ring_ctl_window_t a1;
  ap_ring_ctl_window_t a2;
  /* The card's dual-ported RAM, reached through the `a2` window's `+406`.
   * `[S3K]` §1.5.4 gives the board one buffer, so it belongs to the card rather
   * than to a window. */
  uint16_t buffer[AP_RING_CTL_BUFFER_WORDS];
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

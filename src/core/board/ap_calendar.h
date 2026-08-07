/* Apollo calendar: the MC146818A as the board wires it.
 *
 * `008778-03` §3.6 and Table 2-8, "010900 - 0109FF  CALENDAR". The part is
 * `device/ap_mc146818.h`; this is the placement and the interrupt line.
 *
 * ## Byte-consecutive, unlike the interval timer
 *
 * The calendar is at **stride 1** — its sixty-four registers occupy sixty-four
 * consecutive bytes — where the timer next door is on odd addresses at stride 2.
 * Two byte-wide peripherals, two different placements, so neither can be
 * inferred from the other and both were measured.
 *
 * The evidence is a dump of `010900`: `21 00 09 00 89 00 06 31 07 26 ...`. Read
 * at stride 1 that is a coherent clock — 21 seconds, 09 minutes, hours `89`
 * (the twelve-hour mode's PM bit plus 9), weekday 6, day 31, month 07, year 26 —
 * and 2026-07-31 really is a Friday, which is weekday 6 when Sunday is 1. Read
 * at stride 2 the same bytes would make the minutes `89`, which is not a legal
 * BCD minute at all. One reading is a working clock and the other is impossible,
 * which is as clean as a placement measurement gets.
 *
 * The sixty-four registers then **alias** through the 256-byte range, also
 * measured: the month register reads `07` at offsets `08`, `48`, `88` and `C8`,
 * and a byte written to the RAM at `+10` reads back at `+50` and `+90`.
 */

#ifndef APOLLO_BOARD_AP_CALENDAR_H
#define APOLLO_BOARD_AP_CALENDAR_H

#include <stdbool.h>
#include <stdint.h>

#include "device/ap_mc146818.h"

/* `008778-03` Table 2-8. */
#define AP_CALENDAR_ADDR 0x010900u
#define AP_CALENDAR_RANGE 0x100u

/* `008778-03` Table 2-3: "IRQ8 ... MC146818 Calendar", the first line of the
 * slave controller and priority 4+1 — the highest of the cascaded group. */
#define AP_CALENDAR_IRQ 8u

/* ## The configuration table, in the battery RAM
 *
 * `008778-03` §3.6 says only that the part has "50 bytes of low-power static
 * RAM. The RAM is used to store configuration information", and stops there.
 * **`002398-04` p. 12-3 lays the information out**, and it is the manual this
 * project needed for a session and a half — the DN3000 chapter of the Domain
 * Engineering Handbook, on disk the whole time:
 *
 *     0E-11  CHECKSUM          } 50 bytes of battery backed up RAM
 *     12-15  VALID PATTERN     } used by diagnostics for config info
 *     16-1D  MEM BOARD ARRAY
 *     1E-21  NODEID
 *     22-25  DEV BIT ARRAY   <= bit 0 = flp      4 = ring
 *     26     RING TYPE          1 = ctape        5 = user device
 *     27     DISP TYPE          2 = win          6 = ethernet
 *     28     DISK TYPE          3 = fpu          7 = serial/parallel board
 *     29-3F  UNUSED
 *
 * **It corroborates a measurement exactly.** The boot PROM was found to touch
 * the calendar precisely once in a hundred million instructions — one 32-bit
 * read at `010912`, returning zero — and its whole judgement that
 * "Configuration information is not initialized" rests on that longword. `12`
 * is where the handbook puts the **VALID PATTERN**, and it is four bytes wide,
 * which is the width of the read. A measurement and a document arriving at the
 * same field independently is as good as this gets.
 *
 * ## The pattern's value, from the firmware
 *
 * The handbook names the field and not its contents, so the boot PROM was asked
 * instead. `--boot-watch-read 010912` stops the machine on the access, at
 * **133,067,640 instructions**, and names the instruction — and the code is
 * unambiguous:
 *
 *     001784  lea.l   $1090E.l, a0            ; the table's base
 *     00178A  cmpi.l  #$1234ABCD, $4(a0)      ; the valid pattern, at 010912
 *     001792  bne.b   $17A2                   ; no pattern: skip the table
 *     001794  tst.b   $1D(a0)                 ; register 2B, and it must be set
 *     001798  beq.b   $17A2
 *     00179A  move.b  $1D(a0), d0             ; overriding moveq #2,d0
 *     00179E  move.b  $1E(a0), d4             ; register 2C, overriding clr.b d4
 *
 * `a0` is loaded with `01090E`, which is `AP_CALENDAR_ADDR` plus the handbook's
 * **CHECKSUM** offset: the firmware bases its table exactly where the manual
 * starts it, and reads the pattern at base + 4 = `12`. The address measured
 * from a running machine, the field printed in 1987 and the base the firmware
 * computes all agree, which is three independent sources on one structure.
 *
 * Two notes, both recorded rather than resolved:
 *
 * - **The checksum is not checked here.** This path tests the pattern alone.
 *   Whether anything else verifies `0E-11`, and by what sum, is unknown and is
 *   not guessed at.
 * - **Registers `2B` and `2C` are `UNUSED` in the handbook.** They are the
 *   DN3000's table and this is a DN3500's firmware, so a later machine using
 *   two of the spare bytes is the ordinary explanation. What they *hold* is not
 *   claimed: `d0` defaults to `2` and `d4` to `0` when they are zero, and what
 *   those select is a separate read.
 *
 * Nothing is written into the table here. Knowing the pattern makes seeding
 * *possible*; whether an empty table is what stops Domain/OS is still the
 * unmeasured question, and a battery RAM this core fills at reset would answer
 * it by assumption instead of by measurement.
 */

/* `00178A`'s immediate. A firmware fact, not a manual's. */
#define AP_CALENDAR_CONFIG_VALID_PATTERN_VALUE UINT32_C(0x1234ABCD)

/* Where the boot PROM bases the table, which is the checksum field and not the
 * pattern -- so `$4(a0)` in the listing above is the pattern's offset. */
#define AP_CALENDAR_CONFIG_BASE AP_CALENDAR_CONFIG_CHECKSUM
#define AP_CALENDAR_CONFIG_CHECKSUM 0x0Eu
#define AP_CALENDAR_CONFIG_VALID_PATTERN 0x12u
#define AP_CALENDAR_CONFIG_MEM_BOARD_ARRAY 0x16u
#define AP_CALENDAR_CONFIG_NODEID 0x1Eu
#define AP_CALENDAR_CONFIG_DEV_BIT_ARRAY 0x22u
#define AP_CALENDAR_CONFIG_RING_TYPE 0x26u
#define AP_CALENDAR_CONFIG_DISP_TYPE 0x27u
#define AP_CALENDAR_CONFIG_DISK_TYPE 0x28u
#define AP_CALENDAR_CONFIG_UNUSED 0x29u

/* The `DEV BIT ARRAY`'s bits, from the same page. A device present is a bit
 * set; which of the four bytes at `22` carries them is not stated, and is not
 * guessed at here. */
#define AP_CALENDAR_DEV_FLOPPY 0u
#define AP_CALENDAR_DEV_CARTRIDGE_TAPE 1u
#define AP_CALENDAR_DEV_WINCHESTER 2u
#define AP_CALENDAR_DEV_FPU 3u
#define AP_CALENDAR_DEV_RING 4u
#define AP_CALENDAR_DEV_USER 5u
#define AP_CALENDAR_DEV_ETHERNET 6u
#define AP_CALENDAR_DEV_SERIAL_PARALLEL 7u

typedef struct {
  ap_mc146818_t rtc;
} ap_calendar_t;

/* Reset, with the clock set where the caller says. There is no default instant
 * here on purpose: a board that picked one would be choosing a wall clock, and
 * the whole point of this device's design is that it does not have one. */
[[nodiscard]] bool ap_calendar_reset(ap_calendar_t *calendar,
                                     const ap_mc146818_time_t *start);

/* Whether an address decodes to the calendar, and to which register. */
[[nodiscard]] bool ap_calendar_decode(uint32_t address, uint8_t *reg);

[[nodiscard]] uint8_t ap_calendar_read(ap_calendar_t *calendar,
                                       uint32_t address);
void ap_calendar_write(ap_calendar_t *calendar, uint32_t address,
                       uint8_t value);

void ap_calendar_advance(ap_calendar_t *calendar, ap_time_t now);

/* The IRQ pin, to be wired to `AP_CALENDAR_IRQ`. The board does the wiring. */
[[nodiscard]] bool ap_calendar_irq(const ap_calendar_t *calendar);

/* ## The battery, which is the fifty bytes and *not* the clock
 *
 * `008778-03` §3.6: "The calendar chip has a backup battery to ensure that no
 * data is lost when the ac power is removed." A machine that forgets its
 * configuration at every power-on is a machine whose battery is flat, and that
 * is the machine this core has been every run — which is exactly what the boot
 * PROM complains about.
 *
 * These carry the RAM across a run so a caller can keep it in a file, the way
 * `--disk` keeps a volume. **The clock is deliberately not included.** Ten of
 * the sixty-four bytes are the time, and persisting them would make a run's
 * starting instant depend on when the last run ended — a wall clock arriving
 * through the back door, which `CLAUDE.md` forbids and which
 * `ap_calendar_reset` refuses to the caller's face by demanding a start time.
 * So the battery holds configuration and the clock is always given.
 *
 * `count` is clamped to the fifty bytes; a shorter buffer fills from the base
 * and leaves the rest, which is what a partially written table looks like.
 */
#define AP_CALENDAR_BATTERY_BYTES \
  ((unsigned)(AP_MC146818_BYTES - AP_MC146818_RAM_BASE))

void ap_calendar_load_battery(ap_calendar_t *calendar, const uint8_t *bytes,
                              unsigned count);
[[nodiscard]] unsigned ap_calendar_save_battery(const ap_calendar_t *calendar,
                                                uint8_t *out,
                                                unsigned capacity);

#endif /* APOLLO_BOARD_AP_CALENDAR_H */

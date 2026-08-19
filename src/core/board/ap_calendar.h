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
/* Eight entries of one byte, the field's whole extent at `16`-`1D`; of which
 * this board populates the four the DN3500's SELF_TEST enumerates, reporting
 * memory slots `00000000` through `00000003`. */
#define AP_CALENDAR_CONFIG_MEM_BOARDS 8u
#define AP_CALENDAR_CONFIG_MEM_BOARDS_FITTED 4u
#define AP_CALENDAR_CONFIG_NODEID 0x1Eu
#define AP_CALENDAR_CONFIG_DEV_BIT_ARRAY 0x22u
#define AP_CALENDAR_CONFIG_RING_TYPE 0x26u
#define AP_CALENDAR_CONFIG_DISP_TYPE 0x27u
#define AP_CALENDAR_CONFIG_DISK_TYPE 0x28u
#define AP_CALENDAR_CONFIG_UNUSED 0x29u

/* Two bytes the DN3000 page calls unused and this machine's PROM reads.
 *
 * Decoded from `3500_BOOT_12191_7.bin` itself, which is the authority for what
 * the firmware does with the table:
 *
 *     001784  LEA  $0001090E,A0              ; A0 -> register 0E
 *     00178A  CMPI.L #$1234ABCD,$4(A0)       ; register 12, the valid pattern
 *     001792  BNE.S 0017A2
 *     001794  TST.B $1D(A0)                  ; register 2B
 *     001798  BEQ.S 0017A2
 *     00179A  MOVE.B $1D(A0),D0              ; register 2B
 *     00179E  MOVE.B $1E(A0),D4              ; register 2C
 *
 * `MOVEQ #2,D0` and `CLR.B D4` immediately precede the sequence, so `2` and `0`
 * are the values used when the pattern does not match or `2B` is zero.
 *
 * **What `D0` selects is now decoded: an option-ROM class.** The two bytes are
 * carried straight into the boot PROM's expansion-ROM scan --
 *
 *     0017A2  LEA   $104E(PC),A0     ; the matcher, as a callback
 *     0017A6  BSR.W $F7E             ; the scan itself
 *
 * -- and `$104E` accepts a ROM whose magic is `335E91B6` / `0000A0B6` **and
 * whose `field_1a` equals `D0`**. So register `2B` names which class of
 * expansion ROM this machine should look for, and a ring ROM's `field_1a` is
 * `0002`. The PROM carries a second matcher at `$106A` for a different class
 * (`C000A0B7`) with no such check, which is the one an early scan uses.
 *
 * Measured rather than inferred: with `2B = 2` and a ring ROM mapped where the
 * scan looks, the early scan still runs `$106A` and rejects it, and `001784` --
 * this sequence -- is not reached at all in 60 M instructions. So the class
 * selector is settled and *when* the accepting scan runs is not.
 *
 * The same fragment settles a second question: this path compares the valid
 * pattern and **computes no checksum**, so the four bytes at `0E` are not
 * verified by the PROM. Whatever writes them, it is not this. */
#define AP_CALENDAR_CONFIG_PROM_SELECT 0x2Bu
#define AP_CALENDAR_CONFIG_PROM_SELECT_2 0x2Cu

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

/* ## The configuration table's CHECKSUM, recovered from the utility that
 * writes it -- `RING.md` 117
 *
 * `002398-04` p. 12-3 names the field at `0E`-`11` and does not say how it is
 * computed, and that gap is what finding 83a and 83b were stuck on: a table
 * with a valid pattern but no correct checksum is accepted by the **boot
 * PROM**, which checks only the pattern, and rejected by the SELF_TEST
 * diagnostic loaded off the disk, which prints "Configuration information is
 * not initialized". The algorithm is not in any manual held.
 *
 * It is in `sau8/config`, the utility that *writes* the table, extracted from
 * the SR10.3 boot cartridge with `tools/ct_extract.py`. Its routine at
 * `$17560`:
 *
 *     clr.l   d0                          ; sum = 0
 *     moveq   #$2d, d1                    ; 46 iterations, dbra
 *     moveq   #$5,  d2
 *     clr.l   d3
 *     move.b  $fe95(a0,d2.w), d3          ; first byte is at -$166 == the
 *     addq.w  #$1, d2                     ;   VALID PATTERN
 *     add.l   d3, d0
 *     dbra    d1, ...
 *
 * So: **a 32-bit sum of the 46 bytes beginning at the valid pattern**, each
 * byte zero-extended. Registers `12` through `3F` -- which with the four
 * checksum bytes at `0E`-`11` is exactly the part's fifty bytes of RAM, so the
 * field covers all of the table and none of the clock. The caller at `$17866`
 * compares this against the stored longword at `-$16a`, four bytes before the
 * pattern, which is `0E`.
 *
 * `bytes` is the battery image, `AP_CALENDAR_BATTERY_BYTES` long, based at
 * register `0E` -- the same buffer `ap_calendar_save_battery` fills. */
[[nodiscard]] uint32_t ap_calendar_config_checksum(const uint8_t *battery,
                                                   unsigned count);

/* Write that checksum into its own field, `0E`-`11`, big-endian. A table this
 * has been called on is one the diagnostic accepts; one it has not is what
 * every boot of this core has carried. */
void ap_calendar_seal_config(uint8_t *battery, unsigned count);

/* The DEV BIT ARRAY's bits, `002398-04` p. 12-3, in the layout above. */
#define AP_CONFIG_DEV_FLOPPY 0u
#define AP_CONFIG_DEV_CTAPE 1u
#define AP_CONFIG_DEV_WINCHESTER 2u
#define AP_CONFIG_DEV_FPU 3u
#define AP_CONFIG_DEV_RING 4u
#define AP_CONFIG_DEV_USER 5u
#define AP_CONFIG_DEV_ETHERNET 6u
#define AP_CONFIG_DEV_SERIAL_PARALLEL 7u

/* Build a configuration table a *configured* machine would carry: the valid
 * pattern, the node ID, the device bits, and a sealed checksum.
 *
 * Every field's position is p. 12-3's, and the checksum is the one
 * `sau8/config` computes -- so this produces what the utility would have
 * written, which is what the SELF_TEST diagnostic validates. `RING.md` 118c
 * names what that does and does not unblock.
 *
 * `devices` is a bitmask of `AP_CONFIG_DEV_*`. `battery` is filled from
 * register `0E` and must be `AP_CALENDAR_BATTERY_BYTES` long. */
void ap_calendar_build_config(uint8_t *battery, unsigned count,
                              uint32_t node_id, uint32_t devices);

/* Fill the MEM BOARD ARRAY at `16`-`1D` and re-seal.
 *
 * `002398-04` p. 12-3 names the field and gives its extent -- eight bytes --
 * and no encoding. **The utility that writes it supplies the units**:
 * `sau7/config` prompts `Board #  Size in megabytes` and reports
 * `Total configured memory: %UD megabytes`, so each entry is a board's size in
 * **megabytes**, not bytes and not pages. Extracted from the SR10.3 boot
 * cartridge with `tools/ct_extract.py`, the same route finding 118a took for
 * the checksum.
 *
 * Eight bytes over `AP_CALENDAR_CONFIG_MEM_BOARDS` boards, and the width is the
 * one thing the utility's strings do not settle -- so it is written as the
 * diagnostic's own arithmetic checks it: Domain/OS SELF_TEST prints, per slot,
 * both "megabytes of memory in configuration table" and "megabytes of memory
 * sized", and the two agreeing is the confirmation. `PROJECT_STATUS.md`.
 *
 * ## Per slot, because "the total over four" is not the same question
 *
 * This took a total and divided it by the four fitted slots. That is right on
 * the two sizes it was ever exercised at -- 16 MB is four 4 MB boards, 32 MB is
 * four 8 MB ones -- and wrong on every other configuration the firmware's own
 * decode chain lists: **20 MB is `8-4-4-4`**, not five megabytes four times,
 * and 12 MB is `4-4-4-0`, three boards and an empty slot rather than four
 * boards of three. Neither five nor three is a size Apollo shipped, so the
 * configuration table was describing hardware that does not exist -- and this
 * is precisely the field SELF_TEST compares against what it sizes.
 *
 * `ap_sio_ram_bank_layout` is where the layouts now live; they had been
 * comments beside the strap bytes since they were read out of the boot PROMs.
 * The caller passes what that returns, so the two halves of SELF_TEST's
 * comparison come from one source. */
void ap_calendar_set_memory_boards(uint8_t *battery, unsigned count,
                                   const unsigned *slot_megabytes,
                                   unsigned slots);

void ap_calendar_load_battery(ap_calendar_t *calendar, const uint8_t *bytes,
                              unsigned count);
[[nodiscard]] unsigned ap_calendar_save_battery(const ap_calendar_t *calendar,
                                                uint8_t *out,
                                                unsigned capacity);

#endif /* APOLLO_BOARD_AP_CALENDAR_H */

#include "board/ap_calendar.h"

#include <stddef.h>
#include <string.h>

bool ap_calendar_reset(ap_calendar_t *calendar,
                       const ap_mc146818_time_t *start) {
  return ap_mc146818_reset(&calendar->rtc, start);
}

bool ap_calendar_decode(uint32_t address, uint8_t *reg) {
  if ((address & ~(AP_CALENDAR_RANGE - 1u)) != AP_CALENDAR_ADDR) {
    return false;
  }
  /* Sixty-four registers aliased through a 256-byte range, measured: the month
   * reads alike at four offsets a quarter of the range apart, and a RAM write
   * reappears twice. Masking rather than bounding is what reproduces that -- a
   * decode that refused above `3F` would answer nothing where the hardware
   * answers a register. */
  *reg = (uint8_t)((address - AP_CALENDAR_ADDR) & (AP_MC146818_BYTES - 1u));
  return true;
}

uint8_t ap_calendar_read(ap_calendar_t *calendar, uint32_t address) {
  uint8_t reg;
  if (!ap_calendar_decode(address, &reg)) {
    return 0u;
  }
  return ap_mc146818_read(&calendar->rtc, reg);
}

void ap_calendar_write(ap_calendar_t *calendar, uint32_t address,
                       uint8_t value) {
  uint8_t reg;
  if (!ap_calendar_decode(address, &reg)) {
    return;
  }
  ap_mc146818_write(&calendar->rtc, reg, value);
}

void ap_calendar_advance(ap_calendar_t *calendar, ap_time_t now) {
  ap_mc146818_advance(&calendar->rtc, now);
}

bool ap_calendar_irq(const ap_calendar_t *calendar) {
  return ap_mc146818_irq(&calendar->rtc);
}

/* Straight into the RAM array rather than through `ap_mc146818_write`: this is
 * a battery holding its charge, not a program writing registers, and the write
 * path has side effects on the four control registers that a restore must not
 * trigger. The base is above them, so nothing here can reach one -- but going
 * through the register interface would make that a coincidence rather than a
 * property. */
/* Register `12` -- the VALID PATTERN -- as an index into a battery image based
 * at register `0E`, and the 46 bytes the utility sums from there. Both are the
 * disassembly's own numbers rather than a reading of the layout: `moveq #$2d`
 * with `dbra` is 46, and the first displacement lands on the pattern. */
#define CONFIG_SUM_FIRST 4u
#define CONFIG_SUM_BYTES 46u

uint32_t ap_calendar_config_checksum(const uint8_t *battery, unsigned count) {
  if (battery == NULL || count < CONFIG_SUM_FIRST + CONFIG_SUM_BYTES) {
    return 0u;
  }
  uint32_t sum = 0u;
  for (unsigned i = 0; i < CONFIG_SUM_BYTES; i++) {
    /* Zero-extended and added as a longword: `clr.l d3` before each
     * `move.b`, then `add.l`. A byte-wide accumulator would wrap at 255 and
     * agree with this for a table of mostly-zero bytes, which is every table
     * a test would think to write. */
    sum += battery[CONFIG_SUM_FIRST + i];
  }
  return sum;
}

void ap_calendar_seal_config(uint8_t *battery, unsigned count) {
  if (battery == NULL || count < CONFIG_SUM_FIRST) {
    return;
  }
  const uint32_t sum = ap_calendar_config_checksum(battery, count);
  /* The field is at `0E`, which is offset 0 of the battery image, and the
   * utility compares it with `cmp.l` -- so big-endian, like every other
   * longword this machine stores. */
  battery[0] = (uint8_t)(sum >> 24);
  battery[1] = (uint8_t)(sum >> 16);
  battery[2] = (uint8_t)(sum >> 8);
  battery[3] = (uint8_t)sum;
}

/* Offsets into the battery image, which is based at register `0E`. */
#define CONFIG_AT(reg) ((unsigned)((reg) - 0x0Eu))

void ap_calendar_build_config(uint8_t *battery, unsigned count,
                              uint32_t node_id, uint32_t devices) {
  if (battery == NULL || count < AP_CALENDAR_BATTERY_BYTES) {
    return;
  }
  memset(battery, 0, AP_CALENDAR_BATTERY_BYTES);
  /* `12`-`15`: the VALID PATTERN, whose value is the boot PROM's own
   * `cmpi.l #$1234ABCD` (finding 83) rather than anything the handbook
   * states -- the page names the field and not its contents. */
  battery[CONFIG_AT(0x12u) + 0u] = 0x12u;
  battery[CONFIG_AT(0x12u) + 1u] = 0x34u;
  battery[CONFIG_AT(0x12u) + 2u] = 0xABu;
  battery[CONFIG_AT(0x12u) + 3u] = 0xCDu;
  /* `1E`-`21`: NODEID, big-endian like every other longword here. */
  battery[CONFIG_AT(0x1Eu) + 0u] = (uint8_t)(node_id >> 24);
  battery[CONFIG_AT(0x1Eu) + 1u] = (uint8_t)(node_id >> 16);
  battery[CONFIG_AT(0x1Eu) + 2u] = (uint8_t)(node_id >> 8);
  battery[CONFIG_AT(0x1Eu) + 3u] = (uint8_t)node_id;
  /* `22`-`25`: DEV BIT ARRAY. */
  battery[CONFIG_AT(0x22u) + 0u] = (uint8_t)(devices >> 24);
  battery[CONFIG_AT(0x22u) + 1u] = (uint8_t)(devices >> 16);
  battery[CONFIG_AT(0x22u) + 2u] = (uint8_t)(devices >> 8);
  battery[CONFIG_AT(0x22u) + 3u] = (uint8_t)devices;
  /* `2B`: the option-ROM class the boot PROM scans for, which the handbook
   * calls UNUSED and this machine's firmware reads. `001794` requires it
   * **non-zero** -- `tst.b $1D(a0)` / `beq` skips the whole table otherwise --
   * and `$104E` then accepts a ROM whose `field_1a` equals it, with a ring
   * ROM's being `0002` (see this header's decode of `001784`-`00179E`). A
   * table that leaves it zero is one the PROM reads the pattern from and then
   * abandons, which is what every run before this did. */
  if ((devices & (1u << AP_CONFIG_DEV_RING)) != 0u) {
    battery[CONFIG_AT(AP_CALENDAR_CONFIG_PROM_SELECT)] = 0x02u;
  }
  /* `26`-`28` are RING TYPE, DISP TYPE and DISK TYPE, left zero: the handbook
   * names the fields and not their encodings, and a made-up type is worse
   * than a zero one. */
  ap_calendar_seal_config(battery, AP_CALENDAR_BATTERY_BYTES);
}

void ap_calendar_set_memory_boards(uint8_t *battery, unsigned count,
                                   unsigned megabytes) {
  if (battery == NULL || count < AP_CALENDAR_BATTERY_BYTES) {
    return;
  }
  /* **One byte per board, and the diagnostic said so.** Written first as four
   * 16-bit entries -- `00 04 00 04 00 04 00 04` -- the SELF_TEST report changed
   * from flagging slots 0, 1, 2 and 3 to flagging only **0 and 2**: boards 1
   * and 3 had picked up the `04` from the low half of each pair and stopped
   * disagreeing with what was sized. A width that halves the number of
   * complaints and leaves exactly the even slots is a byte array being read as
   * one, so the eight bytes are eight boards. */
  const unsigned per_board = megabytes / AP_CALENDAR_CONFIG_MEM_BOARDS_FITTED;
  for (unsigned i = 0; i < AP_CALENDAR_CONFIG_MEM_BOARDS; i++) {
    battery[CONFIG_AT(AP_CALENDAR_CONFIG_MEM_BOARD_ARRAY) + i] =
        i < AP_CALENDAR_CONFIG_MEM_BOARDS_FITTED ? (uint8_t)per_board : 0u;
  }
  /* The array is inside the checksummed span, so the seal must follow it. */
  ap_calendar_seal_config(battery, count);
}

void ap_calendar_load_battery(ap_calendar_t *calendar, const uint8_t *bytes,
                              unsigned count) {
  if (calendar == NULL || bytes == NULL) {
    return;
  }
  if (count > AP_CALENDAR_BATTERY_BYTES) {
    count = AP_CALENDAR_BATTERY_BYTES;
  }
  for (unsigned i = 0; i < count; i++) {
    calendar->rtc.ram[AP_MC146818_RAM_BASE + i] = bytes[i];
  }
}

unsigned ap_calendar_save_battery(const ap_calendar_t *calendar, uint8_t *out,
                                  unsigned capacity) {
  if (calendar == NULL || out == NULL) {
    return 0u;
  }
  const unsigned count = capacity < AP_CALENDAR_BATTERY_BYTES
                             ? capacity
                             : AP_CALENDAR_BATTERY_BYTES;
  for (unsigned i = 0; i < count; i++) {
    out[i] = calendar->rtc.ram[AP_MC146818_RAM_BASE + i];
  }
  return count;
}

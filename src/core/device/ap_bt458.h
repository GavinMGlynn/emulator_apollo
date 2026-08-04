/* The Brooktree Bt458 colour lookup table: 256 x 24 RAM behind triple 8-bit
 * DACs, as the DN3500's 8-plane colour graphics controller uses it.
 *
 * ## Identifying the part, from two documents that do not mention each other
 *
 * `008778-03` §10.3's change list for the 8-plane board says "Lookup Tables use
 * combined RAM and triple 8-bit DAC's, changing table size to **256 x 24**".
 * The 1991 *Brooktree Product Databook* lists the Bt458 as a "Triple 8-bit
 * RAMDAC with **256 x 24** RAM". Chapter 10 is physical only and never names a
 * part; the databook never mentions Apollo. They agree on the shape, and the
 * oracle closes the identification by driving the thing as a `bt458` behind a
 * control register whose low two bits it calls `LUT_C1` and `LUT_C0` -- which
 * are the RAMDAC's own control inputs.
 *
 * So this is transcribed from a **named part's published datasheet**, not
 * recovered from firmware writes. That is the whole reason it was done first:
 * every other piece of the drawing engine has to be reasoned out of 803
 * register writes, and this one did not.
 *
 * ## Table 1, and the two bits the MPU cannot see
 *
 * `C1`/`C0` select what an access reaches, in conjunction with the internal
 * address register:
 *
 *     C1 C0   addressed
 *      0  0   the address register itself
 *      0  1   colour palette RAM, $00-$FF
 *      1  1   overlay colour 0-3, at address $00-$03
 *      1  0   read mask $04, blink mask $05, command $06, test $07
 *
 * Colour is moved **three bytes at a time** -- red, green, blue -- and the part
 * tracks which by two bits called `ADDRa,b` that "count modulo three". The
 * datasheet is explicit that "the MPU does not have access to these bits", and
 * that they "are reset to zero when the MPU reads or writes to the address
 * register". So a driver that writes an address and then two bytes leaves the
 * part mid-colour, and the *next* byte completes a colour at that address --
 * which is a state a model without the counter cannot represent at all.
 *
 * On the blue cycle "the 3 bytes of colour information are concatenated into a
 * 24-bit word and written to the location specified by the address register",
 * and only then does the address advance. A model that stored each byte as it
 * arrived would be observationally identical here and wrong the moment a
 * partial colour is read back.
 *
 * ## The advance is not the same in the two colour spaces
 *
 * Palette RAM wraps: "the address register resets to $00 after a blue read or
 * write cycle to location $FF". The overlay registers do **not** -- "the address
 * register increments to $04 following a blue read or write cycle to overlay
 * register 3", which is the read mask, in a different `C1`/`C0` space. Walking
 * off the end of the overlays lands on a control register rather than back at
 * overlay 0, and a model that wrapped them to zero would silently keep writing
 * colours where the driver had moved on to masks.
 */

#ifndef APOLLO_DEVICE_AP_BT458_H
#define APOLLO_DEVICE_AP_BT458_H

#include <stdbool.h>
#include <stdint.h>

/* Table 1's `C1`/`C0`, as a two-bit selector with `C1` above `C0`. */
typedef enum {
  AP_BT458_ADDRESS = 0u, /* C1=0 C0=0 */
  AP_BT458_PALETTE = 1u, /* C1=0 C0=1 */
  AP_BT458_CONTROL = 2u, /* C1=1 C0=0 */
  AP_BT458_OVERLAY = 3u, /* C1=1 C0=1 */
} ap_bt458_select_t;

#define AP_BT458_PALETTE_ENTRIES 256u
#define AP_BT458_OVERLAY_ENTRIES 4u

/* The four control registers, at the addresses Table 1 gives them. Reached
 * with `C1`/`C0` = 10, so the address register is doing double duty: $04-$07
 * here are not palette entries $04-$07. */
#define AP_BT458_READ_MASK 0x04u
#define AP_BT458_BLINK_MASK 0x05u
#define AP_BT458_COMMAND 0x06u
#define AP_BT458_TEST 0x07u

typedef struct {
  /* `ADDR0-7`, the eight bits the MPU can see. */
  uint8_t address;
  /* `ADDRa,b`: which of red, green, blue the next colour byte is. Counts
   * modulo three and is not readable -- see the header. */
  unsigned component;

  /* Held bytes of a colour not yet complete. The datasheet has the three
   * concatenated and written on the blue cycle, so a colour only exists once
   * all three have arrived. */
  uint8_t pending[3];

  uint8_t palette[AP_BT458_PALETTE_ENTRIES][3];
  uint8_t overlay[AP_BT458_OVERLAY_ENTRIES][3];

  uint8_t read_mask;
  uint8_t blink_mask;
  uint8_t command;
  uint8_t test;
} ap_bt458_t;

void ap_bt458_reset(ap_bt458_t *lut);

void ap_bt458_write(ap_bt458_t *lut, ap_bt458_select_t select, uint8_t value);
[[nodiscard]] uint8_t ap_bt458_read(ap_bt458_t *lut, ap_bt458_select_t select);

/* Which colour component the next access moves, 0 for red. Exposed because it
 * is the one piece of state a driver cannot read back, so a test asserting the
 * modulo-three counter has no other way to see it -- and because a colour
 * written two bytes at a time is a real state a driver can leave the part in. */
[[nodiscard]] unsigned ap_bt458_component(const ap_bt458_t *lut);

/* A palette entry's three bytes. False for an index past the table. */
[[nodiscard]] bool ap_bt458_palette(const ap_bt458_t *lut, unsigned index,
                                    uint8_t rgb[3]);

#endif /* APOLLO_DEVICE_AP_BT458_H */

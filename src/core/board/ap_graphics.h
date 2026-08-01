/* Apollo display controller identification.
 *
 * Two register blocks on the DN3500's map, one per controller family:
 *
 *     05D800-05DC07  monochrome controller registers
 *     05E800-05EC07  colour controller registers
 *
 * ## What this module is, and what it deliberately is not
 *
 * Only **identification** is modelled: the device ID register, and the fact
 * that both blocks decode whether or not a screen is fitted. Drawing, the
 * blitter, the colour lookup table, the graphics memories at `0A0000` and
 * `0FA0000` -- none of that is here.
 *
 * That is a complete answer to a real question rather than a stub, because the
 * boot PROM's first contact with the display is a *probe*, and a probe only
 * needs to be answered correctly. Modelling the ID register and nothing else is
 * honest; modelling a blitter that draws nothing would not be.
 *
 * ## The address answers even when no screen is fitted
 *
 * This is the whole point, and getting it wrong is what sent an investigation
 * after a phantom bug in the CPU's exception path.
 *
 * A DN3500 decodes both blocks unconditionally. With no screen fitted the ID
 * register reads `FF`, which matches none of the four screen types, and the
 * firmware concludes there is no display and moves on. It does **not** bus
 * error. A machine that faults here instead makes the firmware take an
 * exception the real one never takes, and everything downstream of that --
 * handler entry, the stack descending, a double fault -- looks like a defect in
 * whatever the handler touches. It is not. It is this device being absent.
 *
 * "Nothing is fitted" and "nothing is there" are different answers, and only
 * the second is a bus error.
 *
 * ## Each block answers only for its own family
 *
 * The colour block reports a colour screen's ID and the monochrome block a
 * monochrome one; each reads `FF` for the other's. So a machine with a colour
 * screen answers `FF` at `05D801` and its ID at `05E801`, which is exactly what
 * lets the firmware tell which of the two controllers is present by reading
 * both.
 */

#ifndef APOLLO_BOARD_AP_GRAPHICS_H
#define APOLLO_BOARD_AP_GRAPHICS_H

#include <stdbool.h>
#include <stdint.h>

#define AP_GRAPHICS_MONO_ADDR 0x05D800u
#define AP_GRAPHICS_COLOUR_ADDR 0x05E800u
/* `05D800-05DC07` inclusive is 0x408 bytes. Not a power of two, and not
 * aliased: the block is decoded as a range, which is what the map gives. */
#define AP_GRAPHICS_RANGE 0x408u

/* The graphics memories, from the oracle's map: `0A0000-0BFFFF` colour and
 * `FA0000-FDFFFF` monochrome.
 *
 * Both fall **inside** the AT bus memory window, so they must be decoded before
 * it or the window swallows them and the machine reports its own frame buffer
 * as an empty expansion slot. The I/O window has the same hazard and a test for
 * it; this is the memory window's, and it was live until the graphics memories
 * were named. */
#define AP_GRAPHICS_COLOUR_MEMORY_ADDR 0x0A0000u
#define AP_GRAPHICS_COLOUR_MEMORY_END 0x0BFFFFu
#define AP_GRAPHICS_MONO_MEMORY_ADDR 0xFA0000u
#define AP_GRAPHICS_MONO_MEMORY_END 0xFDFFFFu

/* The device ID register, at offset 1 of either block. */
#define AP_GRAPHICS_DEVICE_ID 1u

/* The screen types the firmware knows, by the value it compares the ID
 * register against. The boot PROM tests for them in this order: `08` and `0A`
 * at the colour block, then `09` and `0B` at the monochrome one. */
typedef enum {
  AP_SCREEN_NONE = 0,           /* no display controller fitted */
  AP_SCREEN_COLOUR_4_PLANE = 8, /* C4P */
  AP_SCREEN_MONO_19_INCH = 9,   /* 19I */
  AP_SCREEN_COLOUR_8_PLANE = 10,/* C8P */
  AP_SCREEN_MONO_15_INCH = 11,  /* 15I */
} ap_screen_kind_t;

typedef struct {
  ap_screen_kind_t screen;

  /* The graphics memories, caller-owned as main memory is: this core allocates
   * nothing. NULL until a caller attaches them, and a card with no memory
   * attached reads `FF` exactly as an absent one does -- a frame buffer that
   * answered zero would look like a screen showing black rather than like no
   * screen at all. */
  uint8_t *colour_memory;
  uint32_t colour_bytes;
  uint8_t *mono_memory;
  uint32_t mono_bytes;
} ap_graphics_t;

void ap_graphics_init(ap_graphics_t *graphics, ap_screen_kind_t screen);

/* Attach the graphics memories. Either may be NULL, which is what a machine
 * with one controller and not the other has.
 *
 * Storage only: nothing here decodes a pixel or drives a screen, and a test
 * that writes and reads back proves the memory works and says nothing about a
 * display. That is the honest limit of this module until the controller lands,
 * and it is stated here so the round-trip test cannot be mistaken for one. */
void ap_graphics_attach_memory(ap_graphics_t *graphics, uint8_t *colour,
                               uint32_t colour_bytes, uint8_t *mono,
                               uint32_t mono_bytes);

/* True when the screen fitted is a colour one. `AP_SCREEN_NONE` is neither, so
 * both blocks answer `FF`. */
[[nodiscard]] bool ap_graphics_is_colour(ap_screen_kind_t screen);
[[nodiscard]] bool ap_graphics_is_monochrome(ap_screen_kind_t screen);

/* Decode an address to one of the two blocks. `colour` says which. */
[[nodiscard]] bool ap_graphics_decode(uint32_t address, bool *colour,
                                      uint32_t *offset);

/* Decode an address to one of the two graphics memories. Separate from the
 * register decode because the two answer differently: a register block reports
 * a screen's identity, while the memory is storage a fitted card provides and
 * an absent one does not. */
[[nodiscard]] bool ap_graphics_decode_memory(uint32_t address, bool *colour,
                                             uint32_t *offset);

/* Read a register. Both blocks decode whether or not a screen is fitted; a
 * register this module does not model reads `FF`, which is also what an absent
 * screen's ID register reads. */
[[nodiscard]] uint8_t ap_graphics_read(const ap_graphics_t *graphics,
                                       uint32_t address);

/* Writes are accepted and discarded. The blocks are decoded, so a write
 * terminates normally -- refusing it would be a bus error the hardware does not
 * raise. What the registers would *do* is not modelled, which is why this
 * stores nothing rather than storing something a later read would have to
 * invent a meaning for. */
void ap_graphics_write(ap_graphics_t *graphics, uint32_t address,
                       uint8_t value);

#endif /* APOLLO_BOARD_AP_GRAPHICS_H */

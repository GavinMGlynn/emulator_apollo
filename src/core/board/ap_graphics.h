/* The Apollo display controller.
 *
 * Two register blocks on the DN3500's map, one per controller family:
 *
 *     05D800-05DC07  monochrome controller registers
 *     05E800-05EC07  colour controller registers
 *
 * ## What this module is, and what it deliberately is not
 *
 * Identification, the register file, the blitter's data path and plane loop,
 * `CR0`'s mode dispatch -- so a CPU write to the image memory is the blit cycle
 * it really is rather than a store -- and the scanout. What is **not** here:
 * the lookup table, which is a separate part (`device/ap_bt458.c`) and is not
 * yet wired to a board, so an index cannot become a colour; the status
 * register, whose bits report states this core does not have; and which plane
 * the CPU's 128 KB window selects, which is unmeasured.
 *
 * The module grew in that order for a reason. The boot PROM's first contact
 * with the display is a *probe*, and a probe only needs to be answered
 * correctly, so identification alone was a complete answer to a real question
 * rather than a stub. Everything after it was added when it could be checked:
 * modelling a blitter that draws nothing would not have been honest.
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

#include "device/ap_bt458.h"

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

/* ## The register file, and the offsets it lives at
 *
 * Until now `CR0`-`CR2` and the raster operation were *arguments*: every
 * function that used one was handed it, and a write to the block was accepted
 * and discarded. That was honest while nothing could read one back, and it is
 * what a real picture waits on -- the firmware programs the controller and then
 * blits, and a blitter that cannot see what was programmed cannot draw what was
 * asked for.
 *
 * ### Where these come from
 *
 * `008778-03` Chapter 10 is *physical only* -- board dimensions, connectors,
 * cables -- and gives no register offsets at all. Its §10.3 change list names
 * the registers and their widths, which is what settled `CR2`'s plane selects
 * and the 32-bit ROP register, and the offsets themselves are the **oracle's**.
 * That is the same position this subsystem has been in since it started and it
 * is stated again here rather than left implicit.
 *
 * ### The block decodes sixteen registers, aliased
 *
 * `05D800-05DC07` is `0x408` bytes and an access is decoded as `offset & 0x407`
 * -- bit 10 and the low three bits. So the block is two groups of eight,
 * `000-007` and `400-407`, each repeating through the range.
 *
 * ### The byte lanes are scrambled, and no reading of the names predicts them
 *
 * The write enable register is sixteen bits across offsets 0 and 1, and the
 * raster operation thirty-two across 2, 3, 4 and 5. Neither is in the order the
 * addresses suggest:
 *
 *     offset 0  write enable, bits 15-8      offset 1  write enable, bits 7-0
 *     offset 2  raster op,    bits 15-8      offset 3  raster op,    bits 7-0
 *     offset 4  raster op,    bits 31-24     offset 5  raster op,    bits 23-16
 *
 * Every pair is high byte first, and the *pairs* run low half before high half.
 * A model assembling either register in address order gets the halves the right
 * way round and the bytes within them backwards, which for the ROP means every
 * plane's function comes from its neighbour -- a screen that draws, in the
 * wrong operations.
 *
 * Offsets 4 and 5 are the raster operation's high half on an **8-plane** board
 * and a diagnostic memory-refresh trigger on the others, which is the same
 * per-family split `CR1`'s top bits have.
 */

/* The two groups of eight the block decodes into. */
#define AP_GRAPHICS_REGISTER_MASK 0x407u

/* The low group: data-path registers, byte at a time. */
#define AP_GRAPHICS_REG_STATUS 0x000u        /* read */
#define AP_GRAPHICS_REG_WRITE_ENABLE_HI 0x000u /* write: bits 15-8 */
#define AP_GRAPHICS_REG_WRITE_ENABLE_LO 0x001u /* write: bits 7-0 */
#define AP_GRAPHICS_REG_ROP_15_8 0x002u
#define AP_GRAPHICS_REG_ROP_7_0 0x003u
#define AP_GRAPHICS_REG_ROP_31_24 0x004u
#define AP_GRAPHICS_REG_ROP_23_16 0x005u

/* The high group: control registers. */
#define AP_GRAPHICS_REG_CR0 0x400u
#define AP_GRAPHICS_REG_CR1 0x402u
#define AP_GRAPHICS_REG_CR2 0x404u  /* CR2A on an 8-plane board */
#define AP_GRAPHICS_REG_CR2B 0x405u /* 8-plane only */
#define AP_GRAPHICS_REG_CR3A 0x406u
#define AP_GRAPHICS_REG_CR3B 0x407u /* 8-plane only */

/* The widest board this machine has. The 8-plane colour controller; every
 * other is a subset, and the arrays below are sized for the widest rather than
 * per-board because a card is chosen at run time. */
#define AP_GRAPHICS_MAX_PLANES 8u

typedef struct {
  uint8_t cr0;
  uint8_t cr1;
  uint8_t cr2;
  uint8_t cr2b;
  uint8_t cr3a;
  uint8_t cr3b;
  uint16_t write_enable;
  uint32_t rop;
} ap_graphics_registers_t;

/* ## The lookup table's two ports, and their active-low chip selects
 *
 * The Bt458 is not on the bus. It sits behind two registers of the 8-plane
 * board's own -- a **data** port at `401` and a **control** port at `403` --
 * and the control port's bits say which of three things the data port is
 * talking to. Every one of those selects is **active low**, so a control
 * register of `FF` selects nothing at all.
 *
 * `C1` and `C0` are passed straight through to the RAMDAC's own control inputs,
 * which is what identified the part in the first place.
 *
 * ### The FIFO, and why a palette load is deferred
 *
 * With `FIFO_CS` asserted, data-port writes go into a 1024-byte FIFO instead of
 * the part. They are drained into it when `CPAL_CS` is **released** -- the
 * transition, not the level. So a driver loads a whole palette into the buffer
 * and commits it in one go, which is how the table is rewritten without tearing
 * the picture. A model writing straight through would be observationally
 * identical until something read the palette back mid-load.
 *
 * The depth is the oracle's; no manual in `docs/references/` gives one. An
 * overrun is therefore *counted* rather than silently dropped, because the
 * number that would be exceeded is not one this project can defend.
 *
 * ### The read and write orders are not the same
 *
 * A write tries `AD_CS`, then `CPAL_CS`, then `FIFO_CS`. A read tries `FIFO_CS`
 * **first**, then the direction bit, then `AD_CS`, then `CPAL_CS`. That
 * asymmetry is not a transcription slip -- it is what lets a driver push into
 * the FIFO and read back the part in the same control-register setting.
 */
#define AP_GRAPHICS_LUT_AD_CS 0x80u
#define AP_GRAPHICS_LUT_CPAL_CS 0x40u
#define AP_GRAPHICS_LUT_FIFO_CS 0x20u
#define AP_GRAPHICS_LUT_FIFO_RST 0x10u
#define AP_GRAPHICS_LUT_ST_LUK 0x08u
#define AP_GRAPHICS_LUT_R_W 0x04u
#define AP_GRAPHICS_LUT_C1_C0 0x03u
#define AP_GRAPHICS_LUT_FIFO_BYTES 1024u

/* The lookup table's ports, in the high group beside the control registers. */
#define AP_GRAPHICS_REG_LUT_DATA 0x401u
#define AP_GRAPHICS_REG_LUT_CONTROL 0x403u

typedef struct {
  ap_screen_kind_t screen;

  /* What the firmware programmed, readable back. */
  ap_graphics_registers_t reg;

  /* The colour lookup table, and the two ports it lives behind. Only an
   * 8-plane board has one. */
  ap_bt458_t lut;
  uint8_t lut_control;
  uint8_t lut_data;
  uint8_t lut_fifo[AP_GRAPHICS_LUT_FIFO_BYTES];
  unsigned lut_fifo_head;
  unsigned lut_fifo_count;
  /* Counted, not assumed away: the depth is the oracle's and no manual gives
   * one, so a run that overruns it is a run whose palette cannot be trusted. */
  unsigned lut_fifo_overruns;
  /* The A/D converter behind the third chip select. Not modelled -- it reads a
   * monitor's identification and a brightness pot, neither of which this core
   * has -- so an access is counted rather than answered with a number nothing
   * stands behind. */
  unsigned lut_ad_accesses;

  /* The guard latch, one 32-bit entry per plane -- see `ap_graphics_blit_t`.
   * Controller state, carried between the two bus cycles of modes 1 and 3. */
  uint32_t guard_latch[AP_GRAPHICS_MAX_PLANES];
  /* Which cycle of a two-cycle mode is next: 0 for the first. */
  unsigned blt_cycle;

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
 * screen's ID register reads.
 *
 * **Not const**, and that is a fact about the hardware rather than an oversight:
 * a read of the lookup table's data port advances the Bt458's colour counter,
 * so reading this device changes it. The same rule that makes `--boot-watch`
 * refuse a non-memory address, and the same one that defeated the Bt458 suite's
 * own first draft. */
[[nodiscard]] uint8_t ap_graphics_read(ap_graphics_t *graphics,
                                       uint32_t address);

/* Writes are accepted and discarded. The blocks are decoded, so a write
 * terminates normally -- refusing it would be a bus error the hardware does not
 * raise. What the registers would *do* is not modelled, which is why this
 * stores nothing rather than storing something a later read would have to
 * invent a meaning for. */
void ap_graphics_write(ap_graphics_t *graphics, uint32_t address,
                       uint8_t value);

/* ## The control registers' mode fields
 *
 * `CR0` bits 7-5 select one of eight operating modes and `CR2` bits 7-6 one of
 * four access modes. Decoded here as a pure data module -- names and bit
 * positions only -- because that is the part that can be got right before any
 * of it draws anything, and because a mode field read from the wrong bits is a
 * defect that survives every test of the thing above it.
 *
 * ## Two of the eight are unknown, and stay unknown
 *
 * The oracle's own source lists modes 5 and 6 as `???`, and access mode 2 the
 * same. That is the state of the knowledge, not a gap in the transcription, so
 * they are named `UNKNOWN` rather than given a plausible label. A guess here
 * would be indistinguishable from a fact for as long as nobody exercised it,
 * and the first thing to exercise it would be firmware doing something real.
 */

typedef enum {
  AP_GRAPHICS_CR0_CPU_DEST_BLT = 0u,
  AP_GRAPHICS_CR0_ALTERNATING_BLT = 1u,
  AP_GRAPHICS_CR0_VECTOR = 2u,
  AP_GRAPHICS_CR0_CPU_SOURCE_BLT = 3u,
  AP_GRAPHICS_CR0_DOUBLE_ACCESS_BLT = 4u,
  AP_GRAPHICS_CR0_UNKNOWN_5 = 5u,
  AP_GRAPHICS_CR0_UNKNOWN_6 = 6u,
  AP_GRAPHICS_CR0_NORMAL = 7u,
} ap_graphics_cr0_mode_t;

typedef enum {
  AP_GRAPHICS_CR2_CONSTANT_ACCESS = 0u,
  AP_GRAPHICS_CR2_PIXEL_ACCESS = 1u,
  AP_GRAPHICS_CR2_SHIFT_ACCESS = 2u,
  AP_GRAPHICS_CR2_PLANE_ACCESS = 3u,
} ap_graphics_cr2_access_t;

/* `CR0` bits 7-5, `CR2` bits 7-6. Every value of each is defined, so neither
 * can fail -- an eight-way field read from three bits has no invalid case, and
 * pretending otherwise would invent an error the hardware cannot report. */
[[nodiscard]] ap_graphics_cr0_mode_t ap_graphics_cr0_mode(uint8_t cr0);
[[nodiscard]] ap_graphics_cr2_access_t ap_graphics_cr2_access(uint8_t cr2);

/* Names, for traces. The unknown modes say so rather than reading as blank. */
[[nodiscard]] const char *ap_graphics_cr0_mode_name(ap_graphics_cr0_mode_t m);
[[nodiscard]] const char *ap_graphics_cr2_access_name(ap_graphics_cr2_access_t a);

/* `CR0`'s other field: bits 4-0 are a shift count. The register carries two
 * fields, and decoding only the mode -- which is what this module did first --
 * leaves the other reading as part of neither. */
#define AP_GRAPHICS_CR0_SHIFT_MASK 0x1Fu
[[nodiscard]] unsigned ap_graphics_cr0_shift(uint8_t cr0);

/* ## `CR1`, whose top two bits mean different things per family
 *
 * On a monochrome controller bit 7 is INV and bit 6 is DADDR_16; on a 4- or
 * 8-plane colour one the same bits are AD_BIT and DV_CK. The lower six are
 * common.
 *
 * That is why these are named per family rather than given one set of names
 * with a comment. A single `AP_GRAPHICS_CR1_INV` would be silently wrong on
 * half the machines this core is meant to model, and wrong in the direction
 * that still runs -- the bit would be read, believed, and mean something else.
 */
#define AP_GRAPHICS_CR1_MONO_INV 0x80u
#define AP_GRAPHICS_CR1_MONO_DADDR_16 0x40u
#define AP_GRAPHICS_CR1_COLOUR_AD_BIT 0x80u
#define AP_GRAPHICS_CR1_COLOUR_DV_CK 0x40u

/* Common to both families. */
#define AP_GRAPHICS_CR1_DH_CK 0x20u
#define AP_GRAPHICS_CR1_ROP_EN 0x10u
#define AP_GRAPHICS_CR1_RESET 0x08u
#define AP_GRAPHICS_CR1_DP_CK 0x04u
#define AP_GRAPHICS_CR1_SYNC_EN 0x02u
#define AP_GRAPHICS_CR1_DISP_EN 0x01u

/* ## `CR2`'s plane selects, whose widths are the two boards' difference
 *
 * `008778-03` §10.3's change list for the 8-plane board says "Destination Plane
 * Selection (D_PLANE) increased to **8 bits**" and "Source Plane Selection
 * (S_PLANE) increased to **3 bits** and moved to the added 82C55A". So the two
 * families read the same register differently, as `CR1`'s top bits do:
 *
 *     4-plane   S_PLANE = CR2[5:4] (two bits), D_PLANE = CR2[3:0] (four)
 *     8-plane   S_PLANE = three bits, D_PLANE = a whole byte, on separate ports
 *
 * The manual states that the widths changed and by how much; the oracle carries
 * both encodings side by side as `CR2_S_PLANE`/`CR2_D_PLANE` against
 * `CR2B_S_PLANE`/`CR2A_D_PLANE`. Neither source alone settles it. */
#define AP_GRAPHICS_CR2_S_PLANE_MASK 0x30u
#define AP_GRAPHICS_CR2_S_PLANE_SHIFT 4u
#define AP_GRAPHICS_CR2_D_PLANE_MASK 0x0Fu
#define AP_GRAPHICS_CR2_S_PLANE_MASK_8 0x07u

[[nodiscard]] unsigned ap_graphics_cr2_source_plane(uint8_t cr2, bool eight);
[[nodiscard]] unsigned ap_graphics_cr2_dest_plane(uint8_t cr2, bool eight);

/* ## The raster operation
 *
 * `008778-03` §10.3: "ROP Register specifiers increased to **32 bits**" -- which
 * is eight planes of four bits, and the four bits are a boolean function of
 * source and destination. All sixteen are defined, because sixteen is exactly
 * how many functions of two bits there are: the field cannot hold an invalid
 * value and this cannot fail.
 *
 * The identities are worth naming rather than numbering. `0011` is *source* --
 * a plain copy, the blit that does no combining -- and `0101` is *destination*,
 * which writes nothing at all. A blitter whose ROP decode was off by one would
 * turn every copy into an AND and still draw something.
 *
 * `CR1`'s `ROP_EN` gates the whole thing: with it clear the source passes
 * through whatever the register says, so a driver that programmed a ROP and
 * forgot the enable gets a copy rather than the operation it asked for.
 *
 * **The underlying type is fixed**, and that is not decoration. An enum whose
 * type C leaves to the implementation is `unsigned` under one ABI and signed
 * `int` under another, so widening one to a `uint32_t` register is a plain
 * conversion on Linux and a *signedness change* on Windows -- which
 * `-Wsign-conversion` refuses, as it should. That difference compiled here and
 * broke CI on one platform only. A four-bit function code has no business being
 * signed anywhere, so it says so. */
typedef enum : uint8_t {
  AP_GRAPHICS_ROP_ZERO = 0x0u,
  AP_GRAPHICS_ROP_SRC_AND_DST = 0x1u,
  AP_GRAPHICS_ROP_SRC_AND_NOT_DST = 0x2u,
  AP_GRAPHICS_ROP_SRC = 0x3u,
  AP_GRAPHICS_ROP_NOT_SRC_AND_DST = 0x4u,
  AP_GRAPHICS_ROP_DST = 0x5u,
  AP_GRAPHICS_ROP_SRC_XOR_DST = 0x6u,
  AP_GRAPHICS_ROP_SRC_OR_DST = 0x7u,
  AP_GRAPHICS_ROP_SRC_NOR_DST = 0x8u,
  AP_GRAPHICS_ROP_SRC_XNOR_DST = 0x9u,
  AP_GRAPHICS_ROP_NOT_DST = 0xAu,
  AP_GRAPHICS_ROP_SRC_OR_NOT_DST = 0xBu,
  AP_GRAPHICS_ROP_NOT_SRC = 0xCu,
  AP_GRAPHICS_ROP_NOT_SRC_OR_DST = 0xDu,
  AP_GRAPHICS_ROP_SRC_NAND_DST = 0xEu,
  AP_GRAPHICS_ROP_ONE = 0xFu,
} ap_graphics_rop_t;

/* Eight planes of four bits -- the same eight the board can have. */
#define AP_GRAPHICS_ROP_PLANES AP_GRAPHICS_MAX_PLANES

/* Which function a plane's four bits select. */
[[nodiscard]] ap_graphics_rop_t ap_graphics_rop_for(uint32_t rop_register,
                                                    unsigned plane);

/* Apply it. `cr1` is passed whole rather than as a flag, because `ROP_EN` is
 * what decides whether the register means anything at all -- and a caller that
 * had to remember to check it separately would eventually not. */
[[nodiscard]] uint16_t ap_graphics_rop_apply(uint8_t cr1, uint32_t rop_register,
                                             unsigned plane, uint16_t source,
                                             uint16_t destination);

/* ## The blitter's word-level data path
 *
 * Four steps, in order: the source word is produced according to `CR2[7:6]`'s
 * access mode, combined with the destination by the plane's raster operation,
 * masked by the write enable register, and written to the planes `CR2`'s
 * destination select names.
 *
 * ### Two of those are active **low**, and both invert a whole screen
 *
 * A destination plane is written when its `D_PLANE` bit is **zero**. A model
 * reading a set bit as "write this plane" draws into exactly the planes it
 * should have left alone -- and on a monochrome screen that is an image and its
 * negative, which looks like a polarity bug anywhere else in the pipeline.
 *
 * The write enable register runs the same way within a word: a bit **set**
 * protects the destination bit and a bit clear lets the source through. So the
 * register named "write enable" enables writing where it is zero, which is the
 * kind of name that survives being read carefully.
 *
 * ### The access modes are how one source word becomes a pattern
 *
 * `CONST` is all ones, which is what a vector draw wants; `PIXEL` and `SHIFT`
 * replicate a single bit across the whole word, turning one bit of a source
 * into a solid word for the plane it belongs to; `PLANE` passes the word
 * through, shifted by `CR0`'s count. Only the last is a copy in the ordinary
 * sense, and it is the one the manual calls "normal use".
 */

/* The source word `CR2[7:6]`'s access mode produces, before the raster
 * operation sees it. `latched` is what the blitter read from the source. */
[[nodiscard]] uint16_t ap_graphics_source_data(uint8_t cr0,
                                               ap_graphics_cr2_access_t access,
                                               unsigned plane,
                                               uint32_t latched);

/* Whether a destination plane is written. **Active low**: see above. */
[[nodiscard]] bool ap_graphics_plane_selected(unsigned d_plane, unsigned plane);

/* Merge a source word into a destination under the write enable register and
 * the bus's byte mask. **A write enable bit set protects the destination.** */
[[nodiscard]] uint16_t ap_graphics_combine(uint16_t write_enable,
                                           uint16_t mem_mask, uint16_t source,
                                           uint16_t destination);

/* ## A blit, which is the plane loop around all of the above
 *
 * One destination word per plane, the planes laid out one after another in the
 * image memory with a fixed stride. Everything the operation needs is gathered
 * here rather than passed as eight arguments, because the *combination* is what
 * a blit is and a caller assembling it piecemeal can leave one stale.
 *
 * `latched` is the **guard latch**, one entry per plane, and it is *thirty-two*
 * bits: each new source word is shifted in from the bottom, so the latch holds
 * the previous word above the current one. That width is the whole reason the
 * latch exists. `CR0`'s shift then operates across the pair, and a shifted blit
 * pulls the bits it needs out of the *previous* word -- which is what draws a
 * bitmap that does not begin on a word boundary, and therefore what draws
 * almost any text. A sixteen-bit latch shifts zeroes in instead: the picture
 * still appears, with a blank sliver at the leading edge of every word.
 *
 * Which entry a plane uses is not always its own: on a single-plane board, or
 * when `CR1`'s colour `AD_BIT` is set, every plane takes the *source plane's*
 * word instead. That is how one source is broadcast to many destinations, and a
 * model that always indexed by the destination plane would draw the right shape
 * in the wrong colours.
 */
typedef struct {
  uint8_t cr0;
  uint8_t cr1;
  ap_graphics_cr2_access_t access;
  uint32_t rop_register;
  uint16_t write_enable;
  /* `CR2`'s selects, already decoded -- the widths differ per board and
   * `ap_graphics_cr2_*_plane` is where that lives. */
  unsigned d_plane;
  unsigned s_plane;
  /* How many planes the board has, and how far apart they are in the image
   * memory, in words. */
  unsigned planes;
  uint32_t plane_stride;
} ap_graphics_blit_t;

/* Perform one blit into `image`, `bytes` long, addressed in **words** -- which
 * is how the controller addresses it and how a caller's `dest` and
 * `plane_stride` are counted. `dest` is the word offset of plane 0's
 * destination; each further plane is `plane_stride` beyond the last.
 *
 * The image is the *board's* memory, bytes, big-endian as the 68030 wrote it.
 * It used to be a host-order `uint16_t` array, which meant the blitter and the
 * scanout could not share one buffer and the joining had to be done by hand in
 * a test. One memory is what the hardware has.
 *
 * Returns how many planes were actually written, which is not the plane count:
 * `D_PLANE` masks planes out, and a destination past the end of the memory is
 * skipped rather than wrapped. A caller that assumed every plane landed would
 * not notice either. */
[[nodiscard]] unsigned ap_graphics_blit(const ap_graphics_blit_t *blit,
                                        uint8_t *image, uint32_t bytes,
                                        uint32_t dest, uint16_t mem_mask,
                                        const uint32_t *latched);

/* ## Scanout: the image memory read out as pixels
 *
 * Everything above writes *into* the image memory. This reads it out, which is
 * the only thing that turns a controller into a display and the only check the
 * plan's verification line accepts -- "a controller that passes register tests
 * and draws nothing is the standard way this goes wrong".
 *
 * ### The memory is wider than the screen, and the manual says by how much
 *
 * Each geometry below is `008778-03`'s own, and the *buffer* widths -- the part
 * that looks like an implementation detail -- fall straight out of the printed
 * memory capacities:
 *
 *     4-plane colour   "512 KB of image memory arranged in four 128-KB planes",
 *                      §1.5.3, and 128 KB is 1024 x 1024 bits. Visible
 *                      1024 x 800, §10.1.
 *     8-plane colour   "eight memory planes, each consists of a 1024 pixel by
 *                      1024 line memory, with a resolution of 1024 pixels x 800
 *                      lines", §1.5.3 -- which states both geometries outright.
 *                      "Dual-port, 1-MB image memory", §10.3, agrees.
 *     1280x1024 mono   "256-KB image memory", §1.5.3 and §10.2, and 256 KB is
 *                      2048 x 1024 bits. Visible 1280 x 1024.
 *
 * So a line occupies `buffer_width / 16` words and only the first
 * `width / 16` of them are displayed. A model using the visible width as the
 * stride shears the image progressively down the screen, which reads as a
 * timing fault rather than as an arithmetic one.
 *
 * The 15-inch 1024 x 800 monochrome is **not in this manual** -- Chapter 10
 * covers the 4-plane, the 1280 x 1024 monochrome and the 8-plane, and that
 * board is later. Its geometry is the oracle's, and is marked as such here
 * rather than given a citation it does not have.
 *
 * ### Plane 0 is the least significant bit, and the high bit is the left pixel
 *
 * The planes are consecutive blocks of `plane_words`, and a pixel's index is
 * assembled with plane 0 as bit 0. Within a word, bit 15 is the *leftmost*
 * pixel -- a word is drawn left to right from the top down, which is the
 * ordering a big-endian machine's bitmap has and the opposite of the one a
 * shift-right loop falls into by accident. Either mistake mirrors the screen,
 * and mirroring is symmetric enough to look plausible in a thumbnail.
 */

typedef struct {
  unsigned planes;
  /* What the monitor shows. */
  unsigned width;
  unsigned height;
  /* What the memory holds, which is wider and taller. */
  unsigned buffer_width;
  unsigned buffer_height;
  /* One plane, in 16-bit words: `buffer_width * buffer_height / 16`. */
  uint32_t plane_words;
} ap_graphics_geometry_t;

/* ## `CR0`'s mode, and why a write to the graphics memory is not a store
 *
 * This is the piece that makes the firmware's own drawing appear. A CPU write
 * into the image memory is a **blit cycle**, and which one is `CR0` bits 7-5.
 * The address, the data and the write-enable register mean different things in
 * each, and three of the seven take *two* bus cycles to complete one blit.
 *
 *     0  CPU destination   the write carries an address only; the controller
 *                          latches the source from memory and the CPU reads it
 *                          back. Nothing is drawn by the write.
 *     1  alternating       two writes: the first names the source address, the
 *                          second carries the write enables and the destination.
 *     2  vector / fill     one write: the data *is* the write-enable register
 *                          and the address is the destination. With `CONST`
 *                          access the source is all ones, which is how a line
 *                          is drawn -- shape from the addresses, not the data.
 *     3  CPU source        two writes: the first carries source data, the
 *                          second the write enables and the destination.
 *     4  double access     one write: the address is the source and the *data*
 *                          is the destination word offset.
 *     5, 6                 unknown, and they stay unknown.
 *     7  normal            one write: the data is the source and the address
 *                          the destination. The ordinary case.
 *
 * A model that stored the word instead would draw nothing in every mode, and
 * would look identical to a firmware that never wrote -- which is exactly what
 * a screenshot of this core showed before the dispatch existed.
 *
 * ### The two-cycle modes need state, and it is the controller's
 *
 * Modes 1 and 3 count bus cycles, so the controller carries a cycle counter and
 * the guard latch between them. That is real hardware state and it survives a
 * write to any other register -- which is why it lives in `ap_graphics_t` and
 * not in a caller.
 */

/* What one CPU write to the image memory did. A cycle that only latched wrote
 * no planes, and that is not a failure: it is the first half of a two-cycle
 * mode. */
typedef struct {
  /* How many planes the cycle wrote. Zero for a latching cycle, a declined
   * mode, or a destination past the memory. */
  unsigned planes_written;
  /* True when the cycle completed a blit rather than latching. */
  bool blitted;
  /* True when `CR0` named one of the two modes nothing describes. Counted
   * rather than guessed: a run that hits one is a run whose picture cannot be
   * trusted, and silence would hide that. */
  bool unknown_mode;
} ap_graphics_cycle_t;

/* One CPU write to the image memory, `offset` in **words** from the base of the
 * memory and `mem_mask` the bus's byte mask -- `FFFF` for a word access,
 * `FF00` or `00FF` for a byte one.
 *
 * The controller is 16 bits wide, so this is the access it actually sees; the
 * board presenting two byte stores instead is the plumbing this waits on. */
ap_graphics_cycle_t ap_graphics_memory_cycle(ap_graphics_t *graphics,
                                             uint32_t offset, uint16_t data,
                                             uint16_t mem_mask);

/* `CR2`'s fields, decoded for the fitted board -- which is not one register on
 * all three. A monochrome controller has **one plane**, so its selects are
 * fixed rather than read: source plane 0, and a destination mask of `0E` that
 * leaves only plane 0 selected. A 4-plane board reads both selects and the
 * access mode out of `CR2`. An 8-plane board takes the destination mask from
 * `CR2A` as a whole byte, and the source plane *and the access mode* from
 * `CR2B` -- a different register, which is the trap: a model reading the access
 * mode from `CR2` on that board gets whatever the destination mask's top two
 * bits happen to be. */
void ap_graphics_cr2_fields(const ap_graphics_t *graphics, unsigned *s_plane,
                            unsigned *d_plane,
                            ap_graphics_cr2_access_t *access);

/* False for `AP_SCREEN_NONE`, which has no geometry rather than a zero one. */
[[nodiscard]] bool ap_graphics_geometry(ap_screen_kind_t kind,
                                        ap_graphics_geometry_t *out);

/* `CR1`'s `DISP_EN`. Separate from the scanout, and deliberately: a disabled
 * display is **black**, and black is not a pixel index -- index 0 on a
 * monochrome screen is *white*. Folding "disabled" into the index domain would
 * mean writing a value whose meaning depends on the screen type, so the caller
 * paints black and the scanout only ever reports what the memory holds. */
[[nodiscard]] bool ap_graphics_display_enabled(uint8_t cr1);

/* Read the image memory out as one byte of pixel index per pixel, `width *
 * height` of them, row by row from the top left.
 *
 * An index is what the *controller* produces; what colour it becomes is the
 * lookup table's answer and is not this module's. On a monochrome screen the
 * index is one bit, and a set bit is a **dark** pixel -- the bitmap stores ink,
 * not light. `CR1`'s monochrome `INV` inverts the memory word before that,
 * which is why it is applied here and not by whoever paints.
 *
 * Returns the number of pixels written, or zero if there is no screen, no
 * memory attached, the memory is too small for the geometry, or the buffer
 * given is. */
[[nodiscard]] uint32_t ap_graphics_scanout(const ap_graphics_t *graphics,
                                           uint8_t cr1, uint8_t *pixels,
                                           uint32_t capacity);

#endif /* APOLLO_BOARD_AP_GRAPHICS_H */

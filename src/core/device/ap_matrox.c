/* DN4500 Matrox graphics controller. See `ap_matrox.h` and
 * `docs/references/GRAPHICS.md`; every behaviour here cites a ROM address. */

#include "device/ap_matrox.h"

#include <stddef.h>

void ap_matrox_reset(ap_matrox_t *matrox) {
  if (matrox == NULL) {
    return;
  }
  *matrox = (ap_matrox_t){0};
}

static bool in_block(uint32_t address, uint32_t base, uint32_t size) {
  return address >= base && address < base + size;
}

void ap_matrox_attach_frame(ap_matrox_t *matrox, uint8_t *frame,
                            uint32_t bytes) {
  if (matrox == NULL) {
    return;
  }
  matrox->frame = frame;
  matrox->frame_bytes = bytes > AP_MATROX_FRAME_BYTES ? AP_MATROX_FRAME_BYTES
                                                      : bytes;
}

bool ap_matrox_decode(uint32_t address, uint32_t *block, uint32_t *offset) {
  uint32_t base = 0;
  if (in_block(address, AP_MATROX_FRAME_ADDR, AP_MATROX_FRAME_BYTES)) {
    base = AP_MATROX_FRAME_ADDR;
  } else if (in_block(address, AP_MATROX_DATA_ADDR, AP_MATROX_DATA_RANGE)) {
    base = AP_MATROX_DATA_ADDR;
  } else if (in_block(address, AP_MATROX_XFER_ADDR, AP_MATROX_XFER_RANGE)) {
    base = AP_MATROX_XFER_ADDR;
  } else if (in_block(address, AP_MATROX_CTL_ADDR, AP_MATROX_CTL_RANGE)) {
    base = AP_MATROX_CTL_ADDR;
  } else {
    return false;
  }
  if (block != NULL) {
    *block = base;
  }
  if (offset != NULL) {
    *offset = address - base;
  }
  return true;
}

uint8_t ap_matrox_read8(ap_matrox_t *matrox, uint32_t block, uint32_t offset) {
  if (matrox == NULL) {
    return 0u;
  }
  if (block == AP_MATROX_FRAME_ADDR) {
    /* Plain memory, which is the whole claim. Nothing here interprets the bits
     * -- the geometry that turns them into a picture is the frontend's, and is
     * a hypothesis until a render tests it (`GRAPHICS.md` 17b). Beyond an
     * attached frame the range reads as an undriven bus does. */
    if (matrox->frame == NULL || offset >= matrox->frame_bytes) {
      return 0xFFu;
    }
    return matrox->frame[offset];
  }
  if (block == AP_MATROX_CTL_ADDR) {
    /* **The status byte, and why it is zero.** `$59E` polls bit 3 for clear
     * with a 15,728,640-iteration budget and `$5B8` requires bit 6 clear; a
     * failure of either stores `$FFFF` as the routine's verdict. Those are the
     * only two bits any measurement has constrained, and both want clear.
     *
     * The rest are zero because nothing has asked about them yet -- the `btst`
     * sites for bits 4 and 5 are past this routine and unreached. `RING.md` 62
     * made exactly this choice for the ring's ID lane and gave the reason:
     * answering a set bit would claim a condition nobody has seen.
     *
     * **Bit 5 is the exception, because the firmware asserts it the other
     * way**: `$2EC`-`$310` polls it and leaves early on `bne`, so a set bit is
     * what ends that wait (`GRAPHICS.md` 13c). Bit 4 stays clear -- its site
     * at `$3BA` has not been reached, so nothing has said which way it goes. */
    return AP_MATROX_STATUS_READY;
  }
  if (block == AP_MATROX_DATA_ADDR) {
    /* Finding 7's write-then-read. `$5D6` reads this 4003 times and **discards
     * every one** -- `move.w $d40000.l,d1` with `d1` never examined -- so what
     * it answers is unconstrained by that loop. The latch is returned because
     * a port that swallowed writes and read back nothing would be a claim too,
     * and this one at least cannot contradict the four literal words the
     * firmware wrote at `$578`-`$590`. */
    return (uint8_t)((offset & 1u) != 0u ? (matrox->data_latch & 0xFFu)
                                         : (matrox->data_latch >> 8));
  }
  /* `$D80000`. Bit 7 of `+4` is polled once after each transfer to `+8`
   * (finding 8) and the branch it feeds has not been traced, so the ready
   * state is modelled as **ready** -- zero, the same restraint as above, which
   * is what lets a transfer complete rather than hang. */
  return 0u;
}

void ap_matrox_write8(ap_matrox_t *matrox, uint32_t block, uint32_t offset,
                      uint8_t value) {
  if (matrox == NULL) {
    return;
  }
  if (block == AP_MATROX_FRAME_ADDR) {
    if (matrox->frame == NULL || offset >= matrox->frame_bytes) {
      return;
    }
    matrox->frame[offset] = value;
    matrox->frame_writes++;
    return;
  }
  if (block == AP_MATROX_CTL_ADDR) {
    /* **The microcode port.** Finding 4b: `$504` writes 2358 words to `+0`
     * without ever incrementing the pointer, so every access to this offset is
     * one word of the image. Counted in bytes here because the bus delivers
     * halves; the word count is what `GRAPHICS.md` states and what a test can
     * check against the ROM's own `length`.
     *
     * Not stored. The image is 4,716 bytes of a program for a processor this
     * project has not identified, nothing executes it, and a buffer holding it
     * would be state that no observation could ever check. */
    if (offset == 0u || offset == 1u) {
      if ((offset & 1u) != 0u) {
        matrox->microcode_words++;
      }
    }
    return;
  }
  if (block == AP_MATROX_DATA_ADDR) {
    if ((offset & 1u) != 0u) {
      matrox->data_latch = (uint16_t)((matrox->data_latch & 0xFF00u) | value);
    } else {
      matrox->data_latch =
          (uint16_t)((matrox->data_latch & 0x00FFu) |
                     (uint16_t)((uint32_t)value << 8));
    }
    return;
  }
  /* `$D80000`: `+5` takes `$80` before a transfer and `+8` takes the longword
   * itself. Both are kept so the ready bit has something to be about. */
  if (offset == 5u) {
    matrox->transfer_armed = (value & 0x80u) != 0u;
    return;
  }
  if (offset >= 8u && offset < 12u) {
    const unsigned shift = (3u - (offset - 8u)) * 8u;
    matrox->last_transfer =
        (matrox->last_transfer & ~((uint32_t)0xFFu << shift)) |
        ((uint32_t)value << shift);
  }
}

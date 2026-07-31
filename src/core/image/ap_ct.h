/* Apollo cartridge tape images (`.ct`).
 *
 * The Domain/OS distribution in `media/` is supplied this way, and
 * `FINDINGS.md` C24 establishes the format by measurement: a **raw image of
 * 512-byte blocks with no container and no wrapper**. The boot cartridge is
 * 53,678,592 bytes, exactly 104,841 blocks with no remainder. So there is
 * nothing to parse -- only to address -- and this module is correspondingly
 * small.
 *
 * ## It takes memory, not a filename
 *
 * `src/core` has no frontend dependencies and no file I/O, and a deterministic
 * core cannot have a device reaching for a path at run time. The caller maps or
 * reads the image and hands over the bytes; this module never learns where they
 * came from.
 *
 * That also keeps the tests free of `media/`, which is gitignored because Apollo
 * distribution media is not ours to redistribute. A test builds the image it
 * needs.
 *
 * The directory is `image/` and not `media/` for the same reason, the hard way:
 * `.gitignore`'s `media/` rule is unanchored and matches a directory of that
 * name at *any* depth, so `src/core/media/` was silently untracked. Renaming
 * was preferred to anchoring the rule -- the rule protects firmware that is not
 * ours to redistribute, and narrowing it to buy a directory name is a poor
 * trade.
 *
 * ## The boot record
 *
 * Block 0 of a bootable cartridge carries four big-endian 32-bit words, an
 * ASCII identification, and then MC68000 code. The measured boot cartridge:
 *
 *     0013D800  0013D82A  0013F6BC  56AC0D83  "SYSBOOT REV" ... "M68K"
 *
 * `ap_ct_boot_image` names them, and may: `FINDINGS.md` C24 confirms the reading
 * from the boot code itself. The record's first instruction is
 * `LEA (-44,PC),A0`, which executed at word 1 computes word 0 exactly -- so word
 * 1 is where the code runs and word 0 is where the image sits, and the firmware
 * would break if either were otherwise.
 *
 * `ap_ct_boot_record` still returns the words verbatim and does **not** name
 * them.
 * They read as load address, entry point, end address and checksum -- word 1 is
 * word 0 plus `0x2A`, exactly where the code begins, and word 2 gives a
 * 7868-byte image -- but C24 records that as an inference, and three addresses
 * of something else would fit the same arithmetic. A field named `load_address`
 * here would turn a guess into an assertion for every later reader.
 *
 * What *is* established is the identification: a cartridge announcing "SYSBOOT
 * REV" and "M68K" can be recognised as bootable, and as 68000, without
 * executing a byte of it.
 */

#ifndef APOLLO_IMAGE_AP_CT_H
#define APOLLO_IMAGE_AP_CT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AP_CT_BLOCK_SIZE 512u

/* The identification strings of a bootable cartridge, at fixed offsets in
 * block 0. */
#define AP_CT_ID_OFFSET 0x10u
#define AP_CT_ID_SYSBOOT "SYSBOOT REV"
#define AP_CT_PROCESSOR_OFFSET 0x20u
#define AP_CT_PROCESSOR_M68K "M68K"

typedef struct {
  const uint8_t *data;
  size_t size;
  uint64_t blocks;
} ap_ct_t;

/* Block 0's header, returned verbatim. The words are deliberately numbered
 * rather than named; see the header. */
typedef struct {
  uint32_t word[4];
  bool bootable;  /* the SYSBOOT identification is present */
  bool m68k;      /* the processor identification is present */
} ap_ct_boot_t;

/* Attach to an image already in memory.
 *
 * Fails on a size that is not a whole number of blocks. That is the one
 * structural check the format admits, and it is worth making: a truncated or
 * mis-decompressed image would otherwise present a short final block full of
 * whatever followed it. */
[[nodiscard]] bool ap_ct_open(ap_ct_t *ct, const uint8_t *data, size_t size);

[[nodiscard]] uint64_t ap_ct_blocks(const ap_ct_t *ct);

/* Copy one block out. False for a block past the end -- never a short read, and
 * never a partial copy, so a caller cannot act on half a block. */
[[nodiscard]] bool ap_ct_read_block(const ap_ct_t *ct, uint64_t index,
                                    uint8_t *out);

/* Parse block 0 as a boot record. False if the image has no blocks at all; the
 * `bootable` and `m68k` flags report whether the identification is actually
 * there, so a non-bootable cartridge parses successfully and says so. */
[[nodiscard]] bool ap_ct_boot_record(const ap_ct_t *ct, ap_ct_boot_t *out);

/* The bootable image a cartridge carries, with its words named -- which the
 * confirmation in C24 licenses and nothing before it did. */
typedef struct {
  uint32_t load_address; /* word 0: where the image belongs in memory */
  uint32_t entry_point;  /* word 1: where execution begins */
  uint32_t length;       /* word 2 - word 0 */
  const uint8_t *data;   /* into the image; not copied */
} ap_ct_boot_image_t;

/* Locate the bootable image. False unless the cartridge announces itself as
 * `SYSBOOT` for `M68K`, the length is non-zero, and the image fits inside the
 * cartridge -- a header describing more than the file holds is corrupt, and
 * loading what there is of it would put a partial program in memory and jump
 * into it. */
[[nodiscard]] bool ap_ct_boot_image(const ap_ct_t *ct,
                                    ap_ct_boot_image_t *out);

#endif /* APOLLO_IMAGE_AP_CT_H */

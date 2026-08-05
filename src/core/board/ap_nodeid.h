/* Apollo node ID PROM.
 *
 * `008778-03` Table 2-8: "011200 - 0112FF  NETWORK ID PROM". Every Apollo node
 * carries a unique identifier in hardware; Domain/OS reports it as the node ID
 * and the ring uses it as an address.
 *
 * ## Layout, measured
 *
 * A dump of `011200` in the oracle:
 *
 *     011200: 00 00 01 00 23 00 45 00 00 00 00 00 00 00 00 00
 *     011210: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 69 00
 *     011220: (the first line again)
 *
 * **Stride 2**, sixteen positions over thirty-two bytes, aliased through the
 * 256-byte range.
 *
 * But not quite the serial ports' arrangement, although those are stride 2 too.
 * There the dump reads every value *twice* -- `07 07 0C 0C` -- because both
 * bytes of a word reach the register. Here the odd byte reads zero. Two devices
 * on the same board at the same stride, differing in what the odd byte does,
 * which is one more reason no placement here may be copied from a neighbour.
 *
 * Reading the even bytes as sixteen registers gives `00 01 23 45` in the first
 * four and `69` in the **last**, everything else zero. So the identifier is
 * `012345` held big-endian in registers 0-3, and register 15 is a **checksum**:
 * `0x01 + 0x23 + 0x45 = 0x69`, exactly. That arithmetic is what makes this a
 * reading rather than a guess -- three bytes and their sum, all four present.
 *
 * ## The boot PROM settles what the dump could not
 *
 * This said the checksum was in register *14*, which the dump above does not
 * show: `69` sits at `0112 1E`, and `0112 1E` is register 15. The prose was
 * wrong and the code followed the prose, which nothing noticed while nothing
 * read the register.
 *
 * CPU self-test 8 reads it, and its eleven instructions at `008218` say the
 * whole rule:
 *
 *     movea.l #$11200, a2
 *     clr.l   d1
 *     movea.l a2, a0
 *     add.b   (a0), d1          ; sum, stride 2 ...
 *     addq.l  #$2, a0
 *     cmpa.l  #$1121e, a0       ; ... over everything below 0112 1E
 *     bne.b   -8
 *     lea.l   $1e(a2), a0
 *     move.b  (a0), d0          ; the checksum byte itself
 *     cmp.b   d0, d1
 *
 * So the checksum covers **registers 0 through 14** and lives in register 15 --
 * which is the question this file recorded as unsettleable from a dump whose
 * other bytes are all zero. It is a plain sum and not a complement: the
 * firmware compares, it does not require the total to come out zero.
 *
 * With the byte one register early the sum included it and the compare found
 * nothing: `0x69 + 0x69 = 0xD2` against a zero, which is exactly what the
 * failure printed.
 *
 * ## The identifier comes from the caller
 *
 * Not from a constant here, and not from the oracle's default. `012345` is what
 * *that* machine happens to hold, and baking it in would make every emulated
 * node the same node -- which for a device whose entire purpose is to be unique
 * per machine is the one error that matters.
 *
 * Phase 3's plan item adds that the node ID may be "taken from the logical
 * volume label", so on a configured machine it comes from media. That is a
 * source above this module, and the module's job is only to present whatever it
 * is given in the layout the hardware uses.
 */

#ifndef APOLLO_BOARD_AP_NODEID_H
#define APOLLO_BOARD_AP_NODEID_H

#include <stdbool.h>
#include <stdint.h>

#define AP_NODEID_ADDR 0x011200u
#define AP_NODEID_RANGE 0x100u
#define AP_NODEID_REGISTERS 16u

/* Register **15**, the last of the sixteen -- byte `0112 1E`, which is where
 * the dump above shows `69` and where the boot PROM goes to find it. It was
 * written 14 here, following this file's own prose rather than its own dump,
 * and the two disagreed for as long as nothing read the register. */
#define AP_NODEID_CHECKSUM_REGISTER 15u

typedef struct {
  /* The identifier, in the low 24 bits. Registers 0-3 present it big-endian,
   * so the unused top byte reads zero as the dump shows. */
  uint32_t id;
} ap_nodeid_t;

/* `id` is the node's identifier; only the low 24 bits are presented. */
void ap_nodeid_init(ap_nodeid_t *prom, uint32_t id);

[[nodiscard]] bool ap_nodeid_decode(uint32_t address, unsigned *reg);
[[nodiscard]] uint8_t ap_nodeid_read(const ap_nodeid_t *prom, uint32_t address);

/* The checksum byte the PROM presents in register 14: the sum of the identifier
 * bytes, truncated to eight bits. */
[[nodiscard]] uint8_t ap_nodeid_checksum(const ap_nodeid_t *prom);

#endif /* APOLLO_BOARD_AP_NODEID_H */

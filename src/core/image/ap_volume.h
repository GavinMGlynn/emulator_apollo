/* The Domain physical volume label, and the node ID it carries.
 *
 * `board/ap_nodeid.h` models the node ID PROM and takes its identifier from a
 * caller, because "a device whose purpose is to be unique per machine must not
 * be identical on every one". This is the source that caller is supposed to
 * have: a Domain volume records the node of the machine that initialised it,
 * and a node booting that volume must present the same one or the file system's
 * own object identifiers refer to a machine that is not there.
 *
 * ## What is measured, and what is inferred from it
 *
 * `media/` is gitignored, so nothing here can be checked against a real volume
 * in CI and the reading has to be stated rather than assumed. Eleven `.awd`
 * images from one machine's SR10.4 install -- taken at different stages, from
 * `preos` through `invol-done` and `osclean` to the finished disk -- carry the
 * identical layout, so the *offsets* are stable across everything the install
 * rewrites. They are one machine's volume eleven times over, not eleven
 * independent observations, and the node they agree on is the one this project
 * configures its boards with.
 *
 * ## The UID, and why the node is in it
 *
 * At `+0x48` of block 0 is an eight-byte Apollo UID: `A45AA673 10012345` on
 * every image. Apollo's UID is a creation time and the node that created it, and
 * the split that fits is **36 bits of time, eight zero bits, twenty bits of
 * node**: `0xA45AA6731` then `0x00` then `0x12345`. The second UID in the same
 * block, at `+0x0C`, has a low word of zero throughout -- a nil node, which is
 * what the split predicts for a UID with no machine behind it and is the reason
 * to prefer it over reading the whole low word as an identifier.
 *
 * Twenty bits is also what the PROM holds: `board/ap_nodeid.h` carries the
 * identifier in registers 0-3 and this project's boards are built with
 * `0x012345`, which is exactly what these volumes report. That agreement is the
 * check available without a second machine's disk.
 *
 * ## The signature, and a framing this file used to get wrong
 *
 * There is no signature at the start: the image opens with a block count and two
 * UIDs. `FEDCA986` appears at absolute offset `0x418` on every image, and that
 * is what makes "this is a Domain volume" answerable at all.
 *
 * This file used to call that "block 1 `+0x18`", assuming 1024-byte blocks. The
 * oracle addresses an `.awd` in **1056-byte sectors** --
 * `omti8621.cpp`'s `fseek(diskaddr * OMTI_DISK_SECTOR_SIZE)` with
 * `OMTI_DISK_SECTOR_SIZE 1056` -- so `0x418` is not the start of anything, it
 * is 1048 bytes into the *first physical sector*. The offsets below were
 * measured and are unchanged; only the description of what they were offsets
 * *into* was wrong, and a wrong frame is what makes a later reader compute the
 * next structure's address incorrectly.
 *
 * What the last eight bytes of a 1056-byte sector are for is not settled here.
 * The reader uses absolute offsets and needs no answer; a caller walking the
 * volume's later structures does, and should establish it rather than assume a
 * 1024-byte stride.
 */

#ifndef APOLLO_IMAGE_AP_VOLUME_H
#define APOLLO_IMAGE_AP_VOLUME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How much of the image the label needs. Two 1024-byte units historically, and
 * kept at that size because it covers every measured offset with room to
 * spare -- not because the image is framed that way. */
#define AP_VOLUME_LABEL_BYTES 2048u

/* Absolute offset `0x418`. Named for what it is rather than what it might stand
 * for: no manual here explains the value, and it is a signature by behaviour --
 * present on every volume, absent everywhere else. */
#define AP_VOLUME_MAGIC 0xFEDCA986u
#define AP_VOLUME_MAGIC_OFFSET 0x418u

/* The label's fields, at the offsets the images agree on. */
#define AP_VOLUME_NAME_OFFSET 0x22u
#define AP_VOLUME_NAME_BYTES 30u
#define AP_VOLUME_CREATOR_UID_OFFSET 0x48u

/* The *mount history*, and it decides whether a volume can be booted at all.
 *
 * `002398-04`'s physical-volume-label diagram gives the structure by name and
 * label-relative offset: `+B0 .label_write_time`, `+B4 .last_mounted_node`,
 * `+B8 .node_boot_time`, `+BC .mounted_time`, `+C0 .dismounted_time`,
 * `+C4 .salvage_node`, `+C8 .salvage_time`.
 *
 * **The base is `0x440`, found by differencing rather than read off the
 * diagram.** An installed volume against an INVOL-only one differ at `0x4F4`
 * by `00 01 23 45`, which is the oracle's `DEFAULT_NODE_ID` sitting in
 * `.last_mounted_node` at `+B4` -- so the base is `0x4F4 - 0xB4`. That is a
 * measurement; the diagram is a scan whose hex OCRs badly, and `CLAUDE.md` says
 * so about exactly this kind of table.
 *
 * ## And it is the **logical** volume label, not the physical one
 *
 * This block said "physical-volume-label diagram" above, and the measurement
 * could not tell which label it had landed on. Three pages of `002398-04`
 * chapter 2 and four bytes of the image settle it:
 *
 *   - p. 2-9's layout: **block 00 of the physical volume is the physical volume
 *     label**, and **block 00 of the logical volume is the logical volume
 *     label**, one block further on.
 *   - p. 2-8: every block on the volume carries a **32-byte header** whose
 *     first field is the UID of the object the block belongs to.
 *   - p. 2-16: the canned UIDs -- `pv_label_$uid` is `00000200,0` and
 *     `lv_label_$uid` is `00000201,0`.
 *
 * So the label a block holds is readable from its own header, and
 * `media/dn3500-sr10.4-installed.awd` answers:
 *
 *     block 0 @ 0x0000  header UID 00 00 02 00 00 00 00 00   pv_label_$uid
 *     block 1 @ 0x0420  header UID 00 00 02 01 00 00 00 00   lv_label_$uid
 *
 * `0x440` is `0x420 + 0x20` -- block **1**, past its block header. The mount
 * history this file reads is therefore the **logical** volume label's, and the
 * physical volume label is the block *before* it, at `0x20`.
 *
 * The constant keeps its name: every offset built on it is right, nothing reads
 * the physical label, and renaming it would churn callers to fix a comment. What
 * was wrong was the description, and a reader who went looking for these fields
 * in the physical label would not have found them.
 *
 * Block 2's header UID on the same image is `a4 5a a7 cd 30 01 23 45`, which is
 * a real object's -- and its low bytes are `01 23 45`, the node ID, which is
 * p. 2-15's UID layout confirming itself: "N..N - Node ID" in the low bits of
 * the second longword.
 *
 * ## And p. 2-18 prints the label, which agrees offset for offset
 *
 * `002398-04` p. 2-18, "VOLUME LABEL -- **LOGICAL**", `lv_label_t` in
 * `vol.ins.pas`, laid out label-relative:
 *
 *     +00  version                    +B0  .label_write_time
 *     +04  logical volume name        +B4  .last_mounted_node
 *     +24  UID of the logical volume  +B8  .node_boot_time
 *     +2C  BAT header                 +BC  .mounted_time
 *     +4C  VTOC header                +C0  .dismounted_time
 *                                     +C4  .salvage_node
 *                                     +C8  .salvage_time
 *
 * **All seven of the offsets below are on that list, at those names.** The
 * differencing measurement and the diagram agree completely, which is what
 * turns `0x440` from a number that worked into a number that is understood --
 * and the `+2C` BAT header is p. 2-3's, whose own note says its offsets are
 * "from start of label", closing that loop too.
 *
 * **Two fields on the page this file does not read**, named rather than added
 * because nothing here needs them yet: `+CC` is `.salvage_mode` over
 * `.sys_shut_state` -- **the shutdown state**, which is the other half of the
 * boot refusal this block describes -- and `+D0`-`+D8` are the dump times and
 * the UID of the item being dumped. `.sys_shut_state` is the one to reach for
 * if the fourteen-day rule ever needs more than the timestamps.
 *
 * **Why they are worth modelling.** Domain/OS refuses to boot a volume whose
 * last shutdown is more than fourteen days behind the clock, and a volume that
 * was never cleanly dismounted carries `.dismounted_time` **zero** -- so the
 * difference is the whole of the clock at *every* clock and no power-on date can
 * satisfy it. Three clocks were tried against such a volume before its label
 * was read, which is three more than were needed. `FINDINGS.md` C132. */
#define AP_VOLUME_LABEL_BASE 0x440u
#define AP_VOLUME_LABEL_WRITE_TIME_OFFSET (AP_VOLUME_LABEL_BASE + 0xB0u)
#define AP_VOLUME_LAST_MOUNTED_NODE_OFFSET (AP_VOLUME_LABEL_BASE + 0xB4u)
#define AP_VOLUME_NODE_BOOT_TIME_OFFSET (AP_VOLUME_LABEL_BASE + 0xB8u)
#define AP_VOLUME_MOUNTED_TIME_OFFSET (AP_VOLUME_LABEL_BASE + 0xBCu)
#define AP_VOLUME_DISMOUNTED_TIME_OFFSET (AP_VOLUME_LABEL_BASE + 0xC0u)
#define AP_VOLUME_SALVAGE_NODE_OFFSET (AP_VOLUME_LABEL_BASE + 0xC4u)
#define AP_VOLUME_SALVAGE_TIME_OFFSET (AP_VOLUME_LABEL_BASE + 0xC8u)

/* A label time is the **high 32 bits of Apollo's 48-bit 4 microsecond clock**,
 * counted from 1980-01-01. So one tick is `4 us * 65536 = 262144 us`, about
 * 0.262 s, and a 32-bit field reaches 2016.
 *
 * **Calibrated against two independent statements by the machine itself**, not
 * assumed:
 *
 *   - `dn3500-sr10.3-installed.awd`'s `.mounted_time` is `FFF808EE`, which
 *     decodes to 2015-09-03 15:57, and its own `CALENDAR` said in as many words
 *     "last recorded time was 2015/09/03 15:47:46 UTC" -- ten minutes apart on a
 *     35-year span.
 *   - `dn3500-sr10.4-installed.awd`'s `.mounted_time` decodes to 2002-11-27,
 *     and `FINDINGS.md` C52 recorded that session's `CALENDAR` reading as
 *     `2002/11/27`.
 *
 * A first attempt read the tick as a plain quarter-second, which is wrong by
 * 4.9% and lands over a year out on both -- close enough to look right and not
 * to be. The 4 microsecond clock alone would overflow 32 bits in under five
 * hours, which is what makes the high-half reading the only one that fits. */
#define AP_VOLUME_TIME_TICK_MICROSECONDS 262144u
#define AP_VOLUME_TIME_EPOCH_YEAR 1980

/* Microseconds since 1980-01-01 for a label time. Microseconds and not seconds
 * because the tick is not a whole number of them, and a seconds-returning
 * helper would round every date it was asked for. */
[[nodiscard]] uint64_t ap_volume_time_microseconds(uint32_t ticks);

/* An Apollo UID: creation time above, node below. Kept whole as well as split,
 * because a caller comparing two volumes cares about the identity and a caller
 * configuring a machine cares only about the node. */
typedef struct {
  uint32_t high;
  uint32_t low;
} ap_uid_t;

typedef struct {
  /* Space-padded on the volume; trimmed here, and NUL-terminated. */
  char name[AP_VOLUME_NAME_BYTES + 1u];
  ap_uid_t creator;
  /* The low twenty bits of the creator UID. */
  uint32_t node_id;

  /* The mount history, raw. Kept as ticks rather than converted, because a
   * *zero* is the load-bearing case and a converted zero reads as a date. */
  uint32_t label_write_time;
  uint32_t last_mounted_node;
  uint32_t node_boot_time;
  uint32_t mounted_time;
  uint32_t dismounted_time;
  uint32_t salvage_node;
  uint32_t salvage_time;
} ap_volume_label_t;

/* Whether Domain/OS can boot this volume without salvaging it first.
 *
 * A volume that was never cleanly dismounted carries `.dismounted_time` zero,
 * and the kernel's "more than 14 days have elapsed since the last shutdown"
 * check measures from that -- so a zero fails at every possible clock. This
 * answers the question the frontend used to leave a reader to work out from a
 * boot that stopped. */
[[nodiscard]] bool ap_volume_cleanly_dismounted(const ap_volume_label_t *label);

/* The node ID a UID carries: its low twenty bits. */
[[nodiscard]] uint32_t ap_uid_node_id(ap_uid_t uid);

/* Parse the first two blocks of a Domain volume.
 *
 * False if there is not enough of it, or if the magic is absent -- which is a
 * refusal rather than a default, because a node ID invented from a file that is
 * not a Domain volume would configure a machine to lie about its identity, and
 * every object the file system then created would carry it. */
[[nodiscard]] bool ap_volume_read_label(const uint8_t *blocks, size_t bytes,
                                        ap_volume_label_t *out);

#endif /* APOLLO_IMAGE_AP_VOLUME_H */

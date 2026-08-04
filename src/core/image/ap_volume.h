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
} ap_volume_label_t;

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

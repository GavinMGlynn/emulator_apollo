/* Deterministic state hashing.
 *
 * A whole-machine state hash is how a boot collapses to one number, how two
 * runs are shown to be identical, and -- in Phase 8 -- how an optimised core is
 * proved to behave exactly like the reference core. All three uses have the
 * same hard requirement: the hash of a given machine state must be **the same
 * number on every platform, every build type and every compiler**. A hash that
 * is merely self-consistent on one machine is worthless as an identity harness.
 *
 * Three things follow from that, and each is enforced by construction here:
 *
 * 1. **A published algorithm, not an ad-hoc one.** This is FNV-1a 64-bit, whose
 *    constants and test vectors are external to this project. `ap_hash_bytes`
 *    is exactly FNV-1a, so `hash_suite` can check it against published vectors
 *    rather than against its own output -- the difference between verifying and
 *    merely reproducing.
 *
 * 2. **An explicit byte order.** Every multi-byte value is fed in
 *    little-endian regardless of host endianness. Hashing the raw object
 *    representation of a uint32_t would give a different answer on a big-endian
 *    host, which is exactly the silent cross-platform divergence the golden
 *    regression exists to catch.
 *
 * 3. **No host pointers, ever.** Pointer values are addresses in *our* process,
 *    not state of the emulated machine: they vary run to run under ASLR and
 *    differ by allocator and word size. There is deliberately no
 *    `ap_hash_ptr()`. Hash what a pointer points *at*, or the emulated address
 *    it stands for -- never the pointer itself.
 *
 * A fourth hazard is structural rather than numeric. Plain byte concatenation
 * cannot distinguish two uint16_t values from one uint32_t with the same bytes,
 * so a state layout change could silently preserve the hash. The typed helpers
 * therefore mix in a one-byte width tag before the value, making
 * `u16(0x0102), u16(0x0304)` and `u32(0x03040102)` hash differently. Use the
 * typed helpers for state; `ap_hash_bytes` is for opaque blobs such as RAM.
 */

#ifndef APOLLO_STATE_AP_HASH_H
#define APOLLO_STATE_AP_HASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "time/ap_time.h"

/* FNV-1a 64-bit parameters. External constants: see the FNV reference
 * (Fowler/Noll/Vo). Reproduced here so the values are visible at the point of
 * use rather than buried. */
#define AP_HASH_OFFSET_BASIS UINT64_C(14695981039346656037)
#define AP_HASH_PRIME UINT64_C(1099511628211)

/* Rolling hash state. A plain uint64_t in a struct so the type system stops a
 * caller passing a half-built hash where a finished one is wanted. */
typedef struct {
  uint64_t h;

  /* ## The dump is the hash walk, not a second one
   *
   * A full-state dump and the state hash must never disagree about *which*
   * fields are state. Written as two traversals they drift, and the failure is
   * silent in the worst direction: the dump shows two machines agreeing while
   * their hashes differ, because the field that differs is one the dump does
   * not visit. So this is not a separate walker -- it is the same one, with an
   * optional output attached, and every field absorbed is a field emitted.
   *
   * `out` is null for an ordinary hash and the cost is one branch per field.
   * `scope` names the subsystem being walked and `index` numbers the fields
   * within it, so a dump is readable and diffable before a single field has
   * been given a name of its own -- which makes naming them an improvement
   * rather than a prerequisite. */
  void *out;
  const char *scope;
  unsigned index;
  /* Inside a group: fields are hashed but not emitted, and one summary line is
   * written when it closes. */
  const char *group;
} ap_hash_t;

/* Width tags mixed in by the typed helpers, so re-typing a field changes the
 * hash. The values are arbitrary but fixed forever: changing one invalidates
 * every golden, so they are part of the on-disk format, not an implementation
 * detail. */
enum {
  AP_HASH_TAG_U8 = 0x01,
  AP_HASH_TAG_U16 = 0x02,
  AP_HASH_TAG_U32 = 0x04,
  AP_HASH_TAG_U64 = 0x08,
  AP_HASH_TAG_TIME = 0x10,
};

/* Begin a hash. */
[[nodiscard]] ap_hash_t ap_hash_begin(void);

/* Absorb `len` opaque bytes. This is plain FNV-1a with no width tag, so it
 * matches published test vectors exactly. Use it for RAM and other blobs whose
 * structure is already fixed; use the typed helpers for individual fields. */
void ap_hash_bytes(ap_hash_t *st, const void *data, size_t len);

/* Absorb a single field, width-tagged and little-endian. */
void ap_hash_u8(ap_hash_t *st, uint8_t v);
void ap_hash_u16(ap_hash_t *st, uint16_t v);
void ap_hash_u32(ap_hash_t *st, uint32_t v);
void ap_hash_u64(ap_hash_t *st, uint64_t v);

/* Absorb an instant or duration in AP_TIME_BASE_HZ units. Distinct from
 * ap_hash_u64 by tag: a time and a bare 64-bit register holding the same
 * number are different machine state and must hash differently. */
void ap_hash_time(ap_hash_t *st, ap_time_t t);

/* Attach a dump target. `out` is a `FILE *`; passing null returns the hash to
 * silent operation. Every field absorbed after this is written as
 * `scope.index = value` with its width tag, in traversal order. */
void ap_hash_dump_to(ap_hash_t *st, void *out);

/* Name the subsystem being walked, and restart the field numbering. Call it at
 * the top of each device's hash function; a null or empty name means the
 * fields are numbered without a prefix. */
void ap_hash_scope(ap_hash_t *st, const char *scope);

/* Collapse a run of fields to one dump line, without changing the hash.
 *
 * A large uniform array -- the ring controller's 64 KB buffer, the translation
 * map's 1024 entries -- is hashed field by field because that is what keeps the
 * hash portable: `ap_hash_bytes` over a `uint16_t[]` would absorb the host's
 * byte order and give different numbers on a big-endian machine. But dumping it
 * field by field buries everything else: an unfitted ring card accounted for
 * 94% of a dump's lines.
 *
 * So the *hash* keeps visiting every element and the *dump* emits one line
 * carrying the running hash after them all, which differs whenever any element
 * does. Locating which element is then a separate question, and one worth
 * asking only once the summary line has shown a difference.
 *
 * Nesting is not supported: a group inside a group would need a stack, and
 * nothing here has that shape. */
void ap_hash_group_begin(ap_hash_t *st, const char *name);
void ap_hash_group_end(ap_hash_t *st);

/* Whether a dump target is attached, so a caller can skip work that only a
 * dump needs. */
[[nodiscard]] bool ap_hash_dumping(const ap_hash_t *st);

/* Finish, yielding the state hash. Does not modify *st, so a caller may
 * checkpoint an in-progress hash and continue absorbing. */
[[nodiscard]] uint64_t ap_hash_end(const ap_hash_t *st);

#endif /* APOLLO_STATE_AP_HASH_H */

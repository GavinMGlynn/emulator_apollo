/* Oracle-compatibility quirks: deliberate divergences, selectable at run time.
 *
 * ## Why this exists
 *
 * A full-state differential against MAME will find differences of three kinds,
 * and only one of them is a defect here:
 *
 * 1. **This core is wrong.** Fix it, against the reference documentation, with
 *    the citation in the commit. No quirk is involved.
 * 2. **The oracle is wrong** -- it does something the manuals contradict. Then
 *    fixing "the difference" would mean breaking this core to match a defect,
 *    and *not* fixing it means every later field diverges too and the
 *    comparison stops being able to find anything.
 * 3. **Neither is wrong**: the manuals do not settle it. Recorded as open.
 *
 * Case 2 is what this module is for. Each such divergence becomes a **named
 * quirk**: this core implements the *reference* behaviour by default and the
 * *oracle's* behaviour when the quirk is selected, so a comparison run can be
 * carried past the difference instead of drowning in its consequences. The
 * default is always the documented behaviour -- a switch that made the oracle's
 * choice the default would quietly make this core a MAME clone.
 *
 * ## What a quirk must carry
 *
 * Adding one is a claim that the oracle contradicts a document, so each entry
 * states **what the reference says**, **what MAME does instead**, and **where
 * the difference shows** -- in `ap_quirk.c`, beside the name. A quirk without a
 * citation is an unexplained difference wearing a switch, which is worse than
 * an unexplained difference, because it looks settled.
 *
 * ## And they are machine state
 *
 * Two machines that compute different answers are different machines, so the
 * selected set is hashed like any other configuration. A run comparing against
 * the oracle and a run of the reference machine are not interchangeable and
 * their hashes say so.
 *
 * ## When *not* to add one, which has already come up once
 *
 * A quirk is for a divergence that must be lived with. When the oracle's defect
 * can be **fixed in the oracle** instead, that is better: both sides then run
 * the documented behaviour and there is no second path here to maintain or to
 * get wrong.
 *
 * The case that settled this: MAME's `duart_channel::write_CR` gated
 * enable-transmitter on an *edge* against the previous command register, which
 * the MC68681 datasheet §4.2.7 contradicts, and which hung the oracle's
 * normal-mode boot. Modelling it here would have meant storing the previous
 * command word -- a state field this core does not otherwise have, because a
 * command is acted on when it is written -- and then either hashing it, which
 * invalidates every golden for a field that exists only to support a bug, or
 * not hashing it, which breaks the premise that two machines behaving
 * differently hash differently. It was fixed in `ext/mame` instead
 * (`tools/mame-oracle/duart-tx-enable.patch`), and no quirk exists for it.
 *
 * The test is therefore: *can the oracle be corrected?* If yes, correct it. A
 * quirk is for what remains -- a divergence in code we cannot or should not
 * change, where the comparison would otherwise drown in its consequences.
 */

#ifndef APOLLO_MODEL_AP_QUIRK_H
#define APOLLO_MODEL_AP_QUIRK_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  /* The display controller answers its ID register only when the block's own
   * family is the fitted one -- a monochrome machine reads `FF` from the colour
   * block. MAME answers the 8-plane ID from the colour block whatever is
   * configured, so its firmware takes the colour path on a machine set to
   * monochrome. Measured 2026-08-11; see `ap_quirk.c` for the evidence. */
  AP_QUIRK_GRAPHICS_ID_ALWAYS_COLOUR = 0,

  AP_QUIRK_COUNT
} ap_quirk_t;

/* The selected set. A bitset rather than a list so the hash covers it in one
 * field and so asking is a mask test on a hot path. */
typedef struct {
  uint32_t bits;
} ap_quirks_t;

static_assert(AP_QUIRK_COUNT <= 32, "the quirk set is a uint32_t bitset");

/* None selected: the reference machine, which is the default everywhere. */
[[nodiscard]] static inline ap_quirks_t ap_quirks_none(void) {
  return (ap_quirks_t){.bits = 0u};
}

[[nodiscard]] static inline bool ap_quirk_selected(ap_quirks_t set,
                                                   ap_quirk_t quirk) {
  return (set.bits & (1u << (unsigned)quirk)) != 0u;
}

static inline void ap_quirk_select(ap_quirks_t *set, ap_quirk_t quirk) {
  set->bits |= (1u << (unsigned)quirk);
}

/* The command-line name, stable because it appears in run invocations that get
 * recorded beside results. */
[[nodiscard]] const char *ap_quirk_name(ap_quirk_t quirk);

/* One line: what the reference says and what the oracle does instead. */
[[nodiscard]] const char *ap_quirk_description(ap_quirk_t quirk);

/* Resolve a name. False for an unknown one, so a typo is refused rather than
 * silently running the reference machine and reporting an oracle comparison. */
[[nodiscard]] bool ap_quirk_by_name(const char *name, ap_quirk_t *out);

#endif /* APOLLO_MODEL_AP_QUIRK_H */

/* The five M68040 family members and how they differ.
 *
 * `MC68040 User's Manual (1993)` §1.1, with Table 1-4's notes 2, 6 and 8.
 *
 * §1 opens by naming five parts -- MC68040, MC68040V, MC68LC040, MC68EC040 and
 * MC68EC040V -- and then spends §1.1 on nothing but their differences, because
 * "unless otherwise noted, all references to M68040 ... will apply to" all
 * five. Every later section carries a scope note that only makes sense against
 * this table, so it is worth having as a table.
 *
 * ## §1.1 qualifies §5's reset straps out of existence on four parts
 *
 * §5 has three pins that are strapping options at reset: `CDIS` selects the
 * multiplexed bus, `MDIS` selects DLE mode, and `IPL2-IPL0` select output
 * buffer sizes. §1.1.1 and §1.1.2 then say, of the MC68040V, MC68LC040,
 * MC68EC040 and MC68EC040V alike, that they "do not implement the data latch
 * enable (DLE), multiplexed, or output buffer impedance selection modes of
 * operation. They implement only the small output buffer mode of operation."
 *
 * So all three straps are live on exactly one member of the family. A model
 * that took §5 alone would give four parts three configuration options they do
 * not have.
 *
 * ## And it settles which of §5's two summary tables to believe
 *
 * `[040]` §5 has `DLE` scoped three ways -- Table 5-1 "only available on the
 * MC68040", §5.11's heading "ONLY ON MC68040", Table 5-7 "not available on the
 * MC68LC040 and MC68EC040" -- and `MDIS` scoped three ways as well. §1.1 gives
 * the mechanism rather than an arbitration: **the pins are renamed**. "The DLE
 * pin name has been changed to JS0 on both the MC68040V and MC68LC040", and
 * "the DLE and MDIS pin names have been changed to JS0 and JS1, respectively"
 * on the EC parts. The pin is still physically there on all five; what is
 * MC68040-only is `DLE` *the function*. Table 5-1 is therefore literally
 * correct and Table 5-7's note is merely incomplete, and this is a better
 * answer than the one §5's walk reached on its own -- see `M68040_WALK.md`,
 * where that reasoning is kept beneath its correction.
 *
 * ## The EC parts have an ACU, not an MMU, and `PFLUSH`/`PTEST` are hazardous
 *
 * "The access control unit (ACU) replaces the MMU. The MC68EC040 and
 * MC68EC040V ACU has two data and two instruction registers that are called
 * data and instruction transparent translation registers in the MC68040." So
 * the TTRs survive under another name and nothing else of §3 does -- which is
 * what §3's and §4's opening scope notes have been saying.
 *
 * The sharp part is what those parts do with the MMU instructions. Not a trap,
 * and not a no-op: "PTEST and PFLUSH instructions cause an **undetermined
 * number of bus cycles**; the user should not execute these instructions."
 * Table 1-4's note 8 says only "not available", which is the softer statement of
 * the two; §1.1.2 is the one to model against.
 *
 * ## §1.1.2's last bullet is a copy-paste error
 *
 * Every bullet in §1.1.2 names "the MC68EC040 and MC68EC040V" except the last,
 * which reads "The MC68040V is a 3.3 volt static microprocessor that operates
 * down to 0 MHz" -- word for word the sentence that closes §1.1.1, about a part
 * §1.1.2 is not describing. Read on the page image at 600 dpi, so this is the
 * print. Whether the MC68EC040V is also 3.3 V and static cannot be answered
 * from §1, so it is recorded as unstated here and is a question for Appendix C,
 * which covers both V parts. The same goes for whether the MC68EC040V is pin
 * compatible with the MC68040: §1.1.2 says the MC68EC040 is and says nothing
 * about the other.
 */

#ifndef APOLLO_CPU_M68040_AP_M68040_FAMILY_H
#define APOLLO_CPU_M68040_AP_M68040_FAMILY_H

#include <stdbool.h>

typedef enum {
  AP_M68040_MC68040,
  AP_M68040_MC68040V,
  AP_M68040_MC68LC040,
  AP_M68040_MC68EC040,
  AP_M68040_MC68EC040V,
  AP_M68040_FAMILY_COUNT
} ap_m68040_family_part_t;

/* Three-valued because §1.1 leaves two properties of the MC68EC040V unsaid and
 * a model that guesses at them is inventing silicon. */
typedef enum {
  AP_M68040_FEATURE_ABSENT,
  AP_M68040_FEATURE_PRESENT,
  AP_M68040_FEATURE_UNSTATED
} ap_m68040_feature_t;

/* What `PFLUSH` and `PTEST` do on a part. */
typedef enum {
  AP_M68040_MMU_INSTRUCTIONS_WORK,
  /* "PTEST and PFLUSH instructions cause an undetermined number of bus cycles;
   * the user should not execute these instructions." Not a trap and not a
   * no-op -- undefined bus activity. */
  AP_M68040_MMU_INSTRUCTIONS_UNDETERMINED
} ap_m68040_mmu_instruction_t;

typedef struct {
  const char *name;
  bool has_fpu;
  /* False on the EC parts, where "the access control unit (ACU) replaces the
   * MMU" and keeps only the four transparent translation registers. */
  bool has_mmu;
  ap_m68040_mmu_instruction_t mmu_instructions;
  /* §1.1's three unimplemented modes, all three of which are §5 reset straps.
   * "They implement only the small output buffer mode of operation." */
  bool implements_dle_mode;
  bool implements_multiplexed_bus;
  bool implements_buffer_impedance_selection;
  /* `LPSTOP` and Table 5-6's PST encoding 6, "MC68040V and MC68EC040V only". */
  bool has_low_power_stop;
  ap_m68040_feature_t pin_compatible_with_mc68040;
  ap_m68040_feature_t three_volt_static;
  /* "The DLE pin name has been changed to JS0", and on the EC parts "the DLE
   * and MDIS pin names have been changed to JS0 and JS1, respectively." NULL
   * where the pin keeps its own name. */
  const char *dle_pin_name;
  const char *mdis_pin_name;
} ap_m68040_family_member_t;

[[nodiscard]] const ap_m68040_family_member_t *
ap_m68040_family(ap_m68040_family_part_t part);

/* Whether §5's three reset straps do anything on this part. True for the
 * MC68040 alone. */
[[nodiscard]] bool ap_m68040_has_reset_straps(ap_m68040_family_part_t part);

#endif /* APOLLO_CPU_M68040_AP_M68040_FAMILY_H */

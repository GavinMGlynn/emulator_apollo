/* MC68851 address translation descriptors.
 *
 * `MC68851 PMMU User's Manual, Third Edition` §5.1.5, Figures 5-10 and 5-12
 * through 5-20, all read from the page images.
 *
 * ## A descriptor does not know what it is
 *
 * This is the rule the whole module is built around, and it is easy to model
 * wrongly because every other MMU makes the descriptor self-describing:
 *
 *   "The exact interpretation of the bits in a descriptor is determined by
 *   three factors: the value of the DT field of the descriptor, the state of
 *   the table search, and the value of the DT field of the **previous**
 *   descriptor used in the search. The value of the previous descriptor
 *   determines whether the current descriptor is of the long or short format."
 *
 * So two independent things come from outside the descriptor:
 *
 *  - **Its width.** `DT = $2` in the *previous* descriptor means this one is
 *    four bytes, `$3` means eight. A descriptor read at the wrong width is not
 *    a descriptor with a wrong field -- it is a misaligned read of the table.
 *  - **Its type.** `DT = $2` means a *table* descriptor while table index
 *    fields remain, and an *indirect* descriptor once they are exhausted. Same
 *    bits, different meaning, decided by how far the search has got.
 *
 * Figure 5-10 gives the whole table, including two entries marked illegal --
 * an indirect descriptor pointing at another indirect descriptor -- and says
 * what becomes of them: "the table entries marked 'illegal' are not valid
 * configurations and are treated as the 'invalid' type by the MC68851." Treated
 * as invalid, not faulted separately, so a chain of indirections terminates
 * rather than looping.
 *
 * ## Type-1 and type-2 differ by one field
 *
 * "Note that the only difference in the long format of the type-1 and type-2
 * page descriptors is the presence of the LIMIT field and L/U bit in the long
 * format of the type-2 descriptor. The type-1 and type-2 short format
 * descriptors are identical."
 *
 * A type-2 arises when the search ends *early* -- a `DT = $1` found while table
 * index fields remain -- so there are still levels below it that a limit can
 * bound. A type-1 arises when the indices are exhausted and there is nothing
 * left to bound, which is why it has no limit to carry.
 */

#ifndef APOLLO_CPU_M68851_AP_M68851_DESCRIPTOR_H
#define APOLLO_CPU_M68851_AP_M68851_DESCRIPTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68851/ap_m68851_rp.h"

/* Figure 5-10's three columns: how far the table search has got. */
typedef enum {
  AP_M68851_SEARCH_TI_FIELDS_REMAIN,
  AP_M68851_SEARCH_TI_FIELDS_EXHAUSTED,
  AP_M68851_SEARCH_INDIRECT_DESCRIPTOR_SEEN,
} ap_m68851_search_state_t;

/* Figure 5-10's cells: what a descriptor actually is. */
typedef enum {
  AP_M68851_DESC_INVALID,
  AP_M68851_DESC_PAGE_TYPE_1,
  AP_M68851_DESC_PAGE_TYPE_2,
  AP_M68851_DESC_TABLE,
  AP_M68851_DESC_INDIRECT,
} ap_m68851_descriptor_kind_t;

/* Figure 5-10 in one call. The illegal cells return `INVALID`, which is what
 * the manual says the part does with them. */
[[nodiscard]] ap_m68851_descriptor_kind_t
ap_m68851_descriptor_kind(ap_m68851_descriptor_type_t dt,
                          ap_m68851_search_state_t state);

/* Whether the cell was one of Figure 5-10's two "illegal" entries. Reported
 * separately from the kind because the two are different facts -- the part
 * treats it as invalid, and the table still calls it a configuration error. */
[[nodiscard]] bool
ap_m68851_descriptor_is_illegal(ap_m68851_descriptor_type_t dt,
                                ap_m68851_search_state_t state);

/* The width of the *next* descriptor, decided by this one's DT: four bytes for
 * `$2` and eight for `$3`. Zero when this descriptor names no table. */
[[nodiscard]] unsigned
ap_m68851_descriptor_next_width(ap_m68851_descriptor_type_t dt);

/* The union of every field any descriptor format carries. Which members are
 * meaningful depends on the kind and the format, and the decoders below leave
 * the rest zeroed rather than filling them with whatever the bits held --
 * "the other bits are undefined and are available for use by the system
 * software", so reading them as fields would read software's private data. */
typedef struct {
  ap_m68851_descriptor_type_t dt;

  /* Access levels, long format only: three bits each. */
  unsigned read_access_level;  /* RAL, bits 47-45 */
  unsigned write_access_level; /* WAL, bits 44-42 */

  bool shared_globally; /* SG, bit 41, long format only */
  bool supervisor;      /* S, bit 40, long format only */
  bool gate;            /* G */
  bool cache_inhibit;   /* CI */
  bool lock;            /* L */
  bool modified;        /* M */
  bool used;            /* U */
  bool write_protect;   /* WP */

  /* Type-2 page descriptors and table descriptors in long format. */
  bool lower_limit;
  unsigned limit;

  /* The address this descriptor names. A table address is 16-byte aligned, a
   * page address 256-byte aligned and an indirect descriptor address 4-byte
   * aligned -- three different alignments, because each points at a different
   * kind of thing. */
  uint32_t address;
} ap_m68851_descriptor_t;

/* The six formats. Each takes exactly the width its figure draws, so a caller
 * that picked the width wrongly cannot silently get a plausible answer. */
[[nodiscard]] ap_m68851_descriptor_t
ap_m68851_short_table_descriptor(uint32_t value); /* Figure 5-12 */
[[nodiscard]] ap_m68851_descriptor_t
ap_m68851_long_table_descriptor(uint64_t value); /* Figure 5-13 */
[[nodiscard]] ap_m68851_descriptor_t
ap_m68851_short_page_descriptor(uint32_t value); /* Figure 5-14 */
/* Figures 5-15 and 5-16. One decoder, because the formats differ only in
 * whether the top word is a limit -- and `type_2` is what says it is. */
[[nodiscard]] ap_m68851_descriptor_t
ap_m68851_long_page_descriptor(uint64_t value, bool type_2);
[[nodiscard]] ap_m68851_descriptor_t
ap_m68851_short_indirect_descriptor(uint32_t value); /* Figure 5-17 */
[[nodiscard]] ap_m68851_descriptor_t
ap_m68851_long_indirect_descriptor(uint64_t value); /* Figure 5-18 */

/* The invalid formats, Figures 5-19 and 5-20, carry nothing but `DT`: "the
 * descriptor is of one of the 'invalid' types and the other bits are undefined
 * and are available for use by the system software." There is deliberately no
 * decoder for them -- there is nothing to decode, and providing one would
 * invite reading software's storage as hardware fields. */

#endif /* APOLLO_CPU_M68851_AP_M68851_DESCRIPTOR_H */

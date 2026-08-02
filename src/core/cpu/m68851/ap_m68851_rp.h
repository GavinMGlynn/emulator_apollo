/* MC68851 root pointer registers: CRP, SRP and DRP.
 *
 * `MC68851 PMMU User's Manual, Third Edition` §6.1.1 and Figure 6-1, read from
 * the page image.
 *
 * ## Three registers, one format, three address spaces
 *
 * "The three MC68851 root pointer registers, CRP, SRP, and DRP contain the
 * physical address of the root of the translation tree for user, supervisor,
 * and DMA accesses, respectively." A DMA root pointer is the part of this the
 * 68030 has no equivalent for: on a DN3000 the bus masters translate through
 * their own tree, and which register a cycle uses is decided by the function
 * code and by `TC`'s `SRE` bit.
 *
 * ## The limit is a bound in one of two directions
 *
 * "If L/U equals zero, the limit field contains the unsigned upper limit of
 * indices and all table indices must be less than or equal to the value ... If
 * L/U equals one, the limit field contains the unsigned lower limit of indices
 * and all table indices must be greater than or equal to the value."
 *
 * So one bit reverses the sense of the comparison, and the manual gives both
 * ways to switch the check off: "either setting L/U to zero and setting the
 * limit field to all ones ($7FFF) or by setting L/U to one and clearing the
 * limit field ($8000)". Both are named here, because a model that recognised
 * only the first would silently enforce a lower bound of zero -- which happens
 * to be harmless, and would still be the wrong reason for it.
 *
 * The check is also skippable from elsewhere: "if function code lookup is
 * enabled, the limit field and the L/U bit of a root pointer are ignored" --
 * with one exception, `DT = $1`, where the limit check runs "regardless of the
 * state of the FCL bit".
 *
 * ## `$0` is loadable but not by `PMOVE`
 *
 * "The MC68851 does not allow the operating system to load a root pointer with
 * an 'invalid' descriptor type with the PMOVE instruction. An 'invalid'
 * descriptor may be loaded by the PRESTORE instruction; however, the operation
 * of the MC68851 is undefined should this occur." Two instructions, two rules,
 * and the difference is not a detail -- `PRESTORE` reloads a saved state
 * wholesale and cannot afford to validate it.
 */

#ifndef APOLLO_CPU_M68851_AP_M68851_RP_H
#define APOLLO_CPU_M68851_AP_M68851_RP_H

#include <stdbool.h>
#include <stdint.h>

/* §6.1.1.4's four values. */
typedef enum {
  AP_M68851_DT_INVALID = 0,
  /* "A translation table for this root pointer does not exist and ... the
   * MC68851 should internally create an ATC entry (page descriptor)": a direct
   * mapping with a constant offset, so no table is walked at all. */
  AP_M68851_DT_PAGE_DESCRIPTOR = 1,
  AP_M68851_DT_VALID_4_BYTE = 2, /* short format descriptors */
  AP_M68851_DT_VALID_8_BYTE = 3, /* long format descriptors */
} ap_m68851_descriptor_type_t;

typedef struct {
  /* L/U, bit 63: which direction the limit bounds in. */
  bool lower_limit;
  /* LIMIT, bits 62-48. */
  unsigned limit;
  /* SG, bit 41: "the logical address space mapped by the root pointer is
   * shared globally by all tasks", so one ATC entry serves every task instead
   * of one per task alias. */
  bool shared_globally;
  ap_m68851_descriptor_type_t descriptor_type; /* DT, bits 33-32 */
  /* Table address, bits 31-4, held in place: a physical address whose low four
   * bits are not part of the field, so every translation table is on a 16-byte
   * boundary. */
  uint32_t table_address;
  /* Bits 3-0: "not used by the MC68851 and may be used by the operating system
   * for other purposes." Kept rather than dropped, because software may read
   * back what it stored there and the part must not eat it. */
  unsigned software_bits;
} ap_m68851_rp_t;

[[nodiscard]] ap_m68851_rp_t ap_m68851_rp_decode(uint64_t value);
[[nodiscard]] uint64_t ap_m68851_rp_encode(const ap_m68851_rp_t *rp);

/* How many bytes a table index is scaled by at this level: four for the short
 * descriptor format and eight for the long. Zero when the type names no table,
 * which is the caller's signal that there is nothing to walk. */
[[nodiscard]] unsigned ap_m68851_rp_descriptor_bytes(const ap_m68851_rp_t *rp);

/* Whether a table index passes the limit check. "All table indices must be less
 * than or equal to" an upper limit, "greater than or equal to" a lower one --
 * both inclusive. */
[[nodiscard]] bool ap_m68851_rp_index_within_limit(const ap_m68851_rp_t *rp,
                                                   unsigned index);

/* Whether this root pointer's limit check is switched off by its own value, by
 * either of the two documented means. Does not consider `FCL`, which suppresses
 * the check from outside the register. */
[[nodiscard]] bool ap_m68851_rp_limit_suppressed(const ap_m68851_rp_t *rp);

/* Whether `PMOVE` may load this value. "The MC68851 does not allow the
 * operating system to load a root pointer with an 'invalid' descriptor type
 * with the PMOVE instruction." `PRESTORE` may, and the manual calls the result
 * undefined rather than faulted. */
[[nodiscard]] bool ap_m68851_rp_loadable_by_pmove(const ap_m68851_rp_t *rp);

/* Whether the limit check runs for this root pointer given the `TC` `FCL` bit.
 * "If function code lookup is enabled ... the limit field and the L/U bit of a
 * root pointer are ignored", except that "if the DT field of a root pointer is
 * set to $1, the MC68851 performs a limit check regardless of the state of the
 * FCL bit". */
[[nodiscard]] bool ap_m68851_rp_limit_applies(const ap_m68851_rp_t *rp,
                                              bool function_code_lookup);

#endif /* APOLLO_CPU_M68851_AP_M68851_RP_H */

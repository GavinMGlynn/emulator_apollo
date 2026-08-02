/* MC68851 status and protection registers: PCSR, PSR, AC, CAL, VAL and SCC.
 *
 * `MC68851 PMMU User's Manual, Third Edition` §6.1.2 and §6.1.4 through §6.1.8,
 * Figures 6-2 and 6-4 through 6-7, read from the page images.
 *
 * ## These are the registers the 68020's module calls drive
 *
 * §6.1: "The MC68020 instructions CALLM and RTM can read and alter CAL and VAL
 * under control of the MC68851 access level protection mechanism." So the
 * "external hardware" that the 68020 manual defers to for verifying an access
 * level change is this part, and `src/core/cpu/m68020/ap_m68020_module.c` is
 * the other end of one mechanism split across two chips. `AC`'s `MDS` field
 * even fixes the alignment a module descriptor must satisfy -- a rule about
 * the 68020's data structure, enforced by the MMU.
 *
 * ## `CAL` and `VAL` are eight bits wide and three bits deep
 *
 * "The register is eight bits wide, but only the upper three bits are
 * implemented. Unimplemented bits always read as zeros and are ignored when
 * written." Three bits because `ALC` allows at most three address bits of
 * access level, so eight levels is the ceiling -- and the level sits in the
 * *upper* bits so it lines up with the top of a logical address rather than
 * needing a shift.
 *
 * ## `SCC` is indexed by level, not by a value
 *
 * "If the current access level is n and the MC68020 requests a call to a module
 * of privilege m where m < n (greater privilege), the MC68851 will instruct the
 * CPU to change stack pointers if **any bit of SCC between n and m (inclusive)
 * is set**."
 *
 * A range test over a bitmap, not a comparison: crossing any level that demands
 * a stack change forces one, even if neither endpoint does. Reading it as
 * "check the destination level's bit" would skip the change on exactly the
 * calls the operating system set the intermediate bits to catch.
 */

#ifndef APOLLO_CPU_M68851_AP_M68851_REGS_H
#define APOLLO_CPU_M68851_AP_M68851_REGS_H

#include <stdbool.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * PMMU cache status, §6.1.2 and Figure 6-2. Read-only.
 * ------------------------------------------------------------------------- */

typedef struct {
  /* F, bit 15: "when the MC68851 flushes entries from the ATC as the result of
   * a write to the CRP, bit [15] (F) of PCSR is set to indicate that entries
   * with the task alias shown in the TA field have been flushed." So it reports
   * a flush that already happened, and names whose entries went. */
  bool flush;
  /* LW, bit 14: "set when all entries in the ATC but one have been locked ...
   * no additional entries will be locked into the ATC until others are removed,
   * regardless of the state of L bits in translation descriptors." A warning
   * that is also an override -- the last entry cannot be locked away. */
  bool lock_warning;
  unsigned task_alias; /* TA, bits 2-0 */
} ap_m68851_pcsr_t;

[[nodiscard]] ap_m68851_pcsr_t ap_m68851_pcsr_decode(uint16_t value);
[[nodiscard]] uint16_t ap_m68851_pcsr_encode(const ap_m68851_pcsr_t *pcsr);

/* Bits 13-3 are drawn as zeros. */
#define AP_M68851_PCSR_IMPLEMENTED_MASK 0xC007u

/* ---------------------------------------------------------------------------
 * PMMU status, §6.1.8 and Figure 6-7.
 * ------------------------------------------------------------------------- */

typedef struct {
  bool bus_error;             /* B, bit 15 */
  bool limit_violation;       /* L, bit 14 */
  bool supervisor_only;       /* S, bit 13 */
  bool access_level_violation; /* A, bit 12 */
  bool write_protected;       /* W, bit 11 */
  bool invalid;               /* I, bit 10 */
  bool modified;              /* M, bit 9 */
  bool gate;                  /* G, bit 8 */
  bool globally_sharable;     /* C, bit 7 */
  /* N, bits 2-0: "number of levels". Three bits, so a table search of up to
   * seven levels is reportable -- more than the four `TC` can describe, because
   * the function code lookup and indirection add levels of their own. */
  unsigned levels;
} ap_m68851_psr_t;

[[nodiscard]] ap_m68851_psr_t ap_m68851_psr_decode(uint16_t value);
[[nodiscard]] uint16_t ap_m68851_psr_encode(const ap_m68851_psr_t *psr);

/* Bits 6-3 are drawn as zeros. */
#define AP_M68851_PSR_IMPLEMENTED_MASK 0xFF87u

/* ---------------------------------------------------------------------------
 * Access control, §6.1.7 and Figure 6-6.
 * ------------------------------------------------------------------------- */

/* §6.1.7.2's four encodings. Note that the value is the *number of address
 * bits*, so the number of levels is two raised to it -- and `$0` disables the
 * mechanism rather than meaning one level. */
typedef enum {
  AP_M68851_ALC_DISABLED = 0,    /* "Access Level Checking is Disabled" */
  AP_M68851_ALC_ONE_BIT = 1,     /* two access levels */
  AP_M68851_ALC_TWO_BITS = 2,    /* four */
  AP_M68851_ALC_THREE_BITS = 3,  /* eight, the maximum */
} ap_m68851_alc_t;

/* §6.1.7.3's four encodings. `$0` invalidates every module descriptor rather
 * than allowing any alignment, which is how the mechanism is switched off. */
typedef enum {
  AP_M68851_MDS_ALL_INVALID = 0,
  AP_M68851_MDS_16_BYTE = 1,
  AP_M68851_MDS_32_BYTE = 2,
  AP_M68851_MDS_64_BYTE = 3,
} ap_m68851_mds_t;

typedef struct {
  /* MC, bit 7: "when MC is set, module operations are enabled ... If MC is
   * clear, module operations are disabled ... all reads of the access status
   * ALCR return the illegal code ($0) causing all MC68020 CALLM and RTM
   * instructions to trap." */
  bool module_control;
  ap_m68851_alc_t access_level_control; /* ALC, bits 5-4 */
  ap_m68851_mds_t module_descriptor_size; /* MDS, bits 1-0 */
} ap_m68851_ac_t;

[[nodiscard]] ap_m68851_ac_t ap_m68851_ac_decode(uint16_t value);
[[nodiscard]] uint16_t ap_m68851_ac_encode(const ap_m68851_ac_t *ac);

/* Bits 15-8, 6, 3 and 2 are drawn as zeros. */
#define AP_M68851_AC_IMPLEMENTED_MASK 0x00B3u

/* How many access levels `ALC` selects: 0 when checking is disabled, else two
 * raised to the number of address bits. */
[[nodiscard]] unsigned ap_m68851_ac_access_levels(const ap_m68851_ac_t *ac);

/* The boundary a module descriptor must fall on, in bytes. Zero when `MDS` is
 * `$0`, where "all module descriptors are invalid" -- no alignment satisfies
 * it, which is not the same as any alignment doing. */
[[nodiscard]] uint32_t
ap_m68851_ac_module_descriptor_alignment(const ap_m68851_ac_t *ac);

/* Whether a module descriptor at this address is validly aligned. */
[[nodiscard]] bool
ap_m68851_ac_module_descriptor_aligned(const ap_m68851_ac_t *ac,
                                       uint32_t address);

/* ---------------------------------------------------------------------------
 * The protection registers, §6.1.4 through §6.1.6, Figures 6-4 and 6-5.
 * ------------------------------------------------------------------------- */

/* `CAL` and `VAL` hold a three-bit level in the upper bits of an eight-bit
 * register. These convert between the two, because the level is compared
 * against "a field of the high-order logical address" and so is stored where
 * that field lands rather than at the bottom of the register. */
[[nodiscard]] unsigned ap_m68851_access_level_decode(uint8_t value);
[[nodiscard]] uint8_t ap_m68851_access_level_encode(unsigned level);

/* §6.1.6's rule, which is a range test over a bitmap and not a comparison:
 * "if the current access level is n and the MC68020 requests a call to a module
 * of privilege m where m < n (greater privilege), the MC68851 will instruct the
 * CPU to change stack pointers if any bit of SCC between n and m (inclusive) is
 * set."
 *
 * `scc` is the register, `current` is n and `target` is m. A call that does not
 * increase privilege -- m >= n -- is outside the rule's premise and changes no
 * stack. */
[[nodiscard]] bool ap_m68851_scc_changes_stack(uint8_t scc, unsigned current,
                                               unsigned target);

#endif /* APOLLO_CPU_M68851_AP_M68851_REGS_H */

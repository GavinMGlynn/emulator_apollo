/* MC68030 translation table descriptors and the state a table search
 * accumulates as it walks them.
 *
 * `[030]` §9.5.1.1 pp. 9-20 ff., cited throughout.
 *
 * Two things live here, because they are the same idea seen from two ends:
 *
 *  1. What a descriptor *is*. The 2-bit DT field does not name a descriptor
 *     type on its own -- the same encoding means different things depending on
 *     whether it was found in a pointer table or in a page table. Resolving
 *     that is `ap_m68030_desc_role()`.
 *
 *  2. What a search *accumulates*. Protection on the 68030 is not a property of
 *     the final page descriptor: "The states of all WP bits encountered during
 *     a table search are logically ORed, and the result is copied to the ATC
 *     entry at the end of a table search." A page reached through a
 *     write-protected pointer is write-protected however permissive its own
 *     descriptor is. That is `ap_m68030_search_t`.
 *
 * Nothing here reads memory. The walk that fetches descriptors is a separate
 * item; this is the part that says what a fetched descriptor means, and it is
 * fully specified on paper, so it is built and tested first.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_DESC_H
#define APOLLO_CPU_M68030_AP_M68030_DESC_H

#include <stdbool.h>
#include <stdint.h>

/* `[030]` §9.5.1.1, the DT field. "The first two types apply to the descriptor
 * itself. The other two types apply to the descriptors in the table at the next
 * level of the tree." */
typedef enum {
  AP_M68030_DT_INVALID = 0,     /* "A table search ends when an invalid
                                 * descriptor is encountered." */
  AP_M68030_DT_PAGE = 1,        /* page, or early termination page higher up */
  AP_M68030_DT_VALID_4BYTE = 2, /* next table is short format; index x 4 */
  AP_M68030_DT_VALID_8BYTE = 3, /* next table is long format; index x 8 */
} ap_m68030_dt_t;

/* What a descriptor turns out to be, once its DT is read together with the
 * level it was found at. */
typedef enum {
  AP_M68030_ROLE_INVALID,
  AP_M68030_ROLE_PAGE,            /* DT=1 in a page table */
  AP_M68030_ROLE_EARLY_PAGE,      /* DT=1 above the page table */
  AP_M68030_ROLE_TABLE,           /* DT=2/3 above the page table */
  AP_M68030_ROLE_INDIRECT,        /* DT=2/3 *in* a page table */
} ap_m68030_desc_role_t;

/* Resolve a DT field against the level it was found at.
 *
 * `in_page_table` means the bottom level of the tree. The distinction is the
 * whole point: "When used in a page table (bottom level of a translation tree),
 * this code identifies an indirect descriptor" -- so DT=2 is a table descriptor
 * at one level and an indirect descriptor at another, and a walk that ignores
 * the level would follow an indirect descriptor as though it were a table. */
[[nodiscard]] ap_m68030_desc_role_t ap_m68030_desc_role(ap_m68030_dt_t dt,
                                                        bool in_page_table);

/* Size in bytes of a descriptor in the table this one points to, used to scale
 * the next index: "The MC68030 multiplies the index for the next table by
 * four" (DT=2) or "by eight" (DT=3). Zero when the DT does not point at a
 * table. */
[[nodiscard]] uint32_t ap_m68030_desc_next_table_stride(ap_m68030_dt_t dt);

/* True when a table search ends at this descriptor rather than continuing. */
[[nodiscard]] bool ap_m68030_desc_terminates(ap_m68030_desc_role_t role);

/* The limit check, `[030]` §9.5.1.1 LIU and LIMIT.
 *
 * "When the LIU bit is set, the LIMIT field contains the unsigned lower limit;
 * the index value for the next level of the tree must be greater than or equal
 * to the value in the LIMIT field. When the bit is cleared, the limit is an
 * unsigned upper limit, and the index value must be less than or equal to the
 * LIMIT." Returns true when the index is within bounds. */
[[nodiscard]] bool ap_m68030_desc_index_within_limit(uint16_t limit,
                                                     bool lower_limit,
                                                     uint32_t index);

/* Mask a 24-bit PAGE ADDRESS field for the configured page size.
 *
 * "When the page size is larger than 256 bytes, one or more of the least
 * significant bits of this field are not used. The number of unused bits is
 * equal to the PS field value in the TC register minus eight." */
[[nodiscard]] uint32_t ap_m68030_desc_page_address(uint32_t page_address_field,
                                                   uint8_t page_size_bits);

/* State accumulated across a table search. Reset once per translation, then
 * fed each descriptor encountered. */
typedef struct {
  bool write_protected; /* OR of every WP encountered */
  bool supervisor_only; /* OR of every S encountered */
  bool cache_inhibited; /* OR of every CI encountered */
  bool limit_violation; /* the B bit: an out-of-bounds index aborted the search */
  bool invalid;         /* an invalid descriptor ended the search */
} ap_m68030_search_t;

void ap_m68030_search_reset(ap_m68030_search_t *search);

/* Fold one descriptor's protection bits into the search.
 *
 * They accumulate rather than replace, because `[030]` says so for WP and says
 * the same thing in the other direction for S and CI: an access "is not
 * restricted to supervisor-only unless the access is restricted by some other
 * level of the translation tree". */
void ap_m68030_search_accumulate(ap_m68030_search_t *search, bool write_protect,
                                 bool supervisor, bool cache_inhibit);

/* Record that an index fell outside a descriptor's limit. "An out-of-bounds
 * access causes the B bit in the ATC entry for the address to be set and causes
 * the table search to abort." */
void ap_m68030_search_fail_limit(ap_m68030_search_t *search);

/* Record that the search ended on an invalid descriptor. */
void ap_m68030_search_fail_invalid(ap_m68030_search_t *search);

/* Whether a write is permitted given what the search accumulated. */
[[nodiscard]] bool ap_m68030_search_permits_write(const ap_m68030_search_t *s);

/* Whether an access at this privilege level is permitted. */
[[nodiscard]] bool ap_m68030_search_permits_access(const ap_m68030_search_t *s,
                                                   bool supervisor_access);

/* Whether the M bit should be set for this access.
 *
 * "The MC68030 sets the M bit in the corresponding page descriptor before a
 * write operation to a page for which the M bit is zero, **except after a
 * descriptor with the WP bit set is encountered, or after a supervisor
 * violation is encountered**. An access is considered to be a write for
 * updating purposes if either the R/W or RMC signal is low." */
[[nodiscard]] bool ap_m68030_search_should_set_modified(
    const ap_m68030_search_t *s, bool write, bool read_modify_write,
    bool supervisor_access, bool modified_already);

#endif /* APOLLO_CPU_M68030_AP_M68030_DESC_H */

/* MC68851 translation control register.
 *
 * `MC68851 PMMU User's Manual, Third Edition` §6.1.3 and Figure 6-3, read from
 * the page image.
 *
 * ## The register that decides the shape of the translation tree
 *
 * The 68030's on-chip MMU has a TC too, and it is not this one: the 68851 has
 * *four* table index fields where the 68030 has four of a different width, and
 * an `SRE` bit the 68030 spells differently. Sharing the type between them
 * would put a DN3000's tree shape and a DN3500's in one struct that is right
 * for neither.
 *
 * ## The consistency check is the interesting part
 *
 * "When written with the E bit (bit 31) set (translation enabled), a
 * consistency check is performed on the values of PS, IS, and TIx as follows.
 * The TIx fields are added together, and this sum is added to PS and IS. The
 * total must be 32, or an MMU configuration exception is signaled."
 *
 * That equation is the whole tree geometry in one line: the bits the initial
 * shift discards, plus the bits each level indexes with, plus the bits of page
 * offset, must account for all thirty-two of a logical address and no more.
 *
 * Two further rules make most bit patterns illegal:
 *
 *  - "Page size bit [3] must always be one. Writing values of zero to bit [3]
 *    of this field will cause an MMU configuration exception." So the smallest
 *    page is 256 bytes and `PS` is a logarithm, not an index.
 *  - "A zero value in a TIx field specifies that the lookup process is over
 *    when that field is encountered during a table search." A zero is a
 *    *terminator*, not a level with no index bits -- so `TIB = 0` with
 *    `TIC = 4` describes a tree whose C and D levels are unreachable.
 *
 * ## Writing it has side effects
 *
 * "Writing a value with its enable bit clear to this register cause a flush of
 * the entire ATC", and "if an exception is taken, the TC register is updated
 * with the data except that the E bit is cleared". So a failed write is not a
 * write that did not happen -- the register takes the new value and only the
 * enable is dropped, which is what lets software read back what it tried.
 */

#ifndef APOLLO_CPU_M68851_AP_M68851_TC_H
#define APOLLO_CPU_M68851_AP_M68851_TC_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  bool enable;    /* E, bit 31 */
  bool supervisor_root_pointer_enable; /* SRE, bit 25 */
  bool function_code_lookup;           /* FCL, bit 24 */
  unsigned page_size;   /* PS, bits 23-20: a log2, so $8 is 256 bytes */
  unsigned initial_shift; /* IS, bits 19-16 */
  unsigned table_index[4]; /* TIA, TIB, TIC, TID: bits 15-12 down to 3-0 */
} ap_m68851_tc_t;

/* Bits 30-26 are drawn as zeros: "all unimplemented fields of this register are
 * read as zeros and must always be written as zeros." So the implemented bits
 * are E at 31, SRE at 25, FCL at 24 and the whole lower half -- five bits of
 * hole between the enable and the two enables below it. */
#define AP_M68851_TC_IMPLEMENTED_MASK 0x83FFFFFFu

[[nodiscard]] ap_m68851_tc_t ap_m68851_tc_decode(uint32_t value);
[[nodiscard]] uint32_t ap_m68851_tc_encode(const ap_m68851_tc_t *tc);

/* Why a TC value was refused, or that it was accepted. All the refusals raise
 * one MMU configuration exception; they are named apart because the manual
 * names them apart and because they fail for unrelated reasons. */
typedef enum {
  AP_M68851_TC_OK,
  /* "Page size bit [3] must always be one." */
  AP_M68851_TC_PAGE_SIZE_TOO_SMALL,
  /* "The total must be 32." */
  AP_M68851_TC_INCONSISTENT,
} ap_m68851_tc_status_t;

/* Check a value as a write with `E` set would. A write with `E` clear is never
 * checked -- the manual conditions the whole check on the enable bit -- so this
 * is deliberately not called on every write. */
[[nodiscard]] ap_m68851_tc_status_t
ap_m68851_tc_check(const ap_m68851_tc_t *tc);

/* The sum the check tests: IS + TIA + TIB + TIC + TID + PS. Exposed because it
 * is the tree's geometry and not merely an intermediate -- it is what says a
 * logical address is fully accounted for. */
[[nodiscard]] unsigned ap_m68851_tc_bit_total(const ap_m68851_tc_t *tc);

/* Page size in bytes: `1 << PS`. Defined only for a `PS` the check accepts. */
[[nodiscard]] uint32_t ap_m68851_tc_page_bytes(const ap_m68851_tc_t *tc);

/* How many levels a table search visits before a `TIx` of zero ends it, not
 * counting the function code lookup. "A zero value in a TIx field specifies
 * that the lookup process is over when that field is encountered." */
[[nodiscard]] unsigned ap_m68851_tc_levels(const ap_m68851_tc_t *tc);

#endif /* APOLLO_CPU_M68851_AP_M68851_TC_H */

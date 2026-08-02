/* MC68040 table search.
 *
 * `MC68040 User's Manual (1993)` §3.2, Figures 3-8 and 3-9.
 *
 * ## Three levels, fixed
 *
 * Figure 3-8 splits a logical address into four fields and the widths are not
 * configurable:
 *
 *     RI   bits 31-25   7 bits   128 root-level descriptors
 *     PI   bits 24-18   7 bits   128 pointer-level descriptors
 *     PGI  bits 17-12   6 bits    64 page descriptors  (4-Kbyte pages)
 *          bits 17-13   5 bits    32 page descriptors  (8-Kbyte pages)
 *     offset  the rest
 *
 * So the 68851's four `TIx` fields, its initial shift and its per-level limits
 * are all absent. What the page size changes is where `PGI` ends, and nothing
 * else about the shape.
 *
 * ## The concatenation widths confirm the descriptor masks
 *
 * §3.2.1 states each step as an arithmetic identity, and each one adds to
 * thirty-two against the descriptor address widths already transcribed in
 * `ap_m68040_descriptor.c`:
 *
 *   - "The seven bits of a logical address PI field are multiplied by 4 ... and
 *     concatenated with the fetched root-level descriptor's **upper 23 bits**"
 *     -- 23 + (7 + 2) = 32, and the root descriptor's address field is bits
 *     31-9, which is 23 bits.
 *   - "For 8-Kbyte pages, the five bits of the PGI field are multiplied by 4
 *     ... concatenated with the fetched pointer-level descriptor's **upper 25
 *     bits**" -- 25 + (5 + 2) = 32, and the 8K pointer descriptor's address
 *     field is bits 31-7, which is 25 bits.
 *   - The 4-Kbyte case is 24 + (6 + 2) = 32 against bits 31-8.
 *
 * Two independent statements of the same geometry agreeing is worth more than
 * either alone, so the tests check the identity rather than the constants.
 *
 * ## What this module does not do
 *
 * It walks and decides; it does not touch the bus, does not write `U` and `M`
 * back into the tables, and does not fill an ATC. Descriptor fetches go through
 * a caller-supplied function, as the 68851's search does, so the same logic can
 * later be driven one bus cycle at a time.
 */

#ifndef APOLLO_CPU_M68040_AP_M68040_SEARCH_H
#define APOLLO_CPU_M68040_AP_M68040_SEARCH_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68040/ap_m68040_descriptor.h"

/* The three index fields, and the offset. */
[[nodiscard]] unsigned ap_m68040_root_index(uint32_t logical_address);
[[nodiscard]] unsigned ap_m68040_pointer_index(uint32_t logical_address);
[[nodiscard]] unsigned ap_m68040_page_index(uint32_t logical_address,
                                            ap_m68040_page_size_t page_size);
[[nodiscard]] uint32_t ap_m68040_page_offset(uint32_t logical_address,
                                             ap_m68040_page_size_t page_size);

typedef enum {
  AP_M68040_SEARCH_RESIDENT,
  /* A descriptor whose type field says invalid, at any level. "When an invalid
   * descriptor is encountered, an ATC entry is created for the logical address
   * with the resident bit in the MMUSR clear" -- so this is a normal outcome
   * that gets cached, not an error. */
  AP_M68040_SEARCH_INVALID,
  /* A transfer error acknowledge during a descriptor fetch. */
  AP_M68040_SEARCH_BUS_ERROR,
} ap_m68040_search_status_t;

typedef struct {
  ap_m68040_search_status_t status;
  uint32_t physical_address;
  /* Accumulated over every descriptor the search read: "setting the W-bit in a
   * table descriptor write protects all pages accessed with that descriptor",
   * so protection is a property of the path rather than of the leaf. */
  bool write_protect;
  /* Likewise for supervisor-only, which "identifies a pointer table or a page
   * as a supervisor-only table or page" -- a pointer table can carry it, so it
   * too accumulates. */
  bool supervisor;
  /* Copied from the terminating page descriptor. */
  ap_m68040_cache_mode_t cache_mode;
  bool modified;
  bool global;
  bool user_attribute_0;
  bool user_attribute_1;
  /* Whether an indirect descriptor was followed. */
  bool indirect;
  /* How many descriptors were fetched. */
  unsigned fetches;
} ap_m68040_search_result_t;

typedef bool (*ap_m68040_fetch_fn)(void *context, uint32_t address,
                                   uint32_t *value);

typedef struct {
  uint32_t root_pointer; /* URP or SRP, chosen by the caller's privilege mode */
  ap_m68040_page_size_t page_size;
  ap_m68040_fetch_fn fetch;
  void *fetch_context;
} ap_m68040_search_config_t;

[[nodiscard]] ap_m68040_search_result_t
ap_m68040_search(const ap_m68040_search_config_t *config,
                 uint32_t logical_address);

#endif /* APOLLO_CPU_M68040_AP_M68040_SEARCH_H */

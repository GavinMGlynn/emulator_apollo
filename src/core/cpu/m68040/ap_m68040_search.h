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
 * It walks and decides; it does not touch the bus and does not fill an ATC.
 * Descriptor fetches go through a caller-supplied function, as the 68851's
 * search does, so the same logic can later be driven one bus cycle at a time.
 *
 * ## It does write `U` and `M` back, and Table 3-1 is the whole rule
 *
 * §3.2.5: "During a table search, the U-bit in each encountered descriptor is
 * checked and set if not already set. Similarly, when the table search is for a
 * write access and the M-bit of the page descriptor is clear, the processor
 * sets the bit if the table search does not encounter a set W-bit or a
 * supervisor violation." And: "The U-bit and M-bit are updated **before** the
 * M68040 allows a page to be accessed or written."
 *
 * **The qualifier is attached to `M` alone.** `U` is set on every encountered
 * descriptor, write-protected or not -- Table 3-1 has a `WP` column and no
 * supervisor column, and every one of its twelve rows ends with `U` set. The
 * 68030's rule is different and its walk states the difference explicitly
 * ("except after a supervisor violation is detected"); this part's manual
 * carries no such exception for `U`, so none is modelled.
 *
 * **Three descriptors have no history bits and are skipped, for three separate
 * reasons.** An *invalid* descriptor at any level: `UDT`/`PDT` invalid leaves
 * the other thirty bits software-defined, so bit 3 is not a `U` bit to set. An
 * *indirect* page descriptor: Figure 3-12 gives it as `DESCRIPTOR ADDRESS` in
 * bits 31-2 and `PDT` in 1-0, so bit 3 is part of the pointer and writing it
 * would corrupt the chain. And the search does not reach past a bus error.
 *
 * **Locked or not is a real distinction and Table 3-1 states it row by row.**
 * "Locked RMW Access to Set U" appears against every row that sets `U` alone;
 * "Write to Set U and M" and "Write to Set M" are plain writes. So the rule is
 * that *an update is a locked read-modify-write exactly when it sets `U`
 * without setting `M`* -- which makes every table-descriptor update locked,
 * there being no `M` at those levels. §3.2.5 gives the consequence a bus model
 * will need: "Read-modify-write table search accesses ... are treated as
 * noncachable and force a matching cache line to be pushed and invalidated",
 * while the unlocked ones are "cachable/write-through but do not allocate in
 * the cache for misses". The flag is carried to the callback for that reason;
 * this core has no bus lock for it to assert yet, and says so rather than
 * dropping the distinction on the floor.
 *
 * **A NULL `update` is a search that must not disturb the tree**, which is what
 * an observer -- `--dump-logical`, `--dump-walk` -- needs and what the 68030's
 * walk already provides the same way.
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
  /* Copied from the terminating page descriptor. `modified` is its state
   * *after* any update this search made, which is what an ATC entry caches. */
  ap_m68040_cache_mode_t cache_mode;
  bool modified;
  bool global;
  bool user_attribute_0;
  bool user_attribute_1;
  /* Whether an indirect descriptor was followed. */
  bool indirect;
  /* How many descriptors were fetched. */
  unsigned fetches;
  /* Descriptor write cycles the search performed, and how many of those were
   * locked read-modify-writes. Both are zero when `update` is NULL. */
  unsigned updates;
  unsigned locked_updates;
} ap_m68040_search_result_t;

typedef bool (*ap_m68040_fetch_fn)(void *context, uint32_t address,
                                   uint32_t *value);

/* The write half of a history-bit update. `locked` is Table 3-1's "Locked RMW
 * Access" against its plain "Write", carried so a bus model can assert `LOCK`
 * and so the cache rule in §3.2.5 has something to key on. Returning false is a
 * transfer error, which ends the search exactly as a failed fetch does. */
typedef bool (*ap_m68040_update_fn)(void *context, uint32_t address,
                                    bool set_used, bool set_modified,
                                    bool locked);

typedef struct {
  uint32_t root_pointer; /* URP or SRP, chosen by the caller's privilege mode */
  ap_m68040_page_size_t page_size;
  /* Table 3-1's "Access Type", and the privilege the access carries. Both feed
   * the `M` rule alone: `write` is the row group, and `supervisor` decides
   * §3.2.5's "supervisor violation" against the page descriptor's `S`. */
  bool write;
  bool supervisor;
  ap_m68040_fetch_fn fetch;
  void *fetch_context;
  /* NULL for a search that must leave the tables exactly as it found them. */
  ap_m68040_update_fn update;
  void *update_context;
} ap_m68040_search_config_t;

[[nodiscard]] ap_m68040_search_result_t
ap_m68040_search(const ap_m68040_search_config_t *config,
                 uint32_t logical_address);

#endif /* APOLLO_CPU_M68040_AP_M68040_SEARCH_H */

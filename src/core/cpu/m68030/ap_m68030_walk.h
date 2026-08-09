/* MC68030 translation table search (the table walk).
 *
 * `[030]` §9.2 and §9.5, cited throughout. This is the module that joins the
 * other four: it uses `ap_m68030_tc` to split the logical address, walks the
 * tree fetching descriptors, applies `ap_m68030_desc`'s rules to each, and
 * produces what `ap_m68030_atc` will cache.
 *
 * ## Why it counts fetches
 *
 * The ATC costs nothing on a hit (`[030]` §9.4). *All* MMU time is here, in the
 * descriptor fetches a miss has to perform, and each of those is a real bus
 * cycle through `ap_m68030_bus`. So the walk reports `descriptor_fetches`: it
 * is the quantity a timing probe measures, and the reason a three-level tree
 * costs more than an early-terminating one.
 *
 * ## Descriptors arrive decoded, and that is deliberate
 *
 * The walk asks its caller for a descriptor rather than reading raw long words,
 * because the descriptor *status* bit positions did not survive the scan.
 * `[030]` Figures 9-10 and 9-11 do give the word-level structure -- a
 * short-format table descriptor is a 28-bit TABLE ADDRESS at 31-4 over a 4-bit
 * status field, and a long-format one is LIMIT at 31-16 with a 16-bit status
 * word, then TABLE ADDRESS at 31-4 -- but which status bit is U, WP, DT, LU, S,
 * CI or M is lost, exactly as it was for the transparent translation registers.
 *
 * The walk is implemented and tested in full against that decoded form, and it
 * stays the interface. `ap_m68030_descriptor_unpack_*` below turns real memory
 * into it, and carries its own derivation argument.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_WALK_H
#define APOLLO_CPU_M68030_AP_M68030_WALK_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_atc.h"
#include "cpu/m68030/ap_m68030_desc.h"
#include "cpu/m68030/ap_m68030_tc.h"

/* A descriptor as the walk needs it, already decoded from memory. */
typedef struct {
  ap_m68030_dt_t dt;
  uint32_t address_field; /* TABLE, PAGE, or DESCRIPTOR ADDRESS, as the DT says */
  uint16_t limit;         /* long format only */
  bool lower_limit;       /* LU: limit is a lower bound rather than an upper */
  bool has_limit;         /* short-format descriptors carry no limit field */
  bool write_protect;     /* WP */
  bool supervisor;        /* S */
  bool cache_inhibit;     /* CI */
  bool modified;          /* M */
  bool used;              /* U, as read: already set means no update is needed */
} ap_m68030_descriptor_t;

/* The access the search is being performed for. The history bits depend on the
 * access and not only on the tree: `[030]` §9.5.1.1 sets M only "when the table
 * search is for a write access", and counts the read half of a
 * read-modify-write as a write for that purpose. */
typedef struct {
  bool write;
  bool read_modify_write;
  bool supervisor;
} ap_m68030_search_access_t;

/* Decode a descriptor from memory.
 *
 * ## Derived, and deliberately labelled as such
 *
 * These positions are **not** transcribed from `[030]`, because that figure did
 * not survive. They are derived, and the derivation is recorded here so it can
 * be checked rather than trusted:
 *
 * 1. `MC68851 PMMU User's Manual 3ed` §5.1.5.3 states every position in prose,
 *    unambiguously: S is "bit [40] of long format table and page descriptors",
 *    CI "bit [38] ... bit [6] of short format", M "[36] ... [4]", U "[35] ...
 *    [3]", WP "[34] ... [2]", DT "bits [33-32] of all long format descriptors",
 *    TABLE ADDRESS "[31-4]", PAGE ADDRESS "[31-8]", INDIRECT "[31-2]".
 * 2. `[030]` §9.6 says "the MC68030 is program compatible with the
 *    MC68020/MC68851 combination" and lists what the 68030 MMU *lacks* --
 *    access levels, breakpoints, root pointer table, task aliases, lockable ATC
 *    entries, globally shared entries. Descriptor *format* is not among the
 *    differences, and could not be: a tree built for one would not translate on
 *    the other.
 * 3. Those missing features are exactly the 68851 bits the 68030 has no field
 *    for -- RAL(47-45), WAL(44-42), SG(41), G(39), Lock(37) -- leaving S, CI,
 *    M, U, WP and DT, which is precisely the 68030's set.
 * 4. `[030]` Figure 9-10 does survive in raw extraction as far as its
 *    boundaries: TABLE ADDRESS at 31-4 over a **4-bit** status field. The 68851
 *    says U and WP are on "page or table descriptors" while CI, M, G and Lock
 *    are page-descriptor-only, so a table descriptor's status is U, WP and DT
 *    -- exactly four bits.
 * 5. `[030]` Table 9-3 independently says MMUSR's S is set from "the S bit of a
 *    **long** format table descriptor or long format page descriptor", matching
 *    the 68851 placing S at bit 40, long format only. That is the 68030's own
 *    text confirming a 68851 position.
 *
 * Five agreeing sources is a derivation, not a guess -- but it is still a
 * derivation, so it is a named item in `docs/COMPLETION_PLAN.md` to confirm
 * against the oracle, which decodes real Domain/OS tables every boot.
 *
 * Bits 7 and 5 of a short-format page descriptor, and 47-41/39/37 of a long
 * one, are the 68851's gate, lock, access level and shared-globally fields. The
 * 68030 has no such features, so they are ignored on unpack rather than
 * rejected: "All fields marked 'unused' do not affect the operation of the
 * MC68851", and an operating system is free to use them. */
#define AP_M68030_DESC_DT_MASK 0x3u
#define AP_M68030_DESC_SHORT_WP_BIT 2u
#define AP_M68030_DESC_SHORT_U_BIT 3u
#define AP_M68030_DESC_SHORT_M_BIT 4u
#define AP_M68030_DESC_SHORT_CI_BIT 6u
/* Long-format status bits, numbered within the *upper* long word, so the
 * manual's bit 40 is bit 8 here. */
#define AP_M68030_DESC_LONG_WP_BIT 2u
#define AP_M68030_DESC_LONG_U_BIT 3u
#define AP_M68030_DESC_LONG_M_BIT 4u
#define AP_M68030_DESC_LONG_CI_BIT 6u
#define AP_M68030_DESC_LONG_S_BIT 8u
#define AP_M68030_DESC_LONG_LIMIT_SHIFT 16u
#define AP_M68030_DESC_LONG_LU_BIT 31u

/* `in_page_table` selects the role, which decides how much of the word is
 * address and how much is status -- a table descriptor keeps 31-4, a page
 * descriptor 31-8, an indirect descriptor 31-2. */
[[nodiscard]] ap_m68030_descriptor_t
ap_m68030_descriptor_unpack_short(uint32_t word, bool in_page_table);

/* The long format is two long words: LIMIT and status in the upper, the address
 * field in the lower. */
[[nodiscard]] ap_m68030_descriptor_t
ap_m68030_descriptor_unpack_long(uint32_t upper, uint32_t lower,
                                 bool in_page_table);

/* Fetch the descriptor at a physical address. Returns false for a bus error,
 * which `[030]` §9.4 says sets the B bit of the resulting ATC entry.
 * `long_format` says whether the table being indexed holds 8-byte descriptors,
 * which the *previous* descriptor's DT determined. */
typedef bool (*ap_m68030_fetch_fn)(void *context, uint32_t physical,
                                   bool long_format,
                                   ap_m68030_descriptor_t *out);

/* Set the history bits of the descriptor at a physical address.
 *
 * Expressed as "set U" / "set M" rather than as a long word, for the same
 * reason `ap_m68030_fetch_fn` returns a decoded descriptor: the status bit
 * positions did not survive the scan, so the *semantics* are modelled and the
 * packing is deferred.
 *
 * This is the write half of a read-modify-write. "Since the read-modify-write
 * (RMC) signal is asserted throughout the entire table search operation, the
 * read and write operations to update the history bits are guaranteed to be
 * uninterrupted." Returning false is a bus error, which sets B like any other. */
typedef bool (*ap_m68030_update_fn)(void *context, uint32_t physical,
                                    bool set_used, bool set_modified);

typedef struct {
  bool ok;               /* a translation was produced */
  uint32_t physical;     /* the translated address, when ok */
  ap_m68030_search_t search; /* accumulated WP/S/CI and the failure flags */
  unsigned descriptor_fetches; /* read bus cycles the search cost */
  unsigned history_writes;     /* U and M updates, each the write half of an RMC.
                                * `[030]` §11 p. 11-56 counts the table search in
                                * reads and writes separately, and says "an RMC
                                * cycle to set the U bit is counted as one read
                                * and one write" -- so this is measured time, not
                                * bookkeeping. */
  unsigned levels_walked;

  /* The physical address of the last descriptor the search fetched, which
   * `PTEST` returns in an address register when its A bit is set: "The physical
   * address of the last descriptor fetched can be returned in an address
   * register." A fault handler uses it to find the descriptor that refused the
   * access, so it is the address *fetched from*, not the address the descriptor
   * pointed at. Zero when nothing was fetched.
   *
   * **Successfully** fetched, which is not the same thing: "the physical address
   * of the last descriptor successfully fetched loads into the address register.
   * A successfully fetched descriptor occurs only if all portions of the
   * descriptor can be read by the MC68030 without abnormal termination of the
   * bus cycle." So a fetch that bus errors leaves this holding the *previous*
   * descriptor's address, and a handler that reads it is pointed at a descriptor
   * it can actually inspect rather than at the address that just refused it. */
  uint32_t last_descriptor_address;

  /* `max_levels` was reached before the tables terminated -- only `PTEST` can
   * produce this, and it is **not a fault**: the search was asked to stop, so
   * nothing about the mapping has been disproved. Kept apart from
   * `search.invalid` for exactly that reason; folding the two together makes a
   * truncated probe report a broken tree, which is what the 68851's sibling
   * `AP_M68851_SEARCH_TYPE_TRUNCATED` exists to avoid. */
  bool truncated;

  bool early_termination; /* ended on a page descriptor above the page table */
  bool used_indirect;
  bool page_modified;     /* M of the page descriptor as it now stands, which is
                           * what the ATC entry caches */
  bool bus_error;         /* a fetch or a history write actually failed, as
                           * distinct from the tree being invalid.
                           *
                           * The ATC's B bit folds the two together -- §9.4 sets
                           * it for a bus error *or* an invalid descriptor -- but
                           * `MMUSR` does not: Table 9-3 gives B and I separately,
                           * B for "a bus error ... encountered during the table
                           * search" and I for an invalid DT field. So the two
                           * causes are kept apart here and merged only where the
                           * hardware merges them. */
} ap_m68030_walk_result_t;

/* Where the walk starts: the root pointer's table address and the format of the
 * table it points at, which the root pointer's own DT gives. */
typedef struct {
  uint32_t table_address;
  bool long_format;
  uint16_t limit;
  bool lower_limit;
  bool has_limit;
} ap_m68030_root_t;

/* The upper long word of a root pointer register, which is the same layout as a
 * long-format table descriptor's: "The field descriptions in the preceding
 * section apply to corresponding fields of the CRP and SRP". Written here
 * rather than in the step so the packing sits beside the unpacking it inverts.
 *
 * The lower long word is simply the table address, so it needs no function. */
[[nodiscard]] uint32_t ap_m68030_root_pack_upper(const ap_m68030_root_t *root);

/* Perform a table search for `address`.
 *
 * Applies the limit check at each level, accumulates protection down the tree,
 * follows an indirect descriptor at the bottom, and honours an early
 * termination page descriptor by taking every remaining logical address bit as
 * page offset.
 *
 * Updates the U and M history bits through `update`, which may be NULL for a
 * search that must not disturb the tree -- which is what `PTEST` performs. */
[[nodiscard]] ap_m68030_walk_result_t
ap_m68030_walk(const ap_m68030_tc_t *tc, const ap_m68030_root_t *root,
               uint32_t address, const ap_m68030_search_access_t *access,
               ap_m68030_fetch_fn fetch, ap_m68030_update_fn update,
               void *context);

/* The same search, stopped after `max_levels` tables. Zero means no ceiling,
 * which is what an ordinary translation wants -- a translation stops when the
 * tables say so, not when a count runs out -- so `ap_m68030_walk` is this with
 * zero.
 *
 * The ceiling exists for `PTEST`, whose level operand is a search *depth* and
 * not merely a choice between the ATC and the tables: "The <level> operand
 * specifies the level of the search. Level 0 specifies searching the address
 * translation cache only. Levels 1-7 specify searching the translation tables
 * only. **The search ends at the specified level.**" And again, three paragraphs
 * on: "Execution of the instruction continues to the requested level or until
 * detecting one of the following conditions: Invalid Descriptor, Limit
 * Violation, Bus Error Assertion (Physical Bus Error)."
 *
 * `[PRM]` p. 6-63, the `PTEST` page, read as the page image. This was modelled
 * as "levels 1-7 all mean search the tables" and the depth ignored, so a
 * `PTEST` of level 1 reported what a full walk found -- a different `MMUSR`, a
 * different `N`, and an address register pointing at the wrong descriptor. The
 * 68851's own search took its `max_levels` from the first day; the two parts
 * were built from the same table and only one of them got this. */
[[nodiscard]] ap_m68030_walk_result_t ap_m68030_walk_to_level(
    const ap_m68030_tc_t *tc, const ap_m68030_root_t *root, uint32_t address,
    const ap_m68030_search_access_t *access, ap_m68030_fetch_fn fetch,
    ap_m68030_update_fn update, void *context, unsigned max_levels);

/* Install what a completed search produced into the ATC, and return the entry
 * index used. This is the join that makes the cost model whole: the search's
 * bus cycles are paid once, and every later access to the page is a hit that
 * `[030]` §9.4 says costs nothing.
 *
 * A *failed* search loads an entry too, rather than leaving the address
 * uncached: "If a limit violation is detected, the ATC is loaded with an entry
 * having the bus error (B) bit set." So a faulting address does not re-run the
 * table search on every access -- the fault itself is cached, which is a timing
 * claim as much as a correctness one.
 *
 * B covers more than a bus error. `[030]` §9.4 sets it for "a bus error ...
 * encountered during the table search", an invalid descriptor, a supervisor
 * violation, or a limit violation, so all four are folded in here. */
int ap_m68030_walk_fill_atc(ap_m68030_atc_t *atc,
                            const ap_m68030_walk_result_t *result,
                            const ap_m68030_search_access_t *access,
                            uint8_t function_code, uint32_t address,
                            uint8_t page_size_bits);

#endif /* APOLLO_CPU_M68030_AP_M68030_WALK_H */

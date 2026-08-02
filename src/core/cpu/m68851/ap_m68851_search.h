/* MC68851 table search.
 *
 * `MC68851 PMMU User's Manual, Third Edition` Figures 5-23 (the master
 * flowchart), 5-26 (the limit check) and the root pointer selection truth
 * table, all read from the page images.
 *
 * ## The flowchart is the specification
 *
 * Figure 5-23 is not an illustration of prose stated elsewhere -- it is the
 * only complete statement of the algorithm, and several of its rules appear
 * nowhere in the text. This module is a transcription of it, and the naming
 * follows the flowchart's own variables (`x`, `y`, `SIZE`, `LAST_SIZE`) so the
 * two can be read against each other.
 *
 * ## `LAST_SIZE` exists because the limit lives in the *previous* descriptor
 *
 * The flowchart carries `SIZE` and `LAST_SIZE` through the search and it is not
 * obvious why until Figure 5-26: the limit check is skipped outright when
 * `LAST_SIZE = 4`. A short-format descriptor has no limit field, so whether a
 * limit check happens at level B is decided by the *format of the descriptor
 * found at level A*. `LAST_SIZE` starts at 8 because a root pointer is always
 * 64 bits and always carries a limit.
 *
 * ## Where the search ends decides which kind of page descriptor it made
 *
 * Reaching a `DT = $1` at a level with more levels below it is an *early*
 * termination -- the flowchart advances `x` and asks whether the next `TIx` is
 * zero. If it is, the search was complete after all and the type is `NORMAL`;
 * if not, levels were skipped and the type is `EARLY`. That is exactly the
 * type-2 versus type-1 distinction, arrived at from the other end.
 *
 * ## An indirect descriptor may only name a page
 *
 * After following an indirection the flowchart accepts `DT = 'PAGE
 * DESCRIPTOR'` and makes everything else `INVALID` -- which is Figure 5-10's
 * two "illegal" cells seen from the algorithm's side, and is what stops a chain
 * of indirections from looping.
 *
 * ## What this module does not do
 *
 * It walks and it decides; it does not touch the bus. Descriptor fetches go
 * through a caller-supplied function so the search can be tested without a
 * memory system, and so the cycle-stepped core can drive it one bus cycle at a
 * time later without this logic changing.
 */

#ifndef APOLLO_CPU_M68851_AP_M68851_SEARCH_H
#define APOLLO_CPU_M68851_AP_M68851_SEARCH_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68851/ap_m68851_descriptor.h"
#include "cpu/m68851/ap_m68851_rp.h"
#include "cpu/m68851/ap_m68851_tc.h"

/* Which root pointer a search uses. */
typedef enum {
  AP_M68851_ROOT_CRP,
  AP_M68851_ROOT_SRP,
  AP_M68851_ROOT_DRP,
} ap_m68851_root_t;

/* The truth table beside Figure 5-23, in full:
 *
 *     FC3  FC2  SRE   ROOT        FC3  FC2  SRE   ROOT
 *      0    0    0    CRP          1    0    0    DRP
 *      0    0    1    CRP          1    0    1    DRP
 *      0    1    0    CRP          1    1    0    DRP
 *      0    1    1    SRP          1    1    1    DRP
 *
 * So `FC3` alone decides the DMA case -- a bus master other than the CPU always
 * translates through the DRP whatever its other function code bits say -- and
 * the supervisor root pointer is reached only when a supervisor access meets an
 * `SRE` that enables it. */
[[nodiscard]] ap_m68851_root_t ap_m68851_select_root(unsigned function_code,
                                                     bool sre);

/* The flowchart's `TYPE`, which is what the search produces. */
typedef enum {
  /* A page descriptor found with table index fields still to go: levels were
   * skipped, so this is the type-2 case. */
  AP_M68851_SEARCH_TYPE_EARLY,
  /* A page descriptor found exactly when the indices ran out. */
  AP_M68851_SEARCH_TYPE_NORMAL,
  /* Reached through an indirect descriptor. */
  AP_M68851_SEARCH_TYPE_INDIRECT,
  /* An invalid descriptor, a limit violation, or a bus error. The ATC entry is
   * still made -- with its `B` bit set -- so the denial is cached. */
  AP_M68851_SEARCH_TYPE_INVALID,
  /* `max_levels` was reached before the tables terminated. Only `PTEST` can
   * produce this, and it is not a fault: the search was *asked* to stop, so
   * nothing about the mapping has been disproved. Reporting it apart from
   * `INVALID` is what keeps a truncated `PTEST` from looking like one. */
  AP_M68851_SEARCH_TYPE_TRUNCATED,
} ap_m68851_search_type_t;

/* Why a search ended invalid. All of them make the same kind of ATC entry;
 * they are told apart because `PSR` reports them separately. */
typedef enum {
  AP_M68851_SEARCH_FAULT_NONE,
  AP_M68851_SEARCH_FAULT_INVALID_DESCRIPTOR,
  AP_M68851_SEARCH_FAULT_LIMIT_VIOLATION,
  AP_M68851_SEARCH_FAULT_BUS_ERROR,
} ap_m68851_search_fault_t;

/* Every descriptor a search read, in the order it read them.
 *
 * Recorded because §5.1.5.3.11 requires the `U` bit to be written back into
 * *each* of them, and a result alone cannot say where they were: "in a pointer
 * table, this bit is set to indicate that the pointer has been fetched by the
 * MC68851 as part of a table search."
 *
 * The path is kept even when the search ends invalid, and that is the manual's
 * rule rather than an accident: "note that a pointer may be fetched, and its U
 * bit set, for an address to which access is denied at another level of the
 * tree." Discarding it on a fault would leave the tables claiming a pointer was
 * never walked. */
typedef struct {
  /* Where the descriptor itself was read from. */
  uint32_t address;
  /* The descriptor status byte as it was read.
   *
   * It sits at `address + 3` in both formats, which is not a coincidence worth
   * hiding: `U` is bit 35 of a long descriptor and bit 3 of a short one, and
   * `M` is bit 36 and bit 4 -- and in each case that is bit 3 and bit 4 of the
   * fourth byte. One address arithmetic serves both, which is why the manual
   * can speak of a single "descriptor status byte". */
  uint8_t status;
  /* A page descriptor carries `M` as well as `U`; a pointer carries only `U`,
   * and §4.3.1 notes the consequence: "pointer table descriptors, which do not
   * contain modified bits, are not referenced using read-modify-write
   * sequences." */
  bool is_page;
} ap_m68851_visited_t;

/* A function code lookup, four table levels and an indirection is the deepest
 * a search can go. */
#define AP_M68851_SEARCH_MAX_PATH 8u

/* `U` and `M` within the status byte, in both descriptor formats. */
#define AP_M68851_STATUS_USED (1u << 3)
#define AP_M68851_STATUS_MODIFIED (1u << 4)

typedef struct {
  ap_m68851_search_type_t type;
  ap_m68851_search_fault_t fault;
  /* The page frame, meaningful unless the type is INVALID. */
  uint32_t physical_address;
  /* Accumulated down the tree rather than copied from one descriptor: §5.2.1.2
   * calls the ATC's copy "the effective write protection determined during the
   * translation table search", so a write protect at any level protects
   * everything below it. */
  bool write_protect;
  /* Copied from the terminating page descriptor. */
  bool cache_inhibit;
  bool modified;
  bool gate;
  bool lock;
  bool shared_globally;
  /* How many descriptors were fetched. `PSR`'s `N` field. */
  unsigned levels;
  /* The descriptors themselves, for the status write-back. `path_length` can
   * differ from `levels` only if a search is truncated by the path bound, which
   * the tree's own depth limit makes unreachable. */
  ap_m68851_visited_t path[AP_M68851_SEARCH_MAX_PATH];
  unsigned path_length;
} ap_m68851_search_result_t;

/* One byte write the part would perform to keep the tables consistent with the
 * ATC. §4.3.2.2: "the only write cycles initiated by the MC68851 are byte
 * operations to update the used bit, modified bit, or both". */
typedef struct {
  uint32_t address; /* the status byte, not the descriptor */
  uint8_t value;
  /* True when the byte must be read back and merged rather than simply
   * written. §4.3.1: the part "utilizes a read-modify-write sequence to update
   * the descriptor status byte whenever it is required to set the used bit but
   * not affect the state of the modified bit", so that two MMUs sharing a table
   * cannot lose each other's `M`. A pointer has no `M` to protect and is
   * written plainly. */
  bool read_modify_write;
} ap_m68851_status_write_t;

/* The write cycles a completed search implies, from §5.1.5.3.11's table:
 *
 *     Action                              U  M  R/W   U' M'
 *     RMW Cycle to Set U (M Not Changed)  0  0   R    1  X
 *     Write to Set U and M                0  0   W    1  1
 *     RMW to Set U                        0  1   R    1  1
 *     RMW to Set U                        0  1   W    1  1
 *     No Write                            1  0   R    1  0
 *     Write to Set M (U Written Set)      1  0   W    1  1
 *     No Write                            1  1   R    1  1
 *     No Write                            1  1   W    1  1
 *
 * The part "optimizes its activity by examining the U and M bits in descriptors
 * as they are fetched, and only performing write cycles to modify these bits
 * are required", so a descriptor already carrying the right bits produces no
 * entry at all -- which is why this returns a count rather than one write per
 * level.
 *
 * `write_access` is the access being translated. "A bus cycle executed by a
 * logical bus master is considered to be a write for updating purposes if
 * either R/W or RMC is low", so a read-modify-write counts as a write here even
 * though it reads first.
 *
 * Returns the number of writes, which is at most `AP_M68851_SEARCH_MAX_PATH`. */
[[nodiscard]] unsigned
ap_m68851_status_writes(const ap_m68851_search_result_t *result,
                        bool write_access, ap_m68851_status_write_t *out,
                        unsigned capacity);

/* How the search reads a descriptor. Returns false for a bus error, which ends
 * the search with `B` set in the entry it makes. `bytes` is 4 or 8. */
typedef bool (*ap_m68851_fetch_fn)(void *context, uint32_t address,
                                   unsigned bytes, uint64_t *value);

typedef struct {
  const ap_m68851_tc_t *tc;
  const ap_m68851_rp_t *root;
  /* True when the selected root pointer is the DRP, which suppresses the root
   * pointer's limit check on its own -- Figure 5-26's "FCL = 1 OR DRP IS RP". */
  bool root_is_drp;
  ap_m68851_fetch_fn fetch;
  void *fetch_context;
  /* A ceiling on descriptor fetches, for `PTEST`: "the PTEST instruction
   * continues searching the translation tables until the requested level is
   * reached or until a condition occurs that makes further searching
   * impossible". Zero means no ceiling, which is what an ordinary translation
   * wants -- a translation stops when the tables say so, not when a count runs
   * out. */
  unsigned max_levels;
} ap_m68851_search_config_t;

/* Walk the tree for one logical address. */
[[nodiscard]] ap_m68851_search_result_t
ap_m68851_search(const ap_m68851_search_config_t *config,
                 uint32_t logical_address, unsigned function_code);

/* The index a level takes from a logical address. The initial shift discards
 * the top bits, then each level consumes its `TIx` bits in turn -- so a level's
 * index depends on every level above it, which is why this takes the whole
 * table rather than one field. */
[[nodiscard]] unsigned ap_m68851_search_index(const ap_m68851_tc_t *tc,
                                              uint32_t logical_address,
                                              unsigned level);

#endif /* APOLLO_CPU_M68851_AP_M68851_SEARCH_H */

/* MC68030 address translation cache (ATC).
 *
 * `[030]` §9.4, pp. 9-17 ff., cited throughout.
 *
 * "The ATC is a 22-entry fully associative (content addressable) cache that
 * contains address translations in a form similar to the corresponding page
 * descriptors in memory."
 *
 * ## What this costs in emulated time, which is the reason it is modelled
 *
 * "The MC68030 is organized such that the translation time of the ATC is always
 * completely overlapped by other operations; **thus, no performance penalty is
 * associated with ATC searches.** The address translation occurs in parallel
 * with on-chip instruction and data cache accesses before an external bus cycle
 * begins."
 *
 * So an ATC hit must cost exactly zero clocks in our timing. All the time lives
 * in the *miss*: the table search's bus cycles. Modelling the ATC as though a
 * lookup cost something would put time into the machine that the hardware does
 * not spend.
 *
 * One consequence is easy to miss and is a real, measurable timing effect: a
 * write to a page that was previously only *read* costs a full table search
 * even though the translation is already cached. See `AP_M68030_ATC_MODIFY`.
 *
 * ## PROVISIONAL: the replacement algorithm
 *
 * The manual says what the policy is called and what it is built from, but not
 * what it does: "the ATC selects a valid entry to be replaced, using a pseudo
 * least recently used algorithm. The ATC uses a validity bit and an internal
 * history bit to implement this replacement algorithm." The exact rule is not
 * published, so it is **not invented** here to a false precision -- see
 * `docs/PROJECT_STATUS.md`'s PROVISIONAL table. What is documented and is
 * implemented exactly: invalid entries are reused first ("If possible, when the
 * ATC stores a new address translation, it replaces an entry that is no longer
 * valid"), and the policy uses one history bit per entry.
 *
 * This matters for timing rather than for correctness -- which entry is evicted
 * changes later hit and miss rates, not the translation any hit produces.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_ATC_H
#define APOLLO_CPU_M68030_AP_M68030_ATC_H

#include <stdbool.h>
#include <stdint.h>

/* "a 22-entry fully associative ... cache" */
#define AP_M68030_ATC_ENTRIES 22

/* The logical and physical address fields hold A31-A8, so both are shifted by
 * eight. Public because a table search has to scale the physical address it
 * produced into this field when it fills an entry. */
#define AP_M68030_ATC_ADDRESS_SHIFT 8u

/* One entry. The tag portion is 28 bits (V, 3-bit FC, 24-bit logical address)
 * and the data portion 28 bits (B, CI, WP, M, 24-bit physical address); both
 * are held here as decoded fields. Physical and logical addresses are the
 * A31-A8 bits, so a 256-byte page is the finest granularity either describes. */
typedef struct {
  bool valid;              /* V */
  uint8_t function_code;   /* FC, 3 bits */
  uint32_t logical;        /* 24-bit tag: logical A31-A8 */

  bool bus_error;     /* B: bus error, invalid descriptor, supervisor or limit
                       * violation during the table search for this entry */
  bool cache_inhibit; /* CI */
  bool write_protect; /* WP */
  bool modified;      /* M */
  uint32_t physical;  /* 24-bit: physical A31-A8 */

  bool history; /* the "internal history bit" of the replacement algorithm */
} ap_m68030_atc_entry_t;

typedef struct {
  ap_m68030_atc_entry_t entry[AP_M68030_ATC_ENTRIES];
} ap_m68030_atc_t;

typedef enum {
  AP_M68030_ATC_MISS,   /* no entry: run a table search, then retry */
  AP_M68030_ATC_HIT,    /* translated, at no time cost */
  AP_M68030_ATC_FAULT,  /* B set, or WP set on a write: bus error exception */
  AP_M68030_ATC_MODIFY, /* hit, but a write to an entry whose M is clear */
} ap_m68030_atc_status_t;

typedef struct {
  ap_m68030_atc_status_t status;
  uint32_t physical;   /* complete physical address, page offset merged in */
  bool cache_inhibit;  /* drives CIOUT */
  int index;           /* which entry answered, or -1 */
} ap_m68030_atc_result_t;

/* Invalidate every entry. "A flush operation clears the bit." */
void ap_m68030_atc_flush(ap_m68030_atc_t *atc);

/* Invalidate the entry matching this logical address and function code, as
 * PFLUSH does. */
void ap_m68030_atc_flush_entry(ap_m68030_atc_t *atc, uint8_t function_code,
                               uint32_t address, uint8_t page_size_bits);

/* Look an access up.
 *
 * `[030]` on the ordering this implements: B "causes the MC68030 to take a bus
 * error exception" on any access; WP set means "a write access or a
 * read-modify-write access ... causes a bus error exception to be taken
 * immediately"; and M clear on a write means the processor "aborts the access
 * and initiates a table search".
 *
 * "All page index bits of the logical address are transferred to the bus
 * controller without translation", so the returned physical address is the
 * entry's page frame with the logical page offset merged back in. */
[[nodiscard]] ap_m68030_atc_result_t
ap_m68030_atc_lookup(const ap_m68030_atc_t *atc, uint8_t function_code,
                     uint32_t address, uint8_t page_size_bits, bool write,
                     bool read_modify_write);

/* Install a translation, choosing a victim. Returns the entry index used.
 *
 * See the PROVISIONAL note above: invalid entries are reused first, which is
 * documented; the choice among valid entries is not. */
int ap_m68030_atc_insert(ap_m68030_atc_t *atc, uint8_t function_code,
                         uint32_t address, uint8_t page_size_bits,
                         uint32_t physical_page, bool write_protect,
                         bool cache_inhibit, bool modified, bool bus_error);

#endif /* APOLLO_CPU_M68030_AP_M68030_ATC_H */

/* MC68030 translation control register (TC) and logical address decomposition.
 *
 * `[030]` §9.7.2, pp. 9-54 ff.
 *
 * TC is what turns a logical address into a path through the translation tree:
 * it says how many high-order bits to ignore, how many bits index each of the
 * four possible table levels, and how many are left over as the page offset.
 * Everything the table walk does is driven from here, so it is built before the
 * walk itself.
 *
 * ## The bit layout, and why it is trusted here when TT's was not
 *
 * `ap_m68030_tt.h` refuses to pack its register because Figure 9-37's lower
 * half did not survive the scan. Figure 9-36 is a different case, and the
 * difference is worth stating rather than leaving as an inconsistency:
 *
 *  - The prose pins the one bit everything else hangs off: "When written with
 *    the E bit (**bit 31**) set...". That is text, not figure.
 *  - The figure's *column markers* survived -- 31, 25, 24, 20, 16 on the first
 *    row and 15, 12 on the second -- and those are the field boundaries.
 *  - The field order survived as text: `E | ... | SRE | FCL | PS | IS`, then
 *    `TIA TIB TIC TID`.
 *  - PS and IS are stated in prose to be 4-bit fields, and exactly eight bits
 *    lie between the 24 and 16 markers, so they can only be 23-20 and 19-16.
 *  - TIA-TID are each stated to be 4-bit, TIA is "the index into the highest
 *    level table" and TID the lowest, and the 15/12 markers put TIA at 15-12.
 *    Four 4-bit fields then fill 15-0 in order.
 *
 * So no bit position here is inferred from a plausible-looking layout: each is
 * pinned by surviving text or a surviving column marker. Where that was not
 * true -- TT -- the packing was left undone.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_TC_H
#define APOLLO_CPU_M68030_AP_M68030_TC_H

#include <stdbool.h>
#include <stdint.h>

/* The four table levels TC can describe, not counting the optional level
 * indexed by function code. */
#define AP_M68030_TC_LEVELS 4

typedef struct {
  bool enable;              /* E, bit 31 */
  bool supervisor_root;     /* SRE, bit 25 */
  bool function_code_lookup;/* FCL, bit 24 */
  uint8_t page_size_bits;   /* PS, bits 23-20: page offset width in bits */
  uint8_t initial_shift;    /* IS, bits 19-16: high-order bits ignored */
  uint8_t table_index[AP_M68030_TC_LEVELS]; /* TIA..TID, bits 15-12 .. 3-0 */
} ap_m68030_tc_t;

/* Decode a 32-bit TC value into fields. Always succeeds; validity is a separate
 * question, answered by ap_m68030_tc_is_consistent(). */
[[nodiscard]] ap_m68030_tc_t ap_m68030_tc_decode(uint32_t value);

/* Page size in bytes, or 0 when the PS field is one of the reserved encodings.
 *
 * "1000 - 256 bytes ... 1111 - 32K bytes. All other bit combinations are
 * reserved by Motorola for future use; an attempt to load other values into
 * this field of the TC register causes an MMU configuration exception." So the
 * encoding is simply the page offset width, and anything below 8 is reserved. */
[[nodiscard]] uint32_t ap_m68030_tc_page_size(const ap_m68030_tc_t *tc);

/* The inverse of the decode, for `PMOVE MRn,<ea>` reading the register back.
 * "All unimplemented fields of this register are read as zeros", so the encode
 * emits only the fields the decode names -- a round trip through this pair
 * therefore *normalises* a value rather than preserving it, which is what the
 * hardware does too. */
[[nodiscard]] uint32_t ap_m68030_tc_encode(const ap_m68030_tc_t *tc);

/* The consistency check `[030]` performs when TC is written with E set:
 *
 *   "The TIx fields are added together until a zero field is reached, and this
 *    sum is added to PS and IS. The total must be 32, or an MMU configuration
 *    exception is taken."
 *
 * A reserved page size also fails. Returns the total in `*total` when non-NULL,
 * so a caller (or a test) can report what the sum actually came to. */
[[nodiscard]] bool ap_m68030_tc_is_consistent(const ap_m68030_tc_t *tc,
                                              uint32_t *total);

/* How a logical address splits up under this TC. `levels` is how many table
 * levels the search uses -- the walk stops at the first zero TIx field. */
typedef struct {
  uint32_t index[AP_M68030_TC_LEVELS];
  uint8_t levels;
  uint32_t page_offset;
} ap_m68030_tc_split_t;

/* Split a logical address into its table indices and page offset.
 *
 * The high-order `initial_shift` bits are dropped first -- they "are ignored
 * during table search operations" -- then each level takes its TIx bits from
 * the top down, and what remains is the page offset. */
[[nodiscard]] ap_m68030_tc_split_t
ap_m68030_tc_split(const ap_m68030_tc_t *tc, uint32_t address);

#endif /* APOLLO_CPU_M68030_AP_M68030_TC_H */

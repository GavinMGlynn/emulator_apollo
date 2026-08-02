/* MC68040 address translation descriptors.
 *
 * `MC68040 User's Manual (1993)` §3.2.2, Figures 3-11 and 3-12, read from the
 * page images.
 *
 * ## A different MMU, not a wider one
 *
 * The 68851 and the on-chip 68030 MMU share a lineage; the 68040's does not.
 * Three differences shape this module:
 *
 *  - **Every descriptor is 32 bits.** There is no long and short format, so
 *    nothing in a search depends on the *previous* descriptor's width -- the
 *    single fact that most complicated the 68851's table walk.
 *  - **The tree is fixed at three levels** -- root, pointer, page -- rather
 *    than configurable through four table index fields. What varies is the page
 *    size, and it varies the *address field width* instead of the format.
 *  - **The type fields have don't-care bits, asymmetrically.** `UDT` treats
 *    `00` and `01` alike (invalid) and `10` and `11` alike (resident), so its
 *    low bit is free. `PDT` treats `01` and `11` alike (resident) -- its
 *    *high* bit is free there -- but `00` and `10` are invalid and indirect,
 *    which are entirely different. A decoder that masked the same bit in both
 *    fields would turn every indirect descriptor into an invalid one.
 *
 * ## Page size changes the field widths, not the layout
 *
 * A pointer table descriptor's address field is bits 31-8 at 4K and 31-7 at 8K,
 * because an 8K page table is half as long and needs one less bit of index.
 * The page descriptor goes the other way: its address is 31-12 at 4K and 31-13
 * at 8K, and the bit freed by the narrower address becomes a *second* `UR` bit
 * rather than being reserved. "This 20-bit field contains the physical base
 * address of a page in memory ... When the page size is 8-Kbyte, the least
 * significant bit of this field is not used."
 */

#ifndef APOLLO_CPU_M68040_AP_M68040_DESCRIPTOR_H
#define APOLLO_CPU_M68040_AP_M68040_DESCRIPTOR_H

#include <stdbool.h>
#include <stdint.h>

/* The two page sizes the part supports. Not a free parameter like the 68851's
 * eight: the 68040 offers these and no others. */
typedef enum {
  AP_M68040_PAGE_4K,
  AP_M68040_PAGE_8K,
} ap_m68040_page_size_t;

/* §3.2.2.3's `UDT`: "00 or 01 = Invalid ... 10 or 11 = Resident". */
typedef enum {
  AP_M68040_UDT_INVALID,
  AP_M68040_UDT_RESIDENT,
} ap_m68040_udt_t;

/* §3.2.2.3's `PDT`. Unlike `UDT` this has three meanings in four encodings. */
typedef enum {
  AP_M68040_PDT_INVALID,  /* 00 */
  AP_M68040_PDT_RESIDENT, /* 01 or 11 */
  AP_M68040_PDT_INDIRECT, /* 10 */
} ap_m68040_pdt_t;

/* §3.2.2.3's `CM`, which is four cache policies rather than one inhibit bit --
 * the 68851 and 68030 have only "cache inhibit". Write-through and copyback are
 * both cachable and differ in when a store reaches memory, which is a coherency
 * property the earlier parts had no way to express. */
typedef enum {
  AP_M68040_CM_CACHABLE_WRITE_THROUGH = 0,
  AP_M68040_CM_CACHABLE_COPYBACK = 1,
  AP_M68040_CM_NONCACHABLE_SERIALIZED = 2,
  AP_M68040_CM_NONCACHABLE = 3,
} ap_m68040_cache_mode_t;

/* A table descriptor: the root level and both pointer-level forms. */
typedef struct {
  ap_m68040_udt_t type;
  bool write_protect; /* W, bit 2 */
  bool used;          /* U, bit 3 */
  uint32_t table_address;
} ap_m68040_table_descriptor_t;

/* A root table is 512 bytes -- 128 descriptors of four bytes, since the root
 * index is seven bits -- so the root pointer is 512-byte aligned. */
#define AP_M68040_ROOT_TABLE_MASK 0xFFFFFE00u

/* The root table descriptor's address is bits 31-9: a pointer table is 512
 * bytes, so it is 512-byte aligned. */
[[nodiscard]] ap_m68040_table_descriptor_t
ap_m68040_root_descriptor(uint32_t value);

/* A pointer table descriptor's address field width depends on the page size,
 * because an 8K page table holds half as many descriptors. */
[[nodiscard]] ap_m68040_table_descriptor_t
ap_m68040_pointer_descriptor(uint32_t value, ap_m68040_page_size_t page_size);

typedef struct {
  ap_m68040_pdt_t type;
  bool write_protect;      /* W, bit 2 */
  bool used;               /* U, bit 3 */
  bool modified;           /* M, bit 4 */
  ap_m68040_cache_mode_t cache_mode; /* CM, bits 6-5 */
  bool supervisor;         /* S, bit 7 */
  bool user_attribute_0;   /* U0, bit 8 */
  bool user_attribute_1;   /* U1, bit 9 */
  bool global;             /* G, bit 10 */
  /* The physical page frame, or -- when the type is indirect -- the address of
   * another page descriptor, which is 4-byte aligned rather than page aligned. */
  uint32_t address;
} ap_m68040_page_descriptor_t;

[[nodiscard]] ap_m68040_page_descriptor_t
ap_m68040_page_descriptor(uint32_t value, ap_m68040_page_size_t page_size);

/* Page size in bytes. */
[[nodiscard]] uint32_t ap_m68040_page_bytes(ap_m68040_page_size_t page_size);

/* Whether a page descriptor's bits form a state the manual forbids: "page
 * descriptors must not have an encoding of U-bit = 0, M-bit = 1 and PDT field =
 * 01 or 11. This encoding indicates that the page descriptor is resident, not
 * used, and modified. The processor's table search algorithm never leaves a
 * descriptor in this state."
 *
 * So this is not something the hardware produces and not something it checks --
 * it is reachable only "through direct manipulation by the operating system",
 * and the manual says violating the restriction "can result in an undefined
 * operation". Modelled as a query rather than a fault for exactly that reason:
 * there is no defined behaviour to implement, only a state worth naming. */
[[nodiscard]] bool
ap_m68040_page_descriptor_is_incoherent(const ap_m68040_page_descriptor_t *page);

#endif /* APOLLO_CPU_M68040_AP_M68040_DESCRIPTOR_H */

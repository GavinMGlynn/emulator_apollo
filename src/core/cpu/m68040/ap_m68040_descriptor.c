/* MC68040 address translation descriptors. See the header; Figures 3-11 and
 * 3-12 read from the page images. */

#include "cpu/m68040/ap_m68040_descriptor.h"

/* `UDT`: "00 or 01 = Invalid ... 10 or 11 = Resident", so only the high bit
 * carries meaning. */
static ap_m68040_udt_t decode_udt(uint32_t value) {
  return (value & 0x2u) ? AP_M68040_UDT_RESIDENT : AP_M68040_UDT_INVALID;
}

/* The three fields every table descriptor shares, at the same bits in all
 * three forms -- only the address width above them changes. */
static ap_m68040_table_descriptor_t decode_table(uint32_t value,
                                                 uint32_t address_mask) {
  return (ap_m68040_table_descriptor_t){
      .type = decode_udt(value),
      .write_protect = (value & 0x4u) != 0u,
      .used = (value & 0x8u) != 0u,
      .table_address = value & address_mask,
  };
}

ap_m68040_table_descriptor_t ap_m68040_root_descriptor(uint32_t value) {
  /* Bits 31-9, with bits 8-4 Motorola-reserved. */
  return decode_table(value, 0xFFFFFE00u);
}

ap_m68040_table_descriptor_t
ap_m68040_pointer_descriptor(uint32_t value, ap_m68040_page_size_t page_size) {
  /* Bits 31-8 at 4K and 31-7 at 8K: an 8K page table holds half as many
   * descriptors, so it needs one less bit of index and one more of base. */
  return decode_table(value, (page_size == AP_M68040_PAGE_8K) ? 0xFFFFFF80u
                                                             : 0xFFFFFF00u);
}

ap_m68040_page_descriptor_t
ap_m68040_page_descriptor(uint32_t value, ap_m68040_page_size_t page_size) {
  ap_m68040_page_descriptor_t out = {
      .write_protect = (value & 0x0004u) != 0u,
      .used = (value & 0x0008u) != 0u,
      .modified = (value & 0x0010u) != 0u,
      .cache_mode = (ap_m68040_cache_mode_t)((value >> 5) & 0x3u),
      .supervisor = (value & 0x0080u) != 0u,
      .user_attribute_0 = (value & 0x0100u) != 0u,
      .user_attribute_1 = (value & 0x0200u) != 0u,
      .global = (value & 0x0400u) != 0u,
  };

  /* `PDT`: three meanings in four encodings. "01 or 11 = Resident" makes the
   * high bit free *there*, but `00` and `10` are invalid and indirect -- so
   * unlike `UDT` this cannot be reduced to one bit. */
  switch (value & 0x3u) {
  case 0x0u:
    out.type = AP_M68040_PDT_INVALID;
    break;
  case 0x2u:
    out.type = AP_M68040_PDT_INDIRECT;
    break;
  default:
    out.type = AP_M68040_PDT_RESIDENT;
    break;
  }

  if (out.type == AP_M68040_PDT_INDIRECT) {
    /* "Bits 31-2 contain the physical address of the page descriptor." A
     * descriptor is four bytes, so this is 4-byte aligned rather than page
     * aligned -- and the attribute bits above are not attributes at all here,
     * since they are part of the address. */
    out.address = value & 0xFFFFFFFCu;
    return out;
  }

  /* Bits 31-12 at 4K, 31-13 at 8K. The bit the narrower address gives up
   * becomes a second `UR` bit rather than being reserved. */
  out.address = value & ((page_size == AP_M68040_PAGE_8K) ? 0xFFFFE000u
                                                          : 0xFFFFF000u);
  return out;
}

uint32_t ap_m68040_page_bytes(ap_m68040_page_size_t page_size) {
  return (page_size == AP_M68040_PAGE_8K) ? 8192u : 4096u;
}

bool ap_m68040_page_descriptor_is_incoherent(
    const ap_m68040_page_descriptor_t *page) {
  /* "Resident, not used, and modified." */
  return page->type == AP_M68040_PDT_RESIDENT && !page->used && page->modified;
}

/* MC68040 address translation descriptors, `[68040]` §3.2.2 and Figures 3-11
 * and 3-12, read from the page images.
 *
 * Several tests contrast this MMU with the 68851's, because the risk in
 * implementing the third MMU in a project is carrying the second one's
 * assumptions into it.
 */

#include "cpu/m68040/ap_m68040_descriptor.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* ---------------------------------------------------------------------------
 * The type fields, and their asymmetric don't-care bits.
 * ------------------------------------------------------------------------- */

static void test_udt_ignores_its_low_bit(void) {
  /* "00 or 01 = Invalid ... 10 or 11 = Resident." Only the high bit carries
   * meaning, so a table descriptor has two encodings for each verdict. */
  TEST_ASSERT_EQUAL_INT(AP_M68040_UDT_INVALID,
                        ap_m68040_root_descriptor(0x0u).type);
  TEST_ASSERT_EQUAL_INT(AP_M68040_UDT_INVALID,
                        ap_m68040_root_descriptor(0x1u).type);
  TEST_ASSERT_EQUAL_INT(AP_M68040_UDT_RESIDENT,
                        ap_m68040_root_descriptor(0x2u).type);
  TEST_ASSERT_EQUAL_INT(AP_M68040_UDT_RESIDENT,
                        ap_m68040_root_descriptor(0x3u).type);
}

static void test_pdt_has_three_meanings_in_four_encodings(void) {
  /* "00 = Invalid; 01 or 11 = Resident; 10 = Indirect." */
  TEST_ASSERT_EQUAL_INT(
      AP_M68040_PDT_INVALID,
      ap_m68040_page_descriptor(0x0u, AP_M68040_PAGE_4K).type);
  TEST_ASSERT_EQUAL_INT(
      AP_M68040_PDT_RESIDENT,
      ap_m68040_page_descriptor(0x1u, AP_M68040_PAGE_4K).type);
  TEST_ASSERT_EQUAL_INT(
      AP_M68040_PDT_INDIRECT,
      ap_m68040_page_descriptor(0x2u, AP_M68040_PAGE_4K).type);
  TEST_ASSERT_EQUAL_INT(
      AP_M68040_PDT_RESIDENT,
      ap_m68040_page_descriptor(0x3u, AP_M68040_PAGE_4K).type);
}

static void test_the_two_type_fields_free_different_bits(void) {
  /* The trap: `UDT`'s free bit is the low one and `PDT`'s -- for the resident
   * case only -- is the high one. A decoder that masked the same bit in both
   * would turn every indirect page descriptor into an invalid one, silently
   * losing a level of the tree. */
  TEST_ASSERT_EQUAL_INT(ap_m68040_root_descriptor(0x2u).type,
                        ap_m68040_root_descriptor(0x3u).type);
  TEST_ASSERT_NOT_EQUAL_INT(
      ap_m68040_page_descriptor(0x0u, AP_M68040_PAGE_4K).type,
      ap_m68040_page_descriptor(0x2u, AP_M68040_PAGE_4K).type);
}

/* ---------------------------------------------------------------------------
 * Table descriptors, Figure 3-11.
 * ------------------------------------------------------------------------- */

static void test_the_root_descriptor_fields(void) {
  /* Pointer table address 31-9, X 8-4, U 3, W 2, UDT 1-0. */
  const ap_m68040_table_descriptor_t d =
      ap_m68040_root_descriptor(0x1234FE0Fu);
  TEST_ASSERT_EQUAL_INT(AP_M68040_UDT_RESIDENT, d.type);
  TEST_ASSERT_TRUE(d.used);
  TEST_ASSERT_TRUE(d.write_protect);
  TEST_ASSERT_EQUAL_HEX32(0x1234FE00u, d.table_address);
}

static void test_a_pointer_table_is_512_byte_aligned_from_the_root(void) {
  /* The root's address field is bits 31-9, so a pointer table starts on a
   * 512-byte boundary -- which is exactly its own size, 128 descriptors of
   * four bytes. */
  TEST_ASSERT_EQUAL_HEX32(
      0xFFFFFE00u, ap_m68040_root_descriptor(0xFFFFFFFFu).table_address);
}

static void test_the_pointer_descriptors_address_widens_with_the_page_size(void) {
  /* Bits 31-8 at 4K and 31-7 at 8K: an 8K page table holds half as many
   * descriptors, so it needs one less bit of index and one more of base. */
  TEST_ASSERT_EQUAL_HEX32(
      0xFFFFFF00u,
      ap_m68040_pointer_descriptor(0xFFFFFFFFu, AP_M68040_PAGE_4K)
          .table_address);
  TEST_ASSERT_EQUAL_HEX32(
      0xFFFFFF80u,
      ap_m68040_pointer_descriptor(0xFFFFFFFFu, AP_M68040_PAGE_8K)
          .table_address);
}

static void test_the_table_descriptor_flags_sit_at_the_same_bits_in_all_forms(void) {
  /* `U`, `W` and `UDT` are at bits 3, 2 and 1-0 in the root and both pointer
   * forms; only the address above them moves. That is what makes one decoder
   * serve all three. */
  const uint32_t flags = 0xFu; /* U, W, UDT = 11 */
  const ap_m68040_table_descriptor_t root = ap_m68040_root_descriptor(flags);
  const ap_m68040_table_descriptor_t p4 =
      ap_m68040_pointer_descriptor(flags, AP_M68040_PAGE_4K);
  const ap_m68040_table_descriptor_t p8 =
      ap_m68040_pointer_descriptor(flags, AP_M68040_PAGE_8K);
  TEST_ASSERT_TRUE(root.used && p4.used && p8.used);
  TEST_ASSERT_TRUE(root.write_protect && p4.write_protect && p8.write_protect);
  TEST_ASSERT_EQUAL_INT(AP_M68040_UDT_RESIDENT, root.type);
  TEST_ASSERT_EQUAL_INT(AP_M68040_UDT_RESIDENT, p4.type);
  TEST_ASSERT_EQUAL_INT(AP_M68040_UDT_RESIDENT, p8.type);
}

/* ---------------------------------------------------------------------------
 * Page descriptors, Figure 3-12.
 * ------------------------------------------------------------------------- */

static void test_the_page_descriptor_fields(void) {
  /* Physical address 31-12, UR 11, G 10, U1 9, U0 8, S 7, CM 6-5, M 4, U 3,
   * W 2, PDT 1-0. */
  const ap_m68040_page_descriptor_t d =
      ap_m68040_page_descriptor(0x12345FFDu, AP_M68040_PAGE_4K);
  TEST_ASSERT_EQUAL_INT(AP_M68040_PDT_RESIDENT, d.type);
  TEST_ASSERT_TRUE(d.write_protect);
  TEST_ASSERT_TRUE(d.used);
  TEST_ASSERT_TRUE(d.modified);
  TEST_ASSERT_EQUAL_INT(AP_M68040_CM_NONCACHABLE, d.cache_mode);
  TEST_ASSERT_TRUE(d.supervisor);
  TEST_ASSERT_TRUE(d.user_attribute_0);
  TEST_ASSERT_TRUE(d.user_attribute_1);
  TEST_ASSERT_TRUE(d.global);
  TEST_ASSERT_EQUAL_HEX32(0x12345000u, d.address);
}

static void test_each_page_attribute_sits_at_its_own_bit(void) {
  /* Six single-bit flags and a two-bit field packed together, which is where
   * an off-by-one hides. */
  TEST_ASSERT_TRUE(
      ap_m68040_page_descriptor(0x0004u, AP_M68040_PAGE_4K).write_protect);
  TEST_ASSERT_TRUE(ap_m68040_page_descriptor(0x0008u, AP_M68040_PAGE_4K).used);
  TEST_ASSERT_TRUE(
      ap_m68040_page_descriptor(0x0010u, AP_M68040_PAGE_4K).modified);
  TEST_ASSERT_TRUE(
      ap_m68040_page_descriptor(0x0080u, AP_M68040_PAGE_4K).supervisor);
  TEST_ASSERT_TRUE(
      ap_m68040_page_descriptor(0x0100u, AP_M68040_PAGE_4K).user_attribute_0);
  TEST_ASSERT_TRUE(
      ap_m68040_page_descriptor(0x0200u, AP_M68040_PAGE_4K).user_attribute_1);
  TEST_ASSERT_TRUE(
      ap_m68040_page_descriptor(0x0400u, AP_M68040_PAGE_4K).global);

  const ap_m68040_page_descriptor_t only_global =
      ap_m68040_page_descriptor(0x0400u, AP_M68040_PAGE_4K);
  TEST_ASSERT_FALSE(only_global.supervisor);
  TEST_ASSERT_FALSE(only_global.user_attribute_1);
  TEST_ASSERT_FALSE(only_global.modified);
}

static void test_the_four_cache_modes(void) {
  /* §3.2.2.3: four policies rather than the earlier parts' single inhibit bit.
   * Write-through and copyback are *both* cachable and differ in when a store
   * reaches memory -- a coherency property a `CI` bit cannot express. */
  const ap_m68040_cache_mode_t expected[4] = {
      AP_M68040_CM_CACHABLE_WRITE_THROUGH, AP_M68040_CM_CACHABLE_COPYBACK,
      AP_M68040_CM_NONCACHABLE_SERIALIZED, AP_M68040_CM_NONCACHABLE};
  for (unsigned cm = 0; cm < 4u; cm++) {
    const ap_m68040_page_descriptor_t d =
        ap_m68040_page_descriptor(cm << 5, AP_M68040_PAGE_4K);
    TEST_ASSERT_EQUAL_INT(expected[cm], d.cache_mode);
  }
}

static void test_the_page_frame_narrows_as_the_page_grows(void) {
  /* "This 20-bit field contains the physical base address of a page in memory
   * ... When the page size is 8-Kbyte, the least significant bit of this field
   * is not used." A larger page needs one fewer bit to name. */
  TEST_ASSERT_EQUAL_HEX32(
      0xFFFFF000u,
      ap_m68040_page_descriptor(0xFFFFF001u, AP_M68040_PAGE_4K).address);
  TEST_ASSERT_EQUAL_HEX32(
      0xFFFFE000u,
      ap_m68040_page_descriptor(0xFFFFF001u, AP_M68040_PAGE_8K).address);
}

static void test_an_indirect_descriptor_addresses_a_descriptor_not_a_page(void) {
  /* "Bits 31-2 contain the physical address of the page descriptor." Four-byte
   * aligned, so the bits that are attributes in a resident descriptor are part
   * of the address here -- reading them as attributes would invent protection
   * out of an address. */
  const ap_m68040_page_descriptor_t d =
      ap_m68040_page_descriptor(0x12345676u, AP_M68040_PAGE_4K);
  TEST_ASSERT_EQUAL_INT(AP_M68040_PDT_INDIRECT, d.type);
  TEST_ASSERT_EQUAL_HEX32(0x12345674u, d.address);
}

static void test_the_indirect_address_ignores_the_page_size(void) {
  /* What it points at is a descriptor, whose size does not change with the
   * page size -- unlike a page frame's. */
  TEST_ASSERT_EQUAL_HEX32(
      ap_m68040_page_descriptor(0x12345676u, AP_M68040_PAGE_4K).address,
      ap_m68040_page_descriptor(0x12345676u, AP_M68040_PAGE_8K).address);
}

static void test_the_two_page_sizes(void) {
  TEST_ASSERT_EQUAL_UINT32(4096u, ap_m68040_page_bytes(AP_M68040_PAGE_4K));
  TEST_ASSERT_EQUAL_UINT32(8192u, ap_m68040_page_bytes(AP_M68040_PAGE_8K));
}

static void test_the_forbidden_resident_unused_modified_state(void) {
  /* "Page descriptors must not have an encoding of U-bit = 0, M-bit = 1 and
   * PDT field = 01 or 11 ... The processor's table search algorithm never
   * leaves a descriptor in this state." Reachable only by the operating system
   * writing it directly, and the manual gives no defined behaviour for it --
   * so this is named rather than faulted. */
  const ap_m68040_page_descriptor_t bad =
      ap_m68040_page_descriptor(0x0011u, AP_M68040_PAGE_4K); /* M set, U clear */
  TEST_ASSERT_EQUAL_INT(AP_M68040_PDT_RESIDENT, bad.type);
  TEST_ASSERT_TRUE(ap_m68040_page_descriptor_is_incoherent(&bad));

  /* Used and modified together is the ordinary state after a write. */
  const ap_m68040_page_descriptor_t good =
      ap_m68040_page_descriptor(0x0019u, AP_M68040_PAGE_4K);
  TEST_ASSERT_FALSE(ap_m68040_page_descriptor_is_incoherent(&good));

  /* And an *invalid* descriptor with those bits is not the forbidden state:
   * "all other bits in the descriptor are ignored" when the type is invalid. */
  const ap_m68040_page_descriptor_t invalid =
      ap_m68040_page_descriptor(0x0010u, AP_M68040_PAGE_4K);
  TEST_ASSERT_FALSE(ap_m68040_page_descriptor_is_incoherent(&invalid));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_udt_ignores_its_low_bit);
  RUN_TEST(test_pdt_has_three_meanings_in_four_encodings);
  RUN_TEST(test_the_two_type_fields_free_different_bits);
  RUN_TEST(test_the_root_descriptor_fields);
  RUN_TEST(test_a_pointer_table_is_512_byte_aligned_from_the_root);
  RUN_TEST(test_the_pointer_descriptors_address_widens_with_the_page_size);
  RUN_TEST(test_the_table_descriptor_flags_sit_at_the_same_bits_in_all_forms);
  RUN_TEST(test_the_page_descriptor_fields);
  RUN_TEST(test_each_page_attribute_sits_at_its_own_bit);
  RUN_TEST(test_the_four_cache_modes);
  RUN_TEST(test_the_page_frame_narrows_as_the_page_grows);
  RUN_TEST(test_an_indirect_descriptor_addresses_a_descriptor_not_a_page);
  RUN_TEST(test_the_indirect_address_ignores_the_page_size);
  RUN_TEST(test_the_two_page_sizes);
  RUN_TEST(test_the_forbidden_resident_unused_modified_state);
  return UNITY_END();
}

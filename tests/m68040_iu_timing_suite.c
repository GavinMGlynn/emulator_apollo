/* MC68040 integer unit instruction timings, `[68040]` §10.6, from the page
 * images.
 *
 * The section prices every instruction over the same seventeen addressing
 * modes and groups those whose columns are identical, so the tests check both
 * the figures and the grouping.
 */

#include <string.h>

#include "cpu/m68040/ap_m68040_iu_timing.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static ap_m68040_iu_cell_t at(const char *instruction,
                              ap_m68040_iu_mode_t mode) {
  return ap_m68040_iu_timing(instruction, mode);
}

/* ---------------------------------------------------------------------------
 * Grouping.
 * ------------------------------------------------------------------------- */

static void test_instructions_sharing_a_column_share_a_group(void) {
  /* `ADD`, `AND`, `EOR`, `OR`, `SUB` and `TST` head one column because their
   * timings are identical -- not because they are related operations. The
   * grouping is the manual's and is worth preserving: it is evidence that six
   * different instructions really do cost the same. */
  const ap_m68040_iu_group_t *add = ap_m68040_iu_find("ADD");
  TEST_ASSERT_NOT_NULL(add);
  const char *const shares[] = {"AND", "EOR", "OR", "SUB", "TST"};
  for (unsigned i = 0; i < sizeof shares / sizeof shares[0]; i++) {
    TEST_ASSERT_EQUAL_PTR(add, ap_m68040_iu_find(shares[i]));
  }
}

static void test_adda_is_priced_apart_from_add(void) {
  /* Address-register destinations cost more: `ADD Dn,Dn` executes in one clock
   * and `ADDA Dn,An` in two, because the result is sign-extended to a long
   * whatever the operation size. */
  TEST_ASSERT_NOT_EQUAL(ap_m68040_iu_find("ADD"), ap_m68040_iu_find("ADDA"));
  TEST_ASSERT_EQUAL_UINT(1u, at("ADD", AP_M68040_IU_DN).execute.base);
  TEST_ASSERT_EQUAL_UINT(2u, at("ADDA", AP_M68040_IU_DN).execute.base);
}

static void test_the_immediate_forms_are_their_own_group(void) {
  const ap_m68040_iu_group_t *addi = ap_m68040_iu_find("ADDI");
  TEST_ASSERT_NOT_NULL(addi);
  const char *const shares[] = {"ANDI", "EORI", "ORI", "SUBI"};
  for (unsigned i = 0; i < 4u; i++) {
    TEST_ASSERT_EQUAL_PTR(addi, ap_m68040_iu_find(shares[i]));
  }
  TEST_ASSERT_NOT_EQUAL(addi, ap_m68040_iu_find("ADD"));
}

/* ---------------------------------------------------------------------------
 * A dash is not a zero.
 * ------------------------------------------------------------------------- */

static void test_an_invalid_mode_is_marked_rather_than_zeroed(void) {
  /* The table prints a dash where the mode does not exist for the group.
   * `ADDI` has no `An` form, no PC-relative form and no immediate form -- the
   * immediate is already the instruction's own operand. Reporting these as
   * zero-cost would let a decoder price an encoding it should reject. */
  const ap_m68040_iu_mode_t absent[] = {
      AP_M68040_IU_AN, AP_M68040_IU_PC_DISPLACEMENT, AP_M68040_IU_IMMEDIATE,
      AP_M68040_IU_PC_INDEXED};
  for (unsigned i = 0; i < 4u; i++) {
    const ap_m68040_iu_cell_t c = at("ADDI", absent[i]);
    TEST_ASSERT_FALSE(c.valid);
    TEST_ASSERT_EQUAL_UINT(0u, c.calculate);
  }
  /* And a mode that *is* valid reports so. */
  TEST_ASSERT_TRUE(at("ADDI", AP_M68040_IU_DN).valid);
}

static void test_add_accepts_every_mode(void) {
  /* The `ADD` group's column has no dashes: all seventeen modes are priced. */
  for (unsigned m = 0; m < AP_M68040_IU_MODE_COUNT; m++) {
    TEST_ASSERT_TRUE(at("ADD", (ap_m68040_iu_mode_t)m).valid);
  }
}

/* ---------------------------------------------------------------------------
 * Figures.
 * ------------------------------------------------------------------------- */

static void test_the_simple_modes_cost_one_clock_each(void) {
  /* `ADD` over the modes with no index register: one clock in each stage,
   * except the PC-relative form which supposition 1 charges. */
  const ap_m68040_iu_mode_t simple[] = {
      AP_M68040_IU_DN,        AP_M68040_IU_AN,       AP_M68040_IU_INDIRECT,
      AP_M68040_IU_POSTINCREMENT, AP_M68040_IU_PREDECREMENT,
      AP_M68040_IU_DISPLACEMENT, AP_M68040_IU_ABSOLUTE,
      AP_M68040_IU_IMMEDIATE};
  for (unsigned i = 0; i < sizeof simple / sizeof simple[0]; i++) {
    const ap_m68040_iu_cell_t c = at("ADD", simple[i]);
    TEST_ASSERT_EQUAL_UINT(1u, c.calculate);
    TEST_ASSERT_EQUAL_UINT(1u, ap_m68040_execute_total(c.execute));
  }
}

static void test_the_pc_relative_mode_costs_more(void) {
  /* `(d16,PC)` is 3 and `2L + 1` against `(d16,An)`'s 1 and 1 -- two more
   * calculate clocks and two of lead, which is supposition 1's charge showing
   * up through the interlock rather than as a flat one. */
  const ap_m68040_iu_cell_t an = at("ADD", AP_M68040_IU_DISPLACEMENT);
  const ap_m68040_iu_cell_t pc = at("ADD", AP_M68040_IU_PC_DISPLACEMENT);
  TEST_ASSERT_EQUAL_UINT(1u, an.calculate);
  TEST_ASSERT_EQUAL_UINT(3u, pc.calculate);
  TEST_ASSERT_EQUAL_UINT(2u, pc.execute.lead);
  TEST_ASSERT_EQUAL_UINT(1u, pc.execute.base);
}

static void test_the_memory_indirect_figures_are_irregular(void) {
  /* Verified at high magnification because the progression looks wrong:
   * `([bd,BR,Xn])` is `1L + 9` and `([bd,BR,Xn],od)` is `1L + 11`, a jump of
   * two where the calculate column moves by one. The `MOVE` table's analogous
   * pair moves by one. The irregularity is the manual's, not a transcription
   * slip, and pinning it stops a later "tidy-up" from smoothing it away. */
  const ap_m68040_iu_cell_t pre = at("ADD", AP_M68040_IU_MEMORY_PREINDEXED);
  TEST_ASSERT_EQUAL_UINT(10u, pre.calculate);
  TEST_ASSERT_EQUAL_UINT(1u, pre.execute.lead);
  TEST_ASSERT_EQUAL_UINT(9u, pre.execute.base);

  const ap_m68040_iu_cell_t pre_od =
      at("ADD", AP_M68040_IU_MEMORY_PREINDEXED_OD);
  TEST_ASSERT_EQUAL_UINT(11u, pre_od.calculate);
  TEST_ASSERT_EQUAL_UINT(1u, pre_od.execute.lead);
  TEST_ASSERT_EQUAL_UINT(11u, pre_od.execute.base);

  const ap_m68040_iu_cell_t post = at("ADD", AP_M68040_IU_MEMORY_POSTINDEXED);
  TEST_ASSERT_EQUAL_UINT(11u, post.calculate);
  TEST_ASSERT_EQUAL_UINT(3u, post.execute.lead);
  TEST_ASSERT_EQUAL_UINT(8u, post.execute.base);

  const ap_m68040_iu_cell_t post_od =
      at("ADD", AP_M68040_IU_MEMORY_POSTINDEXED_OD);
  TEST_ASSERT_EQUAL_UINT(12u, post_od.calculate);
  TEST_ASSERT_EQUAL_UINT(3u, post_od.execute.lead);
  TEST_ASSERT_EQUAL_UINT(10u, post_od.execute.base);
}

static void test_adda_is_never_cheaper_than_add(void) {
  /* My first attempt asserted `ADDA` costs exactly one clock more than `ADD`
   * everywhere, which the table refutes: at `Dn` the difference is one, at
   * `(An)+` it is two (1 against `1L + 2`), and at `An` and `#<xxx>` it is
   * zero. The relationship is not uniform, so the honest invariant is the weak
   * one -- and the specific differences are pinned separately below. */
  for (unsigned m = 0; m < AP_M68040_IU_MODE_COUNT; m++) {
    const ap_m68040_iu_cell_t add = at("ADD", (ap_m68040_iu_mode_t)m);
    const ap_m68040_iu_cell_t adda = at("ADDA", (ap_m68040_iu_mode_t)m);
    if (!add.valid || !adda.valid) {
      continue;
    }
    TEST_ASSERT_TRUE(ap_m68040_execute_total(adda.execute) >=
                     ap_m68040_execute_total(add.execute));
  }
}

static void test_where_adda_costs_the_same_as_add(void) {
  /* An address-register or immediate source needs no extra pass, so those two
   * modes cost the same in both columns -- the only two that do. */
  TEST_ASSERT_EQUAL_UINT(
      ap_m68040_execute_total(at("ADD", AP_M68040_IU_AN).execute),
      ap_m68040_execute_total(at("ADDA", AP_M68040_IU_AN).execute));
  TEST_ASSERT_EQUAL_UINT(
      ap_m68040_execute_total(at("ADD", AP_M68040_IU_IMMEDIATE).execute),
      ap_m68040_execute_total(at("ADDA", AP_M68040_IU_IMMEDIATE).execute));

  /* And where they differ, by one at `Dn` and two at `(An)+`. */
  TEST_ASSERT_EQUAL_UINT(1u, at("ADD", AP_M68040_IU_DN).execute.base);
  TEST_ASSERT_EQUAL_UINT(2u, at("ADDA", AP_M68040_IU_DN).execute.base);
  TEST_ASSERT_EQUAL_UINT(
      1u, ap_m68040_execute_total(at("ADD", AP_M68040_IU_POSTINCREMENT).execute));
  TEST_ASSERT_EQUAL_UINT(
      3u,
      ap_m68040_execute_total(at("ADDA", AP_M68040_IU_POSTINCREMENT).execute));
}

/* ---------------------------------------------------------------------------
 * Structure.
 * ------------------------------------------------------------------------- */

static void test_every_valid_cell_costs_at_least_one_clock(void) {
  for (size_t g = 0; g < ap_m68040_iu_group_count(); g++) {
    const ap_m68040_iu_group_t *group = ap_m68040_iu_group(g);
    TEST_ASSERT_NOT_NULL(group);
    for (unsigned m = 0; m < AP_M68040_IU_MODE_COUNT; m++) {
      const ap_m68040_iu_cell_t c = group->cells[m];
      if (!c.valid) {
        continue;
      }
      TEST_ASSERT_TRUE(c.calculate >= 1u);
      TEST_ASSERT_TRUE(ap_m68040_execute_total(c.execute) >= 1u);
    }
  }
}

static void test_an_instruction_priced_elsewhere_is_not_found_here(void) {
  /* §10.5 prices `NOP` and `SWAP`; §10.4 prices `MOVE`. Reporting NULL is the
   * honest answer rather than inventing a column. */
  TEST_ASSERT_NULL(ap_m68040_iu_find("NOP"));
  TEST_ASSERT_NULL(ap_m68040_iu_find("SWAP"));
  TEST_ASSERT_NULL(ap_m68040_iu_find("MOVE"));
  TEST_ASSERT_FALSE(at("NOP", AP_M68040_IU_DN).valid);
}


/* ---------------------------------------------------------------------------
 * The shift and rotate groups, and the cells that print two figures.
 * ------------------------------------------------------------------------- */

static void test_a_register_shift_count_costs_one_more_clock(void) {
  /* §10.6's footnote: "immediate count specified for shift count/shift count
   * specified in register, respectively". `ASL Dn` prints `3/4` and
   * `ASR`/`LSL`/`LSR` print `2/3`, so a register count costs one clock more in
   * both -- the count has to be read before the shift can begin.
   *
   * A model with one figure per cell would under-price exactly the
   * register-count shifts a compiler emits most. */
  const ap_m68040_iu_cell_t asl = at("ASL", AP_M68040_IU_DN);
  TEST_ASSERT_TRUE(asl.alternate != AP_M68040_IU_ALTERNATE_NONE);
  TEST_ASSERT_EQUAL_UINT(3u, ap_m68040_execute_total(
                                 ap_m68040_iu_execute(asl, false, false)));
  TEST_ASSERT_EQUAL_UINT(4u, ap_m68040_execute_total(
                                 ap_m68040_iu_execute(asl, true, false)));

  const ap_m68040_iu_cell_t asr = at("ASR", AP_M68040_IU_DN);
  TEST_ASSERT_TRUE(asr.alternate != AP_M68040_IU_ALTERNATE_NONE);
  TEST_ASSERT_EQUAL_UINT(2u, ap_m68040_execute_total(
                                 ap_m68040_iu_execute(asr, false, false)));
  TEST_ASSERT_EQUAL_UINT(3u, ap_m68040_execute_total(
                                 ap_m68040_iu_execute(asr, true, false)));
}

static void test_only_the_register_row_prints_two_figures(void) {
  /* A memory shift is always by one, so the distinction cannot arise there --
   * and the table prints a single figure for every mode but `Dn`. */
  for (unsigned m = 0; m < AP_M68040_IU_MODE_COUNT; m++) {
    const ap_m68040_iu_cell_t c = at("ASL", (ap_m68040_iu_mode_t)m);
    if (!c.valid) {
      continue;
    }
    TEST_ASSERT_EQUAL_INT(m == AP_M68040_IU_DN,
                          c.alternate == AP_M68040_IU_ALTERNATE_SHIFT_COUNT);
  }
}

static void test_a_single_figure_cell_ignores_the_count_argument(void) {
  /* Where the table prints one figure the caller may pass either value and get
   * the same answer, which is what lets a scheduler call it unconditionally. */
  const ap_m68040_iu_cell_t add = at("ADD", AP_M68040_IU_DN);
  TEST_ASSERT_FALSE(add.alternate != AP_M68040_IU_ALTERNATE_NONE);
  TEST_ASSERT_EQUAL_UINT(
      ap_m68040_execute_total(ap_m68040_iu_execute(add, false, false)),
      ap_m68040_execute_total(ap_m68040_iu_execute(add, true, false)));
}

static void test_an_arithmetic_left_shift_costs_more_than_the_others(void) {
  /* `ASL` is its own group at 3 clocks where `ASR`, `LSL` and `LSR` share one
   * at 2 -- the only left shift that must detect overflow, which the others
   * have no `V` to set. */
  TEST_ASSERT_NOT_EQUAL(ap_m68040_iu_find("ASL"), ap_m68040_iu_find("ASR"));
  TEST_ASSERT_EQUAL_PTR(ap_m68040_iu_find("ASR"), ap_m68040_iu_find("LSL"));
  TEST_ASSERT_EQUAL_PTR(ap_m68040_iu_find("ASR"), ap_m68040_iu_find("LSR"));
  TEST_ASSERT_TRUE(
      ap_m68040_execute_total(at("ASL", AP_M68040_IU_INDIRECT).execute) >
      ap_m68040_execute_total(at("ASR", AP_M68040_IU_INDIRECT).execute));
}

static void test_the_quick_forms_reject_an_immediate_mode(void) {
  /* `ADDQ`/`SUBQ` carry their immediate in the opcode, so there is no `#<xxx>`
   * addressing mode for the *other* operand -- and no PC-relative mode either,
   * since the destination must be alterable. */
  TEST_ASSERT_FALSE(at("ADDQ", AP_M68040_IU_IMMEDIATE).valid);
  TEST_ASSERT_FALSE(at("ADDQ", AP_M68040_IU_PC_DISPLACEMENT).valid);
  TEST_ASSERT_FALSE(at("ADDQ", AP_M68040_IU_PC_INDEXED).valid);
  /* But unlike `ADDI` they do accept `An`, which is what makes them a separate
   * column rather than sharing one. */
  TEST_ASSERT_TRUE(at("ADDQ", AP_M68040_IU_AN).valid);
  TEST_ASSERT_FALSE(at("ADDI", AP_M68040_IU_AN).valid);
}

static void test_the_shift_groups_reject_the_register_direct_address_mode(void) {
  /* A shift has no `An` form at all: shifting an address register is not an
   * encoding the family provides. */
  TEST_ASSERT_FALSE(at("ASL", AP_M68040_IU_AN).valid);
  TEST_ASSERT_FALSE(at("LSR", AP_M68040_IU_AN).valid);
}


/* ---------------------------------------------------------------------------
 * The bit and bit-field groups, page 10-15.
 * ------------------------------------------------------------------------- */

static void test_three_different_things_are_selected_by_a_second_figure(void) {
  /* §10.6 prints `a/b` cells for three unrelated reasons, and conflating them
   * would silently price one instruction by another's rule:
   *
   *   ASL     shift count immediate or in a register
   *   BCHG    bit number given as `#<xxx>` or in `Dn`
   *   BFEXTS  bit field width and offset both immediate, or either in a
   *           register
   *
   * The cell records which distinction applies, so a caller cannot ask the
   * wrong question of a cell. */
  TEST_ASSERT_EQUAL_INT(AP_M68040_IU_ALTERNATE_SHIFT_COUNT,
                        at("ASL", AP_M68040_IU_DN).alternate);
  TEST_ASSERT_EQUAL_INT(AP_M68040_IU_ALTERNATE_BIT_NUMBER,
                        at("BCHG", AP_M68040_IU_DN).alternate);
  TEST_ASSERT_EQUAL_INT(AP_M68040_IU_ALTERNATE_BITFIELD_OPERAND,
                        at("BFEXTS", AP_M68040_IU_DN).alternate);
}

static void test_a_bit_number_in_a_register_costs_one_more_execute_clock(void) {
  /* Note a: "bit instruction <ea> calculate and execute times T1/T2 apply to
   * #<xxx>/Dn bit numbers." So the *first* figure is the immediate form. */
  const ap_m68040_iu_cell_t c = at("BCHG", AP_M68040_IU_INDIRECT);
  TEST_ASSERT_EQUAL_UINT(
      3u, ap_m68040_execute_total(ap_m68040_iu_execute(c, false, false)));
  TEST_ASSERT_EQUAL_UINT(
      4u, ap_m68040_execute_total(ap_m68040_iu_execute(c, true, false)));
}

static void test_a_bit_number_in_a_register_costs_one_fewer_calculate_clock(void) {
  /* The calculate column is dual too, and runs the *other* way: `BCHG
   * (d16,An)` prints `2/1`, so a register bit number is cheaper to calculate
   * and dearer to execute. A model with a single calculate figure could not
   * express that, and one that assumed both columns move together would get
   * the sign wrong. */
  const ap_m68040_iu_cell_t c = at("BCHG", AP_M68040_IU_DISPLACEMENT);
  TEST_ASSERT_EQUAL_UINT(2u, ap_m68040_iu_calculate(c, false, false));
  TEST_ASSERT_EQUAL_UINT(1u, ap_m68040_iu_calculate(c, true, false));
  TEST_ASSERT_EQUAL_UINT(
      3u, ap_m68040_iu_execute(c, false, false).base);
  TEST_ASSERT_EQUAL_UINT(
      4u, ap_m68040_iu_execute(c, true, false).base);
}

static void test_a_bit_field_spanning_a_long_word_costs_extra(void) {
  /* Note c: "if the bit field spans a long-word boundary, add ten and nine
   * clocks to the <ea> calculate and execute times, respectively. Two memory
   * addresses are accessed in this case."
   *
   * This is a penalty and not a second figure, because it depends on the
   * operand's *address* rather than on the encoding -- no static table could
   * fold it in, and a scheduler that ignored it would be out by nineteen
   * clocks on an unaligned field. */
  const ap_m68040_iu_cell_t c = at("BFCHG", AP_M68040_IU_INDIRECT);
  TEST_ASSERT_EQUAL_UINT(9u, ap_m68040_iu_calculate(c, false, false));
  TEST_ASSERT_EQUAL_UINT(19u, ap_m68040_iu_calculate(c, false, true));
  TEST_ASSERT_EQUAL_UINT(
      10u, ap_m68040_execute_total(ap_m68040_iu_execute(c, false, false)));
  TEST_ASSERT_EQUAL_UINT(
      19u, ap_m68040_execute_total(ap_m68040_iu_execute(c, false, true)));
}

static void test_the_extract_instructions_pay_a_smaller_boundary_penalty(void) {
  /* Note d: "add two clocks to the execute time" -- and nothing to calculate,
   * where note c adds ten. An extract reads two long words; a change must read
   * and write them both. Sharing one penalty between the groups would be wrong
   * by a factor of nine. */
  const ap_m68040_iu_cell_t c = at("BFEXTS", AP_M68040_IU_INDIRECT);
  TEST_ASSERT_EQUAL_UINT(ap_m68040_iu_calculate(c, false, false),
                         ap_m68040_iu_calculate(c, false, true));
  TEST_ASSERT_EQUAL_UINT(
      9u, ap_m68040_execute_total(ap_m68040_iu_execute(c, false, false)));
  TEST_ASSERT_EQUAL_UINT(
      11u, ap_m68040_execute_total(ap_m68040_iu_execute(c, false, true)));
}

static void test_only_the_bit_field_groups_carry_a_boundary_penalty(void) {
  /* Nothing else in §10.6 has an operand that can straddle a long word. */
  for (size_t g = 0; g < ap_m68040_iu_group_count(); g++) {
    const ap_m68040_iu_group_t *group = ap_m68040_iu_group(g);
    const bool bitfield = group->cells[AP_M68040_IU_DN].alternate ==
                          AP_M68040_IU_ALTERNATE_BITFIELD_OPERAND;
    for (unsigned m = 0; m < AP_M68040_IU_MODE_COUNT; m++) {
      const ap_m68040_iu_cell_t c = group->cells[m];
      if (!c.valid) {
        continue;
      }
      if (!bitfield) {
        TEST_ASSERT_EQUAL_UINT(0u, c.boundary_calculate_penalty);
        TEST_ASSERT_EQUAL_UINT(0u, c.boundary_execute_penalty);
      }
    }
  }
}

static void test_the_bit_field_groups_reject_the_incrementing_modes(void) {
  /* A bit field is described by an offset and a width, so postincrement and
   * predecrement have no meaning for it -- the table dashes both. */
  TEST_ASSERT_FALSE(at("BFCHG", AP_M68040_IU_POSTINCREMENT).valid);
  TEST_ASSERT_FALSE(at("BFCHG", AP_M68040_IU_PREDECREMENT).valid);
  TEST_ASSERT_FALSE(at("BFEXTS", AP_M68040_IU_POSTINCREMENT).valid);
}

static void test_only_the_reading_bit_field_group_allows_pc_relative(void) {
  /* `BFEXTS`/`BFEXTU` read a field and so may take it from program space;
   * `BFCHG`/`BFCLR`/`BFSET` write one and cannot. The table dashes the
   * PC-relative rows for the second group alone, which is a protection fact
   * showing up in a timing table. */
  TEST_ASSERT_TRUE(at("BFEXTS", AP_M68040_IU_PC_DISPLACEMENT).valid);
  TEST_ASSERT_TRUE(at("BFEXTS", AP_M68040_IU_PC_INDEXED).valid);
  TEST_ASSERT_FALSE(at("BFCHG", AP_M68040_IU_PC_DISPLACEMENT).valid);
  TEST_ASSERT_FALSE(at("BFCHG", AP_M68040_IU_PC_INDEXED).valid);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_instructions_sharing_a_column_share_a_group);
  RUN_TEST(test_adda_is_priced_apart_from_add);
  RUN_TEST(test_the_immediate_forms_are_their_own_group);
  RUN_TEST(test_an_invalid_mode_is_marked_rather_than_zeroed);
  RUN_TEST(test_add_accepts_every_mode);
  RUN_TEST(test_the_simple_modes_cost_one_clock_each);
  RUN_TEST(test_the_pc_relative_mode_costs_more);
  RUN_TEST(test_the_memory_indirect_figures_are_irregular);
  RUN_TEST(test_adda_is_never_cheaper_than_add);
  RUN_TEST(test_where_adda_costs_the_same_as_add);
  RUN_TEST(test_every_valid_cell_costs_at_least_one_clock);
  RUN_TEST(test_an_instruction_priced_elsewhere_is_not_found_here);
  RUN_TEST(test_a_register_shift_count_costs_one_more_clock);
  RUN_TEST(test_only_the_register_row_prints_two_figures);
  RUN_TEST(test_a_single_figure_cell_ignores_the_count_argument);
  RUN_TEST(test_an_arithmetic_left_shift_costs_more_than_the_others);
  RUN_TEST(test_the_quick_forms_reject_an_immediate_mode);
  RUN_TEST(test_the_shift_groups_reject_the_register_direct_address_mode);
  RUN_TEST(test_three_different_things_are_selected_by_a_second_figure);
  RUN_TEST(test_a_bit_number_in_a_register_costs_one_more_execute_clock);
  RUN_TEST(test_a_bit_number_in_a_register_costs_one_fewer_calculate_clock);
  RUN_TEST(test_a_bit_field_spanning_a_long_word_costs_extra);
  RUN_TEST(test_the_extract_instructions_pay_a_smaller_boundary_penalty);
  RUN_TEST(test_only_the_bit_field_groups_carry_a_boundary_penalty);
  RUN_TEST(test_the_bit_field_groups_reject_the_incrementing_modes);
  RUN_TEST(test_only_the_reading_bit_field_group_allows_pc_relative);
  return UNITY_END();
}

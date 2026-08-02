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
                                 ap_m68040_iu_execute(asl, false, AP_M68040_IU_NO_CONDITIONS)));
  TEST_ASSERT_EQUAL_UINT(4u, ap_m68040_execute_total(
                                 ap_m68040_iu_execute(asl, true, AP_M68040_IU_NO_CONDITIONS)));

  const ap_m68040_iu_cell_t asr = at("ASR", AP_M68040_IU_DN);
  TEST_ASSERT_TRUE(asr.alternate != AP_M68040_IU_ALTERNATE_NONE);
  TEST_ASSERT_EQUAL_UINT(2u, ap_m68040_execute_total(
                                 ap_m68040_iu_execute(asr, false, AP_M68040_IU_NO_CONDITIONS)));
  TEST_ASSERT_EQUAL_UINT(3u, ap_m68040_execute_total(
                                 ap_m68040_iu_execute(asr, true, AP_M68040_IU_NO_CONDITIONS)));
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
      ap_m68040_execute_total(ap_m68040_iu_execute(add, false, AP_M68040_IU_NO_CONDITIONS)),
      ap_m68040_execute_total(ap_m68040_iu_execute(add, true, AP_M68040_IU_NO_CONDITIONS)));
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
      3u, ap_m68040_execute_total(ap_m68040_iu_execute(c, false, AP_M68040_IU_NO_CONDITIONS)));
  TEST_ASSERT_EQUAL_UINT(
      4u, ap_m68040_execute_total(ap_m68040_iu_execute(c, true, AP_M68040_IU_NO_CONDITIONS)));
}

static void test_a_bit_number_in_a_register_costs_one_fewer_calculate_clock(void) {
  /* The calculate column is dual too, and runs the *other* way: `BCHG
   * (d16,An)` prints `2/1`, so a register bit number is cheaper to calculate
   * and dearer to execute. A model with a single calculate figure could not
   * express that, and one that assumed both columns move together would get
   * the sign wrong. */
  const ap_m68040_iu_cell_t c = at("BCHG", AP_M68040_IU_DISPLACEMENT);
  TEST_ASSERT_EQUAL_UINT(2u, ap_m68040_iu_calculate(c, false, AP_M68040_IU_NO_CONDITIONS));
  TEST_ASSERT_EQUAL_UINT(1u, ap_m68040_iu_calculate(c, true, AP_M68040_IU_NO_CONDITIONS));
  TEST_ASSERT_EQUAL_UINT(
      3u, ap_m68040_iu_execute(c, false, AP_M68040_IU_NO_CONDITIONS).base);
  TEST_ASSERT_EQUAL_UINT(
      4u, ap_m68040_iu_execute(c, true, AP_M68040_IU_NO_CONDITIONS).base);
}

static const ap_m68040_iu_condition_t spans[] = {
    AP_M68040_IU_CONDITION_SPANS_LONG_WORD};

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
  TEST_ASSERT_EQUAL_UINT(9u, ap_m68040_iu_calculate(c, false, AP_M68040_IU_NO_CONDITIONS));
  TEST_ASSERT_EQUAL_UINT(19u, ap_m68040_iu_calculate(c, false, spans, 1u));
  TEST_ASSERT_EQUAL_UINT(
      10u, ap_m68040_execute_total(ap_m68040_iu_execute(c, false, AP_M68040_IU_NO_CONDITIONS)));
  TEST_ASSERT_EQUAL_UINT(
      19u, ap_m68040_execute_total(ap_m68040_iu_execute(c, false, spans, 1u)));
}

static void test_the_extract_instructions_pay_a_smaller_boundary_penalty(void) {
  /* Note d: "add two clocks to the execute time" -- and nothing to calculate,
   * where note c adds ten. An extract reads two long words; a change must read
   * and write them both. Sharing one penalty between the groups would be wrong
   * by a factor of nine. */
  const ap_m68040_iu_cell_t c = at("BFEXTS", AP_M68040_IU_INDIRECT);
  TEST_ASSERT_EQUAL_UINT(ap_m68040_iu_calculate(c, false, AP_M68040_IU_NO_CONDITIONS),
                         ap_m68040_iu_calculate(c, false, spans, 1u));
  TEST_ASSERT_EQUAL_UINT(
      9u, ap_m68040_execute_total(ap_m68040_iu_execute(c, false, AP_M68040_IU_NO_CONDITIONS)));
  TEST_ASSERT_EQUAL_UINT(
      11u, ap_m68040_execute_total(ap_m68040_iu_execute(c, false, spans, 1u)));
}

static void test_only_the_bit_field_groups_span_a_long_word(void) {
  /* Nothing else in §10.6 has an operand that can straddle a long word. Other
   * columns do carry conditional penalties -- `CHK2` has two of its own -- so
   * this checks the *condition* rather than merely the presence of a penalty,
   * which is the distinction the tagged conditions exist to make. */
  for (size_t g = 0; g < ap_m68040_iu_group_count(); g++) {
    const ap_m68040_iu_group_t *group = ap_m68040_iu_group(g);
    const bool bitfield = group->cells[AP_M68040_IU_DN].alternate ==
                          AP_M68040_IU_ALTERNATE_BITFIELD_OPERAND;
    for (unsigned m = 0; m < AP_M68040_IU_MODE_COUNT; m++) {
      const ap_m68040_iu_cell_t c = group->cells[m];
      if (!c.valid) {
        continue;
      }
      for (unsigned pi = 0; pi < AP_M68040_IU_MAX_PENALTIES; pi++) {
        if (c.penalty[pi].condition !=
            AP_M68040_IU_CONDITION_SPANS_LONG_WORD) {
          continue;
        }
        TEST_ASSERT_TRUE(bitfield);
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


/* ---------------------------------------------------------------------------
 * Page 10-16, and a note whose letter points at the wrong text.
 * ------------------------------------------------------------------------- */

static void test_the_dn_row_selector_is_read_as_page_10_15_prints_it(void) {
  /* Page 10-16 marks its `Dn` row `3/4^d` and `6/7^d`, and its note `d` reads
   * "if the bit field spans a long-word boundary, add ten and nine clocks...".
   * A data register has no long-word boundary, so that cannot be what the
   * superscript means. Read at 500 dpi to rule out a misread glyph.
   *
   * The correct reading is the one page 10-15 prints for the identical figures:
   * `BFCHG Dn` is also `3/4` and `6/7`, marked `e`, whose note is "immediate
   * count specified for both width and offset and width and/or offset
   * specified in register, respectively".
   *
   * Three further facts support it. No group header on page 10-16 references
   * note `d` -- they carry `a,b`, `a,c` and `a` -- so `d` exists solely for the
   * `Dn` row, which is exactly where a selector note belongs. The `MC68040
   * Designer's Handbook` summarises §10.6 without the per-instruction notes.
   * And neither official errata document (`MC68040UMAD`, `MC68040UMAD2`)
   * mentions §10.6.
   *
   * So the letter is wrong in the manual and the modelling follows page 10-15.
   * This test exists to record that decision where a future reader will find
   * it. */
  const ap_m68040_iu_cell_t bfffo = at("BFFFO", AP_M68040_IU_DN);
  TEST_ASSERT_EQUAL_INT(AP_M68040_IU_ALTERNATE_BITFIELD_OPERAND,
                        bfffo.alternate);
  TEST_ASSERT_EQUAL_UINT(3u, ap_m68040_iu_calculate(bfffo, false, AP_M68040_IU_NO_CONDITIONS));
  TEST_ASSERT_EQUAL_UINT(4u, ap_m68040_iu_calculate(bfffo, true, AP_M68040_IU_NO_CONDITIONS));
  TEST_ASSERT_EQUAL_UINT(
      6u, ap_m68040_execute_total(ap_m68040_iu_execute(bfffo, false, AP_M68040_IU_NO_CONDITIONS)));
  TEST_ASSERT_EQUAL_UINT(
      7u, ap_m68040_execute_total(ap_m68040_iu_execute(bfffo, true, AP_M68040_IU_NO_CONDITIONS)));

  /* And the figures match page 10-15's for the same operand shape. */
  const ap_m68040_iu_cell_t bfchg = at("BFCHG", AP_M68040_IU_DN);
  TEST_ASSERT_EQUAL_UINT(ap_m68040_iu_calculate(bfchg, false, AP_M68040_IU_NO_CONDITIONS),
                         ap_m68040_iu_calculate(bfffo, false, AP_M68040_IU_NO_CONDITIONS));
  TEST_ASSERT_EQUAL_UINT(
      ap_m68040_execute_total(ap_m68040_iu_execute(bfchg, false, AP_M68040_IU_NO_CONDITIONS)),
      ap_m68040_execute_total(ap_m68040_iu_execute(bfffo, false, AP_M68040_IU_NO_CONDITIONS)));
}

static void test_a_register_operand_never_pays_the_boundary_penalty(void) {
  /* The corollary of the reading above: whatever the note letters say, a `Dn`
   * operand cannot straddle a long word, so asking for the penalty there must
   * not change the answer. This is the assertion that would have caught the
   * mistake had I taken note `d` at face value. */
  const char *const bitfield[] = {"BFCHG", "BFEXTS", "BFFFO", "BFINS",
                                  "BFTST"};
  for (unsigned i = 0; i < sizeof bitfield / sizeof bitfield[0]; i++) {
    const ap_m68040_iu_cell_t c = at(bitfield[i], AP_M68040_IU_DN);
    TEST_ASSERT_EQUAL_UINT(0u, c.penalty[0].calculate);
    TEST_ASSERT_EQUAL_UINT(0u, c.penalty[0].execute);
  }
}

static void test_the_three_bit_field_boundary_penalties_differ(void) {
  /* Each group's header names its own note, and the three penalties are
   * genuinely different amounts of work:
   *
   *   BFCHG/BFCLR/BFSET   +10 calculate, +9 execute   read and write two words
   *   BFINS               +7 both                     write two words
   *   BFFFO               +2 execute                  read two words
   *   BFTST               none                        header carries note a only
   *
   * A single shared penalty would be wrong for four of the five groups. */
  TEST_ASSERT_EQUAL_UINT(
      10u, at("BFCHG", AP_M68040_IU_INDIRECT).penalty[0].calculate);
  TEST_ASSERT_EQUAL_UINT(
      9u, at("BFCHG", AP_M68040_IU_INDIRECT).penalty[0].execute);

  TEST_ASSERT_EQUAL_UINT(
      7u, at("BFINS", AP_M68040_IU_INDIRECT).penalty[0].calculate);
  TEST_ASSERT_EQUAL_UINT(
      7u, at("BFINS", AP_M68040_IU_INDIRECT).penalty[0].execute);

  TEST_ASSERT_EQUAL_UINT(
      0u, at("BFFFO", AP_M68040_IU_INDIRECT).penalty[0].calculate);
  TEST_ASSERT_EQUAL_UINT(
      2u, at("BFFFO", AP_M68040_IU_INDIRECT).penalty[0].execute);

  /* `BFTST`'s header carries note `a` alone -- no boundary note at all. */
  TEST_ASSERT_EQUAL_UINT(
      0u, at("BFTST", AP_M68040_IU_INDIRECT).penalty[0].execute);
}

static void test_the_writing_bit_field_instructions_reject_pc_relative(void) {
  /* `BFINS` writes a field, so it has no PC-relative mode, exactly as `BFCHG`
   * has none -- while `BFFFO` and `BFTST` read and do. The pattern established
   * on page 10-15 holds on page 10-16, which is a check that the columns were
   * not transposed. */
  TEST_ASSERT_FALSE(at("BFINS", AP_M68040_IU_PC_DISPLACEMENT).valid);
  TEST_ASSERT_FALSE(at("BFINS", AP_M68040_IU_PC_INDEXED).valid);
  TEST_ASSERT_TRUE(at("BFFFO", AP_M68040_IU_PC_DISPLACEMENT).valid);
  TEST_ASSERT_TRUE(at("BFTST", AP_M68040_IU_PC_DISPLACEMENT).valid);
}


/* ---------------------------------------------------------------------------
 * Page 10-17: BTST, CAS and CHK.
 * ------------------------------------------------------------------------- */

static void test_btst_reads_and_so_accepts_pc_relative(void) {
  /* The read/write pattern again, and the cleanest instance of it: `BTST` only
   * tests a bit, so it takes the PC-relative modes that `BCHG`, `BCLR` and
   * `BSET` -- which all write one back -- are denied. Same bit, same
   * addressing, different protection. */
  TEST_ASSERT_TRUE(at("BTST", AP_M68040_IU_PC_DISPLACEMENT).valid);
  TEST_ASSERT_TRUE(at("BTST", AP_M68040_IU_PC_INDEXED).valid);
  TEST_ASSERT_FALSE(at("BCHG", AP_M68040_IU_PC_DISPLACEMENT).valid);
  TEST_ASSERT_FALSE(at("BSET", AP_M68040_IU_PC_INDEXED).valid);
}

static void test_btst_is_cheaper_than_the_bit_changing_instructions(void) {
  /* `BTST (An)` is `1/2` where `BCHG (An)` is `3/4` -- a test needs no
   * read-modify-write. */
  TEST_ASSERT_EQUAL_UINT(
      1u, ap_m68040_execute_total(
              ap_m68040_iu_execute(at("BTST", AP_M68040_IU_INDIRECT), false,
                                   AP_M68040_IU_NO_CONDITIONS)));
  TEST_ASSERT_EQUAL_UINT(
      3u, ap_m68040_execute_total(
              ap_m68040_iu_execute(at("BCHG", AP_M68040_IU_INDIRECT), false,
                                   AP_M68040_IU_NO_CONDITIONS)));
}

static void test_btst_indexed_modes_have_a_dual_calculate_too(void) {
  /* `BTST (BR,Xn)` prints `7/6` for calculate: a `Dn` bit number is cheaper to
   * calculate here, exactly as it is for `BCHG (d16,An)`. The dual calculate
   * is not confined to one row of one column. */
  const ap_m68040_iu_cell_t c = at("BTST", AP_M68040_IU_BASE_INDEXED);
  TEST_ASSERT_EQUAL_UINT(7u, ap_m68040_iu_calculate(c, false, AP_M68040_IU_NO_CONDITIONS));
  TEST_ASSERT_EQUAL_UINT(6u, ap_m68040_iu_calculate(c, true, AP_M68040_IU_NO_CONDITIONS));
}

static void test_cas_is_typical_rather_than_exact(void) {
  /* Note b: "times listed are typical. This instruction interlocks the <ea>
   * calculate and execute stages and synchronizes some portions of the
   * processor before execution." A read-modify-write that has to synchronise
   * cannot have one figure, so the column is marked rather than trusted. */
  const ap_m68040_iu_group_t *cas = ap_m68040_iu_find("CAS");
  TEST_ASSERT_NOT_NULL(cas);
  TEST_ASSERT_EQUAL_INT(AP_M68040_IU_FIGURE_TYPICAL, cas->confidence);
}

static void test_cas_is_the_most_expensive_column_so_far(void) {
  /* 36 clocks to calculate and `6L + 31` to execute for `CAS (An)`, against one
   * and one for `ADD (An)`. The indivisible read-modify-write is not a variant
   * of an ordinary access -- it is two orders of magnitude of work. */
  const ap_m68040_iu_cell_t c = at("CAS", AP_M68040_IU_INDIRECT);
  TEST_ASSERT_EQUAL_UINT(36u, ap_m68040_iu_calculate(c, false, AP_M68040_IU_NO_CONDITIONS));
  TEST_ASSERT_EQUAL_UINT(6u, c.execute.lead);
  TEST_ASSERT_EQUAL_UINT(31u, c.execute.base);
}

static void test_cas_rejects_every_non_alterable_mode(void) {
  /* `CAS` writes back, so no PC-relative and no immediate -- and no register
   * modes either, since the operand must be in memory for the access to be
   * indivisible. */
  TEST_ASSERT_FALSE(at("CAS", AP_M68040_IU_DN).valid);
  TEST_ASSERT_FALSE(at("CAS", AP_M68040_IU_AN).valid);
  TEST_ASSERT_FALSE(at("CAS", AP_M68040_IU_PC_DISPLACEMENT).valid);
  TEST_ASSERT_FALSE(at("CAS", AP_M68040_IU_PC_INDEXED).valid);
  TEST_ASSERT_FALSE(at("CAS", AP_M68040_IU_IMMEDIATE).valid);
}

static void test_chk_figures_assume_the_check_passes(void) {
  /* Note d: "times listed are for Dn within bounds." So this column prices the
   * case that does *not* trap. A failing check takes an exception whose cost is
   * §10.5's, and adding these figures to that would double-count the operand
   * fetch -- which is why the confidence is recorded rather than the column
   * being treated as a lower bound. */
  const ap_m68040_iu_group_t *chk = ap_m68040_iu_find("CHK");
  TEST_ASSERT_NOT_NULL(chk);
  TEST_ASSERT_EQUAL_INT(AP_M68040_IU_FIGURE_WITHIN_BOUNDS, chk->confidence);
}

static void test_chk_accepts_an_immediate_bound(void) {
  /* `CHK #<xxx>,Dn` is the common form -- a compile-time array bound -- and it
   * costs the same 8 and `1L + 7` as the register form, since neither touches
   * memory. */
  const ap_m68040_iu_cell_t imm = at("CHK", AP_M68040_IU_IMMEDIATE);
  const ap_m68040_iu_cell_t reg = at("CHK", AP_M68040_IU_DN);
  TEST_ASSERT_TRUE(imm.valid);
  TEST_ASSERT_EQUAL_UINT(ap_m68040_iu_calculate(reg, false, AP_M68040_IU_NO_CONDITIONS),
                         ap_m68040_iu_calculate(imm, false, AP_M68040_IU_NO_CONDITIONS));
  TEST_ASSERT_EQUAL_UINT(
      ap_m68040_execute_total(ap_m68040_iu_execute(reg, false, AP_M68040_IU_NO_CONDITIONS)),
      ap_m68040_execute_total(ap_m68040_iu_execute(imm, false, AP_M68040_IU_NO_CONDITIONS)));
}

static void test_only_the_qualified_columns_are_marked(void) {
  /* Everything transcribed so far is exact except `CAS`, `CHK` and `CHK2`, and
   * a column that lost its marking would report a typical figure as a fact. */
  const char *const qualified_names[] = {"CAS", "CHK", "CHK2", "CMP2"};
  for (size_t g = 0; g < ap_m68040_iu_group_count(); g++) {
    const ap_m68040_iu_group_t *group = ap_m68040_iu_group(g);
    bool qualified = false;
    for (unsigned i = 0;
         i < sizeof qualified_names / sizeof qualified_names[0]; i++) {
      if (ap_m68040_iu_find(qualified_names[i]) == group) {
        qualified = true;
      }
    }
    TEST_ASSERT_EQUAL_INT(qualified,
                          group->confidence != AP_M68040_IU_FIGURE_EXACT);
  }
}


/* ---------------------------------------------------------------------------
 * Page 10-18: CHK2's two conditions.
 * ------------------------------------------------------------------------- */

static void test_chk2_carries_two_independent_penalties(void) {
  /* Its footnote: "timing for Dn within bounds, UB > LB. For UB < LB, add three
   * clocks to <ea> calculate and execute times. For Rn = An, add one clock to
   * <ea> calculate and execute times."
   *
   * Two conditions, neither derivable from the encoding alone -- `UB < LB` is a
   * relation between two operands in memory, and both can hold at once. This is
   * why penalties are a tagged list rather than one pair of numbers. */
  const ap_m68040_iu_cell_t c = at("CHK2", AP_M68040_IU_INDIRECT);
  TEST_ASSERT_EQUAL_UINT(11u,
                         ap_m68040_iu_calculate(c, false,
                                                AP_M68040_IU_NO_CONDITIONS));

  const ap_m68040_iu_condition_t reversed[] = {
      AP_M68040_IU_CONDITION_BOUNDS_REVERSED};
  TEST_ASSERT_EQUAL_UINT(14u,
                         ap_m68040_iu_calculate(c, false, reversed, 1u));

  const ap_m68040_iu_condition_t an[] = {
      AP_M68040_IU_CONDITION_ADDRESS_REGISTER};
  TEST_ASSERT_EQUAL_UINT(12u, ap_m68040_iu_calculate(c, false, an, 1u));

  /* Both at once: three plus one. */
  const ap_m68040_iu_condition_t both[] = {
      AP_M68040_IU_CONDITION_BOUNDS_REVERSED,
      AP_M68040_IU_CONDITION_ADDRESS_REGISTER};
  TEST_ASSERT_EQUAL_UINT(15u, ap_m68040_iu_calculate(c, false, both, 2u));
  TEST_ASSERT_EQUAL_UINT(
      9u + 4u,
      ap_m68040_iu_execute(c, false, both, 2u).base);
}

static void test_an_unrelated_condition_changes_nothing(void) {
  /* A caller may pass every condition it knows about; only the ones a cell
   * names take effect. That is what lets one scheduler call this for every
   * instruction without knowing which conditions apply to which. */
  const ap_m68040_iu_cell_t c = at("CHK2", AP_M68040_IU_INDIRECT);
  const ap_m68040_iu_condition_t irrelevant[] = {
      AP_M68040_IU_CONDITION_SPANS_LONG_WORD};
  TEST_ASSERT_EQUAL_UINT(
      ap_m68040_iu_calculate(c, false, AP_M68040_IU_NO_CONDITIONS),
      ap_m68040_iu_calculate(c, false, irrelevant, 1u));
}

static void test_chk2_needs_a_memory_operand(void) {
  /* `CHK2` compares against a bound *pair* held in memory, so unlike `CHK` it
   * has no register or immediate form at all. */
  TEST_ASSERT_FALSE(at("CHK2", AP_M68040_IU_DN).valid);
  TEST_ASSERT_FALSE(at("CHK2", AP_M68040_IU_IMMEDIATE).valid);
  TEST_ASSERT_TRUE(at("CHK", AP_M68040_IU_DN).valid);
  TEST_ASSERT_TRUE(at("CHK", AP_M68040_IU_IMMEDIATE).valid);
}

static void test_clr_and_cmp_differ_only_where_reading_matters(void) {
  /* `CLR` writes a zero and never reads, so it has no PC-relative, no
   * immediate and no `An` form; `CMP` reads both operands and has all three.
   * Where both are valid the figures are identical -- the cost is the
   * addressing, not the operation. */
  TEST_ASSERT_FALSE(at("CLR", AP_M68040_IU_PC_DISPLACEMENT).valid);
  TEST_ASSERT_FALSE(at("CLR", AP_M68040_IU_IMMEDIATE).valid);
  TEST_ASSERT_FALSE(at("CLR", AP_M68040_IU_AN).valid);
  TEST_ASSERT_TRUE(at("CMP", AP_M68040_IU_PC_DISPLACEMENT).valid);
  TEST_ASSERT_TRUE(at("CMP", AP_M68040_IU_IMMEDIATE).valid);
  TEST_ASSERT_TRUE(at("CMP", AP_M68040_IU_AN).valid);

  for (unsigned m = 0; m < AP_M68040_IU_MODE_COUNT; m++) {
    const ap_m68040_iu_cell_t clr = at("CLR", (ap_m68040_iu_mode_t)m);
    const ap_m68040_iu_cell_t cmp = at("CMP", (ap_m68040_iu_mode_t)m);
    if (!clr.valid || !cmp.valid) {
      continue;
    }
    TEST_ASSERT_EQUAL_UINT(
        ap_m68040_iu_calculate(cmp, false, AP_M68040_IU_NO_CONDITIONS),
        ap_m68040_iu_calculate(clr, false, AP_M68040_IU_NO_CONDITIONS));
  }
}


/* ---------------------------------------------------------------------------
 * Page 10-19, and a cell that prints a cost where no instruction exists.
 * ------------------------------------------------------------------------- */

static void test_cmp2_rejects_the_incrementing_modes_despite_printing_zero(void) {
  /* §10.6 prints **`0`** -- not a dash -- for `CMP2 (An)+` and `CMP2 -(An)`,
   * in both columns. Read at 500 dpi to be sure it is a zero and not a dash
   * rendered thinly. `CHK2`'s column on the previous page dashes the same two
   * rows, so the section contradicts itself.
   *
   * The `M68000 Family Programmer's Reference Manual` settles it twice over.
   * `CHK2`'s effective address field takes "only control addressing modes",
   * and its table dashes `(An)+` and `-(An)` explicitly. And of `CMP2` it says:
   * "this instruction is identical to CHK2 except that it sets condition codes
   * rather than taking an exception when the value in Rn is out of bounds" --
   * so the two have the same addressing modes by definition.
   *
   * A third argument needs no source at all: no valid cell anywhere in §10.6
   * costs zero clocks, because an instruction that executes takes at least one.
   *
   * So the `0` means "not applicable" and is modelled as invalid. */
  TEST_ASSERT_FALSE(at("CMP2", AP_M68040_IU_POSTINCREMENT).valid);
  TEST_ASSERT_FALSE(at("CMP2", AP_M68040_IU_PREDECREMENT).valid);

  /* And exactly as `CHK2` has them. */
  TEST_ASSERT_FALSE(at("CHK2", AP_M68040_IU_POSTINCREMENT).valid);
  TEST_ASSERT_FALSE(at("CHK2", AP_M68040_IU_PREDECREMENT).valid);
}

static void test_cmp2_and_chk2_accept_exactly_the_same_modes(void) {
  /* The PRM's "identical to CHK2" is a strong claim, and this is the check
   * that the two columns were transcribed consistently with it. If a later
   * page were misread, the two would diverge here before anything else caught
   * it. */
  for (unsigned m = 0; m < AP_M68040_IU_MODE_COUNT; m++) {
    TEST_ASSERT_EQUAL_INT(at("CHK2", (ap_m68040_iu_mode_t)m).valid,
                          at("CMP2", (ap_m68040_iu_mode_t)m).valid);
  }
}

static void test_cmp2_costs_more_than_chk2_for_the_same_addressing(void) {
  /* Both read a bound pair; `CMP2` then sets condition codes where `CHK2` may
   * trap. The condition-code work is what makes `CMP2` dearer -- 13 against 11
   * to calculate `(An)` -- which is the opposite of the intuition that a
   * trapping instruction must cost more. */
  TEST_ASSERT_EQUAL_UINT(
      11u, ap_m68040_iu_calculate(at("CHK2", AP_M68040_IU_INDIRECT), false,
                                  AP_M68040_IU_NO_CONDITIONS));
  TEST_ASSERT_EQUAL_UINT(
      13u, ap_m68040_iu_calculate(at("CMP2", AP_M68040_IU_INDIRECT), false,
                                  AP_M68040_IU_NO_CONDITIONS));
}

static void test_cmpi_reads_and_so_takes_pc_relative(void) {
  /* `CMPI #<xxx>,<ea>` only reads its destination, so unlike the other
   * immediate forms -- `ADDI`, `ANDI` and the rest, which write back -- it
   * accepts the PC-relative modes. The `ADDI` group dashes them. */
  TEST_ASSERT_TRUE(at("CMPI", AP_M68040_IU_PC_DISPLACEMENT).valid);
  TEST_ASSERT_TRUE(at("CMPI", AP_M68040_IU_PC_INDEXED).valid);
  TEST_ASSERT_FALSE(at("ADDI", AP_M68040_IU_PC_DISPLACEMENT).valid);
}

static void test_cmpa_is_never_cheaper_than_cmp(void) {
  /* I first assumed `CMPA` costs the *same* as `CMP`, reasoning that a
   * comparison discards its result and so needs no sign extension written
   * back. The table refutes that at `(An)+`, `-(An)` and `(d16,An)`, where
   * `CMPA` is one clock dearer -- the column is headed `CMPA.L`, so it always
   * reads a long word where `CMP` may read a word.
   *
   * That is the second time a plausible "the A form costs the same plus a
   * constant" invariant has failed here, after `ADDA`. The weak statement is
   * the true one, and the specific differences are pinned below. */
  for (unsigned m = 0; m < AP_M68040_IU_MODE_COUNT; m++) {
    const ap_m68040_iu_cell_t cmp = at("CMP", (ap_m68040_iu_mode_t)m);
    const ap_m68040_iu_cell_t cmpa = at("CMPA", (ap_m68040_iu_mode_t)m);
    if (!cmp.valid || !cmpa.valid) {
      continue;
    }
    TEST_ASSERT_TRUE(
        ap_m68040_execute_total(
            ap_m68040_iu_execute(cmpa, false, AP_M68040_IU_NO_CONDITIONS)) >=
        ap_m68040_execute_total(
            ap_m68040_iu_execute(cmp, false, AP_M68040_IU_NO_CONDITIONS)));
  }
}

static void test_where_cmpa_costs_more_than_cmp(void) {
  /* Exactly the three modes whose operand address has to be computed or
   * updated: postincrement, predecrement and displacement. Register direct,
   * plain indirect, absolute and immediate all cost the same in both. */
  const ap_m68040_iu_mode_t dearer[] = {AP_M68040_IU_POSTINCREMENT,
                                        AP_M68040_IU_PREDECREMENT,
                                        AP_M68040_IU_DISPLACEMENT};
  for (unsigned i = 0; i < 3u; i++) {
    TEST_ASSERT_EQUAL_UINT(
        1u, ap_m68040_execute_total(ap_m68040_iu_execute(
                at("CMP", dearer[i]), false, AP_M68040_IU_NO_CONDITIONS)));
    TEST_ASSERT_EQUAL_UINT(
        2u, ap_m68040_execute_total(ap_m68040_iu_execute(
                at("CMPA", dearer[i]), false, AP_M68040_IU_NO_CONDITIONS)));
  }

  const ap_m68040_iu_mode_t same[] = {AP_M68040_IU_DN, AP_M68040_IU_AN,
                                      AP_M68040_IU_INDIRECT,
                                      AP_M68040_IU_ABSOLUTE,
                                      AP_M68040_IU_IMMEDIATE};
  for (unsigned i = 0; i < 5u; i++) {
    TEST_ASSERT_EQUAL_UINT(
        ap_m68040_execute_total(ap_m68040_iu_execute(
            at("CMP", same[i]), false, AP_M68040_IU_NO_CONDITIONS)),
        ap_m68040_execute_total(ap_m68040_iu_execute(
            at("CMPA", same[i]), false, AP_M68040_IU_NO_CONDITIONS)));
  }
}


/* ---------------------------------------------------------------------------
 * Page 10-20: the divides, and a cost that replaces rather than adds.
 * ------------------------------------------------------------------------- */

static void test_the_manuals_divide_by_zero_worked_example(void) {
  /* "Execution time for a DIV/0 exception taken and exception processing is
   * approximately 16 + <ea> calculate clocks. For example, DIV.W #0,Dn takes
   * approximately 24 clocks in both the <ea> calculate and execute times."
   *
   * `DIV.W`'s immediate `<ea>` calculate is 8, and 16 + 8 = 24 exactly. The
   * worked example is the check that the formula was read the right way round
   * -- it would not come out even if the 16 were added to the *execute*
   * figure, which is 27. */
  const ap_m68040_iu_group_t *divw = ap_m68040_iu_find("DIVS.W");
  TEST_ASSERT_NOT_NULL(divw);
  TEST_ASSERT_EQUAL_UINT(
      8u, ap_m68040_iu_calculate(at("DIVS.W", AP_M68040_IU_IMMEDIATE), false,
                                 AP_M68040_IU_NO_CONDITIONS));
  TEST_ASSERT_EQUAL_UINT(
      24u, ap_m68040_iu_zero_divide_clocks(divw, AP_M68040_IU_IMMEDIATE));
}

static void test_a_divide_by_zero_replaces_the_figure_rather_than_adding(void) {
  /* The trap: 24 is *less* than the 27 a completed `DIV.W` costs, because the
   * division never happens. Modelling this as a penalty added to the normal
   * time would give 51 -- more than twice the truth, and in the wrong
   * direction. */
  const ap_m68040_iu_group_t *divw = ap_m68040_iu_find("DIVS.W");
  const unsigned normal = ap_m68040_execute_total(ap_m68040_iu_execute(
      at("DIVS.W", AP_M68040_IU_IMMEDIATE), false, AP_M68040_IU_NO_CONDITIONS));
  TEST_ASSERT_EQUAL_UINT(27u, normal);
  TEST_ASSERT_TRUE(
      ap_m68040_iu_zero_divide_clocks(divw, AP_M68040_IU_IMMEDIATE) < normal);
}

static void test_only_the_divide_columns_can_divide_by_zero(void) {
  /* Every other column returns zero, which is how a caller can ask
   * unconditionally. */
  TEST_ASSERT_EQUAL_UINT(0u, ap_m68040_iu_zero_divide_clocks(
                                 ap_m68040_iu_find("ADD"), AP_M68040_IU_DN));
  TEST_ASSERT_EQUAL_UINT(0u, ap_m68040_iu_zero_divide_clocks(
                                 ap_m68040_iu_find("JMP"), AP_M68040_IU_INDIRECT));
  TEST_ASSERT_TRUE(ap_m68040_iu_zero_divide_clocks(
                       ap_m68040_iu_find("DIVU.L"), AP_M68040_IU_DN) > 0u);
}

static void test_an_invalid_mode_has_no_divide_by_zero_cost(void) {
  /* `DIVS.W An` is a dash, so there is no instruction to trap. */
  TEST_ASSERT_EQUAL_UINT(0u, ap_m68040_iu_zero_divide_clocks(
                                 ap_m68040_iu_find("DIVS.W"), AP_M68040_IU_AN));
}

static void test_a_long_divide_costs_far_more_than_a_word_divide(void) {
  /* 44 clocks against 27 -- the divides are the first columns where the
   * *execute* stage dominates everything else in the instruction, and the
   * long form is not merely a little dearer. */
  TEST_ASSERT_EQUAL_UINT(
      27u, ap_m68040_execute_total(ap_m68040_iu_execute(
               at("DIVS.W", AP_M68040_IU_DN), false, AP_M68040_IU_NO_CONDITIONS)));
  TEST_ASSERT_EQUAL_UINT(
      44u, ap_m68040_execute_total(ap_m68040_iu_execute(
               at("DIVS.L", AP_M68040_IU_DN), false, AP_M68040_IU_NO_CONDITIONS)));
}

static void test_the_signed_and_unsigned_divides_share_a_column(void) {
  /* `DIVS` and `DIVU` cost the same, so the sign of the operands is free --
   * and all four long forms share one column, including the `SL`/`UL` variants
   * that produce a remainder. */
  TEST_ASSERT_EQUAL_PTR(ap_m68040_iu_find("DIVS.W"),
                        ap_m68040_iu_find("DIVU.W"));
  const char *const longs[] = {"DIVU.L", "DIVSL.L", "DIVUL.L"};
  for (unsigned i = 0; i < 3u; i++) {
    TEST_ASSERT_EQUAL_PTR(ap_m68040_iu_find("DIVS.L"),
                          ap_m68040_iu_find(longs[i]));
  }
}

static void test_jmp_takes_only_the_control_modes(void) {
  /* A jump needs an address, not a value, so every mode that would fetch an
   * operand is dashed -- including `Dn`, `#<xxx>` and both incrementing forms.
   * `JMP (An)` is the whole point of the instruction. */
  TEST_ASSERT_TRUE(at("JMP", AP_M68040_IU_INDIRECT).valid);
  TEST_ASSERT_TRUE(at("JMP", AP_M68040_IU_PC_DISPLACEMENT).valid);
  TEST_ASSERT_FALSE(at("JMP", AP_M68040_IU_DN).valid);
  TEST_ASSERT_FALSE(at("JMP", AP_M68040_IU_AN).valid);
  TEST_ASSERT_FALSE(at("JMP", AP_M68040_IU_IMMEDIATE).valid);
  TEST_ASSERT_FALSE(at("JMP", AP_M68040_IU_POSTINCREMENT).valid);
  TEST_ASSERT_FALSE(at("JMP", AP_M68040_IU_PREDECREMENT).valid);
}

static void test_jmp_carries_a_large_lead_for_a_small_base(void) {
  /* `JMP (d16,PC)` is `5L + 1`: five clocks of stall tolerance against one of
   * work. A change of flow has almost nothing to execute and everything to
   * wait for, which is the clearest case in §10.6 of why the lead is carried
   * separately rather than folded into a total. */
  const ap_m68040_iu_cell_t c = at("JMP", AP_M68040_IU_PC_DISPLACEMENT);
  TEST_ASSERT_EQUAL_UINT(5u, c.execute.lead);
  TEST_ASSERT_EQUAL_UINT(1u, c.execute.base);
}


/* ---------------------------------------------------------------------------
 * Page 10-21, and an anomaly transcribed rather than corrected.
 * ------------------------------------------------------------------------- */

static void test_jmp_repeats_a_row_where_jsr_increments(void) {
  /* `JMP` prints the *same* figures for `([bd,BR,Xn])` and `([bd,BR,Xn],od)`
   * -- 12 and `1L + 11` for both -- where every other column in §10.6
   * increments between those two rows, and where `JSR`, which computes the
   * identical effective address, goes 12 to 13. Read at 450 dpi.
   *
   * This is transcribed as printed and **not** corrected, which is the
   * difference between it and the `CMP2` zeros. There the `M68000 Family
   * Programmer's Reference Manual` proved the addressing mode does not exist,
   * so the cell could not be a cost. Here the figure is merely *surprising*:
   * `JMP` has nothing to do once it has an address, so the outer
   * displacement's extra add could genuinely be absorbed by the pipeline, and
   * no source says otherwise.
   *
   * Recorded so that a later reader meets the anomaly with the reasoning
   * attached rather than assuming a typo -- and so that if evidence ever turns
   * up, this test is where it lands. */
  const ap_m68040_iu_cell_t plain = at("JMP", AP_M68040_IU_MEMORY_PREINDEXED);
  const ap_m68040_iu_cell_t with_od =
      at("JMP", AP_M68040_IU_MEMORY_PREINDEXED_OD);
  TEST_ASSERT_EQUAL_UINT(12u, ap_m68040_iu_calculate(
                                  plain, false, AP_M68040_IU_NO_CONDITIONS));
  TEST_ASSERT_EQUAL_UINT(12u, ap_m68040_iu_calculate(
                                  with_od, false, AP_M68040_IU_NO_CONDITIONS));
  TEST_ASSERT_EQUAL_UINT(11u, plain.execute.base);
  TEST_ASSERT_EQUAL_UINT(11u, with_od.execute.base);

  /* `JSR` over the same two modes, for contrast. */
  TEST_ASSERT_EQUAL_UINT(
      12u, ap_m68040_iu_calculate(at("JSR", AP_M68040_IU_MEMORY_PREINDEXED),
                                  false, AP_M68040_IU_NO_CONDITIONS));
  TEST_ASSERT_EQUAL_UINT(
      13u, ap_m68040_iu_calculate(at("JSR", AP_M68040_IU_MEMORY_PREINDEXED_OD),
                                  false, AP_M68040_IU_NO_CONDITIONS));
}

static void test_jsr_and_jmp_accept_the_same_modes(void) {
  /* Both need an address rather than a value, so both take only the control
   * modes. A divergence here would mean one of the two columns was misread. */
  for (unsigned m = 0; m < AP_M68040_IU_MODE_COUNT; m++) {
    TEST_ASSERT_EQUAL_INT(at("JMP", (ap_m68040_iu_mode_t)m).valid,
                          at("JSR", (ap_m68040_iu_mode_t)m).valid);
  }
}

static void test_jsr_costs_no_more_than_jmp_for_simple_modes(void) {
  /* The surprise of this page: `JSR (An)` costs exactly what `JMP (An)` does,
   * 3 and `2L + 1`, despite pushing a return address. The push happens in the
   * write-back stage, which §10.1 says is "not listed because [it is] system
   * dependent and [does] not affect either <ea> calculate or execute stages" --
   * so the subroutine call really is free in the two stages this table
   * prices. */
  const ap_m68040_iu_mode_t simple[] = {AP_M68040_IU_INDIRECT,
                                        AP_M68040_IU_DISPLACEMENT,
                                        AP_M68040_IU_ABSOLUTE};
  for (unsigned i = 0; i < 3u; i++) {
    TEST_ASSERT_EQUAL_UINT(
        ap_m68040_iu_calculate(at("JMP", simple[i]), false,
                               AP_M68040_IU_NO_CONDITIONS),
        ap_m68040_iu_calculate(at("JSR", simple[i]), false,
                               AP_M68040_IU_NO_CONDITIONS));
  }
}

static void test_lea_computes_an_address_without_fetching_it(void) {
  /* `LEA (An)` is 1 and 1 where `JMP (An)` is 3 and `2L + 1`: both form an
   * address and only one changes the flow of control. And `LEA` takes the same
   * control modes -- no `Dn`, no immediate, no incrementing forms. */
  TEST_ASSERT_EQUAL_UINT(
      1u, ap_m68040_iu_calculate(at("LEA", AP_M68040_IU_INDIRECT), false,
                                 AP_M68040_IU_NO_CONDITIONS));
  TEST_ASSERT_FALSE(at("LEA", AP_M68040_IU_DN).valid);
  TEST_ASSERT_FALSE(at("LEA", AP_M68040_IU_IMMEDIATE).valid);
  TEST_ASSERT_FALSE(at("LEA", AP_M68040_IU_POSTINCREMENT).valid);
}

static void test_move_from_ccr_writes_and_so_refuses_pc_relative(void) {
  /* It stores the condition codes to its destination, so the PC-relative modes
   * are dashed -- the same read/write pattern that has now held on five
   * consecutive pages. */
  TEST_ASSERT_FALSE(at("MOVE from CCR", AP_M68040_IU_PC_DISPLACEMENT).valid);
  TEST_ASSERT_FALSE(at("MOVE from CCR", AP_M68040_IU_PC_INDEXED).valid);
  TEST_ASSERT_FALSE(at("MOVE from CCR", AP_M68040_IU_IMMEDIATE).valid);
  TEST_ASSERT_TRUE(at("MOVE from CCR", AP_M68040_IU_DN).valid);
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
  RUN_TEST(test_only_the_bit_field_groups_span_a_long_word);
  RUN_TEST(test_the_bit_field_groups_reject_the_incrementing_modes);
  RUN_TEST(test_only_the_reading_bit_field_group_allows_pc_relative);
  RUN_TEST(test_the_dn_row_selector_is_read_as_page_10_15_prints_it);
  RUN_TEST(test_a_register_operand_never_pays_the_boundary_penalty);
  RUN_TEST(test_the_three_bit_field_boundary_penalties_differ);
  RUN_TEST(test_the_writing_bit_field_instructions_reject_pc_relative);
  RUN_TEST(test_btst_reads_and_so_accepts_pc_relative);
  RUN_TEST(test_btst_is_cheaper_than_the_bit_changing_instructions);
  RUN_TEST(test_btst_indexed_modes_have_a_dual_calculate_too);
  RUN_TEST(test_cas_is_typical_rather_than_exact);
  RUN_TEST(test_cas_is_the_most_expensive_column_so_far);
  RUN_TEST(test_cas_rejects_every_non_alterable_mode);
  RUN_TEST(test_chk_figures_assume_the_check_passes);
  RUN_TEST(test_chk_accepts_an_immediate_bound);
  RUN_TEST(test_only_the_qualified_columns_are_marked);
  RUN_TEST(test_chk2_carries_two_independent_penalties);
  RUN_TEST(test_an_unrelated_condition_changes_nothing);
  RUN_TEST(test_chk2_needs_a_memory_operand);
  RUN_TEST(test_clr_and_cmp_differ_only_where_reading_matters);
  RUN_TEST(test_cmp2_rejects_the_incrementing_modes_despite_printing_zero);
  RUN_TEST(test_cmp2_and_chk2_accept_exactly_the_same_modes);
  RUN_TEST(test_cmp2_costs_more_than_chk2_for_the_same_addressing);
  RUN_TEST(test_cmpi_reads_and_so_takes_pc_relative);
  RUN_TEST(test_cmpa_is_never_cheaper_than_cmp);
  RUN_TEST(test_where_cmpa_costs_more_than_cmp);
  RUN_TEST(test_the_manuals_divide_by_zero_worked_example);
  RUN_TEST(test_a_divide_by_zero_replaces_the_figure_rather_than_adding);
  RUN_TEST(test_only_the_divide_columns_can_divide_by_zero);
  RUN_TEST(test_an_invalid_mode_has_no_divide_by_zero_cost);
  RUN_TEST(test_a_long_divide_costs_far_more_than_a_word_divide);
  RUN_TEST(test_the_signed_and_unsigned_divides_share_a_column);
  RUN_TEST(test_jmp_takes_only_the_control_modes);
  RUN_TEST(test_jmp_carries_a_large_lead_for_a_small_base);
  RUN_TEST(test_jmp_repeats_a_row_where_jsr_increments);
  RUN_TEST(test_jsr_and_jmp_accept_the_same_modes);
  RUN_TEST(test_jsr_costs_no_more_than_jmp_for_simple_modes);
  RUN_TEST(test_lea_computes_an_address_without_fetching_it);
  RUN_TEST(test_move_from_ccr_writes_and_so_refuses_pc_relative);
  return UNITY_END();
}

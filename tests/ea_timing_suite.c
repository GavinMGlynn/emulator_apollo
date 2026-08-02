/* MC68030 effective address timings, `[030]` §11.6.1 and §11.6.3.
 *
 * These are the other side of the composition the instruction tables leave
 * half-stated. As with the instruction transcription, they are checked against
 * structure rather than by re-reading: the relationships that must hold between
 * the two tables, and between rows within each.
 */

#include "cpu/m68030/ap_m68030_ea_timing.h"
#include "cpu/m68030/ap_m68030_timing_table.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* A register operand has no address to compute and nothing to read, so it costs
 * nothing in either table. That is what makes `ADD D0,D1` a complete figure
 * while `ADD D0,(A0)` is not — and it is why the register forms could be
 * transcribed and used before these tables existed at all. */
static void test_a_register_operand_costs_nothing_either_way(void) {
  const ap_m68030_ea_kind_t registers[] = {AP_M68030_EA_DATA_REGISTER,
                                           AP_M68030_EA_ADDRESS_REGISTER};
  for (unsigned i = 0; i < 2u; i++) {
    const ap_m68030_ea_timing_t *fetch =
        ap_m68030_ea_fetch_timing(registers[i], 4u);
    const ap_m68030_ea_timing_t *calculate =
        ap_m68030_ea_calculate_timing(registers[i]);
    TEST_ASSERT_NOT_NULL(fetch);
    TEST_ASSERT_NOT_NULL(calculate);
    TEST_ASSERT_EQUAL_UINT(0u, fetch->timing.cache_case);
    TEST_ASSERT_EQUAL_UINT(0u, calculate->timing.cache_case);

    /* And the table gives their head and tail as `-`, not 0: there is no
     * address computation to overlap *with*, which is a different statement
     * from an overlap of zero. */
    TEST_ASSERT_FALSE(fetch->head_applies);
    TEST_ASSERT_FALSE(calculate->head_applies);
  }
}

/* Fetching always costs at least as much as calculating, for every mode: the
 * fetch does the same address computation and then reads. A transcription with
 * the two tables swapped would break this everywhere at once. */
static void test_fetching_never_costs_less_than_calculating(void) {
  const ap_m68030_ea_kind_t modes[] = {
      AP_M68030_EA_DATA_REGISTER,   AP_M68030_EA_ADDRESS_REGISTER,
      AP_M68030_EA_ADDRESS_INDIRECT, AP_M68030_EA_POSTINCREMENT,
      AP_M68030_EA_PREDECREMENT,    AP_M68030_EA_DISPLACEMENT,
      AP_M68030_EA_ABSOLUTE_SHORT,  AP_M68030_EA_ABSOLUTE_LONG,
      AP_M68030_EA_INDEXED,
  };
  for (unsigned i = 0; i < sizeof modes / sizeof modes[0]; i++) {
    const ap_m68030_ea_timing_t *fetch = ap_m68030_ea_fetch_timing(modes[i], 4u);
    const ap_m68030_ea_timing_t *calculate =
        ap_m68030_ea_calculate_timing(modes[i]);
    TEST_ASSERT_NOT_NULL(fetch);
    TEST_ASSERT_NOT_NULL(calculate);
    TEST_ASSERT_TRUE_MESSAGE(
        fetch->timing.cache_case >= calculate->timing.cache_case, fetch->mode);
  }
}

/* The difference between the two tables for `(An)` is the operand read: 3 to
 * fetch against 2 to calculate. That single clock is the size of the error C9
 * measured writ small, and it is why an instruction's footnote — which of the
 * two tables it uses — is load-bearing rather than a formality. */
static void test_the_two_tables_differ_by_the_operand_read(void) {
  const ap_m68030_ea_timing_t *fetch =
      ap_m68030_ea_fetch_timing(AP_M68030_EA_ADDRESS_INDIRECT, 4u);
  const ap_m68030_ea_timing_t *calculate =
      ap_m68030_ea_calculate_timing(AP_M68030_EA_ADDRESS_INDIRECT);
  TEST_ASSERT_NOT_NULL(fetch);
  TEST_ASSERT_NOT_NULL(calculate);

  TEST_ASSERT_EQUAL_UINT(3u, fetch->timing.cache_case);
  TEST_ASSERT_EQUAL_UINT(2u, calculate->timing.cache_case);
}

/* "2+op head": the calculate table writes several heads as the *operation's*
 * head plus a figure, not as a constant. That is the table expressing a
 * dependency between the two halves of Equation (11-2), and a transcription
 * that flattened it to 2 would drop whatever the operation contributes.
 *
 * Not every row is relative — `(An)+` is a plain 0 — so the flag distinguishes
 * rather than applying to the whole table. */
static void test_the_calculate_heads_that_are_relative_say_so(void) {
  const ap_m68030_ea_timing_t *indirect =
      ap_m68030_ea_calculate_timing(AP_M68030_EA_ADDRESS_INDIRECT);
  const ap_m68030_ea_timing_t *postincrement =
      ap_m68030_ea_calculate_timing(AP_M68030_EA_POSTINCREMENT);
  TEST_ASSERT_NOT_NULL(indirect);
  TEST_ASSERT_NOT_NULL(postincrement);

  TEST_ASSERT_TRUE(indirect->head_adds_operation);
  TEST_ASSERT_FALSE(postincrement->head_adds_operation);

  /* The fetch table has no relative heads at all. */
  const ap_m68030_ea_timing_t *fetch =
      ap_m68030_ea_fetch_timing(AP_M68030_EA_ADDRESS_INDIRECT, 4u);
  TEST_ASSERT_NOT_NULL(fetch);
  TEST_ASSERT_FALSE(fetch->head_adds_operation);
}

/* The immediate rows are split by operand size, and byte and word cost the
 * same: Table 2-3's "Low-order byte of the extension word" means a byte
 * immediate still occupies a whole word of instruction stream. A long costs
 * twice, being two words. */
static void test_an_immediate_costs_by_the_words_it_occupies(void) {
  const ap_m68030_ea_timing_t *byte =
      ap_m68030_ea_fetch_timing(AP_M68030_EA_IMMEDIATE, 1u);
  const ap_m68030_ea_timing_t *word =
      ap_m68030_ea_fetch_timing(AP_M68030_EA_IMMEDIATE, 2u);
  const ap_m68030_ea_timing_t *long_word =
      ap_m68030_ea_fetch_timing(AP_M68030_EA_IMMEDIATE, 4u);
  TEST_ASSERT_NOT_NULL(byte);
  TEST_ASSERT_NOT_NULL(word);
  TEST_ASSERT_NOT_NULL(long_word);

  TEST_ASSERT_EQUAL_UINT(2u, byte->timing.cache_case);
  TEST_ASSERT_EQUAL_UINT(2u, word->timing.cache_case);
  TEST_ASSERT_EQUAL_UINT(4u, long_word->timing.cache_case);
}

/* There is no immediate row in the calculate table, and the lookup says so
 * rather than returning zero. An operand in the instruction stream has no
 * address to compute, so an instruction footnoted for that table cannot take
 * one — and a zero would read as "free" rather than as "not a thing". */
static void test_an_immediate_has_no_address_to_calculate(void) {
  TEST_ASSERT_NULL(ap_m68030_ea_calculate_timing(AP_M68030_EA_IMMEDIATE));
  TEST_ASSERT_NOT_NULL(ap_m68030_ea_fetch_timing(AP_M68030_EA_IMMEDIATE, 2u));
}

/* The absolute long is the one fetch row whose two columns differ, its second
 * extension word being another prefetch. Everywhere else in this table the
 * cache and no-cache cases agree, because an effective address computation is
 * long enough to hide its own fetch. */
static void test_only_the_long_absolute_differs_between_the_columns(void) {
  const ap_m68030_ea_kind_t modes[] = {
      AP_M68030_EA_ADDRESS_INDIRECT, AP_M68030_EA_POSTINCREMENT,
      AP_M68030_EA_PREDECREMENT,     AP_M68030_EA_ABSOLUTE_SHORT,
      AP_M68030_EA_INDEXED,
  };
  for (unsigned i = 0; i < sizeof modes / sizeof modes[0]; i++) {
    const ap_m68030_ea_timing_t *entry =
        ap_m68030_ea_fetch_timing(modes[i], 4u);
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(entry->timing.cache_case,
                                   entry->timing.no_cache_case, entry->mode);
  }

  const ap_m68030_ea_timing_t *absolute_long =
      ap_m68030_ea_fetch_timing(AP_M68030_EA_ABSOLUTE_LONG, 4u);
  TEST_ASSERT_NOT_NULL(absolute_long);
  TEST_ASSERT_EQUAL_UINT(4u, absolute_long->timing.cache_case);
  TEST_ASSERT_EQUAL_UINT(5u, absolute_long->timing.no_cache_case);
}

/* Every row is internally consistent by the same rule the instruction rows
 * obey: a head or tail may equal the cache case but never exceed it. */
static void test_every_row_is_internally_consistent(void) {
  const ap_m68030_ea_kind_t modes[] = {
      AP_M68030_EA_ADDRESS_INDIRECT, AP_M68030_EA_POSTINCREMENT,
      AP_M68030_EA_PREDECREMENT,     AP_M68030_EA_DISPLACEMENT,
      AP_M68030_EA_ABSOLUTE_SHORT,   AP_M68030_EA_ABSOLUTE_LONG,
      AP_M68030_EA_INDEXED,
  };
  for (unsigned i = 0; i < sizeof modes / sizeof modes[0]; i++) {
    const ap_m68030_ea_timing_t *fetch = ap_m68030_ea_fetch_timing(modes[i], 4u);
    TEST_ASSERT_NOT_NULL(fetch);
    TEST_ASSERT_TRUE_MESSAGE(ap_m68030_timing_consistent(&fetch->timing),
                             fetch->mode);
  }
}

/* ---------------------------------------------------------------------------
 * Equation (11-2), checked against the manual's own worked example.
 *
 * §11.3.4 works a five-instruction sequence, four of which need an effective
 * address time added, and prints the head, tail and cache case of every
 * component and the answer: **40 clock periods**. That total is external to
 * this project in the strongest sense available for a rule with no measurement
 * behind it — it is Motorola's arithmetic on Motorola's figures, and it is the
 * only published number that exercises (11-2) rather than (11-1).
 *
 * The components are given here **as the manual prints them in the example**,
 * not read from our tables. Feeding our transcription in would check the
 * composition against numbers this project produced, and a mistranscribed row
 * would then move both sides of the comparison together. The tables are checked
 * against the same page separately, below, which is a different claim.
 * ------------------------------------------------------------------------- */

/* Head, tail and cache case as §11.3.4 prints them. */
#define COMPONENT(h, t, cc) ((ap_m68030_timing_t){.head = (h), .tail = (t),     \
                                                  .cache_case = (cc),          \
                                                  .no_cache_case = (cc),       \
                                                  .prefetches = 0})

static void add_pair(ap_m68030_overlap_state_t *state,
                     ap_m68030_timing_t effective_address,
                     ap_m68030_timing_t operation) {
  const ap_m68030_ea_timing_t ea = {"from the worked example", effective_address,
                                    true, false};
  ap_m68030_ea_timing_compose(state, &ea, &operation);
}

static void test_the_worked_example_of_equation_11_2_comes_to_40_clocks(void) {
  ap_m68030_overlap_state_t state = ap_m68030_overlap_begin();

  /* 1. ADD.L -(A1),D1 -- fea -(An) then ADD EA,Dn. */
  add_pair(&state, COMPONENT(2, 2, 4), COMPONENT(0, 0, 2));
  /* 2. AND.L D1,([A2]) -- fea ([B]) then AND Dn,EA. */
  add_pair(&state, COMPONENT(4, 0, 10), COMPONENT(0, 1, 3));
  /* 3. MOVE.L (A6),(8,A1) -- fea (An) then MOVE Source,(d16,An). The
   *    destination's own address time is inside the MOVE row; only the source
   *    is added separately. */
  add_pair(&state, COMPONENT(1, 1, 3), COMPONENT(2, 0, 4));
  /* 4. TAS (A3)+ -- cea (An)+ then TAS Mem. The one that takes the *calculate*
   *    table, because TAS reads its operand itself. */
  add_pair(&state, COMPONENT(0, 0, 2), COMPONENT(3, 0, 12));

  /* 5. NEG D3 -- a register operand, so no effective address component at all,
   *    and the operation overlaps against `TAS Mem`'s tail. The manual writes
   *    this term as `[CCop5 - min(Hop5,Top4)]`, reaching past where an address
   *    component would have been. */
  const ap_m68030_timing_t neg = COMPONENT(2, 0, 2);
  ap_m68030_ea_timing_compose(&state, nullptr, &neg);

  TEST_ASSERT_EQUAL_UINT64(40u, ap_m68030_overlap_total(&state));
}

/* The step in that example a plausible implementation gets wrong. A register
 * operand contributes *no* component; giving it one that costs zero produces
 * the same total only when the previous tail is also zero, and silently eats
 * the overlap otherwise.
 *
 * Constructed so it is not: a previous operation with a tail of 2 against an
 * instruction with a head of 2. Composed correctly the second instruction saves
 * both clocks; with a zero-cost address component in front of it, the component
 * consumes the tail, saves nothing, and the total is 2 higher. */
static void test_a_register_operand_contributes_no_component_at_all(void) {
  const ap_m68030_timing_t first = COMPONENT(0, 2, 4);
  const ap_m68030_timing_t second = COMPONENT(2, 0, 4);

  ap_m68030_overlap_state_t correct = ap_m68030_overlap_begin();
  ap_m68030_ea_timing_compose(&correct, nullptr, &first);
  ap_m68030_ea_timing_compose(&correct, nullptr, &second);
  TEST_ASSERT_EQUAL_UINT64(6u, ap_m68030_overlap_total(&correct));

  /* The register row from our own table, which the tables write as `-` and not
   * as 0, must behave the same way as passing no row at all. */
  ap_m68030_overlap_state_t through_the_table = ap_m68030_overlap_begin();
  const ap_m68030_ea_timing_t *reg =
      ap_m68030_ea_fetch_timing(AP_M68030_EA_DATA_REGISTER, 4u);
  TEST_ASSERT_NOT_NULL(reg);
  ap_m68030_ea_timing_compose(&through_the_table, reg, &first);
  ap_m68030_ea_timing_compose(&through_the_table, reg, &second);
  TEST_ASSERT_EQUAL_UINT64(6u, ap_m68030_overlap_total(&through_the_table));
}

/* "2+op head" resolved. The calculate table gives `(An)` a head of 2 plus the
 * operation's own, so composing it with `TAS Mem` -- head 3 in the worked
 * example -- gives 5, and with a headless operation gives 2. A model that read
 * the bare 2 would offer three clocks less overlap to whatever precedes it. */
static void test_a_relative_head_resolves_against_its_operation(void) {
  const ap_m68030_ea_timing_t *calculate =
      ap_m68030_ea_calculate_timing(AP_M68030_EA_ADDRESS_INDIRECT);
  TEST_ASSERT_NOT_NULL(calculate);
  TEST_ASSERT_TRUE(calculate->head_adds_operation);

  TEST_ASSERT_EQUAL_UINT(5u, ap_m68030_ea_timing_head(calculate, 3u));
  TEST_ASSERT_EQUAL_UINT(2u, ap_m68030_ea_timing_head(calculate, 0u));

  /* A row whose head is a plain figure does not move with the operation, which
   * is what makes the distinction worth carrying. */
  const ap_m68030_ea_timing_t *postincrement =
      ap_m68030_ea_calculate_timing(AP_M68030_EA_POSTINCREMENT);
  TEST_ASSERT_NOT_NULL(postincrement);
  TEST_ASSERT_FALSE(postincrement->head_adds_operation);
  TEST_ASSERT_EQUAL_UINT(0u, ap_m68030_ea_timing_head(postincrement, 3u));
}

/* The other claim on §11.3.4's example, and a separate one: where our tables
 * carry a row the example prints, the two agree. That is a check of the
 * transcription against a **different page** of the manual from the one it was
 * read off — the example prints its components inline, and it was typeset from
 * the tables rather than the other way round.
 *
 * Four rows can be checked this way. The example's other components —
 * `ADD EA,Dn`, `MOVE Source,(d16,An)`, `TAS Mem` and the memory indirect
 * `fea ([B])` — are rows this project has not transcribed, and their absence
 * here is what says so. */
static void test_our_transcription_agrees_with_the_examples_own_figures(void) {
  const struct {
    const ap_m68030_ea_timing_t *row;
    unsigned head;
    unsigned tail;
    unsigned cache_case;
    const char *what;
  } EA_CASES[] = {
      {ap_m68030_ea_fetch_timing(AP_M68030_EA_PREDECREMENT, 4u), 2, 2, 4,
       "fea -(An)"},
      {ap_m68030_ea_fetch_timing(AP_M68030_EA_ADDRESS_INDIRECT, 4u), 1, 1, 3,
       "fea (An)"},
      {ap_m68030_ea_calculate_timing(AP_M68030_EA_POSTINCREMENT), 0, 0, 2,
       "cea (An)+"},
  };

  for (unsigned i = 0; i < sizeof EA_CASES / sizeof EA_CASES[0]; i++) {
    TEST_ASSERT_NOT_NULL_MESSAGE(EA_CASES[i].row, EA_CASES[i].what);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(EA_CASES[i].head,
                                   EA_CASES[i].row->timing.head,
                                   EA_CASES[i].what);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(EA_CASES[i].tail,
                                   EA_CASES[i].row->timing.tail,
                                   EA_CASES[i].what);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(EA_CASES[i].cache_case,
                                   EA_CASES[i].row->timing.cache_case,
                                   EA_CASES[i].what);
  }

  /* `AND Dn,EA`, whose tail of 1 the example uses in `min(1,1)` and which our
   * table carries from §11.6.8. A tail this project got wrong would show as a
   * different total in the example rather than as a wrong-looking number. */
  const ap_m68030_table_entry_t *and_to_memory =
      ap_m68030_timing_for_word(0xC310u); /* AND.B D1,(A0) */
  TEST_ASSERT_NOT_NULL(and_to_memory);
  TEST_ASSERT_EQUAL_UINT(0u, and_to_memory->timing.head);
  TEST_ASSERT_EQUAL_UINT(1u, and_to_memory->timing.tail);
  TEST_ASSERT_EQUAL_UINT(3u, and_to_memory->timing.cache_case);

  /* `NEG Dn`, the example's last instruction and the one that needs no address
   * time at all. */
  const ap_m68030_table_entry_t *neg =
      ap_m68030_timing_for_word(0x4440u); /* NEG.W D0 */
  TEST_ASSERT_NOT_NULL(neg);
  TEST_ASSERT_EQUAL_UINT(2u, neg->timing.head);
  TEST_ASSERT_EQUAL_UINT(0u, neg->timing.tail);
  TEST_ASSERT_EQUAL_UINT(2u, neg->timing.cache_case);
}


/* ---------------------------------------------------------------------------
 * §11.6.1's full-format rows.
 *
 * A transcription cannot be checked by re-reading it, so these check structure:
 * the relationships that must hold across the sixteen rows, and the reading
 * that selects between the table's two groups.
 * ------------------------------------------------------------------------- */

static ap_m68030_extension_t full(ap_m68030_bd_size_t bd, bool base_suppressed,
                                  ap_m68030_indirect_t indirect,
                                  ap_m68030_od_size_t od) {
  return (ap_m68030_extension_t){
      .full_format = true,
      .base_suppressed = base_suppressed,
      .base_displacement_size = bd,
      .indirect = indirect,
      .outer_displacement_size = od,
  };
}

/* The whole space resolves, and every row is internally consistent. Sixteen
 * combinations of base and outer displacement, each of which a full-format
 * extension word can encode -- a gap here would be an addressing mode the core
 * could decode and not price. */
static void test_every_full_format_combination_has_a_row(void) {
  const ap_m68030_bd_size_t bds[] = {AP_M68030_BD_NULL, AP_M68030_BD_WORD,
                                     AP_M68030_BD_LONG};
  const ap_m68030_od_size_t ods[] = {AP_M68030_OD_NULL, AP_M68030_OD_WORD,
                                     AP_M68030_OD_LONG};

  for (unsigned b = 0; b < 3u; b++) {
    for (unsigned suppressed = 0; suppressed < 2u; suppressed++) {
      const ap_m68030_extension_t plain =
          full(bds[b], suppressed != 0u, AP_M68030_INDIRECT_NONE,
               AP_M68030_OD_NONE);
      const ap_m68030_ea_timing_t *row = ap_m68030_ea_fetch_timing_full(&plain);
      TEST_ASSERT_NOT_NULL(row);
      TEST_ASSERT_TRUE_MESSAGE(ap_m68030_timing_consistent(&row->timing),
                               row->mode);

      for (unsigned o = 0; o < 3u; o++) {
        const ap_m68030_extension_t indirect =
            full(bds[b], suppressed != 0u, AP_M68030_INDIRECT_PREINDEXED,
                 ods[o]);
        const ap_m68030_ea_timing_t *entry =
            ap_m68030_ea_fetch_timing_full(&indirect);
        TEST_ASSERT_NOT_NULL(entry);
        TEST_ASSERT_TRUE_MESSAGE(ap_m68030_timing_consistent(&entry->timing),
                                 entry->mode);
      }
    }
  }
}

/* **The reading, isolated.** A word base displacement is free when the base is a
 * register and costs two clocks when it is suppressed. That single difference is
 * what the table's two groups are, and it is the thing marked `PROVISIONAL` --
 * so it gets a test of its own rather than being buried in a sweep. */
static void test_a_word_base_displacement_is_free_with_a_register_base(void) {
  const ap_m68030_extension_t based =
      full(AP_M68030_BD_WORD, false, AP_M68030_INDIRECT_NONE,
           AP_M68030_OD_NONE);
  const ap_m68030_extension_t suppressed =
      full(AP_M68030_BD_WORD, true, AP_M68030_INDIRECT_NONE,
           AP_M68030_OD_NONE);
  const ap_m68030_extension_t none =
      full(AP_M68030_BD_NULL, false, AP_M68030_INDIRECT_NONE,
           AP_M68030_OD_NONE);

  const ap_m68030_ea_timing_t *with_base = ap_m68030_ea_fetch_timing_full(&based);
  const ap_m68030_ea_timing_t *without =
      ap_m68030_ea_fetch_timing_full(&suppressed);
  const ap_m68030_ea_timing_t *null_displacement =
      ap_m68030_ea_fetch_timing_full(&none);
  TEST_ASSERT_NOT_NULL(with_base);
  TEST_ASSERT_NOT_NULL(without);
  TEST_ASSERT_NOT_NULL(null_displacement);

  /* Free: the same 6 clocks as no displacement at all. */
  TEST_ASSERT_EQUAL_UINT(6u, null_displacement->timing.cache_case);
  TEST_ASSERT_EQUAL_UINT(6u, with_base->timing.cache_case);
  /* And two clocks when there is no register to fold it into. */
  TEST_ASSERT_EQUAL_UINT(8u, without->timing.cache_case);

  /* The head column agrees, which is the independent half of the reading: the
   * free rows are the ones the table gives a head of 2 and the rest 4. */
  TEST_ASSERT_EQUAL_UINT(2u, with_base->timing.head);
  TEST_ASSERT_EQUAL_UINT(4u, without->timing.head);
  TEST_ASSERT_EQUAL_UINT(4u, null_displacement->timing.head);
}

/* A long base displacement is never free -- group A has no `d32` row at all,
 * which is what the reading predicts and is the control on it. Without this a
 * transcription that made *every* displacement free would pass the test above. */
static void test_a_long_base_displacement_is_never_free(void) {
  const ap_m68030_extension_t based =
      full(AP_M68030_BD_LONG, false, AP_M68030_INDIRECT_NONE,
           AP_M68030_OD_NONE);
  const ap_m68030_extension_t suppressed =
      full(AP_M68030_BD_LONG, true, AP_M68030_INDIRECT_NONE,
           AP_M68030_OD_NONE);

  const ap_m68030_ea_timing_t *a = ap_m68030_ea_fetch_timing_full(&based);
  const ap_m68030_ea_timing_t *b = ap_m68030_ea_fetch_timing_full(&suppressed);
  TEST_ASSERT_NOT_NULL(a);
  TEST_ASSERT_NOT_NULL(b);
  TEST_ASSERT_EQUAL_UINT(12u, a->timing.cache_case);
  TEST_ASSERT_EQUAL_UINT(12u, b->timing.cache_case);
}

/* An indirection costs a second operand read, everywhere. `(B)` reads once and
 * `([B])` twice, and that holds at every base displacement -- which is the
 * property that distinguishes this table from the calculate one, where the
 * first level of indirection is the *only* read. */
static void test_an_indirection_costs_a_second_read(void) {
  const ap_m68030_bd_size_t bds[] = {AP_M68030_BD_NULL, AP_M68030_BD_WORD,
                                     AP_M68030_BD_LONG};
  for (unsigned b = 0; b < 3u; b++) {
    const ap_m68030_extension_t plain =
        full(bds[b], false, AP_M68030_INDIRECT_NONE, AP_M68030_OD_NONE);
    const ap_m68030_extension_t indirect =
        full(bds[b], false, AP_M68030_INDIRECT_MEMORY, AP_M68030_OD_NULL);
    const ap_m68030_ea_timing_t *direct = ap_m68030_ea_fetch_timing_full(&plain);
    const ap_m68030_ea_timing_t *through =
        ap_m68030_ea_fetch_timing_full(&indirect);
    TEST_ASSERT_NOT_NULL(direct);
    TEST_ASSERT_NOT_NULL(through);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1u, direct->timing.reads, direct->mode);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(2u, through->timing.reads, through->mode);
    TEST_ASSERT_TRUE_MESSAGE(
        through->timing.cache_case > direct->timing.cache_case, through->mode);
  }
}

/* Whether the index is inside the brackets, outside them or absent makes no
 * difference to the figures -- `([B])` and `([B],I)` are both `10(2/0/0)`, and
 * the table says so itself in the note that "scaling and size of Xn do not
 * affect timing". So the three indirection kinds share a row, and a model that
 * gave preindexed and postindexed different costs would be inventing a
 * difference the manual denies. */
static void test_the_indirection_kinds_share_their_figures(void) {
  const ap_m68030_indirect_t kinds[] = {AP_M68030_INDIRECT_PREINDEXED,
                                        AP_M68030_INDIRECT_POSTINDEXED,
                                        AP_M68030_INDIRECT_MEMORY};
  const ap_m68030_ea_timing_t *first = nullptr;
  for (unsigned i = 0; i < 3u; i++) {
    const ap_m68030_extension_t extension =
        full(AP_M68030_BD_NULL, false, kinds[i], AP_M68030_OD_WORD);
    const ap_m68030_ea_timing_t *row =
        ap_m68030_ea_fetch_timing_full(&extension);
    TEST_ASSERT_NOT_NULL(row);
    if (first == nullptr) {
      first = row;
    }
    TEST_ASSERT_EQUAL_PTR(first, row);
  }
}

/* A reserved encoding gets no row, and neither does a brief-format word. Both
 * are the honest answer: the first is not an addressing mode, and the second
 * has its own row in the table above. Returning a full-format figure for a
 * brief-format word would over-charge by up to twelve clocks. */
static void test_a_reserved_or_brief_extension_has_no_full_format_row(void) {
  ap_m68030_extension_t reserved =
      full(AP_M68030_BD_RESERVED, false, AP_M68030_INDIRECT_NONE,
           AP_M68030_OD_NONE);
  TEST_ASSERT_NULL(ap_m68030_ea_fetch_timing_full(&reserved));

  ap_m68030_extension_t flagged =
      full(AP_M68030_BD_WORD, false, AP_M68030_INDIRECT_NONE,
           AP_M68030_OD_NONE);
  flagged.reserved = true;
  TEST_ASSERT_NULL(ap_m68030_ea_fetch_timing_full(&flagged));

  ap_m68030_extension_t brief =
      full(AP_M68030_BD_WORD, false, AP_M68030_INDIRECT_NONE,
           AP_M68030_OD_NONE);
  brief.full_format = false;
  TEST_ASSERT_NULL(ap_m68030_ea_fetch_timing_full(&brief));

  TEST_ASSERT_NULL(ap_m68030_ea_fetch_timing_full(nullptr));
}

/* The worked example's `fea ([B])` -- head 4, tail 0, 10 clocks -- which
 * §11.3.4 prints inline for `AND.L D1,([A2])`. It had to be supplied by hand
 * when the composition was first checked because these rows did not exist; now
 * it can be looked up, which makes that test's inputs and this table agree from
 * two different pages of the manual. */
static void test_the_worked_examples_memory_indirect_row_is_reachable(void) {
  const ap_m68030_extension_t indirect =
      full(AP_M68030_BD_NULL, false, AP_M68030_INDIRECT_MEMORY,
           AP_M68030_OD_NULL);
  const ap_m68030_ea_timing_t *row = ap_m68030_ea_fetch_timing_full(&indirect);
  TEST_ASSERT_NOT_NULL(row);
  TEST_ASSERT_EQUAL_UINT(4u, row->timing.head);
  TEST_ASSERT_EQUAL_UINT(0u, row->timing.tail);
  TEST_ASSERT_EQUAL_UINT(10u, row->timing.cache_case);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_every_full_format_combination_has_a_row);
  RUN_TEST(test_a_word_base_displacement_is_free_with_a_register_base);
  RUN_TEST(test_a_long_base_displacement_is_never_free);
  RUN_TEST(test_an_indirection_costs_a_second_read);
  RUN_TEST(test_the_indirection_kinds_share_their_figures);
  RUN_TEST(test_a_reserved_or_brief_extension_has_no_full_format_row);
  RUN_TEST(test_the_worked_examples_memory_indirect_row_is_reachable);
  RUN_TEST(test_the_worked_example_of_equation_11_2_comes_to_40_clocks);
  RUN_TEST(test_a_register_operand_contributes_no_component_at_all);
  RUN_TEST(test_a_relative_head_resolves_against_its_operation);
  RUN_TEST(test_our_transcription_agrees_with_the_examples_own_figures);
  RUN_TEST(test_a_register_operand_costs_nothing_either_way);
  RUN_TEST(test_fetching_never_costs_less_than_calculating);
  RUN_TEST(test_the_two_tables_differ_by_the_operand_read);
  RUN_TEST(test_the_calculate_heads_that_are_relative_say_so);
  RUN_TEST(test_an_immediate_costs_by_the_words_it_occupies);
  RUN_TEST(test_an_immediate_has_no_address_to_calculate);
  RUN_TEST(test_only_the_long_absolute_differs_between_the_columns);
  RUN_TEST(test_every_row_is_internally_consistent);
  return UNITY_END();
}

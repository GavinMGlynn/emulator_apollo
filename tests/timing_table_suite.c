/* MC68030 published instruction timings, `[030]` §11.6.
 *
 * A transcription cannot be checked by re-reading it, so these tests check it
 * against *structure*: the patterns that repeat across the table, the internal
 * consistency rule the overlap module already enforces, and the markers the
 * table itself carries. A row mistyped in a way that breaks none of those is
 * still possible — but a row mistyped at random almost certainly breaks one.
 */

#include "cpu/m68030/ap_m68030_timing_table.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Every row must satisfy the rule §11.3.2 states in prose: "the heads of some
 * instructions equal the total instruction-cache-case time", so a head may
 * equal the cache case but never exceed it, and the same for the tail. A digit
 * dropped or doubled in transcription usually breaks this. */
static void test_every_transcribed_row_is_internally_consistent(void) {
  unsigned count = 0;
  const ap_m68030_table_entry_t *table = ap_m68030_timing_table(&count);
  TEST_ASSERT_TRUE(count > 0u);

  for (unsigned i = 0; i < count; i++) {
    TEST_ASSERT_TRUE_MESSAGE(
        ap_m68030_timing_consistent(&table[i].timing), table[i].form);
    /* A zero cache case would mean an instruction that takes no time at all,
     * which no row in §11.6 shows -- overlap can absorb an instruction's cost
     * entirely, but its own CC is never zero. */
    TEST_ASSERT_TRUE_MESSAGE(table[i].timing.cache_case > 0u, table[i].form);
  }
}

/* The pattern that appears six times across ADDA, SUBA and CMPA: the word-size
 * address forms cost 4 and the long forms cost 2. It is also the direction that
 * makes physical sense, since the word form sign-extends its source to 32 bits
 * and the long form does not.
 *
 * This is the pattern that showed §11.3.4's worked example to be mislabelled --
 * it gives `SUBA.L` the word form's 4 -- so pinning it here is what stops the
 * table drifting back towards that example. */
static void test_the_word_address_forms_cost_more_than_the_long_ones(void) {
  const ap_m68030_table_entry_t *adda_word =
      ap_m68030_timing_for_word(0xD0C0u); /* ADDA.W D0,A0 */
  const ap_m68030_table_entry_t *adda_long =
      ap_m68030_timing_for_word(0xD1C0u); /* ADDA.L D0,A0 */
  TEST_ASSERT_NOT_NULL(adda_word);
  TEST_ASSERT_NOT_NULL(adda_long);
  TEST_ASSERT_EQUAL_UINT(4u, adda_word->timing.cache_case);
  TEST_ASSERT_EQUAL_UINT(2u, adda_long->timing.cache_case);
  TEST_ASSERT_EQUAL_UINT(4u, adda_word->timing.head);
  TEST_ASSERT_EQUAL_UINT(2u, adda_long->timing.head);

  const ap_m68030_table_entry_t *suba_word =
      ap_m68030_timing_for_word(0x90C0u); /* SUBA.W D0,A0 */
  const ap_m68030_table_entry_t *suba_long =
      ap_m68030_timing_for_word(0x91C0u); /* SUBA.L D0,A0 */
  TEST_ASSERT_NOT_NULL(suba_word);
  TEST_ASSERT_NOT_NULL(suba_long);
  TEST_ASSERT_EQUAL_UINT(4u, suba_word->timing.cache_case);
  /* The value §11.3.4's example contradicts. The table says 2 and six rows
   * agree with it. */
  TEST_ASSERT_EQUAL_UINT(2u, suba_long->timing.cache_case);

  const ap_m68030_table_entry_t *cmpa =
      ap_m68030_timing_for_word(0xB0C0u); /* CMPA.W D0,A0 */
  TEST_ASSERT_NOT_NULL(cmpa);
  TEST_ASSERT_EQUAL_UINT(4u, cmpa->timing.cache_case);
}

/* The ordinary register-to-register operations all cost two clocks with a head
 * of two and no tail. That uniformity is itself a check: a row mistyped among
 * them stands out against the other six. */
static void test_the_register_operations_agree_with_each_other(void) {
  const uint16_t words[] = {
      0xD200u, /* ADD.B D0,D1  */
      0x9200u, /* SUB.B D0,D1  */
      0xC200u, /* AND.B D0,D1  */
      0x8200u, /* OR.B  D0,D1  */
      0xB200u, /* CMP.B D0,D1  */
      0xB300u, /* EOR.B D1,D0  */
      0x7000u, /* MOVEQ #0,D0  */
      0x5200u, /* ADDQ.B #1,D0 */
      0x5300u, /* SUBQ.B #1,D0 */
  };

  for (unsigned i = 0; i < sizeof words / sizeof words[0]; i++) {
    const ap_m68030_table_entry_t *entry = ap_m68030_timing_for_word(words[i]);
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(2u, entry->timing.cache_case, entry->form);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(2u, entry->timing.head, entry->form);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, entry->timing.tail, entry->form);
  }
}

/* "+ Indicates Maximum Time (Actual time is data dependent)". The divides carry
 * that marker, and it must survive into the table: a caller using 56 clocks for
 * every DIVS.W would be slow by a data-dependent amount rather than wrong by a
 * fixed one, which is far harder to notice. */
static void test_the_divides_are_marked_data_dependent(void) {
  const ap_m68030_table_entry_t *divu =
      ap_m68030_timing_for_word(0x80C0u); /* DIVU.W D0,D0 */
  const ap_m68030_table_entry_t *divs =
      ap_m68030_timing_for_word(0x81C0u); /* DIVS.W D0,D0 */

  TEST_ASSERT_NOT_NULL(divu);
  TEST_ASSERT_NOT_NULL(divs);
  TEST_ASSERT_TRUE(divu->data_dependent);
  TEST_ASSERT_TRUE(divs->data_dependent);
  TEST_ASSERT_EQUAL_UINT(44u, divu->timing.cache_case);
  TEST_ASSERT_EQUAL_UINT(56u, divs->timing.cache_case);

  /* And a signed divide costing more than an unsigned one is the direction to
   * expect, which is a weak but real check on not having swapped the pair. */
  TEST_ASSERT_TRUE(divs->timing.cache_case > divu->timing.cache_case);

  /* Nothing else is marked: a marker applied too widely would make every figure
   * look provisional and none of them actionable. */
  unsigned count = 0;
  const ap_m68030_table_entry_t *table = ap_m68030_timing_table(&count);
  unsigned marked = 0;
  for (unsigned i = 0; i < count; i++) {
    if (table[i].data_dependent) {
      marked++;
    }
  }
  TEST_ASSERT_EQUAL_UINT(4u, marked); /* DIVS.W, DIVS.L, DIVU.W, DIVU.L */
}

/* The lookup returns NULL for what is not transcribed, and that is the honest
 * answer rather than a gap to paper over. A memory form's published figure
 * needs an effective address time this module does not carry, so returning the
 * register row for it would under-count by a whole memory access. */
static void test_what_is_not_transcribed_is_reported_as_absent(void) {
  /* ADD.B (A0),D0 -- a memory source, whose row is footnoted "Add Fetch
   * Effective Address Time". */
  TEST_ASSERT_NULL(ap_m68030_timing_for_word(0xD010u));
  /* MULU.W D0,D0 -- family 1100's wide form, not transcribed. */
  TEST_ASSERT_NULL(ap_m68030_timing_for_word(0xC0C0u));
  /* SWAP, which lives in §11.6.13's miscellaneous table and has not been read
   * yet. NOP stood here until §11.6.16 was transcribed -- a placeholder for
   * "not covered" needs replacing whenever coverage grows, which is the right
   * kind of churn. */
  TEST_ASSERT_NULL(ap_m68030_timing_for_word(0x4840u)); /* SWAP D0 */
  /* A register-count shift, whose cost the table marks as count-dependent. */
  TEST_ASSERT_NULL(ap_m68030_timing_for_word(0xE2A8u)); /* LSR.L D1,D0 */
}

/* Every row names the form as §11.6 writes it, so a figure can be traced back
 * to a line in the manual rather than to someone's reading of it. */
static void test_every_row_names_its_form(void) {
  unsigned count = 0;
  const ap_m68030_table_entry_t *table = ap_m68030_timing_table(&count);

  for (unsigned i = 0; i < count; i++) {
    TEST_ASSERT_NOT_NULL(table[i].form);
    TEST_ASSERT_TRUE(table[i].form[0] != '\0');
  }
}

/* The transcribed figures compose through Equation (11-1), which is the whole
 * reason they were transcribed. Two register operations back to back: the
 * second's head of 2 meets the first's tail of 0, so nothing overlaps and the
 * total is the plain sum. */
static void test_the_figures_compose_through_the_overlap_rule(void) {
  const ap_m68030_table_entry_t *add = ap_m68030_timing_for_word(0xD200u);
  const ap_m68030_table_entry_t *sub = ap_m68030_timing_for_word(0x9200u);
  TEST_ASSERT_NOT_NULL(add);
  TEST_ASSERT_NOT_NULL(sub);

  ap_m68030_overlap_state_t state = ap_m68030_overlap_begin();
  ap_m68030_overlap_add(&state, &add->timing);
  ap_m68030_overlap_add(&state, &sub->timing);

  /* 2 + [2 - min(2,0)] = 4. The tail of zero is what makes these instructions
   * unable to absorb the next one's head, which is why a run of register
   * operations costs the sum of its parts. */
  TEST_ASSERT_EQUAL_UINT64(4u, ap_m68030_overlap_total(&state));
}

/* §11.6.16's control rows, identified by whole instruction words rather than by
 * family, since each is a single encoding. The `LINK`/`UNLK` pair splits on bit
 * 3 of `$4E5x` — the same split `ap_m68030_control_decode` makes, so the two
 * modules agree about where the boundary is rather than each having its own. */
static void test_the_control_instructions_are_found_by_their_encodings(void) {
  const ap_m68030_table_entry_t *nop = ap_m68030_timing_for_word(0x4E71u);
  TEST_ASSERT_NOT_NULL(nop);
  TEST_ASSERT_EQUAL_UINT(2u, nop->timing.cache_case);
  /* NOP's head is zero, unlike most register operations: an instruction that
   * does nothing still cannot be overlapped away by its predecessor. */
  TEST_ASSERT_EQUAL_UINT(0u, nop->timing.head);

  const ap_m68030_table_entry_t *rts = ap_m68030_timing_for_word(0x4E75u);
  TEST_ASSERT_NOT_NULL(rts);
  TEST_ASSERT_EQUAL_UINT(9u, rts->timing.cache_case);
  TEST_ASSERT_EQUAL_UINT(11u, rts->timing.no_cache_case);

  /* LINK.W is $4E5x with bit 3 clear and UNLK the same range with it set. */
  for (unsigned reg = 0; reg < 8u; reg++) {
    const ap_m68030_table_entry_t *link =
        ap_m68030_timing_for_word((uint16_t)(0x4E50u + reg));
    const ap_m68030_table_entry_t *unlk =
        ap_m68030_timing_for_word((uint16_t)(0x4E58u + reg));
    TEST_ASSERT_NOT_NULL(link);
    TEST_ASSERT_NOT_NULL(unlk);
    TEST_ASSERT_EQUAL_UINT(4u, link->timing.cache_case);
    TEST_ASSERT_EQUAL_UINT(5u, unlk->timing.cache_case);
  }

  /* And LINK.L is a different encoding entirely, at $480x, costing more. */
  const ap_m68030_table_entry_t *link_long =
      ap_m68030_timing_for_word(0x4808u);
  TEST_ASSERT_NOT_NULL(link_long);
  TEST_ASSERT_EQUAL_UINT(6u, link_long->timing.cache_case);
}

/* Writing the status register costs 12 clocks — six times the same logical
 * operation on a data register. That is the pipe refilling, and it is the same
 * fact §8.1.7 gives as the reason these instructions count as a change of flow
 * for tracing: "the processor must re-prefetch instruction words to fill the
 * pipe again any time an instruction that can modify the status register is
 * executed."
 *
 * Two independent parts of the manual agreeing about one instruction's
 * behaviour is worth pinning: a transcription that had this at 2, matching its
 * data-register sibling, would contradict the trace rule this core already
 * implements. */
static void test_a_status_register_write_costs_a_pipe_refill(void) {
  unsigned count = 0;
  const ap_m68030_table_entry_t *table = ap_m68030_timing_table(&count);

  const ap_m68030_table_entry_t *status = nullptr;
  for (unsigned i = 0; i < count; i++) {
    if (table[i].timing.cache_case == 12u) {
      status = &table[i];
    }
  }
  TEST_ASSERT_NOT_NULL(status);
  TEST_ASSERT_EQUAL_UINT(14u, status->timing.no_cache_case);

  /* Six times the register-operand form of the same logical operation. */
  const ap_m68030_table_entry_t *ordinary = ap_m68030_timing_for_word(0xC200u);
  TEST_ASSERT_NOT_NULL(ordinary);
  TEST_ASSERT_EQUAL_UINT(6u * ordinary->timing.cache_case,
                         status->timing.cache_case);
}

/* The check that would have caught a claim this project got wrong.
 *
 * `(NCC−CC)/p` was asserted to be "0 or 1, never 2, never fractional" from
 * eleven rows chosen while transcribing. Three rows already in the same table
 * falsify it: `BSR` at 1.5, `DBcc` with the condition true at 2, and `LINK.L`
 * at 0.5. The error was not arithmetic — it was stating a pattern found on a
 * subset as though it held generally.
 *
 * So the division now runs over **every** row, and every exception must be
 * named here. A row that becomes inexact without being listed fails this test,
 * which is the property the prose claim could not have. */
static void test_every_inexact_prefetch_cost_is_named(void) {
  /* The rows where `NCC − CC` is not divisible by `p`. `p` is itself "the
   * average of the odd-word-aligned case and the even-word-aligned case
   * (rounded up)", so a true count of one-and-a-half is published as two and
   * the division inherits the rounding -- which is what these look like. */
  static const char *const KNOWN_INEXACT[] = {
      "BSR",     /* (9−6)/2 = 1.5 */
      "LINK.L",  /* (7−6)/2 = 0.5 */
  };

  unsigned count = 0;
  const ap_m68030_table_entry_t *table = ap_m68030_timing_table(&count);
  unsigned inexact_seen = 0;

  for (unsigned i = 0; i < count; i++) {
    const ap_m68030_prefetch_cost_t cost =
        ap_m68030_prefetch_cost(&table[i].timing);
    if (cost.exact) {
      continue;
    }
    inexact_seen++;

    bool named = false;
    for (unsigned k = 0; k < sizeof KNOWN_INEXACT / sizeof KNOWN_INEXACT[0];
         k++) {
      /* Compared by the form string, so a row renamed without this list being
       * updated fails rather than matching by position. */
      const char *a = table[i].form;
      const char *b = KNOWN_INEXACT[k];
      unsigned j = 0;
      while (a[j] != '\0' && b[j] != '\0' && a[j] == b[j]) {
        j++;
      }
      if (a[j] == '\0' && b[j] == '\0') {
        named = true;
      }
    }
    TEST_ASSERT_TRUE_MESSAGE(named, table[i].form);
  }

  /* And the named ones are actually there: a list that had gone stale the other
   * way -- naming rows that no longer exist or are now exact -- would pass the
   * loop above while claiming exceptions it does not have. */
  TEST_ASSERT_EQUAL_UINT(sizeof KNOWN_INEXACT / sizeof KNOWN_INEXACT[0],
                         inexact_seen);
}

/* ---------------------------------------------------------------------------
 * The decomposition: how much of a published figure is bus, and how much is
 * microcode.
 * ------------------------------------------------------------------------- */

/* `CC` contains the instruction's own operand cycles -- "the read, prefetch,
 * and write cycles are included in the total clock cycle number" -- at two
 * clocks each. So no row can have more bus time than total time, and one that
 * does was mistranscribed: a stray `r` or `w` shows up here rather than as an
 * instruction silently priced at nothing.
 *
 * This is a real check and not a tautology, because `reads` and `writes` were
 * transcribed independently of `cache_case`, off the same table line. */
static void test_no_rows_bus_time_exceeds_its_published_total(void) {
  unsigned count = 0;
  const ap_m68030_table_entry_t *table = ap_m68030_timing_table(&count);

  for (unsigned i = 0; i < count; i++) {
    const unsigned bus =
        (table[i].timing.reads + table[i].timing.writes) * 2u;
    TEST_ASSERT_TRUE_MESSAGE(bus <= table[i].timing.cache_case, table[i].form);

    /* And the microcode is what is left, which for these rows is never the
     * whole figure and never none of it: every transcribed row does *some*
     * work beyond its bus cycles. */
    const unsigned microcode = ap_m68030_microcode_clocks(&table[i].timing);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(table[i].timing.cache_case - bus, microcode,
                                   table[i].form);
    TEST_ASSERT_TRUE_MESSAGE(microcode > 0u, table[i].form);
  }
}

/* The memory-destination rows are the ones that make the decomposition worth
 * having, and the two `MOVE` rows are the pair that shows it working. Both
 * write one operand; `MOVE Rn,(An)` is 3 clocks and `MOVE Rn,-(An)` is 4, so
 * after the write's two clocks come out they are 1 and 2 clocks of microcode.
 *
 * That difference is exactly what the predecrement does extra, and it is
 * invisible in the totals until the bus half is removed. */
static void test_the_decomposition_separates_the_predecrement_extra_clock(void) {
  const ap_m68030_table_entry_t *indirect =
      ap_m68030_timing_for_word(0x2080u); /* MOVE.L D0,(A0) */
  const ap_m68030_table_entry_t *predecrement =
      ap_m68030_timing_for_word(0x2100u); /* MOVE.L D0,-(A0) */
  TEST_ASSERT_NOT_NULL(indirect);
  TEST_ASSERT_NOT_NULL(predecrement);

  TEST_ASSERT_EQUAL_UINT(1u, indirect->timing.writes);
  TEST_ASSERT_EQUAL_UINT(1u, predecrement->timing.writes);
  TEST_ASSERT_EQUAL_UINT(1u, ap_m68030_microcode_clocks(&indirect->timing));
  TEST_ASSERT_EQUAL_UINT(2u, ap_m68030_microcode_clocks(&predecrement->timing));
}

/* **The claim this test exists to falsify.** §11.3.3 gives the no-cache figure
 * as "the average of the odd-word-aligned case and the even-word-aligned case
 * (rounded up)". For a single-word instruction that is not a change of flow the
 * odd alignment runs no external fetch at all -- the cache holding register's
 * long word already holds the word -- so the published difference is half the
 * even case, and the even case is `2(NCC − CC)`.
 *
 * A bus cycle is two clocks, so that quantity can only be 0 or 2: such a
 * prefetch either hides completely under the instruction's microcode or not at
 * all. If any row of that class gave 4, the reasoning would be wrong.
 *
 * Computed over **every** row of the class, which the table now carries as data
 * rather than this test carrying a list of names -- the applicability belongs
 * where the figure is used, not only where it is checked. */
static void test_a_single_word_prefetch_either_hides_completely_or_not_at_all(
    void) {
  unsigned count = 0;
  const ap_m68030_table_entry_t *table = ap_m68030_timing_table(&count);
  unsigned exposed = 0;
  unsigned hidden = 0;

  for (unsigned i = 0; i < count; i++) {
    if (table[i].prefetch_class != AP_M68030_PREFETCH_SINGLE_WORD) {
      continue;
    }
    const unsigned exposure = ap_m68030_prefetch_exposure(
        &table[i].timing, table[i].prefetch_class);
    TEST_ASSERT_TRUE_MESSAGE(exposure == 0u || exposure == 2u, table[i].form);
    if (exposure == 2u) {
      exposed++;
    } else {
      hidden++;
    }
  }

  /* Both outcomes actually occur. A rule that only ever produced one of them
   * would satisfy every assertion above and say nothing: the whole point is
   * that some instructions hide their prefetch and some do not. */
  TEST_ASSERT_TRUE(exposed > 0u);
  TEST_ASSERT_TRUE(hidden > 0u);
}

/* The classification itself, which is the part that could be wrong without any
 * arithmetic being wrong. It is a claim about each instruction's *length* and
 * whether it changes flow, so it is checked against those facts rather than
 * against the figures it is used with.
 *
 * A row misclassified as single-word would have its published difference
 * doubled, which is the largest error this model can make -- so the rows that
 * are not single-word are named here individually. */
static void test_the_rows_that_are_not_single_word_are_classified_as_such(void) {
  static const struct {
    const char *form;
    ap_m68030_prefetch_class_t klass;
    const char *why;
  } EXPECTED[] = {
      {"RTS", AP_M68030_PREFETCH_UNKNOWN, "change of flow"},
      {"RTR", AP_M68030_PREFETCH_UNKNOWN, "change of flow"},
      {"RTD", AP_M68030_PREFETCH_UNKNOWN, "change of flow"},
      {"BSR", AP_M68030_PREFETCH_UNKNOWN, "change of flow"},
      {"Bcc (Taken)", AP_M68030_PREFETCH_UNKNOWN, "change of flow"},
      {"DBcc (cc False, Count Not Expired)", AP_M68030_PREFETCH_UNKNOWN,
       "it branches"},
      {"LINK.L", AP_M68030_PREFETCH_UNKNOWN, "three words"},
      {"Bcc.L (Not Taken)", AP_M68030_PREFETCH_UNKNOWN, "three words"},
      {"LINK.W", AP_M68030_PREFETCH_EVEN_WORDS, "two words"},
      {"Bcc.W (Not Taken)", AP_M68030_PREFETCH_EVEN_WORDS, "two words"},
      {"DBcc (cc True)", AP_M68030_PREFETCH_EVEN_WORDS, "two words"},
      {"DBcc (cc False, Count Expired)", AP_M68030_PREFETCH_EVEN_WORDS,
       "two words, and it falls through"},
      {"ANDI/EORI/ORI to SR or CCR", AP_M68030_PREFETCH_EVEN_WORDS,
       "two words"},
      {"ADDI #<data>,Dn", AP_M68030_PREFETCH_EVEN_WORDS, "two words"},
  };

  unsigned count = 0;
  const ap_m68030_table_entry_t *table = ap_m68030_timing_table(&count);
  unsigned matched = 0;
  unsigned non_single = 0;

  for (unsigned i = 0; i < count; i++) {
    if (table[i].prefetch_class != AP_M68030_PREFETCH_SINGLE_WORD) {
      non_single++;
    }
    for (unsigned k = 0; k < sizeof EXPECTED / sizeof EXPECTED[0]; k++) {
      const char *a = table[i].form;
      const char *b = EXPECTED[k].form;
      unsigned j = 0;
      while (a[j] != '\0' && b[j] != '\0' && a[j] == b[j]) {
        j++;
      }
      if (a[j] == '\0' && b[j] == '\0') {
        matched++;
        TEST_ASSERT_EQUAL_INT_MESSAGE(EXPECTED[k].klass,
                                      table[i].prefetch_class, EXPECTED[k].why);
      }
    }
  }

  /* Every named row was found, and no *other* row is anything but single word
   * -- so a row that quietly stops being single-word fails here rather than
   * being priced by a rule that does not apply to it. */
  TEST_ASSERT_EQUAL_UINT(sizeof EXPECTED / sizeof EXPECTED[0], matched);
  TEST_ASSERT_EQUAL_UINT(sizeof EXPECTED / sizeof EXPECTED[0], non_single);
}

/* Which rows they are, and it is the memory destinations. `ADD Dn,EA` and
 * `MOVE Rn,(An)` expose their prefetch; `ADD Rn,Dn` and `MOVE Rn,-(An)` hide
 * it. The last pair is the interesting one -- both write to memory, and the
 * predecrement's extra clock of microcode is what covers the fetch. */
static void test_the_rows_that_expose_a_prefetch_are_the_memory_forms(void) {
  const struct {
    uint16_t word;
    unsigned exposure;
    const char *what;
  } CASES[] = {
      {0xD200u, 0u, "ADD.B D0,D1"},   {0xD110u, 2u, "ADD.B D0,(A0)"},
      {0x2080u, 2u, "MOVE.L D0,(A0)"}, {0x2100u, 0u, "MOVE.L D0,-(A0)"},
      {0x7000u, 0u, "MOVEQ #0,D0"},
  };

  for (unsigned i = 0; i < sizeof CASES / sizeof CASES[0]; i++) {
    const ap_m68030_table_entry_t *row =
        ap_m68030_timing_for_word(CASES[i].word);
    TEST_ASSERT_NOT_NULL_MESSAGE(row, CASES[i].what);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(
        CASES[i].exposure,
        ap_m68030_prefetch_exposure(&row->timing, row->prefetch_class),
        CASES[i].what);
  }
}

/* And the values the exact rows take are *not* confined to 0 and 1, which is
 * the substance of what was withdrawn. `DBcc` with the condition true divides
 * exactly and gives 2. */
static void test_an_exact_prefetch_cost_is_not_always_zero_or_one(void) {
  unsigned count = 0;
  const ap_m68030_table_entry_t *table = ap_m68030_timing_table(&count);

  bool saw_two = false;
  for (unsigned i = 0; i < count; i++) {
    const ap_m68030_prefetch_cost_t cost =
        ap_m68030_prefetch_cost(&table[i].timing);
    if (cost.exact && cost.clocks >= 2u) {
      saw_two = true;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(saw_two,
                           "a cost of 2 exists; the withdrawn claim said none did");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_every_transcribed_row_is_internally_consistent);
  RUN_TEST(test_the_word_address_forms_cost_more_than_the_long_ones);
  RUN_TEST(test_the_register_operations_agree_with_each_other);
  RUN_TEST(test_the_divides_are_marked_data_dependent);
  RUN_TEST(test_what_is_not_transcribed_is_reported_as_absent);
  RUN_TEST(test_every_row_names_its_form);
  RUN_TEST(test_the_control_instructions_are_found_by_their_encodings);
  RUN_TEST(test_a_status_register_write_costs_a_pipe_refill);
  RUN_TEST(test_every_inexact_prefetch_cost_is_named);
  RUN_TEST(test_no_rows_bus_time_exceeds_its_published_total);
  RUN_TEST(test_the_decomposition_separates_the_predecrement_extra_clock);
  RUN_TEST(test_a_single_word_prefetch_either_hides_completely_or_not_at_all);
  RUN_TEST(test_the_rows_that_are_not_single_word_are_classified_as_such);
  RUN_TEST(test_the_rows_that_expose_a_prefetch_are_the_memory_forms);
  RUN_TEST(test_an_exact_prefetch_cost_is_not_always_zero_or_one);
  RUN_TEST(test_the_figures_compose_through_the_overlap_rule);
  return UNITY_END();
}

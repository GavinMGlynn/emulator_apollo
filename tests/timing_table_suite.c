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
  /* DIVS.W, DIVS.L, DIVU.W, DIVU.L, §11.6.8's four `EA,Dn` forms of the word
   * multiplies and divides, which carry `*+`, §11.6.16's two `CAS2` rows,
   * which carry `+`, its four bounds-check rows that carry `+`, and §11.6.8's
   * five rows the extension word selects: `CMP2` and the long `EA,Dn` forms. */
  TEST_ASSERT_EQUAL_UINT(19u, marked);
}

/* The lookup returns NULL for what is not transcribed, and that is the honest
 * answer rather than a gap to paper over. A memory form's published figure
 * needs an effective address time this module does not carry, so returning the
 * register row for it would under-count by a whole memory access. */
static void test_what_is_not_transcribed_is_reported_as_absent(void) {
  /* CLR.W (A0) -- §11.6.11's memory form, footnoted "Add Calculate Effective
   * Address Time", which the step does not compose yet. `ADD.B (A0),D0` and
   * `MULU.W D0,D0` stood here until §11.6.8's `EA` rows were transcribed. */
  TEST_ASSERT_NULL(ap_m68030_timing_for_word(0x4810u)); /* NBCD (A0), unprinted */
  /* MULU.L (A0),D0 -- the long multiplies share `$4C00` with each other and
   * are told apart by the extension word. */
  TEST_ASSERT_NULL(ap_m68030_timing_for_word(0x4C10u));
  /* `MOVEC Rn,Cr`, whose row §11.6.7 splits by control register group -- which
   * only the extension word names. `SWAP` stood here until §11.6.7 was
   * transcribed, and NOP before it: a placeholder for "not covered" needs
   * replacing whenever coverage grows, which is the right kind of churn. */
  TEST_ASSERT_NULL(ap_m68030_timing_for_word(0x4E7Bu)); /* MOVEC Rn,Cr */
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

  /* **RESET is $4E70 and costs 518 clocks, and this core charged nothing.**
   *
   * `[030]` §11.6.17 gives `RESET Instruction` as `518(0/0/0)`, and `[PRM]`'s
   * `RESET` page says the same from the other end: "Asserts the RSTO signal for
   * 512 ... clock periods", which is 512 plus six of overhead. Two independent
   * documents for a figure the instruction arm was charging zero for -- and the
   * boot PROM executes RESET, so the machine was resuming instantly where the
   * hardware holds the reset line for ~20 us at 25 MHz.
   *
   * It is the one row here from outside §11.6.8/§11.6.9, because it is almost
   * all signal duration rather than microcode. Found walking `[PRM]` §6 on
   * 2026-09-07. */
  const ap_m68030_table_entry_t *reset = ap_m68030_timing_for_word(0x4E70u);
  TEST_ASSERT_NOT_NULL(reset);
  TEST_ASSERT_EQUAL_UINT(518u, reset->timing.cache_case);
  TEST_ASSERT_EQUAL_UINT(518u, reset->timing.no_cache_case);
  TEST_ASSERT_EQUAL_UINT(0u, reset->timing.head);
  TEST_ASSERT_EQUAL_UINT(0u, reset->timing.tail);
  /* Not data-dependent, despite dwarfing every other row: the assertion is a
   * fixed number of clock periods, not a range. */
  TEST_ASSERT_FALSE(reset->data_dependent);
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
  /* Found by its instruction, `ANDI #<data>,SR`, and not by being "the row that
   * costs 12": that was unique until `TAS Mem` joined the table at 12 too, and
   * a test that finds a row by a number other rows may share tests the search,
   * not the row. */
  const ap_m68030_table_entry_t *status = ap_m68030_timing_for_word(0x027Cu);
  TEST_ASSERT_NOT_NULL(status);
  TEST_ASSERT_EQUAL_UINT(12u, status->timing.cache_case);
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
      "MOVE EA,xxx.L", /* (7-6)/2 = 0.5, LINK.L's shape: three words */
      "BSR",     /* (9−6)/2 = 1.5 */
      "JSR",     /* (7−4)/2 = 1.5, BSR's shape */
      "Interrupt (I-Stack)", /* (24−23)/2 = 0.5 */
      "Interrupt (M-Stack)", /* (34−33)/2 = 0.5 */
      "TRAPcc.W (Trap)",     /* (26−24)/3, the operand word's and the refill's */
      "TRAPcc.L (Trap)",     /* (28−26)/3 */
      "CHK Dn,Dn (Exception Taken)",   /* (30−28)/3 */
      "CHK EA,Dn (Exception Taken)",   /* (30−28)/3 */
      "CHK2 Mem,Rn (Exception Taken)", /* (42−40)/3 */
      "MOVE EA,(d16,An) or (d16,PC)",            /* (9−8)/2 */
      "MOVE EA,([d16,An],Xn) or ([d16,PC],Xn)",  /* (11−10)/2 */
      "MOVE EA,([d16,An],d32) or ([d16,PC],d32)", /* (16−14)/3 */
      "MOVE EA,([d16,B],d16)",                   /* (17−14)/2 */
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

static bool form_is(const ap_m68030_table_entry_t *row, const char *form) {
  if (row == nullptr) {
    return false;
  }
  unsigned j = 0;
  while (row->form[j] != '\0' && form[j] != '\0' && row->form[j] == form[j]) {
    j++;
  }
  return row->form[j] == '\0' && form[j] == '\0';
}

/* **Every row is returned by some instruction.** Six rows were not -- `NBCD`
 * first, then `ADDI #<data>,Dn`, the status-register forms, `EXT`, `TAS` and
 * `Scc` -- each transcribed, tested for its figures, and charged to nothing,
 * because a test of a row's numbers cannot see that no lookup reaches it. This
 * walks the whole opcode map through the three lookups and requires every row
 * to be reached.
 *
 * Two are named exceptions and not gaps: `DIVS.L` and `DIVU.L` share their
 * instruction word with each other and with `MULS.L`/`MULU.L`, and only bit 11
 * of the extension word tells them apart. A lookup given one word cannot. */
static void test_every_row_is_returned_by_some_instruction(void) {
  unsigned count = 0;
  const ap_m68030_table_entry_t *table = ap_m68030_timing_table(&count);
  TEST_ASSERT_TRUE(count <= 256u);
  bool reached[256] = {false};

  /* The selected lookup's inputs are an extension word and an outcome, and it
   * reads only the register code and the direction bit of the one: a group B
   * code, a group A code, and each with bit 11 set, reach every combination. */
  static const uint16_t EXTENSIONS[] = {0x0002u, 0x0801u, 0x0802u, 0x0000u,
                                        0x0001u, 0x0009u, 0x000Au, 0x000Bu};
  for (uint32_t word = 0; word <= 0xFFFFu; word++) {
    const ap_m68030_table_entry_t *row =
        ap_m68030_timing_for_word((uint16_t)word);
    if (row != nullptr) {
      reached[row - table] = true;
    }
    for (unsigned e = 0; e < sizeof EXTENSIONS / sizeof EXTENSIONS[0]; e++) {
      for (unsigned outcome = 0; outcome < 2u; outcome++) {
        row = ap_m68030_timing_for_selected((uint16_t)word, EXTENSIONS[e],
                                            outcome != 0u);
        if (row != nullptr) {
          reached[row - table] = true;
        }
      }
    }
    for (unsigned taken = 0; taken < 2u; taken++) {
      row = ap_m68030_timing_for_branch((uint16_t)word, taken != 0u);
      if (row != nullptr) {
        reached[row - table] = true;
      }
    }
  }
  for (unsigned c = 0; c < 4u; c++) {
    const ap_m68030_table_entry_t *row =
        ap_m68030_timing_for_dbcc((c & 1u) != 0u, (c & 2u) != 0u);
    if (row != nullptr) {
      reached[row - table] = true;
    }
  }
  /* §11.6.17's exception rows, which no instruction word reaches: by name, and
   * by every vector against the four words vector 7 is told apart by. */
  static const ap_m68030_exception_row_t NAMED[] = {
      AP_M68030_EXCEPTION_INTERRUPT_I_STACK,
      AP_M68030_EXCEPTION_INTERRUPT_M_STACK, AP_M68030_EXCEPTION_TRACE,
      AP_M68030_EXCEPTION_BUS_FAULT_SHORT,   AP_M68030_EXCEPTION_BUS_FAULT_LONG,
      AP_M68030_EXCEPTION_RTE_THROWAWAY};
  for (unsigned k = 0; k < sizeof NAMED / sizeof NAMED[0]; k++) {
    reached[ap_m68030_timing_for_exception(NAMED[k]) - table] = true;
  }
  static const uint16_t RAISERS[] = {0x4E76u, 0x51FAu, 0x51FBu, 0x51FCu,
                                     0x4180u, 0x4190u, 0x02D0u, 0u};
  for (unsigned vector = 0; vector < 256u; vector++) {
    for (unsigned w = 0; w < sizeof RAISERS / sizeof RAISERS[0]; w++) {
      const ap_m68030_table_entry_t *row =
          ap_m68030_timing_for_vector(vector, RAISERS[w]);
      if (row != nullptr) {
        reached[row - table] = true;
      }
    }
  }

  /* §11.6.6's mode-6 destinations, by every extension word a `MOVE` into
   * `(d8,A0,Xn)` could carry. */
  for (uint32_t extension = 0; extension <= 0xFFFFu; extension++) {
    const ap_m68030_table_entry_t *row =
        ap_m68030_timing_for_move_indexed(0x2180u, (uint16_t)extension);
    if (row != nullptr) {
      reached[row - table] = true;
    }
  }
  /* `DIVS.L Dn,Dn` and `DIVU.L Dn,Dn` were the two named exceptions until
   * 2026-09-15, when the selected lookup took the extension word's signed bit.
   * Every row is reached now. */
  for (unsigned i = 0; i < count; i++) {
    TEST_ASSERT_TRUE_MESSAGE(reached[i], table[i].form);
  }
}

/* The new families decode to their own rows, and the neighbours that share
 * their bits do not borrow one. Each case is a trap in the encoding: `MOVEP`
 * inside the dynamic bit operations, `BCLR`'s operation field reading like a
 * long size, `CMP2` behind a size of `11`, a register source moving into an
 * absolute address, and mode 6 whose format lives in the extension. */
static void test_the_immediate_bit_and_move_forms_find_their_rows(void) {
  static const struct {
    uint16_t word;
    const char *form; /* nullptr: no row, by design */
    const char *what;
  } CASES[] = {
      {0x0C80u, "CMPI #<data>,Dn", "CMPI.L #,D0"},
      {0x0C50u, "CMPI #<data>,Mem", "CMPI.W #,(A0)"},
      {0x0610u, "ADDI #<data>,Mem", "ADDI.B #,(A0)"},
      {0x0280u, "ANDI #<data>,Dn", "ANDI.L #,D0"},
      {0x007Cu, "ANDI/EORI/ORI to SR or CCR", "ORI #,SR"},
      {0x023Cu, "ANDI/EORI/ORI to SR or CCR", "ANDI #,CCR"},
      {0x0C3Cu, nullptr, "CMPI to CCR is not an instruction"},
      {0x00D0u, nullptr, "CMP2.B (A0) -- size 11"},
      {0x0811u, "BTST #<data>,Mem", "BTST #6,(A1)"},
      {0x0880u, "BCLR #<data>,Dn", "BCLR #,D0 -- tt 10 is not a size"},
      {0x0100u, "BTST Dn,Dn", "BTST D0,D0"},
      {0x01D0u, "BSET Dn,Mem", "BSET D0,(A0)"},
      {0x0108u, "MOVEP.W (d16,An),Dn", "MOVEP -- mode 001 among the bit operations"},
      {0x01C9u, "MOVEP.L Dn,(d16,An)", "MOVEP.L D0,(d,A1) -- bits 7-6 of 11"},
      {0x013Cu, "BTST Dn,Mem", "BTST D0,#<data>"},
      {0x2250u, "MOVE EA,An", "MOVEA.L (A0),A1"},
      {0x2010u, "MOVE EA,Dn", "MOVE.L (A0),D0"},
      {0x3080u, "MOVE Rn,(An)", "MOVE.W D0,(A0) -- a register source"},
      {0x3090u, "MOVE SOURCE,(An)", "MOVE.W (A0),(A0)"},
      {0x30BCu, "MOVE SOURCE,(An)", "MOVE.W #<data>,(A0)"},
      {0x21C0u, "MOVE EA,xxx.W", "MOVE.L D0,xxx.W"},
      {0x23C0u, "MOVE EA,xxx.L", "MOVE.L D0,xxx.L"},
      {0x2140u, "MOVE EA,(d16,An)", "MOVE.L D0,(d16,A0)"},
      {0x2180u, nullptr, "MOVE.L D0,(d8,A0,Xn) -- mode 6"},
      {0x5290u, "ADDQ #<data>,Mem", "ADDQ.L #1,(A0)"},
      {0x5390u, "SUBQ #<data>,Mem", "SUBQ.L #1,(A0)"},
      {0x57C0u, "Scc Dn", "SEQ D0"},
      {0x48C0u, "EXT Dn", "EXT.L D0"},
      {0x49C0u, "EXT Dn", "EXTB.L D0"},
      {0x4AC0u, "TAS Dn", "TAS D0"},
      /* §11.6.8's memory sources and the wide forms. */
      {0xD010u, "ADD EA,Dn", "ADD.B (A0),D0"},
      {0xD0D0u, "ADD.W EA,An", "ADDA.W (A0),A0"},
      {0xD1D0u, "ADDA.L EA,An", "ADDA.L (A0),A0"},
      {0xB010u, "CMP EA,Dn", "CMP.B (A0),D0"},
      {0xB1D0u, "CMPA EA,An", "CMPA.L (A0),A0"},
      {0xC0C0u, "MULU.W EA,Dn", "MULU.W D0,D0 -- only an EA row exists"},
      {0xC1D0u, "MULS.W EA,Dn", "MULS.W (A0),D0"},
      {0x80D0u, "DIVU.W EA,Dn", "DIVU.W (A0),D0"},
      {0x80C0u, "DIVU.W Dn,Dn", "DIVU.W D0,D0 -- the register row"},
      {0xC150u, "AND Dn,EA", "AND.W D0,(A0) -- the other direction"},
      /* §11.6.10: each of these used to come back as the arithmetic it
       * shares bits with. */
      {0xC100u, "ABCD Dn,Dn", "ABCD D0,D0, not AND"},
      {0xC309u, "ABCD -(An),-(An)", "ABCD -(A1),-(A1)"},
      {0x8100u, "SBCD Dn,Dn", "SBCD D0,D0, not OR"},
      {0x8148u, "PACK -(An),-(An),#<data>", "PACK -(A0),-(A0),#"},
      {0x8180u, "UNPK Dn,Dn,#<data>", "UNPK D0,D0,#"},
      {0xD180u, "ADDX Dn,Dn", "ADDX.L D0,D0, not ADD"},
      {0x9149u, "SUBX -(An),-(An)", "SUBX.W -(A1),-(A0)"},
      {0xB148u, "CMPM (An)+,(An)+", "CMPM.W (A0)+,(A0)+, not EOR"},
      {0xB140u, "EOR Dn,Dn", "EOR.W D0,D0 still EOR"},
      {0xD1C0u, "ADDA.L Rn,An", "ADDA.L D0,A0, a size of 11 is not ADDX"},
      /* §11.6.11's memory forms, and the words inside their groups that are
       * something else. */
      {0x4250u, "CLR Mem", "CLR.W (A0)"},
      {0x4410u, "NEG Mem", "NEG.B (A0)"},
      {0x4A90u, "TST Mem", "TST.L (A0)"},
      {0x4AD0u, "TAS Mem", "TAS (A0)"},
      {0x4AFCu, nullptr, "ILLEGAL, inside TAS's group"},
      {0x57D0u, "Scc Mem", "SEQ (A0)"},
      {0x51C8u, nullptr, "DBF D0 -- mode 1 is DBcc, priced by branch"},
      {0x51FAu, "TRAPcc.W (No Trap)", "TRAPF.W -- mode 7 register 2"},
      /* §11.6.12. */
      {0xE1D0u, "ASL Mem by 1", "ASL (A0)"},
      {0xE3D0u, "LSd Mem by 1", "LSL (A0) -- bits 10-9 of 01"},
      {0xE0D0u, "ASR Mem by 1", "ASR (A0)"},
      {0xE7D0u, "ROd Mem by 1", "ROL (A0)"},
      {0xE8D0u, nullptr, "BFTST (A0) -- bit 11 set is a bit field"},
      {0xE3A0u, "ASL Dx,Dy", "ASL.L D1,D0"},
      {0xE2A0u, nullptr, "ASR.L D1,D0 -- count-dependent"},
      {0xE3B8u, "ROd Dx,Dy", "ROL.L D1,D0"},
      {0xE8C0u, "BFTST Dn", "BFTST D0 -- the register form is the word's"},
      {0xEFC7u, "BFINS Dn", "BFINS D7"},
      {0xEFC8u, nullptr, "BFINS A0 is not an instruction"},
      /* §11.6.16's address forms, and the modes that are not instructions. */
      {0x4ED0u, "JMP", "JMP (A0)"},
      {0x4EFAu, "JMP", "JMP (d16,PC)"},
      {0x4ED8u, nullptr, "JMP (A0)+ is not an instruction"},
      {0x4E90u, "JSR", "JSR (A0)"},
      {0x43D0u, "LEA", "LEA (A0),A1"},
      {0x41FCu, nullptr, "LEA #<data>,A0 is not an instruction"},
      {0x4850u, "PEA", "PEA (A0)"},
      {0x4840u, "SWAP Dn", "SWAP D0 -- PEA's group with a data register"},
      {0x4848u, "BKPT", "BKPT #0 -- PEA's group with an address register"},
      /* §11.6.7, and the two traps that sat in front of it. */
      {0xC141u, "EXG Ry,Rx", "EXG D0,D1, not AND"},
      {0xC148u, "EXG Ry,Rx", "EXG A0,A0"},
      {0xC189u, "EXG Ry,Rx", "EXG D0,A1"},
      {0x4190u, "CHK EA,Dn (No Exception)", "CHK.W (A0),D0 -- not NEGX"},
      {0x4300u, "CHK Dn,Dn (No Exception)", "CHK.L D0,D1 -- not CLR"},
      {0x4188u, nullptr, "CHK.W A0,D0 is not an instruction"},
      {0x49C0u, "EXT Dn", "EXTB.L D0 still EXT"},
      {0x40C0u, "MOVE SR,Dn", "MOVE SR,D0"},
      {0x40D0u, "MOVE SR,Mem", "MOVE SR,(A0)"},
      {0x42C0u, "MOVE CCR,Dn", "MOVE CCR,D0"},
      {0x44C0u, "MOVE Dn,CCR", "MOVE D0,CCR"},
      {0x44FCu, "MOVE EA,CCR", "MOVE #<data>,CCR"},
      {0x44C8u, nullptr, "MOVE A0,CCR is not an instruction"},
      {0x46C0u, "MOVE EA,SR", "MOVE D0,SR -- no separate register row"},
      {0x4E60u, "MOVE An,USP", "MOVE A0,USP"},
      {0x4E6Fu, "MOVE USP,An", "MOVE USP,A7"},
      {0x4E7Au, "MOVEC Cr,Rn", "MOVEC Cr,Rn"},
      /* §11.6.17's instruction rows: the forms that do not trap. */
      {0x4E72u, "STOP", "STOP #"},
      {0x4E76u, "TRAPV (No Trap)", "TRAPV"},
      {0x51FCu, "TRAPcc (No Trap)", "TRAPF"},
      {0x57FBu, "TRAPcc.L (No Trap)", "TRAPEQ.L"},
      {0x57F8u, "Scc Mem", "SEQ (xxx).W -- mode 7 register 0 is still Scc"},
  };
  for (unsigned c = 0; c < sizeof CASES / sizeof CASES[0]; c++) {
    const ap_m68030_table_entry_t *row =
        ap_m68030_timing_for_word(CASES[c].word);
    if (CASES[c].form == nullptr) {
      TEST_ASSERT_NULL_MESSAGE(row, CASES[c].what);
    } else {
      TEST_ASSERT_TRUE_MESSAGE(form_is(row, CASES[c].form), CASES[c].what);
    }
  }
}

/* The rows only a run can choose between, each found by its encoding, its
 * extension word or its outcome -- and the neighbours in their groups that are
 * not them. `MOVES` and `CAS.L` share `$0Ex`, `CAS2` sits in `CAS`'s group with
 * a mode `CAS` cannot take, and `BSET #` is `CAS`'s encoding with a size of
 * `00`. */
static void test_the_rows_an_extension_or_outcome_selects_are_found(void) {
  static const struct {
    uint16_t word;
    uint16_t extension;
    bool outcome;
    const char *form; /* nullptr: no row, by design */
    const char *what;
  } CASES[] = {
      {0x4E7Bu, 0x0002u, false, "MOVEC Rn,Cr-B", "MOVEC D0,CACR"},
      {0x4E7Bu, 0x8801u, false, "MOVEC Rn,Cr-A", "MOVEC A0,VBR"},
      {0x4E7Bu, 0x0003u, false, nullptr, "MOVEC D0,TC -- a 68040 register"},
      {0x0E50u, 0x0000u, false, "MOVES EA,Rn", "MOVES.W (A0),D0"},
      {0x0E90u, 0x0800u, false, "MOVES Rn,EA", "MOVES.L D0,(A0)"},
      {0x0E40u, 0x0800u, false, nullptr, "MOVES.W D0,D0 is not an instruction"},
      {0x0ED0u, 0x0000u, true, "CAS (Successful Compare)", "CAS.L (A0) -- not MOVES"},
      {0x0AD0u, 0x0000u, false, "CAS (Unsuccessful Compare)", "CAS.B (A0)"},
      {0x08D0u, 0x0000u, true, nullptr, "BSET #,(A0) -- CAS with size 00"},
      {0x0CFCu, 0x0000u, true, "CAS2 (Successful Compare)", "CAS2.W"},
      {0x0EFCu, 0x0000u, false, "CAS2 (Unsuccessful Compare)", "CAS2.L"},
      {0xE8D0u, 0x0000u, false, "BFTST Mem (<5 Bytes)", "BFTST (A0)"},
      {0xE8FAu, 0x0000u, true, "BFTST Mem (5 Bytes)", "BFTST (d16,PC)"},
      {0xEAFAu, 0x0000u, false, nullptr, "BFCHG (d16,PC) -- not alterable"},
      {0xEDD0u, 0x0000u, true, "BFFFO Mem (5 Bytes)", "BFFFO (A0)"},
      {0xE8C0u, 0x0000u, false, nullptr, "BFTST D0 -- the word lookup's"},
      {0xD200u, 0x0000u, false, nullptr, "ADD.B D0,D1 -- not selected at all"},
      {0x4E73u, 0x0000u, false, "RTE (Normal Four Word)", "RTE, format $0"},
      {0x4E73u, 0x0002u, false, "RTE (Six Word)", "RTE, format $2"},
      {0x4E73u, 0x000Au, false, "RTE (Short Fault)", "RTE, format $A"},
      {0x4E73u, 0x000Bu, false, "RTE (Long Fault)", "RTE, format $B"},
      {0x4E73u, 0x0007u, false, nullptr, "RTE, format $7 -- a 68040 frame"},
      {0x02D0u, 0x0800u, false, "CHK2 Mem,Rn (No Exception)", "CHK2.W (A0),D0"},
      {0x02D0u, 0x0000u, false, "CMP2 EA,Rn", "CMP2.W (A0),D0 -- §11.6.8's"},
      {0x4C10u, 0x0800u, false, "MULS.L EA,Dn", "MULS.L (A0),D0"},
      {0x4C3Cu, 0x0000u, false, "MULU.L EA,Dn", "MULU.L #,D0"},
      {0x4C08u, 0x0800u, false, nullptr, "MULS.L A0,D0 is not an instruction"},
      {0x4C41u, 0x0800u, false, "DIVS.L Dn,Dn", "DIVS.L D1,D0"},
      {0x4C41u, 0x0000u, false, "DIVU.L Dn,Dn", "DIVU.L D1,D0"},
      {0x4C50u, 0x0000u, false, "DIVU.L EA,Dn", "DIVU.L (A0),D0"},
      {0x4C80u, 0x0000u, false, nullptr, "MOVEM, not a long multiply"},
      {0x02C0u, 0x0800u, false, nullptr, "CHK2.W D0 -- not a control mode"},
  };
  for (unsigned c = 0; c < sizeof CASES / sizeof CASES[0]; c++) {
    const ap_m68030_table_entry_t *row = ap_m68030_timing_for_selected(
        CASES[c].word, CASES[c].extension, CASES[c].outcome);
    if (CASES[c].form == nullptr) {
      TEST_ASSERT_NULL_MESSAGE(row, CASES[c].what);
    } else {
      TEST_ASSERT_TRUE_MESSAGE(form_is(row, CASES[c].form), CASES[c].what);
    }
    /* And the word lookup leaves every row this one finds alone. The refusals
     * are not all the word lookup's to refuse: `BSET #,(A0)` is its row. */
    if (CASES[c].form != nullptr) {
      TEST_ASSERT_NULL_MESSAGE(ap_m68030_timing_for_word(CASES[c].word),
                               CASES[c].what);
    }
  }
}

/* §11.6.17's exceptions are found by the vector an instruction raised, and
 * vector 7 by the instruction as well: `TRAPV` and the three `TRAPcc` sizes share
 * it at four different costs. The vectors the page does not print have no row,
 * so they keep the bus time they always had rather than borrowing a figure. */
static void test_an_exception_is_priced_by_its_vector_and_its_instruction(void) {
  static const struct {
    unsigned vector;
    uint16_t word;
    const char *form; /* nullptr: no row, by design */
    const char *what;
  } CASES[] = {
      {32u, 0x4E40u, "TRAP #n", "TRAP #0"},
      {47u, 0x4E4Fu, "TRAP #n", "TRAP #15"},
      {48u, 0x0000u, nullptr, "vector 48 is the FPCP's, not a TRAP"},
      {4u, 0x4AFCu, "Illegal Instruction", "ILLEGAL"},
      {10u, 0xA000u, "A-Line Trap", "an A-line word"},
      {11u, 0xF000u, "F-Line Trap", "an F-line word"},
      {8u, 0x4E73u, "Privilege Violation", "RTE in user state"},
      {7u, 0x4E76u, "TRAPV (Trap)", "TRAPV"},
      {7u, 0x51FCu, "TRAPcc (Trap)", "TRAPF -- though it never traps"},
      {7u, 0x57FAu, "TRAPcc.W (Trap)", "TRAPEQ.W"},
      {7u, 0x57FBu, "TRAPcc.L (Trap)", "TRAPEQ.L"},
      {7u, 0xF27Au, nullptr, "cpTRAPcc -- not on the page"},
      {5u, 0x81FCu, nullptr, "zero divide"},
      {6u, 0x4180u, "CHK Dn,Dn (Exception Taken)", "CHK.W D0,D0"},
      {6u, 0x4190u, "CHK EA,Dn (Exception Taken)", "CHK.W (A0),D0"},
      {6u, 0x02D0u, "CHK2 Mem,Rn (Exception Taken)", "CHK2.W (A0)"},
      {14u, 0x4E73u, nullptr, "format error"},
  };
  for (unsigned c = 0; c < sizeof CASES / sizeof CASES[0]; c++) {
    const ap_m68030_table_entry_t *row =
        ap_m68030_timing_for_vector(CASES[c].vector, CASES[c].word);
    if (CASES[c].form == nullptr) {
      TEST_ASSERT_NULL_MESSAGE(row, CASES[c].what);
    } else {
      TEST_ASSERT_TRUE_MESSAGE(form_is(row, CASES[c].form), CASES[c].what);
    }
  }
  TEST_ASSERT_TRUE(form_is(
      ap_m68030_timing_for_exception(AP_M68030_EXCEPTION_TRACE), "TRACE"));
  TEST_ASSERT_TRUE(
      form_is(ap_m68030_timing_for_exception(AP_M68030_EXCEPTION_INTERRUPT_M_STACK),
              "Interrupt (M-Stack)"));

  /* The interrupt on the master stack costs ten more than on the interrupt
   * stack, and it writes four more: the throwaway frame's. */
  const ap_m68030_table_entry_t *i_stack =
      ap_m68030_timing_for_exception(AP_M68030_EXCEPTION_INTERRUPT_I_STACK);
  const ap_m68030_table_entry_t *m_stack =
      ap_m68030_timing_for_exception(AP_M68030_EXCEPTION_INTERRUPT_M_STACK);
  TEST_ASSERT_EQUAL_UINT(10u, m_stack->timing.cache_case - i_stack->timing.cache_case);
  TEST_ASSERT_EQUAL_UINT(4u, m_stack->timing.writes - i_stack->timing.writes);
}

/* §11.6.7's `MOVEM` rows are formulas in n, the registers the mask names, built
 * for the n a run moved. The page's "n > 0" leaves an empty mask unpriced, and
 * each direction takes one increment mode and not the other. */
static void test_movem_is_priced_by_the_registers_it_moves(void) {
  ap_m68030_table_entry_t storage;

  /* MOVEM.L (A0),D0-D7: 8 + 4 * 8. */
  const ap_m68030_table_entry_t *in =
      ap_m68030_timing_for_movem(0x4CD0u, 0x00FFu, &storage);
  TEST_ASSERT_TRUE(form_is(in, "MOVEM EA,RL"));
  TEST_ASSERT_EQUAL_UINT(40u, in->timing.cache_case);
  TEST_ASSERT_EQUAL_UINT(8u, in->timing.reads);
  TEST_ASSERT_TRUE(in->data_dependent);
  TEST_ASSERT_EQUAL_INT(AP_M68030_EA_TIME_CALCULATE_IMMEDIATE,
                        in->effective_address_time);
  TEST_ASSERT_TRUE(ap_m68030_timing_consistent(&in->timing));

  /* MOVEM.L D0-D1,-(A7): 4 + 2 * 2. */
  const ap_m68030_table_entry_t *out =
      ap_m68030_timing_for_movem(0x48E7u, 0xC000u, &storage);
  TEST_ASSERT_TRUE(form_is(out, "MOVEM RL,EA"));
  TEST_ASSERT_EQUAL_UINT(8u, out->timing.cache_case);
  TEST_ASSERT_EQUAL_UINT(2u, out->timing.writes);
  TEST_ASSERT_TRUE(ap_m68030_timing_consistent(&out->timing));

  TEST_ASSERT_NULL(ap_m68030_timing_for_movem(0x4CD0u, 0x0000u, &storage));
  TEST_ASSERT_NULL(ap_m68030_timing_for_movem(0x4880u, 0x00FFu, &storage)); /* EXT.W D0 */
  TEST_ASSERT_NULL(ap_m68030_timing_for_movem(0x48D8u, 0x00FFu, &storage)); /* to (A0)+ */
  TEST_ASSERT_NULL(ap_m68030_timing_for_movem(0x4CE0u, 0x00FFu, &storage)); /* from -(A0) */
  TEST_ASSERT_NULL(ap_m68030_timing_for_movem(0x4A90u, 0x00FFu, &storage)); /* TST.L (A0) */
}

/* §11.6.6's mode-6 destinations are chosen by the destination's extension word:
 * the brief format's one row, or the full format's by base displacement,
 * indirection and outer displacement -- group A when a word base displacement
 * rides on a register. Extension words, `D0.W*1` throughout: bit 8 the full
 * format, bit 7 base suppress, bits 5-4 the base displacement size (`01` null,
 * `10` word, `11` long, `00` reserved), bits 2-0 the indirection. */
static void test_a_move_into_mode_six_is_priced_by_its_extension_word(void) {
  static const struct {
    uint16_t instruction;
    uint16_t extension;
    const char *form; /* nullptr: no row, by design */
    const char *what;
  } CASES[] = {
      {0x2180u, 0x0000u, "MOVE EA,(d8,An,Xn)", "brief"},
      {0x2180u, 0x0110u, "MOVE EA,(B)", "full, no displacement"},
      {0x2180u, 0x0120u, "MOVE EA,(d16,An) or (d16,PC)", "word base off A0"},
      {0x2180u, 0x01A0u, "MOVE EA,(d16,B)", "word base, base suppressed"},
      {0x2180u, 0x0130u, "MOVE EA,(d32,B)", "long base"},
      {0x2180u, 0x0111u, "MOVE EA,([B])", "memory indirect, no outer"},
      {0x2180u, 0x0122u, "MOVE EA,([d16,An],d16) or ([d16,PC],d16)",
       "group A, word outer"},
      {0x2180u, 0x0133u, "MOVE EA,([d32,B],d32)", "the largest"},
      {0x2180u, 0x0100u, nullptr, "a reserved base displacement size"},
      {0x2140u, 0x0000u, nullptr, "MOVE.L D0,(d16,A0) -- mode 5"},
      {0xD180u, 0x0000u, nullptr, "not a MOVE"},
  };
  for (unsigned c = 0; c < sizeof CASES / sizeof CASES[0]; c++) {
    const ap_m68030_table_entry_t *row = ap_m68030_timing_for_move_indexed(
        CASES[c].instruction, CASES[c].extension);
    if (CASES[c].form == nullptr) {
      TEST_ASSERT_NULL_MESSAGE(row, CASES[c].what);
    } else {
      TEST_ASSERT_TRUE_MESSAGE(form_is(row, CASES[c].form), CASES[c].what);
    }
  }
}

/* **Domain/OS's SCSI wait loop, priced in every instruction.** The kernel's
 * `3C4E4612` polls the ASC 160,000 times and gives up; three of its six
 * instructions had no row, so the loop ran at 39 clocks an iteration and
 * expired 395 us before the controller's diagnostic ended. A count-limited
 * poll is only as long as its instructions are, which makes this the smallest
 * test that says the boot's timing is not a lower bound here. */
static void test_the_scsi_drivers_wait_loop_is_priced_in_every_instruction(
    void) {
  TEST_ASSERT_NOT_NULL(ap_m68030_timing_for_word(0x2250u)); /* MOVEA.L (A0),A1 */
  TEST_ASSERT_NOT_NULL(ap_m68030_timing_for_word(0x0811u)); /* BTST #6,(A1) */
  TEST_ASSERT_NOT_NULL(ap_m68030_timing_for_word(0x5280u)); /* ADDQ.L #1,D0 */
  TEST_ASSERT_NOT_NULL(ap_m68030_timing_for_word(0x0C80u)); /* CMPI.L #,D0 */
  TEST_ASSERT_NOT_NULL(ap_m68030_timing_for_branch(0x6704u, false)); /* BEQ */
  TEST_ASSERT_NOT_NULL(ap_m68030_timing_for_branch(0x66EAu, true));  /* BNE */
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
      /* Change of flow. §11.3.3 averages over "the alignment of prefetches
       * associated with an instruction", and for these those are the refill at
       * the target -- but a three-deep pipe needs two fetches whichever
       * alignment the target has, so nothing is averaged. */
      {"RTS", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "change of flow"},
      {"RTR", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "change of flow"},
      {"RTD", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "change of flow"},
      {"BSR", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "change of flow"},
      {"JMP", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "change of flow"},
      {"JSR", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "change of flow"},
      {"MOVE EA,SR", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT,
       "a status register write refills the pipe"},
      {"MOVEC Cr,Rn", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"MOVEP.W Dn,(d16,An)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT,
       "two words"},
      {"MOVEP.W (d16,An),Dn", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT,
       "two words"},
      {"MOVEP.L Dn,(d16,An)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT,
       "two words"},
      {"MOVEP.L (d16,An),Dn", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT,
       "two words"},
      /* The rows an extension word or an outcome selects: every one carries
       * its extension word, and `CAS2` carries two. */
      {"MOVEC Rn,Cr-A", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"MOVEC Rn,Cr-B", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"MOVES EA,Rn", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"MOVES Rn,EA", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"BFTST Dn", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"BFTST Mem (<5 Bytes)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"BFTST Mem (5 Bytes)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"BFCHG Dn", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"BFCHG Mem (<5 Bytes)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"BFCHG Mem (5 Bytes)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"BFCLR Dn", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"BFCLR Mem (<5 Bytes)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"BFCLR Mem (5 Bytes)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"BFSET Dn", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"BFSET Mem (<5 Bytes)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"BFSET Mem (5 Bytes)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"BFEXTS Dn", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"BFEXTS Mem (<5 Bytes)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"BFEXTS Mem (5 Bytes)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"BFEXTU Dn", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"BFEXTU Mem (<5 Bytes)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"BFEXTU Mem (5 Bytes)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"BFINS Dn", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"BFINS Mem (<5 Bytes)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"BFINS Mem (5 Bytes)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"BFFFO Dn", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"BFFFO Mem (<5 Bytes)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"BFFFO Mem (5 Bytes)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"CAS (Successful Compare)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT,
       "two words"},
      {"CAS (Unsuccessful Compare)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT,
       "two words"},
      {"CAS2 (Successful Compare)", AP_M68030_PREFETCH_ODD_WORDS, "three words"},
      {"CAS2 (Unsuccessful Compare)", AP_M68030_PREFETCH_ODD_WORDS,
       "three words"},
      /* §11.6.17: the instruction forms by their length, and every exception
       * by its handler's refill, which is two fetches at either alignment. */
      {"STOP", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"TRAPcc.W (No Trap)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"TRAPcc.L (No Trap)", AP_M68030_PREFETCH_ODD_WORDS, "three words"},
      {"Interrupt (I-Stack)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "refill"},
      {"Interrupt (M-Stack)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "refill"},
      {"TRACE", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "refill"},
      {"TRAP #n", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "refill"},
      {"Illegal Instruction", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "refill"},
      {"A-Line Trap", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "refill"},
      {"F-Line Trap", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "refill"},
      {"Privilege Violation", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "refill"},
      {"TRAPcc (Trap)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "refill"},
      {"TRAPcc.W (Trap)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "refill"},
      {"TRAPcc.L (Trap)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "refill"},
      {"TRAPV (Trap)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "refill"},
      /* §11.6.18: the fault frames' refill, and every return's change of flow
       * -- the throwaway's included, since it belongs to one. */
      {"Bus Cycle Fault (Short)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "refill"},
      {"Bus Cycle Fault (Long)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "refill"},
      {"RTE (Normal Four Word)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT,
       "change of flow"},
      {"RTE (Six Word)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "change of flow"},
      {"RTE (Throwaway)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "change of flow"},
      {"RTE (Coprocessor)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT,
       "change of flow"},
      {"RTE (Short Fault)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT,
       "change of flow"},
      {"RTE (Long Fault)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "change of flow"},
      /* §11.6.16's bounds checks: the exceptions' refill, and `CHK2`'s two
       * words. */
      {"CHK Dn,Dn (Exception Taken)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT,
       "refill"},
      {"CHK EA,Dn (Exception Taken)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT,
       "refill"},
      {"CHK2 Mem,Rn (No Exception)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT,
       "two words"},
      {"CHK2 Mem,Rn (Exception Taken)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT,
       "refill"},
      /* §11.6.8's extension-word forms, two words each. */
      {"CMP2 EA,Rn", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"MULS.L EA,Dn", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"MULU.L EA,Dn", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"DIVS.L Dn,Dn", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"DIVU.L Dn,Dn", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"DIVS.L EA,Dn", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"DIVU.L EA,Dn", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      /* §11.6.6's mode-6 destinations, by the instruction's words and the
       * destination's: the extension word plus its displacements. */
      {"MOVE EA,(d8,An,Xn)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"MOVE EA,(d16,An) or (d16,PC)", AP_M68030_PREFETCH_ODD_WORDS,
       "three words"},
      {"MOVE EA,([d16,An],Xn) or ([d16,PC],Xn)", AP_M68030_PREFETCH_ODD_WORDS,
       "three words"},
      {"MOVE EA,([d16,An],d16) or ([d16,PC],d16)",
       AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "four words"},
      {"MOVE EA,([d16,An],d32) or ([d16,PC],d32)", AP_M68030_PREFETCH_ODD_WORDS,
       "five words"},
      {"MOVE EA,(B)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"MOVE EA,(d16,B)", AP_M68030_PREFETCH_ODD_WORDS, "three words"},
      {"MOVE EA,(d32,B)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "four words"},
      {"MOVE EA,([B])", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"MOVE EA,([d16,B])", AP_M68030_PREFETCH_ODD_WORDS, "three words"},
      {"MOVE EA,([d32,B])", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "four words"},
      {"MOVE EA,([B],d16)", AP_M68030_PREFETCH_ODD_WORDS, "three words"},
      {"MOVE EA,([d16,B],d16)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT,
       "four words"},
      {"MOVE EA,([d32,B],d16)", AP_M68030_PREFETCH_ODD_WORDS, "five words"},
      {"MOVE EA,([B],d32)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "four words"},
      {"MOVE EA,([d16,B],d32)", AP_M68030_PREFETCH_ODD_WORDS, "five words"},
      {"MOVE EA,([d32,B],d32)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT,
       "six words"},
      {"Bcc (Taken)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "change of flow"},
      {"DBcc (cc False, Count Not Expired)",
       AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "it branches"},
      /* An even word count: 2 words is one fetch at either alignment. */
      {"LINK.W", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"Bcc.W (Not Taken)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT,
       "two words"},
      {"DBcc (cc True)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"DBcc (cc False, Count Expired)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT,
       "two words, and it falls through"},
      {"ANDI/EORI/ORI to SR or CCR", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT,
       "two words"},
      {"ADDI #<data>,Dn", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      /* The only rows left unknown: an odd word count of three, where the two
       * alignments genuinely differ by one fetch. */
      {"LINK.L", AP_M68030_PREFETCH_ODD_WORDS, "three words"},
      {"Bcc.L (Not Taken)", AP_M68030_PREFETCH_ODD_WORDS, "three words"},
      /* §11.6.9 and §11.6.13's immediate forms, `ADDI #<data>,Dn`'s
       * convention: the immediate is an extension word, so never one. */
      {"ADDI #<data>,Mem", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "immediate"},
      {"ANDI #<data>,Dn", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "immediate"},
      {"ANDI #<data>,Mem", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "immediate"},
      {"EORI #<data>,Dn", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "immediate"},
      {"EORI #<data>,Mem", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "immediate"},
      {"ORI #<data>,Dn", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "immediate"},
      {"ORI #<data>,Mem", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "immediate"},
      {"SUBI #<data>,Dn", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "immediate"},
      {"SUBI #<data>,Mem", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "immediate"},
      {"CMPI #<data>,Dn", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "immediate"},
      {"CMPI #<data>,Mem", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "immediate"},
      {"BTST #<data>,Dn", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "bit number"},
      {"BTST #<data>,Mem", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "bit number"},
      {"BCHG #<data>,Dn", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "bit number"},
      {"BCHG #<data>,Mem", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "bit number"},
      {"BCLR #<data>,Dn", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "bit number"},
      {"BCLR #<data>,Mem", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "bit number"},
      {"BSET #<data>,Dn", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "bit number"},
      {"BSET #<data>,Mem", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "bit number"},
      /* §11.6.6's destinations with extension words of their own: one word
       * of displacement or short address is two words, a long address three. */
      {"MOVE EA,(d16,An)", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"MOVE EA,xxx.W", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"MOVE EA,xxx.L", AP_M68030_PREFETCH_ODD_WORDS, "three words"},
      /* §11.6.10's adjustment-word pair. */
      {"PACK Dn,Dn,#<data>", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"PACK -(An),-(An),#<data>", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT,
       "two words"},
      {"UNPK Dn,Dn,#<data>", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT, "two words"},
      {"UNPK -(An),-(An),#<data>", AP_M68030_PREFETCH_ALIGNMENT_INVARIANT,
       "two words"},
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

  TEST_ASSERT_EQUAL_UINT(sizeof EXPECTED / sizeof EXPECTED[0], matched);
  TEST_ASSERT_EQUAL_UINT(sizeof EXPECTED / sizeof EXPECTED[0], non_single);

  /* **No row declines any more.** Every row in the table is priced by one of
   * the three derived rules, so `UNKNOWN` exists only to catch a row added
   * without a class decision. */
  unsigned unknown = 0;
  for (unsigned i = 0; i < count; i++) {
    if (table[i].prefetch_class == AP_M68030_PREFETCH_UNKNOWN) {
      unknown++;
    }
  }
  TEST_ASSERT_EQUAL_UINT(0u, unknown);
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
  RUN_TEST(test_every_row_is_returned_by_some_instruction);
  RUN_TEST(test_the_immediate_bit_and_move_forms_find_their_rows);
  RUN_TEST(test_the_rows_an_extension_or_outcome_selects_are_found);
  RUN_TEST(test_an_exception_is_priced_by_its_vector_and_its_instruction);
  RUN_TEST(test_movem_is_priced_by_the_registers_it_moves);
  RUN_TEST(test_a_move_into_mode_six_is_priced_by_its_extension_word);
  RUN_TEST(test_the_scsi_drivers_wait_loop_is_priced_in_every_instruction);
  RUN_TEST(test_the_rows_that_expose_a_prefetch_are_the_memory_forms);
  RUN_TEST(test_an_exact_prefetch_cost_is_not_always_zero_or_one);
  RUN_TEST(test_the_figures_compose_through_the_overlap_rule);
  return UNITY_END();
}

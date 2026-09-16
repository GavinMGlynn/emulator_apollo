/* MC68030 logical memory access: the order a read actually takes.
 *
 * Cited to MC68030 User's Manual 3ed §6.1.
 *
 * The whole content of this module is the order, and the order is the reverse
 * of the intuitive one: the cache answers *before* the MMU is consulted, which
 * is only possible because the 68030's caches are logically addressed. These
 * tests assert that ordering directly -- that a cache hit costs no clocks *and*
 * does not consult the MMU -- because a model that translates first produces
 * the same values with the wrong timing and the wrong faults.
 */

#include "cpu/m68030/ap_m68030_access.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define FC_SUPERVISOR_DATA 5u
#define PAGE_FRAME 0x00A00000u
#define ADDRESS 0x00001018u

/* A memory system that answers every fill from a 32-bit STERM port able to
 * burst, and counts how often it was asked. */
typedef struct {
  unsigned fills;
  unsigned table_fetches;
  unsigned stores;
  uint32_t last_store_address;
  uint32_t last_store_value;
  uint32_t last_fill;
  bool tables_invalid;
  bool store_refuses;
  unsigned narrow_reads;
  uint32_t last_narrow_address;
  unsigned last_narrow_size;
  uint32_t inhibit_asked;
} memory_t;

static bool memory_store(void *context, uint32_t physical, uint32_t value,
                         unsigned size) {
  (void)size;
  memory_t *memory = (memory_t *)context;
  memory->stores++;
  memory->last_store_address = physical;
  memory->last_store_value = value;
  return !memory->store_refuses;
}

static void memory_fill(void *context, uint32_t line_address,
                        uint8_t function_code, ap_m68030_fill_answer_t *out) {
  (void)function_code;
  memory_t *memory = (memory_t *)context;
  memory->fills++;
  /* Which address the *bus* was asked for. The cache is tagged logically and
   * filled physically, and only this says which of the two reached memory. */
  memory->last_fill = line_address;
  out->termination = AP_M68030_TERM_STERM;
  out->burst_acknowledge = true;
  const unsigned base = ap_m68030_cache_entry_index(line_address);
  for (unsigned e = 0; e < AP_M68030_BURST_BEATS; e++) {
    out->data[e] = 0xC0DE0000u + ((base + e) % AP_M68030_BURST_BEATS);
  }
}

/* A one-level tree: the root descriptor is an early-termination page. */
static bool table_fetch(void *context, uint32_t physical, bool long_format,
                        ap_m68030_descriptor_t *out) {
  (void)physical;
  (void)long_format;
  memory_t *memory = (memory_t *)context;
  memory->table_fetches++;
  if (memory->tables_invalid) {
    *out = (ap_m68030_descriptor_t){.dt = AP_M68030_DT_INVALID};
    return true;
  }
  *out = (ap_m68030_descriptor_t){.dt = AP_M68030_DT_PAGE,
                                  .address_field = PAGE_FRAME >> 8,
                                  .used = true};
  return true;
}

static bool table_update(void *context, uint32_t physical, bool set_used,
                         bool set_modified) {
  (void)context;
  (void)physical;
  (void)set_used;
  (void)set_modified;
  return true;
}


/* A device the board declares cache-inhibited, and the narrow cycle it answers.
 * Both are recorded by *address*, because the question these tests exist to
 * settle is which of the two addresses -- the one the program named or the one
 * the bus carried -- each of them was asked about. */
static bool inhibits_device(void *context, uint32_t address) {
  memory_t *memory = (memory_t *)context;
  memory->inhibit_asked = address;
  return (address & 0xFFF00000u) == (PAGE_FRAME & 0xFFF00000u);
}

static bool device_read_sized(void *context, uint32_t address,
                              uint8_t function_code, unsigned size,
                              uint32_t *out) {
  /* The function code reaches a device now -- CPU space is not memory. This
   * fake is memory, so it ignores it; `machine_suite` is where the CPU-space
   * decode is exercised. */
  (void)function_code;
  memory_t *memory = (memory_t *)context;
  memory->narrow_reads++;
  memory->last_narrow_address = address;
  memory->last_narrow_size = size;
  *out = 0x5Au;
  return true;
}

static ap_m68030_tc_t three_level_4k(void) {
  return ap_m68030_tc_decode(UINT32_C(0x80000000) | (12u << 20) | (0u << 16) |
                             (7u << 12) | (7u << 8) | (6u << 4) | 0u);
}

typedef struct {
  ap_m68030_cache_t cache;
  ap_m68030_atc_t atc;
  ap_m68030_tc_t tc;
  ap_m68030_root_t root;
  memory_t memory;
} machine_t;

static machine_t make_machine(void) {
  machine_t m = {0};
  ap_m68030_cache_clear(&m.cache);
  ap_m68030_atc_flush(&m.atc);
  m.tc = three_level_4k();
  m.root = (ap_m68030_root_t){.table_address = 0x00010000u};
  return m;
}

static ap_m68030_access_ctx_t context_of(machine_t *m) {
  return (ap_m68030_access_ctx_t){
      .cache = &m->cache,
      .atc = &m->atc,
      .tc = &m->tc,
      .root = &m->root,
      .cache_enabled = true,
      .burst_enabled = true,
      .table_fetch = table_fetch,
      .table_update = table_update,
      .fill = memory_fill,
      .store = memory_store,
      .context = &m->memory,
  };
}

/* ## §7.7.4's two halves of an indivisible operation, and which cycle is which
 *
 * `[030]` §7.7.1: the sequence "causes the bus arbitration state machine to
 * ignore bus requests (assertions of BR) that occur **after the first read
 * cycle** of the read-modify-write sequence". So a lock has two halves that
 * behave differently -- a request during the opening read still walks the
 * arbiter to its grant states, one after it is not acted on at all -- and
 * `ap_m68030_arb.h` has modelled all three states since it was written.
 *
 * **Only two of them were ever driven.** `ap_arbiter_set_processor_rmc` took a
 * `bool`, because the whole sequence ran inside one `ap_m68030_step` and the
 * clocks were delivered afterwards: the board could be told "this instruction
 * held the bus" and not which cycle was running. So the lock was one
 * instruction wide where the hardware's is narrower, which is conservative --
 * it refuses a grant the hardware would allow -- and was a named plan item.
 *
 * With the bus arbitrated inside the cycle the phase is known at each cycle,
 * and this is the test that it is the *right* phase. It asserts the sequence a
 * `TAS` produces: an ordinary access outside any operation, then the opening
 * read, then the write, then an ordinary access again. */
static ap_m68030_rmc_t seen_phase[8];
static unsigned seen_count;

static unsigned record_phase(void *context, ap_m68030_rmc_t rmc) {
  (void)context;
  if (seen_count < 8u) {
    seen_phase[seen_count] = rmc;
  }
  seen_count++;
  return 0u;
}

static void test_the_opening_read_of_a_lock_is_first_read_and_the_rest_locked(
    void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);
  ctx.bus_acquire = record_phase;
  seen_count = 0;

  /* **Warm the translation first, and the reason is the other half of this
   * item.** A table search is itself an extended read-modify-write and now
   * brackets itself with its own phases, so a *cold* access reaches the hook
   * several times -- once for the search's opening read, once as it closes the
   * lock, once as it restores, and once for the data cycle. That is correct and
   * it is not what this test is about, so the ATC entry is made resident first:
   * a read to establish it and a write so its `M` bit is set, since §9.4 makes
   * the first write to a clean page run a search of its own.
   *
   * Every address below is inside the same 4-KB page, so one search covers all
   * of them and the measured cycles are data cycles alone. The cache is cleared
   * afterwards so each read still misses and runs one. */
  (void)ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  (void)ap_m68030_access_write(&ctx, ADDRESS, FC_SUPERVISOR_DATA, 0u, 1u);
  ap_m68030_cache_clear(&m.cache);
  seen_count = 0;

  /* Outside any operation. A cache miss runs a cycle and the hook is reached --
   * a hit would run none, which is why each step below uses an address the one
   * before it did not touch. */
  (void)ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(1u, seen_count,
                                 "a translated read must run one bus cycle");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(AP_M68030_RMC_NONE, seen_phase[0],
                                 "an ordinary access is not part of a lock");

  /* `TAS` asserts RMC, reads, then writes -- §7.3.5's flowchart. The read is
   * the one the arbiter still listens through. */
  ctx.rmc = true;
  (void)ap_m68030_access_read(&ctx, ADDRESS + 0x40u, FC_SUPERVISOR_DATA);
  TEST_ASSERT_EQUAL_UINT(2u, seen_count);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      AP_M68030_RMC_FIRST_READ, seen_phase[1],
      "the read that opens a lock is the first read cycle");

  /* And the write is past it. A write always is: the flowchart reads before it
   * writes, so no write can be the opening cycle. */
  (void)ap_m68030_access_write(&ctx, ADDRESS + 0x40u, FC_SUPERVISOR_DATA, 0x55u,
                               1u);
  TEST_ASSERT_EQUAL_UINT(3u, seen_count);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(AP_M68030_RMC_LOCKED, seen_phase[2],
                                 "a write inside a lock is past the first read");

  /* A second read inside the same operation -- which `CAS2` performs -- is also
   * past the opening cycle, so it must not report itself as the first. */
  (void)ap_m68030_access_read(&ctx, ADDRESS + 0x80u, FC_SUPERVISOR_DATA);
  TEST_ASSERT_EQUAL_UINT(4u, seen_count);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      AP_M68030_RMC_LOCKED, seen_phase[3],
      "only the opening read of an operation is the first read");

  /* And the state does not leak into the next operation: with RMC negated the
   * next access is ordinary, and the one after that opens a fresh lock. */
  ctx.rmc = false;
  (void)ap_m68030_access_read(&ctx, ADDRESS + 0xC0u, FC_SUPERVISOR_DATA);
  TEST_ASSERT_EQUAL_UINT(AP_M68030_RMC_NONE, seen_phase[4]);
  ctx.rmc = true;
  (void)ap_m68030_access_read(&ctx, ADDRESS + 0x100u, FC_SUPERVISOR_DATA);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      AP_M68030_RMC_FIRST_READ, seen_phase[5],
      "a later operation opens with its own first read cycle");
}

/* ## A translation table search locks the bus, because §11.9 says it is one
 *
 * "Since the address translation search is an **extended read-modify-write
 * operation**, the no-cache-case latency is incurred by the longest address
 * translation search required by the system." §12.1.2 gives the pins -- "the
 * MC68030 asserts `RMC` but not `CIOUT`" -- and §11.7's table counts "an RMC
 * cycle to set the U bit ... as one read and one write", so the history-bit
 * write-back is inside the same lock.
 *
 * `ap_m68030_walk.h` had cited the rule from §9 since it was written and
 * nothing asserted it: the walk reads descriptors through a plain callback with
 * no bus object, so there was no cycle to assert anything on, and the plan
 * named it as waiting on the sequencer. With the bus arbitrated inside the
 * cycle there is somewhere to say it.
 *
 * The assertion is on the **phases**, in order: the search opens in §7.7.4's
 * first-read phase -- the one moment a request is still acted on -- then closes
 * the lock for the whole walk, then restores. "Extended" is the point: the lock
 * spans the tree, not one descriptor. */
static void test_a_table_search_locks_the_bus_for_its_whole_walk(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);
  ctx.bus_acquire = record_phase;
  seen_count = 0;

  /* Cold: nothing in the ATC, so this access must search. */
  const ap_m68030_access_result_t r =
      ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_TRUE_MESSAGE(r.descriptor_fetches > 0,
                           "this test needs an access that actually searches");

  /* More than the bracket's own two calls, because each descriptor fetch is a
   * bus cycle now and reaches the hook like any other. */
  TEST_ASSERT_TRUE_MESSAGE(seen_count > 4u,
                           "a search runs a cycle for every descriptor it reads");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(AP_M68030_RMC_FIRST_READ, seen_phase[0],
                                 "the search opens in the first-read phase");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(AP_M68030_RMC_LOCKED, seen_phase[1],
                                 "and then locks for the whole walk");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      AP_M68030_RMC_NONE, seen_phase[seen_count - 1u],
      "the data cycle that follows the search is an ordinary one");
}

/* And a search that runs *inside* an indivisible operation must not release the
 * lock the operation is holding when it finishes. A `TAS` whose page is not yet
 * resident searches in the middle of its own read-modify-write, and dropping
 * `RMC` there would let a master in where §7.7.1 forbids it. */
static void test_a_search_inside_a_lock_restores_the_lock_it_interrupted(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);
  ctx.bus_acquire = record_phase;
  seen_count = 0;

  ctx.rmc = true;
  (void)ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);

  TEST_ASSERT_TRUE(seen_count > 4u);
  TEST_ASSERT_EQUAL_UINT(AP_M68030_RMC_FIRST_READ, seen_phase[0]);
  TEST_ASSERT_EQUAL_UINT(AP_M68030_RMC_LOCKED, seen_phase[1]);
  /* The data cycle is the operation's opening read, which is still the first
   * read cycle of the operation itself -- the search's lock was a different,
   * nested one and does not consume it. It is also the proof that
   * `end_table_search` restored the enclosing operation's lock rather than
   * dropping it: every call between the bracket and this one is `LOCKED`. */
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      AP_M68030_RMC_FIRST_READ, seen_phase[seen_count - 1u],
      "the operation's own opening read follows the search it provoked");
  for (unsigned i = 1u; i + 1u < seen_count; i++) {
    TEST_ASSERT_EQUAL_UINT_MESSAGE(
        AP_M68030_RMC_LOCKED, seen_phase[i],
        "a search inside an operation stays locked throughout");
  }
}

/* ## A table search costs time, which it did not until 2026-09-16
 *
 * `ap_m68030_walk.h` said each descriptor fetch "is a real bus cycle through
 * `ap_m68030_bus`" and it was not one: the walk reached memory through a
 * callback that goes straight to the board, and **no clocks were charged for a
 * search at all** -- `out.clocks` came from the data cycle alone. Every ATC miss
 * translated instantaneously.
 *
 * `[030]` §11.9 is explicit that it should not: the no-cache-case latency is
 * "incurred by the longest address translation search required by the system",
 * which is the whole reason a three-level tree costs more than one that
 * terminates early. The walk counts its fetches to express exactly that, and
 * nothing spent them.
 *
 * The assertion is a comparison rather than a figure, because the figure is the
 * memory system's: the same address read twice, once with the translation cold
 * and once with it resident, differing only by the search. */
static void test_a_table_search_costs_the_clocks_its_fetches_take(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);

  const ap_m68030_access_result_t cold =
      ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  TEST_ASSERT_TRUE(cold.ok);
  TEST_ASSERT_TRUE_MESSAGE(cold.descriptor_fetches > 0,
                           "this test needs an access that actually searches");

  /* The cache is cleared so the second read still runs its data cycle; the ATC
   * is left alone, so it does *not* search. The difference between the two is
   * the search and nothing else. */
  ap_m68030_cache_clear(&m.cache);
  const ap_m68030_access_result_t warm =
      ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  TEST_ASSERT_TRUE(warm.ok);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, warm.descriptor_fetches,
                                 "the translation must be resident by now");

  TEST_ASSERT_TRUE_MESSAGE(
      cold.clocks > warm.clocks,
      "a translation table search must cost the bus time its fetches take");
}

/* The first access misses everything: the MMU is consulted, a table search
 * runs, and the bus is used. */
static void test_a_cold_access_consults_the_mmu_and_pays_for_it(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);

  const ap_m68030_access_result_t r =
      ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_FALSE(r.cache_hit);
  TEST_ASSERT_TRUE(r.mmu_consulted);
  TEST_ASSERT_TRUE(r.descriptor_fetches > 0);
  TEST_ASSERT_TRUE(r.clocks > 0);
}

/* The point of the module. "the MMU is completely ignored" on a cache hit --
 * so the second access costs no clocks *and* does not consult the MMU, which
 * are two separate claims and both are asserted. */
static void test_a_cache_hit_costs_nothing_and_skips_the_mmu(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);

  const ap_m68030_access_result_t first =
      ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  const unsigned fetches_after_first = m.memory.table_fetches;
  const unsigned fills_after_first = m.memory.fills;

  const ap_m68030_access_result_t second =
      ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);

  TEST_ASSERT_TRUE(second.cache_hit);
  TEST_ASSERT_EQUAL_UINT32(0, second.clocks);
  TEST_ASSERT_FALSE(second.mmu_consulted);
  TEST_ASSERT_EQUAL_HEX32(first.value, second.value);

  /* Neither the tables nor the bus were touched again. */
  TEST_ASSERT_EQUAL_UINT(fetches_after_first, m.memory.table_fetches);
  TEST_ASSERT_EQUAL_UINT(fills_after_first, m.memory.fills);
}

/* The burst filled the whole line, so the three neighbouring long words are
 * hits too -- and they likewise skip the MMU. */
/* **The read half of an indivisible operation is forced to miss**, even when the
 * long word is sitting in the data cache from an ordinary read a moment before.
 *
 * `[030]` §6.1.2.2: "The read portion of a read-modify-write cycle is always
 * forced to miss in the data cache." §11.4's note repeats it from the timing
 * end: "RMC cycles (e.g., TAS and CAS) are forced to miss on data cache reads.
 * Therefore, a data cache hit has no effect on these instructions."
 *
 * The reason is not the clock count. A `TAS` answered from the cache reads
 * whatever the line held, and an alternate bus master can have written that
 * location since -- nothing invalidates the line on someone else's write. The
 * forced miss is what makes the semaphore read see memory, which is the whole
 * point of the operation being indivisible.
 *
 * This core passed a literal `false` for the flag, so the rule reached neither
 * this lookup nor the one inside `ap_m68030_cache_read`. Found walking `[030]`
 * §11 on 2026-09-06 and confirmed against §6's page image. */
static void test_an_rmc_read_is_forced_to_miss_the_data_cache(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);

  /* Warm the line with an ordinary read, and check it is warm. */
  (void)ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  const ap_m68030_access_result_t warm =
      ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  TEST_ASSERT_TRUE(warm.cache_hit);
  TEST_ASSERT_EQUAL_UINT32(0, warm.clocks);

  const unsigned fills_before = m.memory.fills;

  /* The same address again, this time as the read half of a TAS or CAS. */
  ctx.rmc = true;
  const ap_m68030_access_result_t locked =
      ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  ctx.rmc = false;

  TEST_ASSERT_FALSE(locked.cache_hit);
  TEST_ASSERT_TRUE(locked.clocks > 0u);
  /* It went to memory: the fill ran, which is also the rest of §6.1.2.2 --
   * "the processor either uses the data read from memory to update a matching
   * entry in the data cache or creates a new entry with the read data". */
  TEST_ASSERT_TRUE(m.memory.fills > fills_before);
  TEST_ASSERT_EQUAL_HEX32(warm.value, locked.value);

  /* And the line is still usable afterwards: the forced miss suppresses the
   * hit, it does not invalidate the entry. */
  const ap_m68030_access_result_t after =
      ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  TEST_ASSERT_TRUE(after.cache_hit);
}

/* The control for the rule above: with `rmc` clear the very same sequence hits,
 * so the test above is measuring the flag and not some other difference. */
static void test_the_same_read_without_rmc_still_hits(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);

  (void)ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  const unsigned fills_before = m.memory.fills;

  const ap_m68030_access_result_t plain =
      ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);

  TEST_ASSERT_TRUE(plain.cache_hit);
  TEST_ASSERT_EQUAL_UINT32(0, plain.clocks);
  TEST_ASSERT_EQUAL_UINT(fills_before, m.memory.fills);
}

static void test_the_rest_of_the_filled_line_also_skips_the_mmu(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);

  (void)ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  const unsigned fetches = m.memory.table_fetches;

  for (unsigned e = 0; e < AP_M68030_BURST_BEATS; e++) {
    const ap_m68030_access_result_t r = ap_m68030_access_read(
        &ctx, 0x00001010u + (e * 4u), FC_SUPERVISOR_DATA);
    TEST_ASSERT_TRUE(r.cache_hit);
    TEST_ASSERT_FALSE(r.mmu_consulted);
    TEST_ASSERT_EQUAL_UINT32(0, r.clocks);
  }
  TEST_ASSERT_EQUAL_UINT(fetches, m.memory.table_fetches);
}

/* With the cache disabled every access consults the MMU -- which is the same
 * effect MD's IC command exposes on real hardware, now visible end to end. */
static void test_a_disabled_cache_consults_the_mmu_every_time(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);
  ctx.cache_enabled = false;

  for (unsigned i = 0; i < 3; i++) {
    const ap_m68030_access_result_t r =
        ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
    TEST_ASSERT_FALSE(r.cache_hit);
    TEST_ASSERT_TRUE(r.mmu_consulted);
    TEST_ASSERT_TRUE(r.clocks > 0);
  }
  TEST_ASSERT_EQUAL_UINT(3, m.memory.fills);
}

/* The CDIS signal overrides CACR, so asserting it has the same effect as
 * disabling the cache in software. */
static void test_the_cache_disable_signal_overrides_the_enable_bit(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);
  ctx.cache_disable = true;

  (void)ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  const ap_m68030_access_result_t second =
      ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);

  TEST_ASSERT_FALSE(second.cache_hit);
  TEST_ASSERT_TRUE(second.mmu_consulted);
}

/* A transparently translated access skips the tables entirely: no descriptor
 * fetch runs, and the physical address is the logical one. */
static void test_a_transparent_access_skips_the_translation_tables(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);
  ctx.cache_enabled = false; /* force the MMU to be consulted every time */

  const ap_m68030_tt_t tt = {.logical_base = 0x00,
                             .logical_mask = 0xFF,
                             .fc_mask = 0x7,
                             .enabled = true,
                             .ignore_read_write = true};
  ctx.tt0 = &tt;

  const ap_m68030_access_result_t r =
      ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);

  TEST_ASSERT_TRUE(r.transparent);
  TEST_ASSERT_EQUAL_UINT(0, m.memory.table_fetches);
  TEST_ASSERT_EQUAL_HEX32(ADDRESS, r.physical);
}

/* A second miss to a *different* page reuses the ATC entry rather than walking
 * again, provided it is the same page -- so the table search is paid once per
 * page, not once per line. */
static void test_the_table_search_is_paid_once_per_page(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);

  (void)ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  const unsigned after_first = m.memory.table_fetches;

  /* A different cache line, same 4K page: a cache miss but an ATC hit. */
  const ap_m68030_access_result_t r =
      ap_m68030_access_read(&ctx, ADDRESS + 0x40u, FC_SUPERVISOR_DATA);

  TEST_ASSERT_FALSE(r.cache_hit);
  TEST_ASSERT_TRUE(r.mmu_consulted);
  TEST_ASSERT_EQUAL_UINT(0, r.descriptor_fetches);
  TEST_ASSERT_EQUAL_UINT(after_first, m.memory.table_fetches);
}


/* ---------------------------------------------------------------------------
 * Writes. The asymmetry with reads is the point: a read can be answered from
 * the cache alone, a write never can, because the data cache is writethrough.
 * ------------------------------------------------------------------------- */

/* A write always consults the MMU, even to a page already cached and already
 * translated -- which is also what makes write protection work on a resident
 * page. */
static void test_a_write_always_consults_the_mmu(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);

  /* Warm everything: after this the line is cached and the page translated. */
  (void)ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  const ap_m68030_access_result_t read_again =
      ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  TEST_ASSERT_TRUE(read_again.cache_hit);
  TEST_ASSERT_FALSE(read_again.mmu_consulted);

  /* The write to that same, fully warm address still consults the MMU. */
  const ap_m68030_access_result_t write = ap_m68030_access_write(
      &ctx, ADDRESS, FC_SUPERVISOR_DATA, 0x12345678u, true);
  TEST_ASSERT_TRUE(write.ok);
  TEST_ASSERT_TRUE(write.mmu_consulted);
}

/* [030] 9.4's consequence end to end: an ATC entry created by a read has M
 * clear, so the first write to that page costs a full table search even though
 * the translation was already cached. */
static void test_a_write_to_a_read_only_warmed_page_costs_a_table_search(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);

  (void)ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  const unsigned after_read = m.memory.table_fetches;

  /* A second *read* pays nothing more -- the ATC entry serves it. */
  (void)ap_m68030_access_read(&ctx, ADDRESS + 0x40u, FC_SUPERVISOR_DATA);
  TEST_ASSERT_EQUAL_UINT(after_read, m.memory.table_fetches);

  /* The first *write* to the same page does pay, because M is clear. */
  const ap_m68030_access_result_t write = ap_m68030_access_write(
      &ctx, ADDRESS, FC_SUPERVISOR_DATA, 0x12345678u, true);
  TEST_ASSERT_TRUE(write.descriptor_fetches > 0);
  TEST_ASSERT_TRUE(m.memory.table_fetches > after_read);
}

/* And once M is set, subsequent writes to the page are ordinary ATC hits that
 * cost no further search -- so the price is paid once, not per write. */
static void test_the_modified_bit_is_paid_for_once(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);

  (void)ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  (void)ap_m68030_access_write(&ctx, ADDRESS, FC_SUPERVISOR_DATA, 1, true);
  const unsigned after_first_write = m.memory.table_fetches;

  const ap_m68030_access_result_t second = ap_m68030_access_write(
      &ctx, ADDRESS, FC_SUPERVISOR_DATA, 2, true);

  TEST_ASSERT_TRUE(second.ok);
  TEST_ASSERT_EQUAL_UINT(0, second.descriptor_fetches);
  TEST_ASSERT_EQUAL_UINT(after_first_write, m.memory.table_fetches);
}

/* A write hit updates the cache as well as memory, so a later read sees the
 * written value rather than the stale filled one. */
static void test_a_write_hit_updates_the_cached_value(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);

  const ap_m68030_access_result_t first =
      ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  /* `4u`, the size. This read `true` until byte-lane merging exposed it: the
   * old cache replaced the whole entry regardless of size, so a one-byte write
   * of a long value looked like a long write and the test passed for the wrong
   * reason. */
  (void)ap_m68030_access_write(&ctx, ADDRESS, FC_SUPERVISOR_DATA, 0xFEEDFACEu,
                               4u);

  const ap_m68030_access_result_t after =
      ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  TEST_ASSERT_TRUE(after.cache_hit);
  TEST_ASSERT_EQUAL_HEX32(0xFEEDFACEu, after.value);
  TEST_ASSERT_NOT_EQUAL_UINT32(first.value, after.value);
}

/* A transparently translated write skips the tables, exactly as a read does. */
static void test_a_transparent_write_skips_the_tables(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);
  const ap_m68030_tt_t tt = {.logical_base = 0x00,
                             .logical_mask = 0xFF,
                             .fc_mask = 0x7,
                             .enabled = true,
                             .ignore_read_write = true};
  ctx.tt0 = &tt;

  const ap_m68030_access_result_t write = ap_m68030_access_write(
      &ctx, ADDRESS, FC_SUPERVISOR_DATA, 0x11223344u, true);

  TEST_ASSERT_TRUE(write.transparent);
  TEST_ASSERT_EQUAL_UINT(0, m.memory.table_fetches);
  TEST_ASSERT_EQUAL_UINT(0, write.descriptor_fetches);
}


/* Writethrough means the external write happens on *every* write that reaches
 * memory, not only on a miss. An access module that updated the cache and
 * skipped the bus would report writethrough while behaving like writeback --
 * and would pass every other test in this file, since nothing else observes
 * memory. This one does. */
static void test_every_write_reaches_memory(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);

  /* Warm the line so the second write is a cache *hit*. */
  (void)ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  (void)ap_m68030_access_write(&ctx, ADDRESS, FC_SUPERVISOR_DATA, 0x11111111u,
                               true);
  TEST_ASSERT_EQUAL_UINT(1, m.memory.stores);

  const ap_m68030_access_result_t hit = ap_m68030_access_write(
      &ctx, ADDRESS, FC_SUPERVISOR_DATA, 0x22222222u, true);
  TEST_ASSERT_TRUE(hit.ok);

  /* The hit wrote through as well: two writes, two stores. */
  TEST_ASSERT_EQUAL_UINT(2, m.memory.stores);
  TEST_ASSERT_EQUAL_HEX32(0x22222222u, m.memory.last_store_value);
  /* And to the *physical* address, not the logical one. */
  TEST_ASSERT_EQUAL_HEX32(hit.physical, m.memory.last_store_address);
}

/* ## The cache is tagged logically and filled physically
 *
 * The MC68030's on-chip caches are logically addressed, so a hit is decided
 * before any translation -- but the bus cycle that fills a miss uses the
 * address the MMU produced. Those were one parameter here, which is invisible
 * with the MMU off because the two are equal, and wrong the moment a page is
 * mapped anywhere but on top of itself: the translation was computed, reported
 * in `out.physical`, and then not used.
 *
 * The write path had always used the physical address, so a mapped page could
 * be *written* where the MMU said and *read* from where it was not. The boot
 * PROM's loaded diagnostic is what found it -- its MMU test maps every page one
 * higher and reads back through the mapping.
 */
static void test_a_read_miss_fills_from_the_physical_address(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t access = context_of(&m);
  const uint32_t logical = 0x00001010u;
  const ap_m68030_access_result_t got = ap_m68030_access_read(
      &access, logical, FC_SUPERVISOR_DATA);

  TEST_ASSERT_TRUE(got.ok);
  TEST_ASSERT_EQUAL_UINT(1u, m.memory.fills);

  /* The translation this harness's table produces, and *not* the logical
   * address it was asked for -- which is what the fill used before. */
  TEST_ASSERT_NOT_EQUAL_UINT32(logical & ~UINT32_C(0xF), got.physical &
                                                             ~UINT32_C(0xF));
  TEST_ASSERT_EQUAL_HEX32(got.physical & ~UINT32_C(0xF), m.memory.last_fill);
}


/* A byte of a device, addressed the way the bus addresses it.
 *
 * The narrow cycle exists so a device sees exactly the width the program asked
 * for -- a wider read would touch registers it never named, and on a part with
 * a FIFO or a read-to-clear status that is a changed machine rather than a
 * wasted cycle. It ran *before* the MMU, and so ran at the logical address.
 *
 * With translation off the two are the same number, which is why this survived
 * every test and every boot until an operating system turned the MMU on:
 * Domain/OS puts its vector table at logical `3C400800`, and the PROM service
 * that reads a byte of it -- `movec vbr,a0; btst #7,(a0)` -- addressed a
 * physical `3C400800` that no memory answers, while the long-word fetch of the
 * vector beside it went the wide way, translated, and worked. The machine
 * faulted on a byte of a page it had just read successfully. */
static void test_a_narrow_device_read_is_addressed_after_translation(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);
  ctx.inhibits_cache = inhibits_device;
  ctx.read_sized = device_read_sized;

  const ap_m68030_access_result_t r =
      ap_m68030_access_read_sized(&ctx, ADDRESS, FC_SUPERVISOR_DATA, 1u);

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_UINT(1u, m.memory.narrow_reads);
  TEST_ASSERT_EQUAL_UINT(1u, m.memory.last_narrow_size);

  /* The whole of it: the device was addressed at the page the tables produced,
   * not at the page the program named. */
  /* The root descriptor is an early-termination page, so every logical bit
   * below the first table index stays as offset -- which is why this is not
   * simply the page-size mask. */
  const uint32_t translated = PAGE_FRAME | (ADDRESS & 0x01FFFFFFu);
  TEST_ASSERT_EQUAL_HEX32(translated, m.memory.last_narrow_address);
  TEST_ASSERT_EQUAL_HEX32(translated, r.physical);
  TEST_ASSERT_TRUE(r.mmu_consulted);

  /* And `CIIN` was asserted against that same address, since the board is a map
   * of physical space and asking it about a logical address is asking it the
   * wrong question. */
  TEST_ASSERT_EQUAL_HEX32(translated, m.memory.inhibit_asked);

  /* Nothing was cached, which is the other half of what the predicate is for:
   * a device value found in the cache is a stale register read. */
  TEST_ASSERT_EQUAL_UINT(0u, m.memory.fills);
}

/* The value lands where a caller extracting with a shift expects it, and the
 * position is taken from the address the cycle ran at. */
static void test_a_narrow_read_positions_its_byte_by_the_bus_address(void) {
  machine_t m = make_machine();
  ap_m68030_access_ctx_t ctx = context_of(&m);
  ctx.inhibits_cache = inhibits_device;
  ctx.read_sized = device_read_sized;

  const ap_m68030_access_result_t r =
      ap_m68030_access_read_sized(&ctx, ADDRESS, FC_SUPERVISOR_DATA, 1u);

  const unsigned offset = (PAGE_FRAME | (ADDRESS & 0x01FFFFFFu)) & 3u;
  TEST_ASSERT_EQUAL_HEX32(0x5Au << ((3u - offset) * 8u), r.value);
  TEST_ASSERT_TRUE(r.clocks > 0u);
}

/* **A fault the MMU raised and a fault the bus raised are different faults**,
 * and until this field the result could not say which.
 *
 * `[040]` §8.4.6.2 gives the consequence: the access-error frame's `ATC` bit is
 * "set for an ATC fault due to a nonresident entry ... or privilege violation"
 * and "cleared for a bus-errored instruction, data, or cache line-push access".
 * A kernel reads that bit to decide whether to page the address in or to
 * declare the machine broken -- and a DS5500 running Domain/OS did declare it
 * broken, printing `BUS ERROR` over a page it should have serviced, because
 * this core reported every fault as the bus's. */
static void test_a_translation_refusal_and_a_bus_error_are_told_apart(void) {
  machine_t m = make_machine();
  m.memory.tables_invalid = true;
  ap_m68030_access_ctx_t ctx = context_of(&m);

  const ap_m68030_access_result_t refused =
      ap_m68030_access_read(&ctx, ADDRESS, FC_SUPERVISOR_DATA);
  TEST_ASSERT_TRUE(refused.fault);
  TEST_ASSERT_TRUE(refused.translation_fault);

  /* The same shape from the other side: the tables answer, the store does not.
   * A cache-inhibited address so the write is not absorbed before the bus. */
  machine_t n = make_machine();
  n.memory.store_refuses = true;
  ap_m68030_access_ctx_t nctx = context_of(&n);

  const ap_m68030_access_result_t unanswered =
      ap_m68030_access_write(&nctx, ADDRESS, FC_SUPERVISOR_DATA, 0x1234u, 4u);
  TEST_ASSERT_TRUE(unanswered.fault);
  TEST_ASSERT_FALSE(unanswered.translation_fault);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_translation_refusal_and_a_bus_error_are_told_apart);
  RUN_TEST(test_a_read_miss_fills_from_the_physical_address);
  RUN_TEST(test_a_cold_access_consults_the_mmu_and_pays_for_it);
  RUN_TEST(test_a_cache_hit_costs_nothing_and_skips_the_mmu);
  RUN_TEST(test_the_rest_of_the_filled_line_also_skips_the_mmu);
  RUN_TEST(test_an_rmc_read_is_forced_to_miss_the_data_cache);
  RUN_TEST(test_the_same_read_without_rmc_still_hits);
  RUN_TEST(test_a_disabled_cache_consults_the_mmu_every_time);
  RUN_TEST(test_the_cache_disable_signal_overrides_the_enable_bit);
  RUN_TEST(test_a_transparent_access_skips_the_translation_tables);
  RUN_TEST(test_the_table_search_is_paid_once_per_page);
  RUN_TEST(test_a_write_always_consults_the_mmu);
  RUN_TEST(test_a_write_to_a_read_only_warmed_page_costs_a_table_search);
  RUN_TEST(test_the_modified_bit_is_paid_for_once);
  RUN_TEST(test_a_write_hit_updates_the_cached_value);
  RUN_TEST(test_a_transparent_write_skips_the_tables);
  RUN_TEST(test_every_write_reaches_memory);
  RUN_TEST(test_a_narrow_device_read_is_addressed_after_translation);
  RUN_TEST(test_a_narrow_read_positions_its_byte_by_the_bus_address);
  RUN_TEST(test_the_opening_read_of_a_lock_is_first_read_and_the_rest_locked);
  RUN_TEST(test_a_table_search_locks_the_bus_for_its_whole_walk);
  RUN_TEST(test_a_table_search_costs_the_clocks_its_fetches_take);
  RUN_TEST(test_a_search_inside_a_lock_restores_the_lock_it_interrupted);
  return UNITY_END();
}

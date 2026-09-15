/* The 68040's caches on a running machine's fetches and operands, `[040]` §4.
 *
 * `m68040_cache_suite` holds the module -- lines, tags, Tables 4-3 and 4-4.
 * This is the access that walks them: a DS5500 machine on flat RAM, its
 * caches enabled the way `MOVEC` enables them, and each test a sentence from
 * §4.3, §4.4 or §4.6 checked through `ap_m68030_access_read`/`write` or a step.
 */

#include <string.h>

#include "cpu/m68030/ap_m68030_cache.h"
#include "cpu/m68040/ap_m68040_cache.h"
#include "machine/ap_machine.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Not a multiple of sixteen, so the line at FFF0 runs off the end: its first
 * two long words are memory and its last two are a bus error. */
#define RAM_BYTES 0xFFF8u
#define PROGRAM 0x00001000u
#define STACK 0x00009000u
#define DATA_FC AP_M68030_FC_SUPERVISOR_DATA
#define PROGRAM_FC AP_M68030_FC_SUPERVISOR_PROGRAM
#define IE_DE 0x80008000u
/* DTTR0 matching 00000000-00FFFFFF in either mode: base 00, mask 00, E, S = 1X
 * (ignore FC2), and CM in bits 6-5 -- `01` copyback, `11` noncachable. */
#define DTTR_COPYBACK 0x0000C020u
#define DTTR_NONCACHABLE 0x0000C060u

static uint8_t ram[RAM_BYTES];

static void put_long(uint32_t at, uint32_t value) {
  ram[at] = (uint8_t)(value >> 24);
  ram[at + 1u] = (uint8_t)(value >> 16);
  ram[at + 2u] = (uint8_t)(value >> 8);
  ram[at + 3u] = (uint8_t)value;
}

static uint32_t get_long(uint32_t at) {
  return ((uint32_t)ram[at] << 24) | ((uint32_t)ram[at + 1u] << 16) |
         ((uint32_t)ram[at + 2u] << 8) | (uint32_t)ram[at + 3u];
}

static void boot(ap_machine_t *m) {
  memset(ram, 0, sizeof ram);
  ap_machine_init_model(m, ram, RAM_BYTES, AP_MODEL_DN5500);
  ap_machine_reset(m, PROGRAM, STACK);
  for (unsigned i = 0; i < 4u; i++) {
    put_long(0x2000u + 4u * i, 0x11111111u * (i + 1u));
  }
}

static void enable(ap_machine_t *m, uint32_t cacr) {
  ap_m68030_cacr_write_variant(&m->cpu.cacr, cacr, &m->instruction_cache,
                               &m->data_cache, m->cpu.caar,
                               AP_M68030_CACR_VARIANT_68040);
  ap_m68030_cacr_publish(&m->cpu);
}

static bool resident(const ap_m68040_cache_t *cache, uint32_t physical) {
  return ap_m68040_cache_lookup(cache, physical) < AP_M68040_CACHE_WAYS;
}

static const ap_m68040_cache_line_t *line_of(const ap_m68040_cache_t *cache,
                                             uint32_t physical) {
  return &cache->line[ap_m68040_cache_set(physical)]
                     [ap_m68040_cache_lookup(cache, physical)];
}

/* The part's own caches on the one part that has them, and nothing on any
 * other: every 68030 model keeps its logical cache and its state hash. */
static void test_only_a_part_with_caches_routes_through_them(void) {
  ap_machine_t ds5500;
  boot(&ds5500);
  TEST_ASSERT_TRUE(ds5500.instruction_access.cache_040 == &ds5500.cpu.icache);
  TEST_ASSERT_TRUE(ds5500.data_access.cache_040 == &ds5500.cpu.dcache);

  ap_machine_t dn3500;
  memset(ram, 0, sizeof ram);
  ap_machine_init_model(&dn3500, ram, RAM_BYTES, AP_MODEL_DN3500);
  TEST_ASSERT_NULL(dn3500.instruction_access.cache_040);
  TEST_ASSERT_NULL(dn3500.data_access.cache_040);
}

/* §4.6.1: a miss reads the whole line -- here the TBI case, "three, sequential,
 * long-word reads" after the first, at §7.4.2's eight clocks -- and the rest of
 * the line then answers with no bus at all (§4.4.3). */
static void test_a_read_miss_fills_the_whole_line_and_the_rest_hit(void) {
  ap_machine_t m;
  boot(&m);
  enable(&m, IE_DE);

  ap_m68030_access_result_t r = ap_m68030_access_read(&m.data_access, 0x2004u, DATA_FC);
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_FALSE(r.cache_hit);
  TEST_ASSERT_EQUAL_HEX32(0x22222222u, r.value);
  TEST_ASSERT_EQUAL_UINT32(8u, r.clocks);
  TEST_ASSERT_TRUE(resident(&m.cpu.dcache, 0x200Cu));

  r = ap_m68030_access_read(&m.data_access, 0x200Cu, DATA_FC);
  TEST_ASSERT_TRUE(r.cache_hit);
  TEST_ASSERT_EQUAL_UINT32(0u, r.clocks);
  TEST_ASSERT_EQUAL_HEX32(0x44444444u, r.value);
}

/* §4.5: coherency with memory is software's job -- which is exactly why a
 * `CINV` has to reach the cache that answers. Until the line is invalidated a
 * change behind it is not seen; after, it is. */
static void test_a_hit_does_not_see_memory_change_until_invalidated(void) {
  ap_machine_t m;
  boot(&m);
  enable(&m, IE_DE);
  (void)ap_m68030_access_read(&m.data_access, 0x2000u, DATA_FC);

  put_long(0x2008u, 0xCAFEF00Du);
  TEST_ASSERT_EQUAL_HEX32(0x33333333u,
                          ap_m68030_access_read(&m.data_access, 0x2008u, DATA_FC).value);
  TEST_ASSERT_TRUE(ap_m68040_cache_invalidate_line(&m.cpu.dcache, 0x2008u));
  TEST_ASSERT_EQUAL_HEX32(0xCAFEF00Du,
                          ap_m68030_access_read(&m.data_access, 0x2008u, DATA_FC).value);
}

/* §4.2: "Accesses by the execution units bypass the caches while they are
 * disabled and do not affect their contents." */
static void test_a_disabled_cache_is_bypassed_and_untouched(void) {
  ap_machine_t m;
  boot(&m);
  const ap_m68030_access_result_t r =
      ap_m68030_access_read(&m.data_access, 0x2004u, DATA_FC);
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_UINT32(2u, r.clocks);
  TEST_ASSERT_FALSE(resident(&m.cpu.dcache, 0x2004u));
}

/* §4.3.1.1: write-through is "always written to the external address" with "a
 * no-write-allocate policy", and a hit updates the long word in the line
 * without changing its state -- a byte included. */
static void test_write_through_writes_memory_and_updates_a_resident_line(void) {
  ap_machine_t m;
  boot(&m);
  enable(&m, IE_DE);

  ap_m68030_access_result_t w =
      ap_m68030_access_write(&m.data_access, 0x3000u, DATA_FC, 0xAABBCCDDu, 4u);
  TEST_ASSERT_TRUE(w.ok);
  TEST_ASSERT_EQUAL_HEX32(0xAABBCCDDu, get_long(0x3000u));
  TEST_ASSERT_FALSE(resident(&m.cpu.dcache, 0x3000u));

  (void)ap_m68030_access_read(&m.data_access, 0x2000u, DATA_FC);
  w = ap_m68030_access_write(&m.data_access, 0x2005u, DATA_FC, 0x77u, 1u);
  TEST_ASSERT_TRUE(w.ok);
  TEST_ASSERT_EQUAL_HEX32(0x22772222u, get_long(0x2004u));
  const ap_m68030_access_result_t r =
      ap_m68030_access_read(&m.data_access, 0x2004u, DATA_FC);
  TEST_ASSERT_TRUE(r.cache_hit);
  TEST_ASSERT_EQUAL_HEX32(0x22772222u, r.value);
  TEST_ASSERT_EQUAL_INT(AP_M68040_LINE_VALID,
                        ap_m68040_cache_line_state(line_of(&m.cpu.dcache, 0x2004u)));
}

/* §4.4.2 and §4.4.4: a copyback write miss reads the line, then writes into
 * it and sets its D-bit; "An external write is not performed." */
static void test_copyback_writes_the_line_and_not_memory(void) {
  ap_machine_t m;
  boot(&m);
  enable(&m, IE_DE);
  m.cpu.dttr_040[0] = DTTR_COPYBACK;

  ap_m68030_access_result_t w =
      ap_m68030_access_write(&m.data_access, 0x2004u, DATA_FC, 0xDEADBEEFu, 4u);
  TEST_ASSERT_TRUE(w.ok);
  TEST_ASSERT_EQUAL_UINT32(8u, w.clocks);
  TEST_ASSERT_EQUAL_HEX32(0x22222222u, get_long(0x2004u));
  TEST_ASSERT_EQUAL_INT(AP_M68040_LINE_DIRTY,
                        ap_m68040_cache_line_state(line_of(&m.cpu.dcache, 0x2004u)));
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu,
                          ap_m68030_access_read(&m.data_access, 0x2004u, DATA_FC).value);

  w = ap_m68030_access_write(&m.data_access, 0x2008u, DATA_FC, 0x01020304u, 4u);
  TEST_ASSERT_EQUAL_UINT32(0u, w.clocks);
}

/* §4.6.2: a dirty line chosen for replacement goes to the push buffer and is
 * written back after the new line arrives -- one dirty long word as a
 * long-word push, two clocks on top of the fill's eight. Four lines in one set
 * are filled first, so the fifth read must replace one, and the counter visits
 * every way within four. */
static void test_a_dirty_line_is_pushed_when_it_is_replaced(void) {
  ap_machine_t m;
  boot(&m);
  enable(&m, IE_DE);
  m.cpu.dttr_040[0] = DTTR_COPYBACK;
  (void)ap_m68030_access_write(&m.data_access, 0x2004u, DATA_FC, 0xDEADBEEFu, 4u);
  for (uint32_t at = 0x2400u; at <= 0x2C00u; at += 0x400u) {
    (void)ap_m68030_access_read(&m.data_access, at, DATA_FC);
  }
  TEST_ASSERT_EQUAL_HEX32(0x22222222u, get_long(0x2004u));

  bool pushed = false;
  for (uint32_t at = 0x3000u; at <= 0x3C00u && !pushed; at += 0x400u) {
    const ap_m68030_access_result_t r =
        ap_m68030_access_read(&m.data_access, at, DATA_FC);
    TEST_ASSERT_TRUE(r.ok);
    if (get_long(0x2004u) == 0xDEADBEEFu) {
      pushed = true;
      TEST_ASSERT_EQUAL_UINT32(10u, r.clocks);
    }
  }
  TEST_ASSERT_TRUE(pushed);
}

/* §4.3.2 and §4.6.2: a cache-inhibited access pushes a resident dirty line
 * "before the external bus access occurs", and the line is gone after. */
static void test_a_cache_inhibited_access_pushes_the_dirty_line_first(void) {
  ap_machine_t m;
  boot(&m);
  enable(&m, IE_DE);
  m.cpu.dttr_040[0] = DTTR_COPYBACK;
  (void)ap_m68030_access_write(&m.data_access, 0x2004u, DATA_FC, 0xDEADBEEFu, 4u);

  m.cpu.dttr_040[0] = DTTR_NONCACHABLE;
  const ap_m68030_access_result_t r =
      ap_m68030_access_read(&m.data_access, 0x2008u, DATA_FC);
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, get_long(0x2004u));
  TEST_ASSERT_FALSE(resident(&m.cpu.dcache, 0x2004u));
  TEST_ASSERT_EQUAL_UINT32(4u, r.clocks);
  TEST_ASSERT_EQUAL_HEX32(0x33333333u, r.value);
}

/* §4.3.3: exception stack accesses and vector fetches do not allocate. So a
 * `TRAP` taken with the whole of memory copyback still leaves its frame *in
 * memory* -- where a stack write that allocated would have left it in a dirty
 * line, and an `RTE` after a `CINV` would return nowhere. */
static void test_an_exception_frame_reaches_memory_under_copyback(void) {
  ap_machine_t m;
  boot(&m);
  put_long(0x0080u, 0x00005000u);           /* vector 32, TRAP #0 */
  ram[PROGRAM] = 0x4Eu;                     /* TRAP #0 */
  ram[PROGRAM + 1u] = 0x40u;
  enable(&m, IE_DE);
  m.cpu.dttr_040[0] = DTTR_COPYBACK;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXCEPTION, ap_machine_step(&m).status);
  TEST_ASSERT_EQUAL_HEX32(0x00005000u, m.cpu.regs.pc);
  TEST_ASSERT_EQUAL_HEX32(PROGRAM + 2u, get_long(STACK - 6u));
  TEST_ASSERT_FALSE(resident(&m.cpu.dcache, STACK - 8u));
}

/* §4.3.2: "locked accesses are implicitly serialized" -- the read of a TAS,
 * CAS or CAS2 operand goes to memory and leaves the cache alone. */
static void test_a_locked_access_bypasses_the_cache(void) {
  ap_machine_t m;
  boot(&m);
  enable(&m, IE_DE);
  m.data_access.rmc = true;
  const ap_m68030_access_result_t r =
      ap_m68030_access_read(&m.data_access, 0x2004u, DATA_FC);
  m.data_access.rmc = false;
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_UINT32(2u, r.clocks);
  TEST_ASSERT_FALSE(resident(&m.cpu.dcache, 0x2004u));
}

/* p. 4-12: a bus error on the first cycle of a line read is the access's own;
 * on a later cycle the fill aborts, the data already read is supplied, and
 * nothing is cached. */
static void test_a_later_bus_error_aborts_the_fill_and_a_first_one_faults(void) {
  ap_machine_t m;
  boot(&m);
  put_long(0xFFF0u, 0x0000FFF0u);
  put_long(0xFFF4u, 0x0000FFF4u);
  enable(&m, IE_DE);

  ap_m68030_access_result_t r = ap_m68030_access_read(&m.data_access, 0xFFF4u, DATA_FC);
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_HEX32(0x0000FFF4u, r.value);
  TEST_ASSERT_FALSE(resident(&m.cpu.dcache, 0xFFF4u));

  r = ap_m68030_access_read(&m.data_access, 0xFFF8u, DATA_FC);
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_TRUE(r.fault);
}

/* §4.7.1: "When an access misses in the cache, the cache controller requests
 * the line containing the required data from memory and places it in the
 * cache." A fetch fills the instruction cache and leaves the data cache
 * alone. */
static void test_a_fetch_fills_the_instruction_cache(void) {
  ap_machine_t m;
  boot(&m);
  enable(&m, IE_DE);
  const ap_m68030_access_result_t r =
      ap_m68030_access_read(&m.instruction_access, 0x2000u, PROGRAM_FC);
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_UINT32(8u, r.clocks);
  TEST_ASSERT_TRUE(resident(&m.cpu.icache, 0x2000u));
  TEST_ASSERT_FALSE(resident(&m.cpu.dcache, 0x2000u));
}

/* The operator's write runs no bus cycle, so what the processor cached for the
 * address is stale -- and on this part that is the 68040's cache. */
static void test_an_operator_write_invalidates_the_68040_caches(void) {
  ap_machine_t m;
  boot(&m);
  enable(&m, IE_DE);
  (void)ap_m68030_access_read(&m.data_access, 0x2004u, DATA_FC);
  TEST_ASSERT_TRUE(resident(&m.cpu.dcache, 0x2004u));
  TEST_ASSERT_TRUE(ap_machine_write(&m, 0x2004u, 4u, 0x12345678u));
  TEST_ASSERT_FALSE(resident(&m.cpu.dcache, 0x2004u));
}

/* §4.3.3: a table search's hits "are handled in the normal manner", so a
 * descriptor written into a copyback page -- still only in a dirty line -- is
 * what the MMU reads, from either side's translation, and a miss reads memory
 * and allocates nothing. Until 2026-09-15 the search read memory alone, and
 * Domain/OS on a DS5500 took a bus error as soon as its kernel turned
 * translation on over tables in copyback pages. */
static void test_a_table_search_reads_descriptors_through_the_data_cache(void) {
  ap_machine_t m;
  boot(&m);
  enable(&m, IE_DE);
  m.cpu.dttr_040[0] = DTTR_COPYBACK;
  (void)ap_m68030_access_write(&m.data_access, 0x2004u, DATA_FC, 0x00400001u, 4u);
  TEST_ASSERT_EQUAL_HEX32(0x22222222u, get_long(0x2004u));

  uint32_t descriptor = 0u;
  TEST_ASSERT_TRUE(ap_m68030_access_table_fetch_040(&m.data_access, 0x2004u,
                                                    &descriptor));
  TEST_ASSERT_EQUAL_HEX32(0x00400001u, descriptor);
  TEST_ASSERT_TRUE(ap_m68030_access_table_fetch_040(&m.instruction_access,
                                                    0x2004u, &descriptor));
  TEST_ASSERT_EQUAL_HEX32(0x00400001u, descriptor);

  put_long(0x3000u, 0x00500001u);
  TEST_ASSERT_TRUE(ap_m68030_access_table_fetch_040(&m.data_access, 0x3000u,
                                                    &descriptor));
  TEST_ASSERT_EQUAL_HEX32(0x00500001u, descriptor);
  TEST_ASSERT_FALSE(resident(&m.cpu.dcache, 0x3000u));
}

/* p. 4-13: translation table updates are locked, and "a dirty cache line hit
 * by a cache-inhibited access is pushed before the external bus access
 * occurs" -- so the history bits land on the descriptor the cache held. */
static void test_a_table_update_pushes_the_dirty_line_it_hits(void) {
  ap_machine_t m;
  boot(&m);
  enable(&m, IE_DE);
  m.cpu.dttr_040[0] = DTTR_COPYBACK;
  (void)ap_m68030_access_write(&m.data_access, 0x2004u, DATA_FC, 0x00400001u, 4u);

  TEST_ASSERT_TRUE(ap_m68030_access_table_update_040(&m.data_access, 0x2004u,
                                                     true, false, true));
  TEST_ASSERT_FALSE(resident(&m.cpu.dcache, 0x2004u));
  TEST_ASSERT_EQUAL_HEX32(0x00400000u, get_long(0x2004u) & 0xFFFFFF00u);
}

/* `CPUSH` "pushes (writes) the cache line to memory if it is dirty and then
 * invalidates the line", and `CINV` invalidates it "without regard to its
 * dirty state" -- so the same copyback-dirty long word reaches memory under the
 * one and is lost under the other. Until 2026-09-15 both lost it. */
static void test_cpush_writes_dirty_data_back_and_cinv_loses_it(void) {
  for (unsigned c = 0; c < 2u; c++) {
    const bool cpush = c == 0u;
    ap_machine_t m;
    boot(&m);
    /* `F4` CACHE `01` (data) push SCOPE `11` (all): CPUSHA $F478, CINVA $F458. */
    const uint16_t word = cpush ? 0xF478u : 0xF458u;
    ram[PROGRAM] = (uint8_t)(word >> 8);
    ram[PROGRAM + 1u] = (uint8_t)word;
    enable(&m, IE_DE);
    m.cpu.dttr_040[0] = DTTR_COPYBACK;
    (void)ap_m68030_access_write(&m.data_access, 0x2004u, DATA_FC, 0xDEADBEEFu, 4u);

    TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_machine_step(&m).status);
    TEST_ASSERT_FALSE(resident(&m.cpu.dcache, 0x2004u));
    TEST_ASSERT_EQUAL_HEX32(cpush ? 0xDEADBEEFu : 0x22222222u, get_long(0x2004u));
  }
}

/* A line-scoped push writes only the line its address register names. */
static void test_a_line_scoped_cpush_writes_only_its_own_line(void) {
  ap_machine_t m;
  boot(&m);
  ram[PROGRAM] = 0xF4u; /* CPUSHL (A0), data cache: $F468 */
  ram[PROGRAM + 1u] = 0x68u;
  enable(&m, IE_DE);
  m.cpu.dttr_040[0] = DTTR_COPYBACK;
  (void)ap_m68030_access_write(&m.data_access, 0x2004u, DATA_FC, 0xDEADBEEFu, 4u);
  (void)ap_m68030_access_write(&m.data_access, 0x3004u, DATA_FC, 0xCAFEF00Du, 4u);
  m.cpu.regs.a[0] = 0x2004u;

  TEST_ASSERT_EQUAL_INT(AP_M68030_STEP_EXECUTED, ap_machine_step(&m).status);
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, get_long(0x2004u));
  TEST_ASSERT_EQUAL_HEX32(0x00000000u, get_long(0x3004u));
  TEST_ASSERT_TRUE(resident(&m.cpu.dcache, 0x3004u));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_only_a_part_with_caches_routes_through_them);
  RUN_TEST(test_a_read_miss_fills_the_whole_line_and_the_rest_hit);
  RUN_TEST(test_a_hit_does_not_see_memory_change_until_invalidated);
  RUN_TEST(test_a_disabled_cache_is_bypassed_and_untouched);
  RUN_TEST(test_write_through_writes_memory_and_updates_a_resident_line);
  RUN_TEST(test_copyback_writes_the_line_and_not_memory);
  RUN_TEST(test_a_dirty_line_is_pushed_when_it_is_replaced);
  RUN_TEST(test_a_cache_inhibited_access_pushes_the_dirty_line_first);
  RUN_TEST(test_an_exception_frame_reaches_memory_under_copyback);
  RUN_TEST(test_a_locked_access_bypasses_the_cache);
  RUN_TEST(test_a_later_bus_error_aborts_the_fill_and_a_first_one_faults);
  RUN_TEST(test_a_fetch_fills_the_instruction_cache);
  RUN_TEST(test_an_operator_write_invalidates_the_68040_caches);
  RUN_TEST(test_a_table_search_reads_descriptors_through_the_data_cache);
  RUN_TEST(test_a_table_update_pushes_the_dirty_line_it_hits);
  RUN_TEST(test_cpush_writes_dirty_data_back_and_cinv_loses_it);
  RUN_TEST(test_a_line_scoped_cpush_writes_only_its_own_line);
  return UNITY_END();
}

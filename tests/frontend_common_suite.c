/* The shared frontend layer exists so headless and SDL cannot drift into
 * disagreeing about what an option means. These tests pin that agreement. */

#include <string.h>

#include "ap_frontend.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* argv entries are `char *` by contract, and casting a string literal to
 * `char *` is exactly the const-dropping cast -Wcast-qual exists to catch. So
 * the tests build argv out of mutable copies. Pass nullptr to end the list. */
#define ARGV_MAX 4
typedef struct {
  char store[ARGV_MAX][32];
  char *argv[ARGV_MAX];
  int argc;
} test_argv_t;

static test_argv_t make_argv(const char *a0, const char *a1, const char *a2,
                             const char *a3) {
  const char *src[ARGV_MAX] = {a0, a1, a2, a3};
  test_argv_t a = {0};
  for (int i = 0; i < ARGV_MAX; ++i) {
    if (src[i] == nullptr) {
      break;
    }
    size_t len = strlen(src[i]);
    TEST_ASSERT_LESS_THAN_size_t(sizeof a.store[0], len + 1u);
    memcpy(a.store[i], src[i], len + 1u);
    a.argv[i] = a.store[i];
    a.argc = i + 1;
  }
  return a;
}

static void test_the_default_machine_is_the_reference_superset(void) {
  ap_common_options_t opt;
  ap_common_options_init(&opt);
  TEST_ASSERT_NOT_NULL(opt.model);
  TEST_ASSERT_EQUAL_STRING("dn3500", opt.model->name);
  TEST_ASSERT_FALSE(opt.list_models);
  TEST_ASSERT_FALSE(opt.help);
}

static void test_model_selects_a_machine_by_name(void) {
  ap_common_options_t opt;
  ap_common_options_init(&opt);
  test_argv_t a = make_argv("apollo", "--model", "dsp4500", nullptr);
  int consumed = 0;
  const char *err = "not cleared"; /* must be cleared by the parser */
  TEST_ASSERT_EQUAL_INT(
      AP_ARG_CONSUMED,
      ap_common_parse_arg(&opt, a.argc, a.argv, 1, &consumed, &err));
  TEST_ASSERT_EQUAL_INT(2, consumed);
  TEST_ASSERT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("dsp4500", opt.model->name);
}

static void test_model_without_a_value_is_an_error(void) {
  ap_common_options_t opt;
  ap_common_options_init(&opt);
  test_argv_t a = make_argv("apollo", "--model", nullptr, nullptr);
  int consumed = 0;
  const char *err = nullptr;
  TEST_ASSERT_EQUAL_INT(
      AP_ARG_ERROR,
      ap_common_parse_arg(&opt, a.argc, a.argv, 1, &consumed, &err));
  TEST_ASSERT_NOT_NULL(err);
  /* The machine must be left as it was, not half-changed. */
  TEST_ASSERT_EQUAL_STRING("dn3500", opt.model->name);
}

/* A typo must not silently emulate the wrong machine. */
static void test_an_unknown_model_name_is_an_error(void) {
  ap_common_options_t opt;
  ap_common_options_init(&opt);
  test_argv_t a = make_argv("apollo", "--model", "dn9999", nullptr);
  int consumed = 0;
  const char *err = nullptr;
  TEST_ASSERT_EQUAL_INT(
      AP_ARG_ERROR,
      ap_common_parse_arg(&opt, a.argc, a.argv, 1, &consumed, &err));
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("dn3500", opt.model->name);
}

static void test_every_model_in_the_table_is_selectable(void) {
  for (size_t i = 0; i < ap_model_count(); ++i) {
    const ap_model_t *m = ap_model_by_id((ap_model_id_t)i);
    ap_common_options_t opt;
    ap_common_options_init(&opt);
    test_argv_t a = make_argv("apollo", "--model", m->name, nullptr);
    int consumed = 0;
    const char *err = nullptr;
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        AP_ARG_CONSUMED,
        ap_common_parse_arg(&opt, a.argc, a.argv, 1, &consumed, &err), m->name);
    TEST_ASSERT_EQUAL_PTR(m, opt.model);
  }
}

static void test_list_models_and_help_are_recognised(void) {
  ap_common_options_t opt;
  ap_common_options_init(&opt);
  test_argv_t a = make_argv("apollo", "--list-models", "--help", "-h");
  int consumed = 0;
  const char *err = nullptr;
  TEST_ASSERT_EQUAL_INT(
      AP_ARG_CONSUMED,
      ap_common_parse_arg(&opt, a.argc, a.argv, 1, &consumed, &err));
  TEST_ASSERT_EQUAL_INT(1, consumed);
  TEST_ASSERT_TRUE(opt.list_models);

  TEST_ASSERT_EQUAL_INT(
      AP_ARG_CONSUMED,
      ap_common_parse_arg(&opt, a.argc, a.argv, 2, &consumed, &err));
  TEST_ASSERT_TRUE(opt.help);

  ap_common_options_init(&opt);
  TEST_ASSERT_EQUAL_INT(
      AP_ARG_CONSUMED,
      ap_common_parse_arg(&opt, a.argc, a.argv, 3, &consumed, &err));
  TEST_ASSERT_TRUE(opt.help);
}

/* A frontend's own flags must survive the shared parser untouched, which is the
 * whole reason it parses one argument at a time. */
static void test_a_frontend_specific_flag_is_left_for_the_frontend(void) {
  ap_common_options_t opt;
  ap_common_options_init(&opt);
  test_argv_t a = make_argv("apollo", "--dump-mem", nullptr, nullptr);
  int consumed = 0;
  const char *err = nullptr;
  TEST_ASSERT_EQUAL_INT(
      AP_ARG_UNKNOWN,
      ap_common_parse_arg(&opt, a.argc, a.argv, 1, &consumed, &err));
  TEST_ASSERT_EQUAL_INT(0, consumed);
}

static void test_an_out_of_range_argument_index_is_an_error(void) {
  ap_common_options_t opt;
  ap_common_options_init(&opt);
  test_argv_t a = make_argv("apollo", nullptr, nullptr, nullptr);
  int consumed = 0;
  const char *err = nullptr;
  TEST_ASSERT_EQUAL_INT(
      AP_ARG_ERROR,
      ap_common_parse_arg(&opt, a.argc, a.argv, 1, &consumed, &err));
  TEST_ASSERT_NOT_NULL(err);
}

/* Naming must be total: a new enumeration variant that nobody named would
 * otherwise surface to users as "unknown". */
static void test_every_model_part_has_a_name(void) {
  for (size_t i = 0; i < ap_model_count(); ++i) {
    const ap_model_t *m = ap_model_by_id((ap_model_id_t)i);
    TEST_ASSERT_NOT_EQUAL_INT(0, strcmp("unknown", ap_cpu_name(m->cpu)));
    TEST_ASSERT_NOT_EQUAL_INT(0, strcmp("unknown", ap_mmu_name(m->mmu)));
    TEST_ASSERT_NOT_EQUAL_INT(0, strcmp("unknown", ap_fpu_name(m->fpu)));
    TEST_ASSERT_NOT_EQUAL_INT(0, strcmp("unknown", ap_display_name(m->display)));
    TEST_ASSERT_NOT_EQUAL_INT(0, strcmp("unknown", ap_oracle_name(m->oracle)));
  }
}

/* The report is compared across build types in CI, so it must be a pure
 * function of the table: same call, same bytes. */
static void test_the_model_table_report_is_deterministic(void) {
  char buf_a[8192];
  char buf_b[8192];
  for (int pass = 0; pass < 2; ++pass) {
    char *buf = pass == 0 ? buf_a : buf_b;
    FILE *f = tmpfile();
    TEST_ASSERT_NOT_NULL(f);
    ap_print_model_table(f);
    rewind(f);
    size_t n = fread(buf, 1u, sizeof buf_a - 1u, f);
    buf[n] = '\0';
    fclose(f);
  }
  TEST_ASSERT_EQUAL_STRING(buf_a, buf_b);
  /* And it must actually mention every machine. */
  for (size_t i = 0; i < ap_model_count(); ++i) {
    const ap_model_t *m = ap_model_by_id((ap_model_id_t)i);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf_a, m->name), m->name);
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_default_machine_is_the_reference_superset);
  RUN_TEST(test_model_selects_a_machine_by_name);
  RUN_TEST(test_model_without_a_value_is_an_error);
  RUN_TEST(test_an_unknown_model_name_is_an_error);
  RUN_TEST(test_every_model_in_the_table_is_selectable);
  RUN_TEST(test_list_models_and_help_are_recognised);
  RUN_TEST(test_a_frontend_specific_flag_is_left_for_the_frontend);
  RUN_TEST(test_an_out_of_range_argument_index_is_an_error);
  RUN_TEST(test_every_model_part_has_a_name);
  RUN_TEST(test_the_model_table_report_is_deterministic);
  return UNITY_END();
}

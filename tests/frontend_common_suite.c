/* The shared frontend layer exists so headless and SDL cannot drift into
 * disagreeing about what an option means. These tests pin that agreement. */

#include <setjmp.h>
#include <stdio.h>
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

/* ## The PNG is written and then read back
 *
 * The drawing engine's verification line asks for "a decoded PNG", and a file
 * that libpng wrote is only half of that: an encoder and a decoder that agree
 * with each other can still agree on the wrong picture. So this writes a
 * pattern whose every property is chosen to fail loudly -- a non-square image
 * so a transposed one cannot pass, a first row unlike the rest so a vertical
 * flip cannot, and indices that are not their own palette values so a
 * pass-through cannot -- and reads it back through libpng's own decoder.
 *
 * Skipped rather than failed on a build with no libpng, because that is a
 * property of the build and not of the code under test.
 */
#ifdef APOLLO_TEST_HAVE_PNG
#include <png.h>
#endif

#include "ap_png.h"

#define PNG_TEST_WIDTH 7u
#define PNG_TEST_HEIGHT 3u

static uint8_t png_index(unsigned x, unsigned y) {
  /* Deliberately not x+y: that is symmetric, and a transposed image would
   * match it everywhere. */
  return (uint8_t)((y * 3u + x) % 5u);
}

static void test_a_written_png_reads_back_as_the_picture_that_went_in(void) {
  if (!ap_png_available()) {
    TEST_IGNORE_MESSAGE("built without libpng");
    return;
  }
  uint8_t pixels[PNG_TEST_WIDTH * PNG_TEST_HEIGHT];
  for (unsigned y = 0; y < PNG_TEST_HEIGHT; y++) {
    for (unsigned x = 0; x < PNG_TEST_WIDTH; x++) {
      pixels[y * PNG_TEST_WIDTH + x] = png_index(x, y);
    }
  }
  /* Indices are not their own colours, so an encoder that ignored the palette
   * and wrote greyscale would come back different. */
  uint8_t palette[5][3] = {
      {0x10u, 0x20u, 0x30u}, {0x40u, 0x50u, 0x60u}, {0x70u, 0x80u, 0x90u},
      {0xA0u, 0xB0u, 0xC0u}, {0xD0u, 0xE0u, 0xF0u},
  };

  const char *path = "frontend_common_suite.png";
  TEST_ASSERT_EQUAL_INT(AP_PNG_OK,
                        ap_png_write_indexed(path, pixels, PNG_TEST_WIDTH,
                                             PNG_TEST_HEIGHT, palette, 5u));

#ifdef APOLLO_TEST_HAVE_PNG
  FILE *file = fopen(path, "rb");
  TEST_ASSERT_NOT_NULL(file);
  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL,
                                           NULL);
  TEST_ASSERT_NOT_NULL(png);
  png_infop info = png_create_info_struct(png);
  TEST_ASSERT_NOT_NULL(info);
  TEST_ASSERT_EQUAL_INT(0, setjmp(png_jmpbuf(png)));
  png_init_io(png, file);
  png_read_info(png, info);

  TEST_ASSERT_EQUAL_UINT32(PNG_TEST_WIDTH, png_get_image_width(png, info));
  TEST_ASSERT_EQUAL_UINT32(PNG_TEST_HEIGHT, png_get_image_height(png, info));
  TEST_ASSERT_EQUAL_INT(PNG_COLOR_TYPE_PALETTE,
                        png_get_color_type(png, info));
  TEST_ASSERT_EQUAL_INT(8, png_get_bit_depth(png, info));

  png_colorp read_palette = NULL;
  int entries = 0;
  TEST_ASSERT_TRUE(png_get_PLTE(png, info, &read_palette, &entries) != 0);
  TEST_ASSERT_EQUAL_INT(5, entries);
  for (unsigned i = 0; i < 5u; i++) {
    TEST_ASSERT_EQUAL_UINT8(palette[i][0], read_palette[i].red);
    TEST_ASSERT_EQUAL_UINT8(palette[i][1], read_palette[i].green);
    TEST_ASSERT_EQUAL_UINT8(palette[i][2], read_palette[i].blue);
  }

  uint8_t row[PNG_TEST_WIDTH];
  for (unsigned y = 0; y < PNG_TEST_HEIGHT; y++) {
    png_read_row(png, row, NULL);
    for (unsigned x = 0; x < PNG_TEST_WIDTH; x++) {
      TEST_ASSERT_EQUAL_UINT8(png_index(x, y), row[x]);
    }
  }
  png_destroy_read_struct(&png, &info, NULL);
  fclose(file);
#endif
  remove(path);
}

/* An index with no palette entry behind it means the scanout and the lookup
 * table disagree about how many colours the screen has. Painting it black
 * would hide exactly that, so it is refused -- and refused *before* the file is
 * opened, so a rejected picture leaves nothing on disk. */
static void test_an_index_past_the_palette_is_refused(void) {
  const uint8_t pixels[4] = {0u, 1u, 2u, 0u};
  const uint8_t palette[2][3] = {{0u, 0u, 0u}, {255u, 255u, 255u}};
  TEST_ASSERT_EQUAL_INT(AP_PNG_BAD_ARGUMENT,
                        ap_png_write_indexed("must-not-exist.png", pixels, 2u,
                                             2u, palette, 2u));
  TEST_ASSERT_NULL(fopen("must-not-exist.png", "rb"));
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
  RUN_TEST(test_a_written_png_reads_back_as_the_picture_that_went_in);
  RUN_TEST(test_an_index_past_the_palette_is_refused);
  return UNITY_END();
}

/* The shared frontend layer exists so headless and SDL cannot drift into
 * disagreeing about what an option means. These tests pin that agreement. */

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ap_frontend.h"
#include "ap_scanout.h"
#include "unity.h"

#include <sys/socket.h>
#include <unistd.h>

#include "ap_ring_link.h"

#include "ap_tap.h"

/* The pipe substitution below is POSIX, and CI builds under MSVC too. */
#if defined(__linux__)
#include <unistd.h>
#endif

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

/* ## The index-to-colour step, and the in-place expansion under it
 *
 * `ap_scanout_rgba` scans indices into the tail of the caller's 32-bit buffer
 * and expands them forward, so a window costs one allocation. That trick is
 * only correct in one direction: index `i` lives at byte `3p + i` and its
 * colour is written to `[4i, 4i+4)`, so going *downwards* the first write
 * lands on `[4p-4, 4p)` and destroys three indices that have not been read.
 * Written the wrong way round first, and this is the test that would have
 * caught it -- a solid-colour screen cannot, because every clobbered index
 * decodes to the same pixel. The picture below is a ramp for that reason. */
static void test_the_scanout_expands_every_pixel_not_just_the_first(void) {
  ap_graphics_t graphics;
  ap_graphics_init(&graphics, AP_SCREEN_MONO_15_INCH);

  ap_graphics_geometry_t geometry;
  TEST_ASSERT_TRUE(ap_graphics_geometry(AP_SCREEN_MONO_15_INCH, &geometry));
  const uint32_t pixels = (uint32_t)geometry.width * geometry.height;

  static uint8_t mono[4u * 1024u * 1024u];
  memset(mono, 0, sizeof mono);
  /* Alternating words, so the scanned-out indices alternate along each row and
   * a clobbered index shows up as a run of one colour where two belong. */
  for (uint32_t i = 0; i + 1 < sizeof mono; i += 4u) {
    mono[i] = 0xFFu;
    mono[i + 1u] = 0xFFu;
  }
  ap_graphics_attach_memory(&graphics, NULL, 0u, mono, (uint32_t)sizeof mono);

  uint32_t *frame = calloc(pixels, sizeof *frame);
  TEST_ASSERT_NOT_NULL(frame);

  uint32_t width = 0;
  uint32_t height = 0;
  TEST_ASSERT_TRUE(ap_scanout_rgba(&graphics, 0u,
                                   frame, pixels, &width, &height));
  TEST_ASSERT_EQUAL_UINT32(geometry.width, width);
  TEST_ASSERT_EQUAL_UINT32(geometry.height, height);

  /* Compared against an **independent** scanout into its own buffer, pixel by
   * pixel, rather than against a property like "every pixel is one of two
   * colours". That weaker check passes with the loop running the wrong way:
   * a clobbered index byte reads back as `00` or `FF`, and on a one-plane
   * screen both land inside the palette or past it and paint a legal ink
   * colour, so the picture is wrong and every pixel still looks plausible.
   * Only the exact expected value catches it. */
  static uint8_t reference[2048u * 1024u];
  TEST_ASSERT_TRUE(pixels <= sizeof reference);
  TEST_ASSERT_EQUAL_UINT32(pixels,
                           ap_graphics_scanout(&graphics, 0u, reference,
                                               pixels));

  ap_scanout_palette_t palette;
  TEST_ASSERT_TRUE(ap_scanout_palette(&graphics, &palette));

  for (uint32_t i = 0; i < pixels; i++) {
    const uint8_t index = reference[i];
    TEST_ASSERT_TRUE(index < palette.colours);
    const uint32_t want = 0xFF000000u |
                          ((uint32_t)palette.rgb[index][0] << 16) |
                          ((uint32_t)palette.rgb[index][1] << 8) |
                          palette.rgb[index][2];
    TEST_ASSERT_EQUAL_HEX32(want, frame[i]);
  }
  free(frame);
}

/* A monochrome screen is ink on paper and the mapping is exact: a set bit is
 * black. Stated as a test because it is the one palette in this machine that
 * is not a guess about a lookup table. */
static void test_a_monochrome_palette_is_ink_on_paper(void) {
  ap_graphics_t graphics;
  ap_graphics_init(&graphics, AP_SCREEN_MONO_19_INCH);

  ap_scanout_palette_t palette;
  TEST_ASSERT_TRUE(ap_scanout_palette(&graphics, &palette));
  TEST_ASSERT_TRUE(palette.real);
  TEST_ASSERT_EQUAL_UINT(2u, palette.colours);
  TEST_ASSERT_EQUAL_HEX8(0xFFu, palette.rgb[0][0]);
  TEST_ASSERT_EQUAL_HEX8(0x00u, palette.rgb[1][0]);
}

/* The 4-plane board's sixteen-entry table is not modelled, so its palette is an
 * even ramp -- and must say so, or a screenshot written from it would be passed
 * off as the colours the monitor showed. */
static void test_an_unmodelled_lookup_table_is_not_claimed_as_real(void) {
  ap_graphics_t graphics;
  ap_graphics_init(&graphics, AP_SCREEN_COLOUR_4_PLANE);

  ap_scanout_palette_t palette;
  TEST_ASSERT_TRUE(ap_scanout_palette(&graphics, &palette));
  TEST_ASSERT_FALSE(palette.real);
  TEST_ASSERT_EQUAL_UINT(16u, palette.colours);
}


/* ## The TAP wire, with a pipe standing in for the kernel
 *
 * `/dev/net/tun` needs `CAP_NET_ADMIN` to attach an interface, which CI does
 * not have and a developer usually does not either. Everything either side of
 * that syscall is ours, though, and a pipe is a file descriptor with the same
 * read and write semantics -- so substituting one exercises the whole path
 * except the kernel's TAP driver itself.
 *
 * That boundary is worth being exact about: this proves frames cross the wire
 * abstraction into the adapter and back out, and it proves nothing about
 * `TUNSETIFF`. */
#if defined(__linux__)
static void test_a_frame_crosses_the_wire_into_an_armed_receive(void) {
  int fds[2];
  TEST_ASSERT_EQUAL_INT(0, pipe(fds));

  ap_tap_t tap = {0};
  tap.fd = fds[0];

  const uint8_t prom[6] = {0x02u, 0x60u, 0x8Cu, 0x12u, 0x34u, 0x56u};
  ap_3c505_adapter_t adapter;
  ap_3c505_adapter_init(&adapter, prom);

  /* Nothing armed: the frame is dropped and counted, not delivered. It carries
   * this station's address as its destination so that `02H`'s receive filter
   * passes it and the question left is the one this test asks -- a frame for
   * somebody else would be dropped one step earlier, and the test would read
   * as passing while proving nothing. */
  const uint8_t frame[6] = {0x02u, 0x60u, 0x8Cu, 0x12u, 0x34u, 0x56u};
  TEST_ASSERT_EQUAL_INT(6, write(fds[1], frame, sizeof frame));
  ap_3c505_pcb_t out = {0};
  TEST_ASSERT_FALSE(ap_tap_poll(&tap, &adapter, &out));
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)tap.frames_in);
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)tap.dropped_in);

  /* Armed, and now it lands. */
  ap_3c505_pcb_t arm = {.command = AP_3C505_CMD_RECEIVE_PACKET, .length = 8u};
  arm.data[4] = 64u;
  TEST_ASSERT_FALSE(ap_3c505_dispatch(&adapter, &arm, &out));

  TEST_ASSERT_EQUAL_INT(6, write(fds[1], frame, sizeof frame));
  TEST_ASSERT_TRUE(ap_tap_poll(&tap, &adapter, &out));
  TEST_ASSERT_EQUAL_HEX8(0x38u, out.command);
  TEST_ASSERT_EQUAL_HEX8(6u, out.data[4]); /* bytes to be DMA'ed */

  uint8_t byte = 0;
  for (unsigned i = 0; i < 6u; i++) {
    TEST_ASSERT_TRUE(ap_3c505_receive_byte(&adapter, &byte));
    TEST_ASSERT_EQUAL_HEX8(frame[i], byte);
  }

  /* A quiet wire is not an error: `EAGAIN` on an empty pipe reads as "nothing
   * arrived", which is the ordinary case and must not stop the machine. */
  (void)close(fds[1]);
  TEST_ASSERT_FALSE(ap_tap_poll(&tap, &adapter, &out));
  (void)close(fds[0]);
}

/* Transmitting goes the other way through the same descriptor, and a frame is
 * whole or it did not go: a short write is a failure, because half a frame on
 * the wire is not a frame. */
static void test_a_transmitted_frame_reaches_the_descriptor_whole(void) {
  int fds[2];
  TEST_ASSERT_EQUAL_INT(0, pipe(fds));

  ap_tap_t tap = {0};
  tap.fd = fds[1];

  ap_3c505_adapter_t adapter;
  ap_3c505_adapter_init(&adapter, NULL);
  adapter.wire = ap_tap_wire(&tap);

  ap_3c505_pcb_t in = {.command = AP_3C505_CMD_TRANSMIT_PACKET, .length = 6u};
  in.data[4] = 4u;
  ap_3c505_pcb_t out = {0};
  TEST_ASSERT_FALSE(ap_3c505_dispatch(&adapter, &in, &out));

  const uint8_t frame[4] = {0xDEu, 0xADu, 0xBEu, 0xEFu};
  for (unsigned i = 0; i < 3u; i++) {
    TEST_ASSERT_FALSE(ap_3c505_transmit_byte(&adapter, frame[i], &out));
  }
  TEST_ASSERT_TRUE(ap_3c505_transmit_byte(&adapter, frame[3], &out));

  TEST_ASSERT_EQUAL_HEX8(0x39u, out.command);
  TEST_ASSERT_EQUAL_HEX8(0u, out.data[4]); /* "0 = successful" */
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)tap.frames_out);

  uint8_t back[4] = {0};
  TEST_ASSERT_EQUAL_INT(4, read(fds[0], back, sizeof back));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(frame, back, 4);
  (void)close(fds[0]);
  (void)close(fds[1]);
}
#endif /* __linux__ */

/* Opening a device that is not there must say so, and say which problem it is:
 * "no such device" and "not permitted" have different fixes. */
static void test_opening_a_missing_tap_device_explains_itself(void) {
  ap_tap_t tap = {0};
  char reason[192] = {0};
  TEST_ASSERT_FALSE(ap_tap_open(&tap, "apollo-no-such-device", reason,
                                sizeof reason));
  TEST_ASSERT_TRUE(reason[0] != '\0');
}


/* ## The cross-process ring link
 *
 * Two emulator *instances* sharing one cable. Driven over a `socketpair` so the
 * test needs no port, no network and no second process -- what it exercises is
 * the wire format and the lock-step, which is all the transport is. */

/* A batch of cells that is not symmetric in any of the ways a bug would hide
 * behind: both windows set, each alone, neither, and not a repeating pattern. */
static void link_fill(ap_ring_cell_t *cells, unsigned n, unsigned seed) {
  for (unsigned i = 0; i < n; i++) {
    cells[i].clock_window = ((i + seed) % 3u) != 0u;
    cells[i].data_window = ((i * 2u + seed) % 5u) < 2u;
  }
}

static void test_a_ring_link_carries_a_cable_of_cells_both_ways(void) {
  int fds[2];
  TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));

  ap_ring_link_t a;
  ap_ring_link_t b;
  const unsigned cable = 64u; /* `[MAC]` §3.4's kilometre, the longest link */
  TEST_ASSERT_TRUE(ap_ring_link_init(&a, fds[0], cable));
  TEST_ASSERT_TRUE(ap_ring_link_init(&b, fds[1], cable));

  ap_ring_cell_t a_out[64];
  ap_ring_cell_t b_out[64];
  ap_ring_cell_t a_in[64];
  ap_ring_cell_t b_in[64];
  link_fill(a_out, cable, 1u);
  link_fill(b_out, cable, 4u);

  /* Both write their whole batch before reading, which is why a single
   * process can drive both ends without deadlocking -- and is exactly what
   * two processes do. */
  /* **Both send before either receives.** `ap_ring_link_exchange` writes and
   * then blocks reading, which is right for two concurrent processes and
   * deadlocks a single thread driving both ends -- the first version of this
   * test hung for exactly that reason, and the split API exists because of it.
   * The bytes on the wire are the same either way. */
  TEST_ASSERT_TRUE(ap_ring_link_send(&a, a_out));
  TEST_ASSERT_TRUE(ap_ring_link_send(&b, b_out));
  TEST_ASSERT_TRUE(ap_ring_link_recv(&a, a_in));
  TEST_ASSERT_TRUE(ap_ring_link_recv(&b, b_in));

  for (unsigned i = 0; i < cable; i++) {
    /* Each end received exactly what the other drove, in order. Both windows
     * are checked: a format that carried only the data window would still
     * deliver a plausible-looking stream and lose every clock transition,
     * which is a bi-phase error at the far end and nothing sooner. */
    TEST_ASSERT_EQUAL_INT((int)b_out[i].clock_window, (int)a_in[i].clock_window);
    TEST_ASSERT_EQUAL_INT((int)b_out[i].data_window, (int)a_in[i].data_window);
    TEST_ASSERT_EQUAL_INT((int)a_out[i].clock_window, (int)b_in[i].clock_window);
    TEST_ASSERT_EQUAL_INT((int)a_out[i].data_window, (int)b_in[i].data_window);
  }
  TEST_ASSERT_EQUAL_UINT64(1u, a.batches);
  close(fds[0]);
  close(fds[1]);
}

/* A peer that goes away must fail the link and keep it failed. A ring that has
 * lost a node does not quietly continue as a shorter one -- the surviving
 * emulator would carry on with a cable that ends nowhere, and every symptom
 * would appear at the protocol layer instead of here. */
static void test_a_ring_link_fails_once_and_stays_failed(void) {
  int fds[2];
  TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
  ap_ring_link_t a;
  TEST_ASSERT_TRUE(ap_ring_link_init(&a, fds[0], 8u));
  ap_ring_cell_t out[8];
  ap_ring_cell_t in[8];
  link_fill(out, 8u, 2u);
  close(fds[1]); /* the peer leaves */

  /* Either the write or the read fails, depending on how far the closed peer's
   * buffer got -- both land in the same place. */
  (void)ap_ring_link_exchange(&a, out, in);
  TEST_ASSERT_TRUE(a.failed);
  TEST_ASSERT_FALSE(ap_ring_link_exchange(&a, out, in));
  close(fds[0]);
}

/* The batch size is a cable length, so it is bounded by the longest cable the
 * medium models. A link that accepted more would be claiming a delay the ring
 * cannot represent, and the two ends would disagree about where a bit is. */
static void test_a_ring_links_batch_is_bounded_by_the_longest_cable(void) {
  ap_ring_link_t link;
  TEST_ASSERT_FALSE(ap_ring_link_init(&link, 1, 0u));
  TEST_ASSERT_FALSE(ap_ring_link_init(&link, 1, AP_RING_MAX_CABLE_BITS + 1u));
  TEST_ASSERT_TRUE(ap_ring_link_init(&link, 1, AP_RING_MAX_CABLE_BITS));
  TEST_ASSERT_FALSE(ap_ring_link_init(&link, -1, 8u));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_scanout_expands_every_pixel_not_just_the_first);
  RUN_TEST(test_a_monochrome_palette_is_ink_on_paper);
  RUN_TEST(test_an_unmodelled_lookup_table_is_not_claimed_as_real);
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
#if defined(__linux__)
  RUN_TEST(test_a_frame_crosses_the_wire_into_an_armed_receive);
  RUN_TEST(test_a_transmitted_frame_reaches_the_descriptor_whole);
#endif
  RUN_TEST(test_opening_a_missing_tap_device_explains_itself);
  RUN_TEST(test_a_ring_link_carries_a_cable_of_cells_both_ways);
  RUN_TEST(test_a_ring_link_fails_once_and_stays_failed);
  RUN_TEST(test_a_ring_links_batch_is_bounded_by_the_longest_cable);
  return UNITY_END();
}

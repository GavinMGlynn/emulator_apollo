/* The interactive frontend.
 *
 * ## What this is for, and what it deliberately is not
 *
 * The headless frontend is the one that must stay reproducible: no wall clock,
 * no host input, no threads, so a boot produces the same bytes on every machine
 * and a golden means something. This one is the opposite by design -- it exists
 * so a person can watch the machine and type at it -- and the two must not be
 * confused, which is why they are separate executables over one core rather
 * than one executable with a `--gui` flag.
 *
 * What it does *not* do is duplicate the core. Everything here is window,
 * texture and event; the scanout and its palette come from
 * `common/ap_scanout.h`, shared with the screenshot writer so the interactive
 * path cannot drift from the one that is diffed against goldens.
 *
 * ## `--frames N`, and why an interactive program needs a bounded mode
 *
 * A window that runs until someone closes it cannot be tested. `--frames N`
 * runs exactly N frames and exits, which under `SDL_VIDEODRIVER=dummy` makes
 * this a CTest entry like any other: it proves the window opens, the texture
 * uploads, the machine steps and the thing exits cleanly, on a machine with no
 * display at all. It is the same argument the headless frontend makes for
 * having no wall clock.
 */

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ap_scanout.h"
#include "board/ap_board.h"
#include "machine/ap_machine.h"
#include "image/ap_awd.h"
#include "model/ap_model.h"

/* Instructions per frame. The reference core is far slower than the hardware
 * and this is a viewing rate rather than a timing claim -- `CLAUDE.md` is
 * explicit that the reference core will not reach real time and that this is
 * accepted. Nothing here is a cycle count and nothing here may become one. */
#define AP_SDL_INSTRUCTIONS_PER_FRAME 200000u

static void usage(const char *program) {
  fprintf(stderr,
          "usage: %s --boot-prom FILE [--disk FILE] [--screen KIND]\n"
          "          [--frames N] [--scale N] [--model NAME]\n"
          "\n"
          "  --frames N   run exactly N frames and exit, for a bounded,\n"
          "               testable run; omit it for an interactive window\n",
          program);
}

static uint8_t *read_file(const char *path, long *size_out) {
  FILE *f = fopen(path, "rb");
  if (f == NULL) {
    return NULL;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  const long size = ftell(f);
  if (size <= 0 || fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return NULL;
  }
  uint8_t *bytes = malloc((size_t)size);
  if (bytes == NULL || fread(bytes, 1u, (size_t)size, f) != (size_t)size) {
    free(bytes);
    fclose(f);
    return NULL;
  }
  fclose(f);
  *size_out = size;
  return bytes;
}

/* The picture, letterboxed. The Apollo's screens are 1024x800 and 1280x1024,
 * neither of which is a common window shape, so a resized window must keep the
 * aspect ratio or every pixel measurement taken off a screenshot becomes a lie.
 * Integer arithmetic throughout: a half-pixel offset is a blurred image. */
static SDL_FRect letterbox(int window_w, int window_h, uint32_t image_w,
                           uint32_t image_h) {
  SDL_FRect out;
  const float scale_x = (float)window_w / (float)image_w;
  const float scale_y = (float)window_h / (float)image_h;
  const float scale = scale_x < scale_y ? scale_x : scale_y;
  out.w = (float)image_w * scale;
  out.h = (float)image_h * scale;
  out.x = ((float)window_w - out.w) * 0.5f;
  out.y = ((float)window_h - out.h) * 0.5f;
  return out;
}

/* A host key press, as a character this keyboard can produce. `ap_board_key_type`
 * takes the *compatibility set* -- Table 12-1's character codes -- and not the
 * matrix indices `ap_kbd_press` wants, which is the distinction that cost a
 * session in the headless frontend. Anything the set cannot produce is dropped
 * rather than guessed at. */
static void deliver_key(ap_board_t *board, const SDL_KeyboardEvent *key) {
  const SDL_Keycode code = key->key;
  char ascii = 0;
  if (code == SDLK_RETURN || code == SDLK_KP_ENTER) {
    ascii = '\r'; /* The firmware wants a carriage return, not a line feed. */
  } else if (code == SDLK_BACKSPACE) {
    ascii = '\b';
  } else if (code == SDLK_TAB) {
    ascii = '\t';
  } else if (code == SDLK_ESCAPE) {
    ascii = 0x1B;
  } else if (code >= 0x20 && code < 0x7F) {
    ascii = (char)code;
    if ((key->mod & SDL_KMOD_SHIFT) != 0 && ascii >= 'a' && ascii <= 'z') {
      ascii = (char)(ascii - 'a' + 'A');
    }
  }
  if (ascii != 0) {
    (void)ap_board_key_type(board, ascii);
  }
}

int main(int argc, char **argv) {
  const char *prom_path = NULL;
  const char *disk_path = NULL;
  ap_screen_kind_t screen = AP_SCREEN_COLOUR_8_PLANE;
  ap_model_id_t model = AP_MODEL_DN3500;
  unsigned frames_limit = 0u; /* 0 means run until closed. */
  unsigned scale = 1u;

  for (int i = 1; i < argc; i++) {
    const char *arg = argv[i];
    const bool has_value = i + 1 < argc;
    if (strcmp(arg, "--boot-prom") == 0 && has_value) {
      prom_path = argv[++i];
    } else if (strcmp(arg, "--disk") == 0 && has_value) {
      disk_path = argv[++i];
    } else if (strcmp(arg, "--frames") == 0 && has_value) {
      frames_limit = (unsigned)strtoul(argv[++i], NULL, 10);
    } else if (strcmp(arg, "--scale") == 0 && has_value) {
      scale = (unsigned)strtoul(argv[++i], NULL, 10);
      if (scale == 0u) {
        scale = 1u;
      }
    } else if (strcmp(arg, "--screen") == 0 && has_value) {
      const char *name = argv[++i];
      if (strcmp(name, "c8p") == 0) {
        screen = AP_SCREEN_COLOUR_8_PLANE;
      } else if (strcmp(name, "c4p") == 0) {
        screen = AP_SCREEN_COLOUR_4_PLANE;
      } else if (strcmp(name, "19i") == 0) {
        screen = AP_SCREEN_MONO_19_INCH;
      } else if (strcmp(name, "15i") == 0) {
        screen = AP_SCREEN_MONO_15_INCH;
      } else {
        fprintf(stderr, "apollo-sdl: unknown screen %s\n", name);
        return 1;
      }
    } else if (strcmp(arg, "--help") == 0) {
      usage(argv[0]);
      return 0;
    } else {
      fprintf(stderr, "apollo-sdl: unexpected argument %s\n", arg);
      usage(argv[0]);
      return 1;
    }
  }

  if (prom_path == NULL) {
    fprintf(stderr, "apollo-sdl: --boot-prom is required\n");
    usage(argv[0]);
    return 1;
  }
  if (screen == AP_SCREEN_NONE) {
    fprintf(stderr, "apollo-sdl: this frontend needs a screen\n");
    return 1;
  }

  ap_graphics_geometry_t geometry;
  if (!ap_graphics_geometry(screen, &geometry)) {
    fprintf(stderr, "apollo-sdl: that screen has no geometry\n");
    return 1;
  }

  long prom_size = 0;
  uint8_t *prom = read_file(prom_path, &prom_size);
  if (prom == NULL) {
    fprintf(stderr, "apollo-sdl: cannot read %s\n", prom_path);
    return 1;
  }

  /* The core allocates nothing; every buffer below is the frontend's, exactly
   * as in the headless one. */
  const uint32_t ram_bytes = 16u * 1024u * 1024u;
  const uint32_t parity_bytes = (ram_bytes + 7u) / 8u;
  const uint32_t image_bytes = geometry.plane_words * 2u * geometry.planes;
  const uint32_t window_colour =
      AP_GRAPHICS_COLOUR_MEMORY_END - AP_GRAPHICS_COLOUR_MEMORY_ADDR + 1u;
  const uint32_t window_mono =
      AP_GRAPHICS_MONO_MEMORY_END - AP_GRAPHICS_MONO_MEMORY_ADDR + 1u;
  const bool is_colour = ap_graphics_is_colour(screen);
  const uint32_t colour_bytes =
      (is_colour && image_bytes > window_colour) ? image_bytes : window_colour;
  const uint32_t mono_bytes =
      (!is_colour && image_bytes > window_mono) ? image_bytes : window_mono;

  uint8_t *ram = calloc(1, ram_bytes);
  uint8_t *parity = calloc(1, parity_bytes);
  uint8_t *colour_memory = calloc(1, colour_bytes);
  uint8_t *mono_memory = calloc(1, mono_bytes);
  ap_board_t *board = calloc(1, sizeof *board);
  const uint32_t pixels = (uint32_t)geometry.width * geometry.height;
  uint32_t *frame = calloc(pixels, sizeof *frame);

  static const ap_mc146818_time_t epoch = {
      .second = 0, .minute = 0, .hour = 0, .day = 1, .month = 1, .year = 96};

  int status = 1;
  SDL_Window *window = NULL;
  SDL_Renderer *renderer = NULL;
  SDL_Texture *texture = NULL;
  ap_awd_t disk_image;
  uint8_t *disk_bytes = NULL;

  if (ram == NULL || parity == NULL || colour_memory == NULL ||
      mono_memory == NULL || board == NULL || frame == NULL) {
    fprintf(stderr, "apollo-sdl: out of memory\n");
    goto done;
  }
  if (!ap_board_init_model(board, ram, ram_bytes, &epoch, 0x012345u, model) ||
      !ap_board_attach_parity(board, parity, parity_bytes)) {
    fprintf(stderr, "apollo-sdl: cannot build the core board\n");
    goto done;
  }
  ap_graphics_init(&board->graphics, screen);
  ap_graphics_attach_memory(&board->graphics, colour_memory, colour_bytes,
                            mono_memory, mono_bytes);
  if (!ap_board_load_prom(board, prom, (uint32_t)prom_size)) {
    fprintf(stderr, "apollo-sdl: %s does not fit the boot PROM region\n",
            prom_path);
    goto done;
  }
  if (disk_path != NULL) {
    long disk_size = 0;
    disk_bytes = read_file(disk_path, &disk_size);
    if (disk_bytes == NULL) {
      fprintf(stderr, "apollo-sdl: cannot read disk image %s\n", disk_path);
      goto done;
    }
    /* Writable: the machine may write to its disk, and the image is this
     * frontend's own buffer rather than the file. */
    if (!ap_awd_open(&disk_image, disk_bytes, (size_t)disk_size,
                     ap_awd_geometry_for(AP_AWD_DRIVE_348MB), true)) {
      fprintf(stderr, "apollo-sdl: %s is not an Apollo Winchester image\n",
              disk_path);
      goto done;
    }
    ap_omti_attach(&board->disk.controller, &disk_image);
  }

  uint32_t stack = 0;
  uint32_t pc = 0;
  if (!ap_board_reset_vector(board, &stack, &pc)) {
    fprintf(stderr, "apollo-sdl: the boot PROM has no reset vector\n");
    goto done;
  }

  ap_machine_t machine;
  ap_machine_init_model(&machine, ram, ram_bytes, model);
  ap_machine_set_board(&machine, board);
  ap_machine_reset(&machine, pc, stack);

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "apollo-sdl: SDL_Init: %s\n", SDL_GetError());
    goto done;
  }
  if (!SDL_CreateWindowAndRenderer(
          "Apollo", (int)(geometry.width * scale),
          (int)(geometry.height * scale), SDL_WINDOW_RESIZABLE, &window,
          &renderer)) {
    fprintf(stderr, "apollo-sdl: cannot open a window: %s\n", SDL_GetError());
    goto done;
  }
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, (int)geometry.width,
                              (int)geometry.height);
  if (texture == NULL) {
    fprintf(stderr, "apollo-sdl: cannot create the texture: %s\n",
            SDL_GetError());
    goto done;
  }
  /* Nearest, not linear: these are pixels a person is reading text off, and a
   * smoothed 1024x800 console is unreadable at any scale that is not integral. */
  (void)SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);

  bool running = true;
  for (unsigned frames = 0; running; frames++) {
    if (frames_limit != 0u && frames >= frames_limit) {
      break;
    }

    /* Mouse motion is accumulated over the frame and sent as one packet.
     * §13.3's counts are one signed byte each and the wire is the keyboard's,
     * so forwarding every SDL motion event separately would flood a link that
     * carries keystrokes -- and a person moving a mouse produces far more
     * events per frame than the packet rate. */
    float motion_x = 0.0f;
    float motion_y = 0.0f;
    bool motion = false;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) {
        running = false;
      } else if (event.type == SDL_EVENT_KEY_DOWN) {
        deliver_key(board, &event.key);
      } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
        motion_x += event.motion.xrel;
        motion_y += event.motion.yrel;
        motion = true;
      } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                 event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        motion = true; /* a press with no movement is still a packet */
      }
    }

    if (motion) {
      const SDL_MouseButtonFlags buttons = SDL_GetMouseState(NULL, NULL);
      /* **Y is inverted deliberately.** §13.3.1: "A positive count means that
       * the pointing device is moving up or right", where SDL's `yrel` is
       * positive downwards. Forwarding it unchanged gives an upside-down
       * mouse, which is the single easiest thing to get wrong here. */
      (void)ap_board_mouse_move(
          board, (int)motion_x, -(int)motion_y,
          (buttons & SDL_BUTTON_LMASK) != 0, (buttons & SDL_BUTTON_MMASK) != 0,
          (buttons & SDL_BUTTON_RMASK) != 0);
    }

    (void)ap_machine_run(&machine, AP_SDL_INSTRUCTIONS_PER_FRAME);

    if (ap_scanout_rgba(&board->graphics, board->graphics.reg.cr1, frame, pixels,
                        NULL, NULL)) {
      (void)SDL_UpdateTexture(texture, NULL, frame,
                              (int)(geometry.width * sizeof *frame));
    }

    int window_w = 0;
    int window_h = 0;
    SDL_GetRenderOutputSize(renderer, &window_w, &window_h);
    const SDL_FRect into =
        letterbox(window_w, window_h, geometry.width, geometry.height);
    (void)SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    (void)SDL_RenderClear(renderer);
    (void)SDL_RenderTexture(renderer, texture, NULL, &into);
    (void)SDL_RenderPresent(renderer);
  }

  status = 0;

done:
  if (texture != NULL) {
    SDL_DestroyTexture(texture);
  }
  if (renderer != NULL) {
    SDL_DestroyRenderer(renderer);
  }
  if (window != NULL) {
    SDL_DestroyWindow(window);
  }
  SDL_Quit();
  free(disk_bytes);
  free(frame);
  free(board);
  free(mono_memory);
  free(colour_memory);
  free(parity);
  free(ram);
  free(prom);
  return status;
}

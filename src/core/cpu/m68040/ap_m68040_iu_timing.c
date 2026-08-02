/* MC68040 §10.6, from the page images. Each group is one column of the
 * manual's table; the instructions in a group share a column because their
 * timings are identical, not because they are related. */

#include <stddef.h>
#include <string.h>

#include "cpu/m68040/ap_m68040_iu_timing.h"

static const char *const names_0[] = {"ADD", "AND", "EOR", "OR", "SUB", "TST", NULL};
static const ap_m68040_iu_cell_t cells_0[AP_M68040_IU_MODE_COUNT] = {
    [0] = {true, 1u, {0u, 1u}},
    [1] = {true, 1u, {0u, 1u}},
    [2] = {true, 1u, {0u, 1u}},
    [3] = {true, 1u, {0u, 1u}},
    [4] = {true, 1u, {0u, 1u}},
    [5] = {true, 1u, {0u, 1u}},
    [6] = {true, 3u, {2u, 1u}},
    [7] = {true, 1u, {0u, 1u}},
    [8] = {true, 1u, {0u, 1u}},
    [9] = {true, 3u, {0u, 3u}},
    [10] = {true, 5u, {1u, 4u}},
    [11] = {true, 6u, {1u, 5u}},
    [12] = {true, 7u, {1u, 6u}},
    [13] = {true, 10u, {1u, 9u}},
    [14] = {true, 11u, {1u, 11u}},
    [15] = {true, 11u, {3u, 8u}},
    [16] = {true, 12u, {3u, 10u}},
};

static const char *const names_1[] = {"ADDA", NULL};
static const ap_m68040_iu_cell_t cells_1[AP_M68040_IU_MODE_COUNT] = {
    [0] = {true, 1u, {0u, 2u}},
    [1] = {true, 1u, {0u, 1u}},
    [2] = {true, 1u, {0u, 2u}},
    [3] = {true, 2u, {1u, 2u}},
    [4] = {true, 2u, {1u, 2u}},
    [5] = {true, 2u, {1u, 2u}},
    [6] = {true, 3u, {2u, 2u}},
    [7] = {true, 1u, {0u, 2u}},
    [8] = {true, 1u, {0u, 1u}},
    [9] = {true, 4u, {0u, 5u}},
    [10] = {true, 5u, {1u, 5u}},
    [11] = {true, 6u, {1u, 6u}},
    [12] = {true, 7u, {1u, 7u}},
    [13] = {true, 10u, {1u, 10u}},
    [14] = {true, 11u, {1u, 12u}},
    [15] = {true, 11u, {3u, 9u}},
    [16] = {true, 12u, {3u, 11u}},
};

static const char *const names_2[] = {"ADDI", "ANDI", "EORI", "ORI", "SUBI", NULL};
static const ap_m68040_iu_cell_t cells_2[AP_M68040_IU_MODE_COUNT] = {
    [0] = {true, 1u, {0u, 1u}},
    [1] = {false, 0u, {0u, 0u}},
    [2] = {true, 1u, {0u, 1u}},
    [3] = {true, 2u, {1u, 1u}},
    [4] = {true, 2u, {1u, 1u}},
    [5] = {true, 2u, {1u, 1u}},
    [6] = {false, 0u, {0u, 0u}},
    [7] = {true, 2u, {1u, 1u}},
    [8] = {false, 0u, {0u, 0u}},
    [9] = {true, 3u, {0u, 3u}},
    [10] = {false, 0u, {0u, 0u}},
    [11] = {true, 7u, {1u, 6u}},
    [12] = {true, 8u, {1u, 7u}},
    [13] = {true, 10u, {1u, 10u}},
    [14] = {true, 11u, {1u, 11u}},
    [15] = {true, 11u, {3u, 9u}},
    [16] = {true, 12u, {3u, 10u}},
};

static const ap_m68040_iu_group_t groups[] = {
    {names_0, cells_0},
    {names_1, cells_1},
    {names_2, cells_2},
};

size_t ap_m68040_iu_group_count(void) {
  return sizeof groups / sizeof groups[0];
}

const ap_m68040_iu_group_t *ap_m68040_iu_group(size_t index) {
  return (index < ap_m68040_iu_group_count()) ? &groups[index] : NULL;
}

const ap_m68040_iu_group_t *ap_m68040_iu_find(const char *instruction) {
  for (size_t g = 0; g < ap_m68040_iu_group_count(); g++) {
    for (const char *const *n = groups[g].instructions; *n != NULL; n++) {
      if (strcmp(*n, instruction) == 0) {
        return &groups[g];
      }
    }
  }
  return NULL;
}

ap_m68040_iu_cell_t ap_m68040_iu_timing(const char *instruction,
                                        ap_m68040_iu_mode_t mode) {
  const ap_m68040_iu_group_t *group = ap_m68040_iu_find(instruction);
  if (group == NULL || mode >= AP_M68040_IU_MODE_COUNT) {
    return (ap_m68040_iu_cell_t){0};
  }
  return group->cells[mode];
}

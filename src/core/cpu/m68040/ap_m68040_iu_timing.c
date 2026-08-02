/* MC68040 §10.6, from the page images. Each group is one column of the
 * manual's table; the instructions in a group share a column because their
 * timings are identical, not because they are related. */

#include <stddef.h>
#include <string.h>

#include "cpu/m68040/ap_m68040_iu_timing.h"

static const char *const names_0[] = {"ADD", "AND", "EOR", "OR", "SUB", "TST", NULL};
static const ap_m68040_iu_cell_t cells_0[AP_M68040_IU_MODE_COUNT] = {
    [0] = {true, 1u, {0u, 1u}, false, {0u, 0u}},
    [1] = {true, 1u, {0u, 1u}, false, {0u, 0u}},
    [2] = {true, 1u, {0u, 1u}, false, {0u, 0u}},
    [3] = {true, 1u, {0u, 1u}, false, {0u, 0u}},
    [4] = {true, 1u, {0u, 1u}, false, {0u, 0u}},
    [5] = {true, 1u, {0u, 1u}, false, {0u, 0u}},
    [6] = {true, 3u, {2u, 1u}, false, {0u, 0u}},
    [7] = {true, 1u, {0u, 1u}, false, {0u, 0u}},
    [8] = {true, 1u, {0u, 1u}, false, {0u, 0u}},
    [9] = {true, 3u, {0u, 3u}, false, {0u, 0u}},
    [10] = {true, 5u, {1u, 4u}, false, {0u, 0u}},
    [11] = {true, 6u, {1u, 5u}, false, {0u, 0u}},
    [12] = {true, 7u, {1u, 6u}, false, {0u, 0u}},
    [13] = {true, 10u, {1u, 9u}, false, {0u, 0u}},
    [14] = {true, 11u, {1u, 11u}, false, {0u, 0u}},
    [15] = {true, 11u, {3u, 8u}, false, {0u, 0u}},
    [16] = {true, 12u, {3u, 10u}, false, {0u, 0u}},
};

static const char *const names_1[] = {"ADDA", NULL};
static const ap_m68040_iu_cell_t cells_1[AP_M68040_IU_MODE_COUNT] = {
    [0] = {true, 1u, {0u, 2u}, false, {0u, 0u}},
    [1] = {true, 1u, {0u, 1u}, false, {0u, 0u}},
    [2] = {true, 1u, {0u, 2u}, false, {0u, 0u}},
    [3] = {true, 2u, {1u, 2u}, false, {0u, 0u}},
    [4] = {true, 2u, {1u, 2u}, false, {0u, 0u}},
    [5] = {true, 2u, {1u, 2u}, false, {0u, 0u}},
    [6] = {true, 3u, {2u, 2u}, false, {0u, 0u}},
    [7] = {true, 1u, {0u, 2u}, false, {0u, 0u}},
    [8] = {true, 1u, {0u, 1u}, false, {0u, 0u}},
    [9] = {true, 4u, {0u, 5u}, false, {0u, 0u}},
    [10] = {true, 5u, {1u, 5u}, false, {0u, 0u}},
    [11] = {true, 6u, {1u, 6u}, false, {0u, 0u}},
    [12] = {true, 7u, {1u, 7u}, false, {0u, 0u}},
    [13] = {true, 10u, {1u, 10u}, false, {0u, 0u}},
    [14] = {true, 11u, {1u, 12u}, false, {0u, 0u}},
    [15] = {true, 11u, {3u, 9u}, false, {0u, 0u}},
    [16] = {true, 12u, {3u, 11u}, false, {0u, 0u}},
};

static const char *const names_2[] = {"ADDI", "ANDI", "EORI", "ORI", "SUBI", NULL};
static const ap_m68040_iu_cell_t cells_2[AP_M68040_IU_MODE_COUNT] = {
    [0] = {true, 1u, {0u, 1u}, false, {0u, 0u}},
    [1] = {false, 0u, {0u, 0u}, false, {0u, 0u}},
    [2] = {true, 1u, {0u, 1u}, false, {0u, 0u}},
    [3] = {true, 2u, {1u, 1u}, false, {0u, 0u}},
    [4] = {true, 2u, {1u, 1u}, false, {0u, 0u}},
    [5] = {true, 2u, {1u, 1u}, false, {0u, 0u}},
    [6] = {false, 0u, {0u, 0u}, false, {0u, 0u}},
    [7] = {true, 2u, {1u, 1u}, false, {0u, 0u}},
    [8] = {false, 0u, {0u, 0u}, false, {0u, 0u}},
    [9] = {true, 3u, {0u, 3u}, false, {0u, 0u}},
    [10] = {false, 0u, {0u, 0u}, false, {0u, 0u}},
    [11] = {true, 7u, {1u, 6u}, false, {0u, 0u}},
    [12] = {true, 8u, {1u, 7u}, false, {0u, 0u}},
    [13] = {true, 10u, {1u, 10u}, false, {0u, 0u}},
    [14] = {true, 11u, {1u, 11u}, false, {0u, 0u}},
    [15] = {true, 11u, {3u, 9u}, false, {0u, 0u}},
    [16] = {true, 12u, {3u, 10u}, false, {0u, 0u}},
};

static const char *const names_3[] = {"ADDQ", "SUBQ", NULL};
static const ap_m68040_iu_cell_t cells_3[AP_M68040_IU_MODE_COUNT] = {
    [0] = {true, 1u, {0u, 1u}, false, {0u, 0u}},
    [1] = {true, 1u, {0u, 1u}, false, {0u, 0u}},
    [2] = {true, 1u, {0u, 1u}, false, {0u, 0u}},
    [3] = {true, 2u, {1u, 1u}, false, {0u, 0u}},
    [4] = {true, 2u, {1u, 1u}, false, {0u, 0u}},
    [5] = {true, 2u, {1u, 1u}, false, {0u, 0u}},
    [6] = {false, 0u, {0u, 0u}, false, {0u, 0u}},
    [7] = {true, 1u, {0u, 1u}, false, {0u, 0u}},
    [8] = {false, 0u, {0u, 0u}, false, {0u, 0u}},
    [9] = {true, 3u, {0u, 3u}, false, {0u, 0u}},
    [10] = {false, 0u, {0u, 0u}, false, {0u, 0u}},
    [11] = {true, 7u, {1u, 6u}, false, {0u, 0u}},
    [12] = {true, 8u, {1u, 7u}, false, {0u, 0u}},
    [13] = {true, 10u, {1u, 9u}, false, {0u, 0u}},
    [14] = {true, 11u, {1u, 11u}, false, {0u, 0u}},
    [15] = {true, 11u, {3u, 8u}, false, {0u, 0u}},
    [16] = {true, 12u, {3u, 10u}, false, {0u, 0u}},
};

static const char *const names_4[] = {"ASL", NULL};
static const ap_m68040_iu_cell_t cells_4[AP_M68040_IU_MODE_COUNT] = {
    [0] = {true, 1u, {0u, 3u}, true, {0u, 4u}},
    [1] = {false, 0u, {0u, 0u}, false, {0u, 0u}},
    [2] = {true, 1u, {0u, 3u}, false, {0u, 0u}},
    [3] = {true, 1u, {0u, 3u}, false, {0u, 0u}},
    [4] = {true, 1u, {0u, 3u}, false, {0u, 0u}},
    [5] = {true, 1u, {0u, 3u}, false, {0u, 0u}},
    [6] = {false, 0u, {0u, 0u}, false, {0u, 0u}},
    [7] = {true, 1u, {0u, 3u}, false, {0u, 0u}},
    [8] = {false, 0u, {0u, 0u}, false, {0u, 0u}},
    [9] = {true, 3u, {0u, 5u}, false, {0u, 0u}},
    [10] = {false, 0u, {0u, 0u}, false, {0u, 0u}},
    [11] = {true, 7u, {1u, 8u}, false, {0u, 0u}},
    [12] = {true, 8u, {1u, 9u}, false, {0u, 0u}},
    [13] = {true, 10u, {1u, 11u}, false, {0u, 0u}},
    [14] = {true, 11u, {1u, 12u}, false, {0u, 0u}},
    [15] = {true, 11u, {3u, 10u}, false, {0u, 0u}},
    [16] = {true, 12u, {3u, 11u}, false, {0u, 0u}},
};

static const char *const names_5[] = {"ASR", "LSL", "LSR", NULL};
static const ap_m68040_iu_cell_t cells_5[AP_M68040_IU_MODE_COUNT] = {
    [0] = {true, 1u, {0u, 2u}, true, {0u, 3u}},
    [1] = {false, 0u, {0u, 0u}, false, {0u, 0u}},
    [2] = {true, 1u, {0u, 2u}, false, {0u, 0u}},
    [3] = {true, 1u, {0u, 2u}, false, {0u, 0u}},
    [4] = {true, 1u, {0u, 2u}, false, {0u, 0u}},
    [5] = {true, 1u, {0u, 2u}, false, {0u, 0u}},
    [6] = {false, 0u, {0u, 0u}, false, {0u, 0u}},
    [7] = {true, 1u, {0u, 2u}, false, {0u, 0u}},
    [8] = {false, 0u, {0u, 0u}, false, {0u, 0u}},
    [9] = {true, 3u, {0u, 4u}, false, {0u, 0u}},
    [10] = {false, 0u, {0u, 0u}, false, {0u, 0u}},
    [11] = {true, 7u, {1u, 7u}, false, {0u, 0u}},
    [12] = {true, 8u, {1u, 8u}, false, {0u, 0u}},
    [13] = {true, 10u, {1u, 10u}, false, {0u, 0u}},
    [14] = {true, 11u, {1u, 11u}, false, {0u, 0u}},
    [15] = {true, 11u, {3u, 9u}, false, {0u, 0u}},
    [16] = {true, 12u, {3u, 10u}, false, {0u, 0u}},
};

static const ap_m68040_iu_group_t groups[] = {
    {names_0, cells_0},
    {names_1, cells_1},
    {names_2, cells_2},
    {names_3, cells_3},
    {names_4, cells_4},
    {names_5, cells_5},
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

ap_m68040_execute_t ap_m68040_iu_execute(ap_m68040_iu_cell_t cell,
                                         bool register_count) {
  /* "Immediate count specified for shift count/shift count specified in
   * register, respectively." Where the table prints one figure the choice does
   * not arise, so the flag decides rather than the argument. */
  return (cell.has_register_count && register_count)
             ? cell.register_count_execute
             : cell.execute;
}

/* MC68040 §10.6, from the page images. Each group is one column of the
 * manual's table; the instructions in a group share a column because their
 * timings are identical, not because they are related. */

#include <stddef.h>
#include <string.h>

#include "cpu/m68040/ap_m68040_iu_timing.h"

static const char *const names_0[] = {"ADD", "AND", "EOR", "OR", "SUB", "TST", NULL};
static const ap_m68040_iu_cell_t cells_0[AP_M68040_IU_MODE_COUNT] = {
    [0] = {true, 1u, {0u,1u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [1] = {true, 1u, {0u,1u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [2] = {true, 1u, {0u,1u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [3] = {true, 1u, {0u,1u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [4] = {true, 1u, {0u,1u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [5] = {true, 1u, {0u,1u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [6] = {true, 3u, {2u,1u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [7] = {true, 1u, {0u,1u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [8] = {true, 1u, {0u,1u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [9] = {true, 3u, {0u,3u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [10] = {true, 5u, {1u,4u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [11] = {true, 6u, {1u,5u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [12] = {true, 7u, {1u,6u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [13] = {true, 10u, {1u,9u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [14] = {true, 11u, {1u,11u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [15] = {true, 11u, {3u,8u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [16] = {true, 12u, {3u,10u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
};

static const char *const names_1[] = {"ADDA", NULL};
static const ap_m68040_iu_cell_t cells_1[AP_M68040_IU_MODE_COUNT] = {
    [0] = {true, 1u, {0u,2u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [1] = {true, 1u, {0u,1u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [2] = {true, 1u, {0u,2u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [3] = {true, 2u, {1u,2u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [4] = {true, 2u, {1u,2u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [5] = {true, 2u, {1u,2u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [6] = {true, 3u, {2u,2u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [7] = {true, 1u, {0u,2u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [8] = {true, 1u, {0u,1u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [9] = {true, 4u, {0u,5u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [10] = {true, 5u, {1u,5u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [11] = {true, 6u, {1u,6u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [12] = {true, 7u, {1u,7u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [13] = {true, 10u, {1u,10u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [14] = {true, 11u, {1u,12u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [15] = {true, 11u, {3u,9u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [16] = {true, 12u, {3u,11u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
};

static const char *const names_2[] = {"ADDI", "ANDI", "EORI", "ORI", "SUBI", NULL};
static const ap_m68040_iu_cell_t cells_2[AP_M68040_IU_MODE_COUNT] = {
    [0] = {true, 1u, {0u,1u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [1] = {false, 0u, {0u,0u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [2] = {true, 1u, {0u,1u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [3] = {true, 2u, {1u,1u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [4] = {true, 2u, {1u,1u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [5] = {true, 2u, {1u,1u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [6] = {false, 0u, {0u,0u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [7] = {true, 2u, {1u,1u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [8] = {false, 0u, {0u,0u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [9] = {true, 3u, {0u,3u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [10] = {false, 0u, {0u,0u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [11] = {true, 7u, {1u,6u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [12] = {true, 8u, {1u,7u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [13] = {true, 10u, {1u,10u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [14] = {true, 11u, {1u,11u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [15] = {true, 11u, {3u,9u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [16] = {true, 12u, {3u,10u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
};

static const char *const names_3[] = {"ADDQ", "SUBQ", NULL};
static const ap_m68040_iu_cell_t cells_3[AP_M68040_IU_MODE_COUNT] = {
    [0] = {true, 1u, {0u,1u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [1] = {true, 1u, {0u,1u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [2] = {true, 1u, {0u,1u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [3] = {true, 2u, {1u,1u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [4] = {true, 2u, {1u,1u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [5] = {true, 2u, {1u,1u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [6] = {false, 0u, {0u,0u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [7] = {true, 1u, {0u,1u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [8] = {false, 0u, {0u,0u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [9] = {true, 3u, {0u,3u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [10] = {false, 0u, {0u,0u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [11] = {true, 7u, {1u,6u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [12] = {true, 8u, {1u,7u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [13] = {true, 10u, {1u,9u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [14] = {true, 11u, {1u,11u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [15] = {true, 11u, {3u,8u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [16] = {true, 12u, {3u,10u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
};

static const char *const names_4[] = {"ASL", NULL};
static const ap_m68040_iu_cell_t cells_4[AP_M68040_IU_MODE_COUNT] = {
    [0] = {true, 1u, {0u,3u}, AP_M68040_IU_ALTERNATE_SHIFT_COUNT, 1u, {0u,4u}, 0u, 0u},
    [1] = {false, 0u, {0u,0u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [2] = {true, 1u, {0u,3u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [3] = {true, 1u, {0u,3u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [4] = {true, 1u, {0u,3u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [5] = {true, 1u, {0u,3u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [6] = {false, 0u, {0u,0u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [7] = {true, 1u, {0u,3u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [8] = {false, 0u, {0u,0u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [9] = {true, 3u, {0u,5u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [10] = {false, 0u, {0u,0u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [11] = {true, 7u, {1u,8u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [12] = {true, 8u, {1u,9u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [13] = {true, 10u, {1u,11u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [14] = {true, 11u, {1u,12u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [15] = {true, 11u, {3u,10u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [16] = {true, 12u, {3u,11u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
};

static const char *const names_5[] = {"ASR", "LSL", "LSR", NULL};
static const ap_m68040_iu_cell_t cells_5[AP_M68040_IU_MODE_COUNT] = {
    [0] = {true, 1u, {0u,2u}, AP_M68040_IU_ALTERNATE_SHIFT_COUNT, 1u, {0u,3u}, 0u, 0u},
    [1] = {false, 0u, {0u,0u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [2] = {true, 1u, {0u,2u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [3] = {true, 1u, {0u,2u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [4] = {true, 1u, {0u,2u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [5] = {true, 1u, {0u,2u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [6] = {false, 0u, {0u,0u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [7] = {true, 1u, {0u,2u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [8] = {false, 0u, {0u,0u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [9] = {true, 3u, {0u,4u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [10] = {false, 0u, {0u,0u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [11] = {true, 7u, {1u,7u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [12] = {true, 8u, {1u,8u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [13] = {true, 10u, {1u,10u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [14] = {true, 11u, {1u,11u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [15] = {true, 11u, {3u,9u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [16] = {true, 12u, {3u,10u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
};

static const char *const names_6[] = {"BCHG", "BCLR", "BSET", NULL};
static const ap_m68040_iu_cell_t cells_6[AP_M68040_IU_MODE_COUNT] = {
    [0] = {true, 1u, {0u,3u}, AP_M68040_IU_ALTERNATE_BIT_NUMBER, 1u, {0u,4u}, 0u, 0u},
    [1] = {false, 0u, {0u,0u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [2] = {true, 1u, {0u,3u}, AP_M68040_IU_ALTERNATE_BIT_NUMBER, 1u, {0u,4u}, 0u, 0u},
    [3] = {true, 1u, {0u,3u}, AP_M68040_IU_ALTERNATE_BIT_NUMBER, 1u, {0u,4u}, 0u, 0u},
    [4] = {true, 1u, {0u,3u}, AP_M68040_IU_ALTERNATE_BIT_NUMBER, 1u, {0u,4u}, 0u, 0u},
    [5] = {true, 2u, {1u,3u}, AP_M68040_IU_ALTERNATE_BIT_NUMBER, 1u, {1u,4u}, 0u, 0u},
    [6] = {false, 0u, {0u,0u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [7] = {true, 2u, {1u,3u}, AP_M68040_IU_ALTERNATE_BIT_NUMBER, 1u, {1u,4u}, 0u, 0u},
    [8] = {false, 0u, {0u,0u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [9] = {true, 3u, {0u,5u}, AP_M68040_IU_ALTERNATE_BIT_NUMBER, 3u, {0u,6u}, 0u, 0u},
    [10] = {false, 0u, {0u,0u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [11] = {true, 7u, {1u,8u}, AP_M68040_IU_ALTERNATE_BIT_NUMBER, 7u, {1u,9u}, 0u, 0u},
    [12] = {true, 8u, {1u,9u}, AP_M68040_IU_ALTERNATE_BIT_NUMBER, 8u, {1u,10u}, 0u, 0u},
    [13] = {true, 10u, {1u,11u}, AP_M68040_IU_ALTERNATE_BIT_NUMBER, 10u, {1u,12u}, 0u, 0u},
    [14] = {true, 11u, {1u,12u}, AP_M68040_IU_ALTERNATE_BIT_NUMBER, 11u, {1u,13u}, 0u, 0u},
    [15] = {true, 11u, {3u,10u}, AP_M68040_IU_ALTERNATE_BIT_NUMBER, 11u, {3u,11u}, 0u, 0u},
    [16] = {true, 12u, {3u,11u}, AP_M68040_IU_ALTERNATE_BIT_NUMBER, 12u, {3u,12u}, 0u, 0u},
};

static const char *const names_7[] = {"BFCHG", "BFCLR", "BFSET", NULL};
static const ap_m68040_iu_cell_t cells_7[AP_M68040_IU_MODE_COUNT] = {
    [0] = {true, 3u, {0u,6u}, AP_M68040_IU_ALTERNATE_BITFIELD_OPERAND, 4u, {0u,7u}, 10u, 9u},
    [1] = {false, 0u, {0u,0u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [2] = {true, 9u, {2u,8u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 10u, 9u},
    [3] = {false, 0u, {0u,0u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [4] = {false, 0u, {0u,0u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [5] = {true, 9u, {2u,8u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 10u, 9u},
    [6] = {false, 0u, {0u,0u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [7] = {true, 9u, {2u,8u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 10u, 9u},
    [8] = {false, 0u, {0u,0u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [9] = {true, 10u, {0u,11u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 10u, 9u},
    [10] = {false, 0u, {0u,0u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [11] = {true, 13u, {1u,13u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 10u, 9u},
    [12] = {true, 14u, {1u,14u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 10u, 9u},
    [13] = {true, 16u, {1u,16u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 10u, 9u},
    [14] = {true, 17u, {1u,17u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 10u, 9u},
    [15] = {true, 17u, {3u,15u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 10u, 9u},
    [16] = {true, 18u, {3u,16u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 10u, 9u},
};

static const char *const names_8[] = {"BFEXTS", "BFEXTU", NULL};
static const ap_m68040_iu_cell_t cells_8[AP_M68040_IU_MODE_COUNT] = {
    [0] = {true, 1u, {0u,4u}, AP_M68040_IU_ALTERNATE_BITFIELD_OPERAND, 2u, {0u,5u}, 0u, 2u},
    [1] = {false, 0u, {0u,0u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [2] = {true, 9u, {2u,7u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 2u},
    [3] = {false, 0u, {0u,0u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [4] = {false, 0u, {0u,0u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [5] = {true, 9u, {2u,7u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 2u},
    [6] = {true, 10u, {3u,7u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 2u},
    [7] = {true, 9u, {2u,7u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 2u},
    [8] = {false, 0u, {0u,0u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 0u},
    [9] = {true, 10u, {0u,10u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 2u},
    [10] = {true, 11u, {1u,10u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 2u},
    [11] = {true, 13u, {1u,12u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 2u},
    [12] = {true, 14u, {1u,13u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 2u},
    [13] = {true, 16u, {1u,15u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 2u},
    [14] = {true, 17u, {1u,16u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 2u},
    [15] = {true, 17u, {3u,14u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 2u},
    [16] = {true, 18u, {3u,15u}, AP_M68040_IU_ALTERNATE_NONE, 0u, {0u,0u}, 0u, 2u},
};

static const ap_m68040_iu_group_t groups[] = {
    {names_0, cells_0},
    {names_1, cells_1},
    {names_2, cells_2},
    {names_3, cells_3},
    {names_4, cells_4},
    {names_5, cells_5},
    {names_6, cells_6},
    {names_7, cells_7},
    {names_8, cells_8},
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

unsigned ap_m68040_iu_calculate(ap_m68040_iu_cell_t cell, bool alternate,
                                bool spans_long_word) {
  unsigned clocks =
      (cell.alternate != AP_M68040_IU_ALTERNATE_NONE && alternate)
          ? cell.alternate_calculate
          : cell.calculate;
  if (spans_long_word) {
    clocks += cell.boundary_calculate_penalty;
  }
  return clocks;
}

ap_m68040_execute_t ap_m68040_iu_execute(ap_m68040_iu_cell_t cell,
                                         bool alternate,
                                         bool spans_long_word) {
  ap_m68040_execute_t e =
      (cell.alternate != AP_M68040_IU_ALTERNATE_NONE && alternate)
          ? cell.alternate_execute
          : cell.execute;
  if (spans_long_word) {
    e.base += cell.boundary_execute_penalty;
  }
  return e;
}

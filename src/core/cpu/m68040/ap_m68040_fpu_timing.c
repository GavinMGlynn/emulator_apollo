/* MC68040 §10.7.1 and §10.7.2, from the page images. Generated shape, hand-
 * checked against pages 10-29 and 10-30; the semantics are in the header. */

#include "cpu/m68040/ap_m68040_fpu_timing.h"

#include <string.h>

#define CELL(c, l, b) \
  {true, (c), {(l), (b)}}
#define DASH \
  {false, 0u, {0u, 0u}}

static const ap_m68040_fpu_cell_t
    support[AP_M68040_FPU_FORMAT_COUNT][AP_M68040_FPU_MODE_COUNT] = {
        [AP_M68040_FPU_FORMAT_BYTE_WORD] = {
            [0] = DASH,
            [1] = CELL(2u, 1u, 2u),
            [2] = CELL(2u, 0u, 2u),
            [3] = CELL(2u, 0u, 2u),
            [4] = CELL(2u, 0u, 2u),
            [5] = CELL(2u, 0u, 2u),
            [6] = CELL(4u, 2u, 2u),
            [7] = CELL(3u, 1u, 2u),
            [8] = CELL(5u, 3u, 2u),
            [9] = CELL(5u, 0u, 5u),
            [10] = CELL(6u, 1u, 5u),
            [11] = CELL(7u, 1u, 6u),
            [12] = CELL(8u, 1u, 7u),
            [13] = CELL(11u, 1u, 10u),
            [14] = CELL(12u, 1u, 11u),
            [15] = CELL(12u, 3u, 9u),
            [16] = CELL(13u, 3u, 10u),
        },
        [AP_M68040_FPU_FORMAT_LONG] = {
            [0] = DASH,
            [1] = CELL(2u, 1u, 2u),
            [2] = CELL(2u, 0u, 2u),
            [3] = CELL(2u, 0u, 2u),
            [4] = CELL(2u, 0u, 2u),
            [5] = CELL(2u, 0u, 2u),
            [6] = CELL(4u, 2u, 2u),
            [7] = CELL(3u, 1u, 2u),
            [8] = CELL(3u, 1u, 2u),
            [9] = CELL(5u, 0u, 5u),
            [10] = CELL(6u, 1u, 5u),
            [11] = CELL(7u, 1u, 6u),
            [12] = CELL(8u, 1u, 7u),
            [13] = CELL(11u, 1u, 10u),
            [14] = CELL(12u, 1u, 11u),
            [15] = CELL(12u, 3u, 9u),
            [16] = CELL(13u, 3u, 10u),
        },
        [AP_M68040_FPU_FORMAT_SINGLE] = {
            [0] = DASH,
            [1] = CELL(2u, 1u, 2u),
            [2] = CELL(2u, 0u, 2u),
            [3] = CELL(2u, 0u, 2u),
            [4] = CELL(2u, 0u, 2u),
            [5] = CELL(2u, 0u, 2u),
            [6] = CELL(4u, 2u, 2u),
            [7] = CELL(3u, 1u, 2u),
            [8] = CELL(3u, 1u, 2u),
            [9] = CELL(5u, 0u, 5u),
            [10] = CELL(6u, 1u, 5u),
            [11] = CELL(7u, 1u, 6u),
            [12] = CELL(8u, 1u, 7u),
            [13] = CELL(11u, 1u, 10u),
            [14] = CELL(12u, 1u, 11u),
            [15] = CELL(12u, 3u, 9u),
            [16] = CELL(13u, 3u, 10u),
        },
        [AP_M68040_FPU_FORMAT_DOUBLE] = {
            [0] = DASH,
            [1] = DASH,
            [2] = CELL(2u, 0u, 2u),
            [3] = CELL(2u, 0u, 2u),
            [4] = CELL(2u, 0u, 2u),
            [5] = CELL(2u, 0u, 2u),
            [6] = CELL(4u, 1u, 3u),
            [7] = CELL(3u, 1u, 2u),
            [8] = CELL(4u, 2u, 2u),
            [9] = CELL(5u, 0u, 5u),
            [10] = CELL(6u, 0u, 6u),
            [11] = CELL(7u, 1u, 6u),
            [12] = CELL(8u, 1u, 7u),
            [13] = CELL(11u, 1u, 10u),
            [14] = CELL(12u, 1u, 11u),
            [15] = CELL(12u, 3u, 9u),
            [16] = CELL(13u, 3u, 10u),
        },
        [AP_M68040_FPU_FORMAT_EXTENDED] = {
            [0] = CELL(2u, 1u, 2u),
            [1] = DASH,
            [2] = CELL(3u, 0u, 3u),
            [3] = CELL(3u, 0u, 3u),
            [4] = CELL(3u, 0u, 3u),
            [5] = CELL(3u, 0u, 3u),
            [6] = CELL(5u, 1u, 4u),
            [7] = CELL(4u, 1u, 3u),
            [8] = CELL(5u, 2u, 3u),
            [9] = CELL(6u, 0u, 6u),
            [10] = CELL(7u, 0u, 7u),
            [11] = CELL(8u, 1u, 7u),
            [12] = CELL(9u, 1u, 8u),
            [13] = CELL(12u, 1u, 11u),
            [14] = CELL(13u, 1u, 12u),
            [15] = CELL(13u, 3u, 10u),
            [16] = CELL(14u, 3u, 11u),
        },
};

/* "FABS, FADD, FCMP, FDIV, FMOVE, FMUL, FNEG, FSQRT, FSUB, FTST <ea>,FPn" --
 * the column heading, verbatim and in its printed order. */
static const char *const support_names[] = {
    "FABS", "FADD", "FCMP",  "FDIV", "FMOVE",
    "FMUL", "FNEG", "FSQRT", "FSUB", "FTST",
};

ap_m68040_fpu_cell_t ap_m68040_fpu_support(ap_m68040_fpu_format_t format,
                                           ap_m68040_fpu_mode_t mode) {
  if (format >= AP_M68040_FPU_FORMAT_COUNT ||
      mode >= AP_M68040_FPU_MODE_COUNT) {
    return (ap_m68040_fpu_cell_t){false, 0u, {0u, 0u}};
  }
  return support[format][mode];
}

size_t ap_m68040_fpu_support_count(void) {
  return sizeof support_names / sizeof support_names[0];
}

const char *ap_m68040_fpu_support_name(size_t index) {
  return index < ap_m68040_fpu_support_count() ? support_names[index] : NULL;
}

bool ap_m68040_fpu_support_prices(const char *name) {
  if (name == NULL) {
    return false;
  }
  for (size_t i = 0; i < ap_m68040_fpu_support_count(); i++) {
    if (strcmp(name, support_names[i]) == 0) {
      return true;
    }
  }
  return false;
}

bool ap_m68040_fpu_mode_has_base_register(ap_m68040_fpu_mode_t mode) {
  switch (mode) {
  case AP_M68040_FPU_BASE_INDEXED:
  case AP_M68040_FPU_BASE_DISPLACEMENT:
  case AP_M68040_FPU_MEMORY_PREINDEXED:
  case AP_M68040_FPU_MEMORY_PREINDEXED_OD:
  case AP_M68040_FPU_MEMORY_POSTINDEXED:
  case AP_M68040_FPU_MEMORY_POSTINDEXED_OD:
    return true;
  case AP_M68040_FPU_FPN:
  case AP_M68040_FPU_DN:
  case AP_M68040_FPU_INDIRECT:
  case AP_M68040_FPU_POSTINCREMENT:
  case AP_M68040_FPU_PREDECREMENT:
  case AP_M68040_FPU_DISPLACEMENT:
  case AP_M68040_FPU_PC_DISPLACEMENT:
  case AP_M68040_FPU_ABSOLUTE:
  case AP_M68040_FPU_IMMEDIATE:
  case AP_M68040_FPU_INDEXED:
  case AP_M68040_FPU_PC_INDEXED:
  case AP_M68040_FPU_MODE_COUNT:
    break;
  }
  return false;
}

ap_m68040_fpu_cell_t ap_m68040_fpu_with_pc_base(ap_m68040_fpu_cell_t cell) {
  if (!cell.valid) {
    return cell;
  }
  cell.calculate += 1u;
  cell.execute.base += 1u;
  return cell;
}

/* §10.7.1, page 10-29. */
static const struct {
  const char *instruction;
  const char *condition;
  ap_m68040_fpu_cell_t cell;
} misc[AP_M68040_FPU_MISC_COUNT] = {
    [AP_M68040_FPU_MISC_FBCC_TAKEN] = {"FBcc", "Taken", CELL(7u, 0u, 7u)},
    [AP_M68040_FPU_MISC_FBCC_NOT_TAKEN] = {"FBcc", "Not Taken", CELL(6u, 0u, 6u)},
    [AP_M68040_FPU_MISC_FDBCC_TRUE] = {"FDBcc", "cc True", CELL(9u, 1u, 7u)},
    [AP_M68040_FPU_MISC_FDBCC_FALSE] = {"FDBcc", "cc False", CELL(11u, 1u, 9u)},
    [AP_M68040_FPU_MISC_FNOP_IDLE] = {"FNOP", "FPU Idle", CELL(6u, 0u, 6u)},
    [AP_M68040_FPU_MISC_FTRAPCC_NOT_TAKEN] = {"FTRAPcc", "Not Taken", CELL(6u, 1u, 5u)},
};

ap_m68040_fpu_cell_t ap_m68040_fpu_misc(ap_m68040_fpu_misc_t which) {
  if (which >= AP_M68040_FPU_MISC_COUNT) {
    return (ap_m68040_fpu_cell_t){false, 0u, {0u, 0u}};
  }
  return misc[which].cell;
}

const char *ap_m68040_fpu_misc_instruction(ap_m68040_fpu_misc_t which) {
  return which < AP_M68040_FPU_MISC_COUNT ? misc[which].instruction : NULL;
}

const char *ap_m68040_fpu_misc_condition(ap_m68040_fpu_misc_t which) {
  return which < AP_M68040_FPU_MISC_COUNT ? misc[which].condition : NULL;
}

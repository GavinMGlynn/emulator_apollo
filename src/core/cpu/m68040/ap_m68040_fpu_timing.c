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
            [AP_M68040_FPU_FPN] = DASH,
            [AP_M68040_FPU_DN] = CELL(2u, 1u, 2u),
            [AP_M68040_FPU_AN] = DASH,
            [AP_M68040_FPU_INDIRECT] = CELL(2u, 0u, 2u),
            [AP_M68040_FPU_POSTINCREMENT] = CELL(2u, 0u, 2u),
            [AP_M68040_FPU_PREDECREMENT] = CELL(2u, 0u, 2u),
            [AP_M68040_FPU_DISPLACEMENT] = CELL(2u, 0u, 2u),
            [AP_M68040_FPU_PC_DISPLACEMENT] = CELL(4u, 2u, 2u),
            [AP_M68040_FPU_ABSOLUTE] = CELL(3u, 1u, 2u),
            [AP_M68040_FPU_IMMEDIATE] = CELL(5u, 3u, 2u),
            [AP_M68040_FPU_INDEXED] = CELL(5u, 0u, 5u),
            [AP_M68040_FPU_PC_INDEXED] = CELL(6u, 1u, 5u),
            [AP_M68040_FPU_BASE_INDEXED] = CELL(7u, 1u, 6u),
            [AP_M68040_FPU_BASE_DISPLACEMENT] = CELL(8u, 1u, 7u),
            [AP_M68040_FPU_MEMORY_PREINDEXED] = CELL(11u, 1u, 10u),
            [AP_M68040_FPU_MEMORY_PREINDEXED_OD] = CELL(12u, 1u, 11u),
            [AP_M68040_FPU_MEMORY_POSTINDEXED] = CELL(12u, 3u, 9u),
            [AP_M68040_FPU_MEMORY_POSTINDEXED_OD] = CELL(13u, 3u, 10u),
        },
        [AP_M68040_FPU_FORMAT_LONG] = {
            [AP_M68040_FPU_FPN] = DASH,
            [AP_M68040_FPU_DN] = CELL(2u, 1u, 2u),
            [AP_M68040_FPU_AN] = DASH,
            [AP_M68040_FPU_INDIRECT] = CELL(2u, 0u, 2u),
            [AP_M68040_FPU_POSTINCREMENT] = CELL(2u, 0u, 2u),
            [AP_M68040_FPU_PREDECREMENT] = CELL(2u, 0u, 2u),
            [AP_M68040_FPU_DISPLACEMENT] = CELL(2u, 0u, 2u),
            [AP_M68040_FPU_PC_DISPLACEMENT] = CELL(4u, 2u, 2u),
            [AP_M68040_FPU_ABSOLUTE] = CELL(3u, 1u, 2u),
            [AP_M68040_FPU_IMMEDIATE] = CELL(3u, 1u, 2u),
            [AP_M68040_FPU_INDEXED] = CELL(5u, 0u, 5u),
            [AP_M68040_FPU_PC_INDEXED] = CELL(6u, 1u, 5u),
            [AP_M68040_FPU_BASE_INDEXED] = CELL(7u, 1u, 6u),
            [AP_M68040_FPU_BASE_DISPLACEMENT] = CELL(8u, 1u, 7u),
            [AP_M68040_FPU_MEMORY_PREINDEXED] = CELL(11u, 1u, 10u),
            [AP_M68040_FPU_MEMORY_PREINDEXED_OD] = CELL(12u, 1u, 11u),
            [AP_M68040_FPU_MEMORY_POSTINDEXED] = CELL(12u, 3u, 9u),
            [AP_M68040_FPU_MEMORY_POSTINDEXED_OD] = CELL(13u, 3u, 10u),
        },
        [AP_M68040_FPU_FORMAT_SINGLE] = {
            [AP_M68040_FPU_FPN] = DASH,
            [AP_M68040_FPU_DN] = CELL(2u, 1u, 2u),
            [AP_M68040_FPU_AN] = DASH,
            [AP_M68040_FPU_INDIRECT] = CELL(2u, 0u, 2u),
            [AP_M68040_FPU_POSTINCREMENT] = CELL(2u, 0u, 2u),
            [AP_M68040_FPU_PREDECREMENT] = CELL(2u, 0u, 2u),
            [AP_M68040_FPU_DISPLACEMENT] = CELL(2u, 0u, 2u),
            [AP_M68040_FPU_PC_DISPLACEMENT] = CELL(4u, 2u, 2u),
            [AP_M68040_FPU_ABSOLUTE] = CELL(3u, 1u, 2u),
            [AP_M68040_FPU_IMMEDIATE] = CELL(3u, 1u, 2u),
            [AP_M68040_FPU_INDEXED] = CELL(5u, 0u, 5u),
            [AP_M68040_FPU_PC_INDEXED] = CELL(6u, 1u, 5u),
            [AP_M68040_FPU_BASE_INDEXED] = CELL(7u, 1u, 6u),
            [AP_M68040_FPU_BASE_DISPLACEMENT] = CELL(8u, 1u, 7u),
            [AP_M68040_FPU_MEMORY_PREINDEXED] = CELL(11u, 1u, 10u),
            [AP_M68040_FPU_MEMORY_PREINDEXED_OD] = CELL(12u, 1u, 11u),
            [AP_M68040_FPU_MEMORY_POSTINDEXED] = CELL(12u, 3u, 9u),
            [AP_M68040_FPU_MEMORY_POSTINDEXED_OD] = CELL(13u, 3u, 10u),
        },
        [AP_M68040_FPU_FORMAT_DOUBLE] = {
            [AP_M68040_FPU_FPN] = DASH,
            [AP_M68040_FPU_DN] = DASH,
            [AP_M68040_FPU_AN] = DASH,
            [AP_M68040_FPU_INDIRECT] = CELL(2u, 0u, 2u),
            [AP_M68040_FPU_POSTINCREMENT] = CELL(2u, 0u, 2u),
            [AP_M68040_FPU_PREDECREMENT] = CELL(2u, 0u, 2u),
            [AP_M68040_FPU_DISPLACEMENT] = CELL(2u, 0u, 2u),
            [AP_M68040_FPU_PC_DISPLACEMENT] = CELL(4u, 1u, 3u),
            [AP_M68040_FPU_ABSOLUTE] = CELL(3u, 1u, 2u),
            [AP_M68040_FPU_IMMEDIATE] = CELL(4u, 2u, 2u),
            [AP_M68040_FPU_INDEXED] = CELL(5u, 0u, 5u),
            [AP_M68040_FPU_PC_INDEXED] = CELL(6u, 0u, 6u),
            [AP_M68040_FPU_BASE_INDEXED] = CELL(7u, 1u, 6u),
            [AP_M68040_FPU_BASE_DISPLACEMENT] = CELL(8u, 1u, 7u),
            [AP_M68040_FPU_MEMORY_PREINDEXED] = CELL(11u, 1u, 10u),
            [AP_M68040_FPU_MEMORY_PREINDEXED_OD] = CELL(12u, 1u, 11u),
            [AP_M68040_FPU_MEMORY_POSTINDEXED] = CELL(12u, 3u, 9u),
            [AP_M68040_FPU_MEMORY_POSTINDEXED_OD] = CELL(13u, 3u, 10u),
        },
        [AP_M68040_FPU_FORMAT_EXTENDED] = {
            [AP_M68040_FPU_FPN] = CELL(2u, 1u, 2u),
            [AP_M68040_FPU_DN] = DASH,
            [AP_M68040_FPU_AN] = DASH,
            [AP_M68040_FPU_INDIRECT] = CELL(3u, 0u, 3u),
            [AP_M68040_FPU_POSTINCREMENT] = CELL(3u, 0u, 3u),
            [AP_M68040_FPU_PREDECREMENT] = CELL(3u, 0u, 3u),
            [AP_M68040_FPU_DISPLACEMENT] = CELL(3u, 0u, 3u),
            [AP_M68040_FPU_PC_DISPLACEMENT] = CELL(5u, 1u, 4u),
            [AP_M68040_FPU_ABSOLUTE] = CELL(4u, 1u, 3u),
            [AP_M68040_FPU_IMMEDIATE] = CELL(5u, 2u, 3u),
            [AP_M68040_FPU_INDEXED] = CELL(6u, 0u, 6u),
            [AP_M68040_FPU_PC_INDEXED] = CELL(7u, 0u, 7u),
            [AP_M68040_FPU_BASE_INDEXED] = CELL(8u, 1u, 7u),
            [AP_M68040_FPU_BASE_DISPLACEMENT] = CELL(9u, 1u, 8u),
            [AP_M68040_FPU_MEMORY_PREINDEXED] = CELL(12u, 1u, 11u),
            [AP_M68040_FPU_MEMORY_PREINDEXED_OD] = CELL(13u, 1u, 12u),
            [AP_M68040_FPU_MEMORY_POSTINDEXED] = CELL(13u, 3u, 10u),
            [AP_M68040_FPU_MEMORY_POSTINDEXED_OD] = CELL(14u, 3u, 11u),
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
  case AP_M68040_FPU_AN:
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

static const ap_m68040_fpu_cell_t
    store_table[AP_M68040_FPU_STORE_FORMAT_COUNT][AP_M68040_FPU_MODE_COUNT] = {
        [AP_M68040_FPU_STORE_BYTE_WORD_LONG] = {
            [AP_M68040_FPU_FPN] = DASH,
            [AP_M68040_FPU_DN] = CELL(9u, 9u, 3u),
            [AP_M68040_FPU_AN] = DASH,
            [AP_M68040_FPU_INDIRECT] = CELL(8u, 9u, 2u),
            [AP_M68040_FPU_POSTINCREMENT] = CELL(8u, 9u, 2u),
            [AP_M68040_FPU_PREDECREMENT] = CELL(8u, 9u, 2u),
            [AP_M68040_FPU_DISPLACEMENT] = CELL(8u, 9u, 2u),
            [AP_M68040_FPU_PC_DISPLACEMENT] = DASH,
            [AP_M68040_FPU_ABSOLUTE] = CELL(8u, 9u, 2u),
            [AP_M68040_FPU_IMMEDIATE] = DASH,
            [AP_M68040_FPU_INDEXED] = CELL(8u, 6u, 5u),
            [AP_M68040_FPU_PC_INDEXED] = DASH,
            [AP_M68040_FPU_BASE_INDEXED] = CELL(7u, 4u, 6u),
            [AP_M68040_FPU_BASE_DISPLACEMENT] = CELL(8u, 4u, 7u),
            [AP_M68040_FPU_MEMORY_PREINDEXED] = CELL(11u, 1u, 10u),
            [AP_M68040_FPU_MEMORY_PREINDEXED_OD] = CELL(12u, 1u, 11u),
            [AP_M68040_FPU_MEMORY_POSTINDEXED] = CELL(12u, 3u, 9u),
            [AP_M68040_FPU_MEMORY_POSTINDEXED_OD] = CELL(13u, 3u, 10u),
        },
        [AP_M68040_FPU_STORE_SINGLE_DOUBLE] = {
            [AP_M68040_FPU_FPN] = DASH,
            [AP_M68040_FPU_DN] = CELL(2u, 1u, 3u),
            [AP_M68040_FPU_AN] = DASH,
            [AP_M68040_FPU_INDIRECT] = CELL(2u, 1u, 2u),
            [AP_M68040_FPU_POSTINCREMENT] = CELL(2u, 1u, 2u),
            [AP_M68040_FPU_PREDECREMENT] = CELL(2u, 1u, 2u),
            [AP_M68040_FPU_DISPLACEMENT] = CELL(2u, 1u, 2u),
            [AP_M68040_FPU_PC_DISPLACEMENT] = DASH,
            [AP_M68040_FPU_ABSOLUTE] = CELL(3u, 1u, 2u),
            [AP_M68040_FPU_IMMEDIATE] = DASH,
            [AP_M68040_FPU_INDEXED] = CELL(5u, 0u, 5u),
            [AP_M68040_FPU_PC_INDEXED] = DASH,
            [AP_M68040_FPU_BASE_INDEXED] = CELL(7u, 1u, 6u),
            [AP_M68040_FPU_BASE_DISPLACEMENT] = CELL(8u, 1u, 7u),
            [AP_M68040_FPU_MEMORY_PREINDEXED] = CELL(11u, 1u, 10u),
            [AP_M68040_FPU_MEMORY_PREINDEXED_OD] = CELL(12u, 1u, 11u),
            [AP_M68040_FPU_MEMORY_POSTINDEXED] = CELL(12u, 3u, 9u),
            [AP_M68040_FPU_MEMORY_POSTINDEXED_OD] = CELL(13u, 3u, 10u),
        },
        [AP_M68040_FPU_STORE_EXTENDED] = {
            [AP_M68040_FPU_FPN] = DASH,
            [AP_M68040_FPU_DN] = DASH,
            [AP_M68040_FPU_AN] = DASH,
            [AP_M68040_FPU_INDIRECT] = CELL(4u, 1u, 3u),
            [AP_M68040_FPU_POSTINCREMENT] = CELL(4u, 1u, 3u),
            [AP_M68040_FPU_PREDECREMENT] = CELL(4u, 1u, 3u),
            [AP_M68040_FPU_DISPLACEMENT] = CELL(4u, 1u, 3u),
            [AP_M68040_FPU_PC_DISPLACEMENT] = DASH,
            [AP_M68040_FPU_ABSOLUTE] = CELL(4u, 1u, 3u),
            [AP_M68040_FPU_IMMEDIATE] = DASH,
            [AP_M68040_FPU_INDEXED] = CELL(6u, 0u, 6u),
            [AP_M68040_FPU_PC_INDEXED] = DASH,
            [AP_M68040_FPU_BASE_INDEXED] = CELL(8u, 1u, 7u),
            [AP_M68040_FPU_BASE_DISPLACEMENT] = CELL(9u, 1u, 8u),
            [AP_M68040_FPU_MEMORY_PREINDEXED] = CELL(12u, 1u, 11u),
            [AP_M68040_FPU_MEMORY_PREINDEXED_OD] = CELL(13u, 1u, 12u),
            [AP_M68040_FPU_MEMORY_POSTINDEXED] = CELL(13u, 3u, 10u),
            [AP_M68040_FPU_MEMORY_POSTINDEXED_OD] = CELL(14u, 3u, 11u),
        },
};

static const ap_m68040_fpu_cell_t control_table[AP_M68040_FPU_MODE_COUNT] = {
    [AP_M68040_FPU_FPN] = DASH,
    [AP_M68040_FPU_DN] = CELL(2u, 1u, 2u),
    [AP_M68040_FPU_AN] = CELL(2u, 1u, 2u),
    [AP_M68040_FPU_INDIRECT] = CELL(4u, 2u, 3u),
    [AP_M68040_FPU_POSTINCREMENT] = CELL(4u, 2u, 3u),
    [AP_M68040_FPU_PREDECREMENT] = CELL(5u, 3u, 3u),
    [AP_M68040_FPU_DISPLACEMENT] = CELL(4u, 2u, 3u),
    [AP_M68040_FPU_PC_DISPLACEMENT] = CELL(5u, 4u, 3u),
    [AP_M68040_FPU_ABSOLUTE] = CELL(4u, 2u, 3u),
    [AP_M68040_FPU_IMMEDIATE] = CELL(4u, 2u, 3u),
    [AP_M68040_FPU_INDEXED] = CELL(5u, 0u, 6u),
    [AP_M68040_FPU_PC_INDEXED] = CELL(6u, 1u, 6u),
    [AP_M68040_FPU_BASE_INDEXED] = CELL(7u, 1u, 7u),
    [AP_M68040_FPU_BASE_DISPLACEMENT] = CELL(8u, 1u, 8u),
    [AP_M68040_FPU_MEMORY_PREINDEXED] = CELL(11u, 1u, 11u),
    [AP_M68040_FPU_MEMORY_PREINDEXED_OD] = CELL(12u, 1u, 13u),
    [AP_M68040_FPU_MEMORY_POSTINDEXED] = CELL(12u, 3u, 10u),
    [AP_M68040_FPU_MEMORY_POSTINDEXED_OD] = CELL(13u, 3u, 12u),
};

static const ap_m68040_fpu_cell_t movem_table[AP_M68040_FPU_MODE_COUNT] = {
    [AP_M68040_FPU_FPN] = DASH,
    [AP_M68040_FPU_DN] = DASH,
    [AP_M68040_FPU_AN] = DASH,
    [AP_M68040_FPU_INDIRECT] = CELL(17u, 2u, 15u),
    [AP_M68040_FPU_POSTINCREMENT] = CELL(17u, 2u, 15u),
    [AP_M68040_FPU_PREDECREMENT] = CELL(16u, 1u, 15u),
    [AP_M68040_FPU_DISPLACEMENT] = CELL(17u, 2u, 15u),
    [AP_M68040_FPU_PC_DISPLACEMENT] = DASH,
    [AP_M68040_FPU_ABSOLUTE] = CELL(19u, 3u, 15u),
    [AP_M68040_FPU_IMMEDIATE] = CELL(19u, 1u, 17u),
    [AP_M68040_FPU_INDEXED] = CELL(19u, 0u, 18u),
    [AP_M68040_FPU_PC_INDEXED] = CELL(20u, 1u, 18u),
    [AP_M68040_FPU_BASE_INDEXED] = CELL(20u, 1u, 19u),
    [AP_M68040_FPU_BASE_DISPLACEMENT] = CELL(21u, 1u, 20u),
    [AP_M68040_FPU_MEMORY_PREINDEXED] = CELL(25u, 1u, 23u),
    [AP_M68040_FPU_MEMORY_PREINDEXED_OD] = CELL(25u, 1u, 24u),
    [AP_M68040_FPU_MEMORY_POSTINDEXED] = CELL(26u, 3u, 22u),
    [AP_M68040_FPU_MEMORY_POSTINDEXED_OD] = CELL(26u, 3u, 23u),
};

static const ap_m68040_fpu_cell_t scc_table[AP_M68040_FPU_MODE_COUNT] = {
    [AP_M68040_FPU_FPN] = DASH,
    [AP_M68040_FPU_DN] = CELL(5u, 0u, 6u),
    [AP_M68040_FPU_AN] = DASH,
    [AP_M68040_FPU_INDIRECT] = CELL(4u, 0u, 5u),
    [AP_M68040_FPU_POSTINCREMENT] = CELL(6u, 2u, 5u),
    [AP_M68040_FPU_PREDECREMENT] = CELL(6u, 2u, 5u),
    [AP_M68040_FPU_DISPLACEMENT] = CELL(4u, 0u, 5u),
    [AP_M68040_FPU_PC_DISPLACEMENT] = DASH,
    [AP_M68040_FPU_ABSOLUTE] = CELL(4u, 0u, 5u),
    [AP_M68040_FPU_IMMEDIATE] = DASH,
    [AP_M68040_FPU_INDEXED] = CELL(7u, 0u, 8u),
    [AP_M68040_FPU_PC_INDEXED] = DASH,
    [AP_M68040_FPU_BASE_INDEXED] = CELL(9u, 1u, 9u),
    [AP_M68040_FPU_BASE_DISPLACEMENT] = CELL(10u, 1u, 10u),
    [AP_M68040_FPU_MEMORY_PREINDEXED] = CELL(13u, 1u, 13u),
    [AP_M68040_FPU_MEMORY_PREINDEXED_OD] = CELL(14u, 1u, 14u),
    [AP_M68040_FPU_MEMORY_POSTINDEXED] = CELL(14u, 3u, 12u),
    [AP_M68040_FPU_MEMORY_POSTINDEXED_OD] = CELL(15u, 3u, 13u),
};

static const ap_m68040_fpu_cell_t
    save_table[AP_M68040_FPU_FRAME_COUNT][AP_M68040_FPU_MODE_COUNT] = {
        [AP_M68040_FPU_FRAME_IDLE_OR_NULL] = {
            [AP_M68040_FPU_FPN] = DASH,
            [AP_M68040_FPU_DN] = DASH,
            [AP_M68040_FPU_AN] = DASH,
            [AP_M68040_FPU_INDIRECT] = CELL(12u, 1u, 11u),
            [AP_M68040_FPU_POSTINCREMENT] = DASH,
            [AP_M68040_FPU_PREDECREMENT] = CELL(11u, 0u, 11u),
            [AP_M68040_FPU_DISPLACEMENT] = CELL(12u, 1u, 11u),
            [AP_M68040_FPU_PC_DISPLACEMENT] = DASH,
            [AP_M68040_FPU_ABSOLUTE] = CELL(13u, 1u, 11u),
            [AP_M68040_FPU_IMMEDIATE] = DASH,
            [AP_M68040_FPU_INDEXED] = CELL(13u, 0u, 13u),
            [AP_M68040_FPU_PC_INDEXED] = DASH,
            [AP_M68040_FPU_BASE_INDEXED] = CELL(16u, 1u, 14u),
            [AP_M68040_FPU_BASE_DISPLACEMENT] = CELL(17u, 1u, 15u),
            [AP_M68040_FPU_MEMORY_PREINDEXED] = CELL(19u, 1u, 18u),
            [AP_M68040_FPU_MEMORY_PREINDEXED_OD] = CELL(21u, 1u, 19u),
            [AP_M68040_FPU_MEMORY_POSTINDEXED] = CELL(20u, 3u, 17u),
            [AP_M68040_FPU_MEMORY_POSTINDEXED_OD] = CELL(22u, 3u, 18u),
        },
        [AP_M68040_FPU_FRAME_SHORT] = {
            [AP_M68040_FPU_FPN] = DASH,
            [AP_M68040_FPU_DN] = DASH,
            [AP_M68040_FPU_AN] = DASH,
            [AP_M68040_FPU_INDIRECT] = CELL(33u, 1u, 32u),
            [AP_M68040_FPU_POSTINCREMENT] = DASH,
            [AP_M68040_FPU_PREDECREMENT] = CELL(32u, 0u, 32u),
            [AP_M68040_FPU_DISPLACEMENT] = CELL(33u, 1u, 32u),
            [AP_M68040_FPU_PC_DISPLACEMENT] = DASH,
            [AP_M68040_FPU_ABSOLUTE] = CELL(34u, 1u, 32u),
            [AP_M68040_FPU_IMMEDIATE] = DASH,
            [AP_M68040_FPU_INDEXED] = CELL(34u, 0u, 34u),
            [AP_M68040_FPU_PC_INDEXED] = DASH,
            [AP_M68040_FPU_BASE_INDEXED] = CELL(37u, 1u, 35u),
            [AP_M68040_FPU_BASE_DISPLACEMENT] = CELL(38u, 1u, 36u),
            [AP_M68040_FPU_MEMORY_PREINDEXED] = CELL(40u, 1u, 39u),
            [AP_M68040_FPU_MEMORY_PREINDEXED_OD] = CELL(42u, 1u, 40u),
            [AP_M68040_FPU_MEMORY_POSTINDEXED] = CELL(41u, 3u, 38u),
            [AP_M68040_FPU_MEMORY_POSTINDEXED_OD] = CELL(46u, 3u, 42u),
        },
        [AP_M68040_FPU_FRAME_LONG] = {
            [AP_M68040_FPU_FPN] = DASH,
            [AP_M68040_FPU_DN] = DASH,
            [AP_M68040_FPU_AN] = DASH,
            [AP_M68040_FPU_INDIRECT] = CELL(50u, 1u, 49u),
            [AP_M68040_FPU_POSTINCREMENT] = DASH,
            [AP_M68040_FPU_PREDECREMENT] = CELL(49u, 0u, 49u),
            [AP_M68040_FPU_DISPLACEMENT] = CELL(50u, 1u, 49u),
            [AP_M68040_FPU_PC_DISPLACEMENT] = DASH,
            [AP_M68040_FPU_ABSOLUTE] = CELL(51u, 1u, 49u),
            [AP_M68040_FPU_IMMEDIATE] = DASH,
            [AP_M68040_FPU_INDEXED] = CELL(51u, 0u, 51u),
            [AP_M68040_FPU_PC_INDEXED] = DASH,
            [AP_M68040_FPU_BASE_INDEXED] = CELL(54u, 1u, 52u),
            [AP_M68040_FPU_BASE_DISPLACEMENT] = CELL(55u, 1u, 53u),
            [AP_M68040_FPU_MEMORY_PREINDEXED] = CELL(57u, 1u, 56u),
            [AP_M68040_FPU_MEMORY_PREINDEXED_OD] = CELL(59u, 1u, 57u),
            [AP_M68040_FPU_MEMORY_POSTINDEXED] = CELL(58u, 3u, 55u),
            [AP_M68040_FPU_MEMORY_POSTINDEXED_OD] = CELL(65u, 3u, 61u),
        },
};

static const ap_m68040_fpu_cell_t
    restore_table[AP_M68040_FPU_FRAME_COUNT][AP_M68040_FPU_MODE_COUNT] = {
        [AP_M68040_FPU_FRAME_IDLE_OR_NULL] = {
            [AP_M68040_FPU_FPN] = DASH,
            [AP_M68040_FPU_DN] = DASH,
            [AP_M68040_FPU_AN] = DASH,
            [AP_M68040_FPU_INDIRECT] = CELL(13u, 1u, 12u),
            [AP_M68040_FPU_POSTINCREMENT] = CELL(13u, 1u, 12u),
            [AP_M68040_FPU_PREDECREMENT] = DASH,
            [AP_M68040_FPU_DISPLACEMENT] = CELL(13u, 1u, 12u),
            [AP_M68040_FPU_PC_DISPLACEMENT] = DASH,
            [AP_M68040_FPU_ABSOLUTE] = CELL(14u, 1u, 12u),
            [AP_M68040_FPU_IMMEDIATE] = DASH,
            [AP_M68040_FPU_INDEXED] = CELL(14u, 0u, 14u),
            [AP_M68040_FPU_PC_INDEXED] = DASH,
            [AP_M68040_FPU_BASE_INDEXED] = CELL(16u, 1u, 14u),
            [AP_M68040_FPU_BASE_DISPLACEMENT] = CELL(17u, 1u, 15u),
            [AP_M68040_FPU_MEMORY_PREINDEXED] = CELL(20u, 1u, 19u),
            [AP_M68040_FPU_MEMORY_PREINDEXED_OD] = CELL(21u, 1u, 19u),
            [AP_M68040_FPU_MEMORY_POSTINDEXED] = CELL(21u, 3u, 18u),
            [AP_M68040_FPU_MEMORY_POSTINDEXED_OD] = CELL(22u, 3u, 19u),
        },
        [AP_M68040_FPU_FRAME_SHORT] = {
            [AP_M68040_FPU_FPN] = DASH,
            [AP_M68040_FPU_DN] = DASH,
            [AP_M68040_FPU_AN] = DASH,
            [AP_M68040_FPU_INDIRECT] = CELL(26u, 1u, 25u),
            [AP_M68040_FPU_POSTINCREMENT] = CELL(26u, 1u, 25u),
            [AP_M68040_FPU_PREDECREMENT] = DASH,
            [AP_M68040_FPU_DISPLACEMENT] = CELL(26u, 1u, 25u),
            [AP_M68040_FPU_PC_DISPLACEMENT] = DASH,
            [AP_M68040_FPU_ABSOLUTE] = CELL(27u, 1u, 25u),
            [AP_M68040_FPU_IMMEDIATE] = DASH,
            [AP_M68040_FPU_INDEXED] = CELL(27u, 0u, 27u),
            [AP_M68040_FPU_PC_INDEXED] = DASH,
            [AP_M68040_FPU_BASE_INDEXED] = CELL(29u, 1u, 27u),
            [AP_M68040_FPU_BASE_DISPLACEMENT] = CELL(30u, 1u, 28u),
            [AP_M68040_FPU_MEMORY_PREINDEXED] = CELL(33u, 1u, 32u),
            [AP_M68040_FPU_MEMORY_PREINDEXED_OD] = CELL(34u, 1u, 32u),
            [AP_M68040_FPU_MEMORY_POSTINDEXED] = CELL(34u, 3u, 31u),
            [AP_M68040_FPU_MEMORY_POSTINDEXED_OD] = CELL(35u, 3u, 31u),
        },
        [AP_M68040_FPU_FRAME_LONG] = {
            [AP_M68040_FPU_FPN] = DASH,
            [AP_M68040_FPU_DN] = DASH,
            [AP_M68040_FPU_AN] = DASH,
            [AP_M68040_FPU_INDIRECT] = CELL(40u, 1u, 39u),
            [AP_M68040_FPU_POSTINCREMENT] = CELL(40u, 1u, 39u),
            [AP_M68040_FPU_PREDECREMENT] = DASH,
            [AP_M68040_FPU_DISPLACEMENT] = CELL(40u, 1u, 39u),
            [AP_M68040_FPU_PC_DISPLACEMENT] = DASH,
            [AP_M68040_FPU_ABSOLUTE] = CELL(41u, 1u, 39u),
            [AP_M68040_FPU_IMMEDIATE] = DASH,
            [AP_M68040_FPU_INDEXED] = CELL(41u, 0u, 41u),
            [AP_M68040_FPU_PC_INDEXED] = DASH,
            [AP_M68040_FPU_BASE_INDEXED] = CELL(43u, 1u, 41u),
            [AP_M68040_FPU_BASE_DISPLACEMENT] = CELL(44u, 1u, 42u),
            [AP_M68040_FPU_MEMORY_PREINDEXED] = CELL(47u, 1u, 46u),
            [AP_M68040_FPU_MEMORY_PREINDEXED_OD] = CELL(48u, 1u, 46u),
            [AP_M68040_FPU_MEMORY_POSTINDEXED] = CELL(48u, 3u, 45u),
            [AP_M68040_FPU_MEMORY_POSTINDEXED_OD] = CELL(49u, 3u, 45u),
        },
};

static ap_m68040_fpu_cell_t lookup(const ap_m68040_fpu_cell_t *table,
                                   ap_m68040_fpu_mode_t mode) {
  if (mode >= AP_M68040_FPU_MODE_COUNT) {
    return (ap_m68040_fpu_cell_t){false, 0u, {0u, 0u}};
  }
  return table[mode];
}

ap_m68040_fpu_cell_t ap_m68040_fpu_store(ap_m68040_fpu_store_format_t format,
                                         ap_m68040_fpu_mode_t mode) {
  if (format >= AP_M68040_FPU_STORE_FORMAT_COUNT) {
    return (ap_m68040_fpu_cell_t){false, 0u, {0u, 0u}};
  }
  return lookup(store_table[format], mode);
}

ap_m68040_fpu_cell_t ap_m68040_fpu_control(ap_m68040_fpu_mode_t mode) {
  return lookup(control_table, mode);
}

ap_m68040_fpu_cell_t ap_m68040_fpu_movem(ap_m68040_fpu_mode_t mode) {
  return lookup(movem_table, mode);
}

ap_m68040_fpu_cell_t ap_m68040_fpu_scc(ap_m68040_fpu_mode_t mode) {
  return lookup(scc_table, mode);
}

ap_m68040_fpu_cell_t ap_m68040_fpu_save(ap_m68040_fpu_frame_t frame,
                                        ap_m68040_fpu_mode_t mode) {
  if (frame >= AP_M68040_FPU_FRAME_COUNT) {
    return (ap_m68040_fpu_cell_t){false, 0u, {0u, 0u}};
  }
  return lookup(save_table[frame], mode);
}

ap_m68040_fpu_cell_t ap_m68040_fpu_restore(ap_m68040_fpu_frame_t frame,
                                           ap_m68040_fpu_mode_t mode) {
  if (frame >= AP_M68040_FPU_FRAME_COUNT) {
    return (ap_m68040_fpu_cell_t){false, 0u, {0u, 0u}};
  }
  return lookup(restore_table[frame], mode);
}

/* Note b, page 10-32. The cell already prices one register, so `registers`
 * counts from one and a zero-register list is refused rather than discounted --
 * an `FMOVEM` of nothing is an encoding that does not exist, not a fast one. */
ap_m68040_fpu_cell_t ap_m68040_fpu_movem_with_list(ap_m68040_fpu_cell_t cell,
                                                   unsigned registers,
                                                   bool dynamic) {
  if (!cell.valid || registers == 0u) {
    return (ap_m68040_fpu_cell_t){false, 0u, {0u, 0u}};
  }
  const unsigned extra = (registers - 1u) * AP_M68040_FPU_MOVEM_PER_EXTRA_REGISTER +
                         (dynamic ? AP_M68040_FPU_MOVEM_DYNAMIC_LIST : 0u);
  cell.calculate += extra;
  cell.execute.base += extra;
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

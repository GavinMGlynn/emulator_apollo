/* MC68040 §10.5, transcribed from the page images of pages 10-11 and 10-12.
 * See the header for what the six notes do to these figures. */

#include <string.h>

#include "cpu/m68040/ap_m68040_misc_timing.h"

/* Shorthands so a row reads like the table's line. `L(n, b)` is the manual's
 * `nL + b`; `F(n)` is a flat figure, which the table prints without an `L`. */
#define L(n, b) ((ap_m68040_execute_t){.lead = (n), .base = (b)})
#define F(n) ((ap_m68040_execute_t){.lead = 0u, .base = (n)})

#define EXACT AP_M68040_TIMING_EXACT
#define MIN AP_M68040_TIMING_MINIMUM
#define TYP AP_M68040_TIMING_TYPICAL

static const ap_m68040_misc_timing_t table[] = {
    {"ABCD", "Dy,Dx", 1u, F(3u), EXACT, false, false},
    {"ABCD", "-(Ay),-(Ax)", 3u, L(1u, 3u), EXACT, false, false},
    {"ADDX", "Dy,Dx", 1u, F(1u), EXACT, false, false},
    {"ADDX", "-(Ay),-(Ax)", 3u, L(1u, 2u), EXACT, false, false},
    {"ANDI #<xxx>,CCR", NULL, 1u, F(4u), EXACT, false, false},
    /* note a */
    {"ANDI #<xxx>,SR", NULL, 9u, L(1u, 8u), MIN, true, false},
    {"Bcc", "Branch Taken", 2u, F(2u), EXACT, false, false},
    {"Bcc", "Branch Not Taken", 3u, F(3u), EXACT, false, false},
    {"BRA", "Branch Taken", 2u, F(2u), EXACT, false, false},
    {"BRA", "Branch Not Taken", 3u, F(3u), EXACT, false, false},
    {"BSR <offset>", NULL, 2u, L(1u, 1u), EXACT, false, false},
    /* note b */
    {"CAS2", "True", 56u, L(6u, 49u), TYP, true, false},
    {"CAS2", "False", 51u, L(6u, 44u), TYP, true, false},
    {"CMPM", NULL, 3u, L(1u, 2u), EXACT, false, false},
    /* note c */
    {"DBcc", "False, Count > -1", 3u, F(3u), EXACT, true, false},
    {"DBcc", "False, Count = -1", 4u, F(4u), EXACT, true, false},
    {"DBcc", "True", 4u, F(4u), EXACT, true, false},
    {"EORI #<xxx>,CCR", NULL, 1u, F(4u), EXACT, false, false},
    {"EORI #<xxx>,SR", NULL, 9u, L(1u, 8u), MIN, true, false},
    {"EXG", "Dy,Dx", 1u, F(1u), EXACT, false, false},
    {"EXG", "Ay,Ax", 2u, L(1u, 1u), EXACT, false, false},
    {"EXG", "Dy,Ax", 1u, F(1u), EXACT, false, false},
    {"EXT", "Word", 1u, F(2u), EXACT, false, false},
    {"EXT", "Long Word", 1u, F(1u), EXACT, false, false},
    {"EXTB", "Long Word", 1u, F(1u), EXACT, false, false},
    {"ILLEGAL", "A-Line Unimplemented", 16u, F(16u), MIN, true, false},
    {"ILLEGAL", "F-Line Unimplemented", 16u, F(16u), MIN, true, false},
    {"LINK", NULL, 3u, L(2u, 1u), EXACT, false, false},
    {"MOVE USP", "USP,An", 3u, L(2u, 1u), EXACT, false, false},
    {"MOVE USP", "An,USP", 7u, L(1u, 6u), MIN, true, false},
    /* notes c and d */
    {"MOVE16", "(Ax)+,(Ay)+", 6u, L(1u, 7u), EXACT, true, true},
    {"MOVE16", "xxx.L,(An)", 4u, F(7u), EXACT, true, true},
    {"MOVE16", "xxx.L,(An)+", 5u, F(8u), EXACT, true, true},
    {"MOVE16", "(An),xxx.L", 4u, F(7u), EXACT, true, true},
    {"MOVE16", "(An)+,xxx.L", 4u, F(7u), EXACT, true, true},
    {"MOVEC", "Rn,Rc", 7u, L(1u, 6u), TYP, true, false},
    {"MOVEC", "Rc,Rn", 11u, L(1u, 10u), TYP, true, false},
    {"MOVEP", "MOVEP.W Dn,d16(An)", 11u, L(2u, 9u), EXACT, true, false},
    {"MOVEP", "MOVEP.L Dn,d16(An)", 13u, L(2u, 11u), EXACT, true, false},
    {"MOVEP", "MOVEP.W d16(An),Dn", 4u, L(2u, 5u), EXACT, true, false},
    {"MOVEP", "MOVEP.L d16(An),Dn", 8u, L(2u, 8u), EXACT, true, false},
    /* `MOVEQ`, which extraction renders as `MOVEa` -- see the header. */
    {"MOVEQ", NULL, 1u, F(1u), EXACT, false, false},
    {"NOP", NULL, 8u, L(1u, 7u), MIN, true, false},
    {"ORI #<xxx>,CCR", NULL, 1u, F(4u), EXACT, false, false},
    {"ORI #<xxx>,SR", NULL, 9u, L(1u, 8u), MIN, true, false},
    {"PACK", "Dx,Dy,#<xxx>", 1u, F(3u), EXACT, false, false},
    {"PACK", "-(Ay),-(Ax),#<xxx>", 3u, L(2u, 3u), EXACT, false, false},
    {"PFLUSH", NULL, 11u, L(1u, 10u), TYP, true, false},
    {"PFLUSHA", NULL, 11u, L(1u, 10u), TYP, true, false},
    {"PFLUSHAN", NULL, 27u, L(1u, 26u), TYP, true, false},
    {"PFLUSHN (An)", NULL, 11u, L(1u, 10u), TYP, true, false},
    /* note e: the most conditional figure in the table. */
    {"PTESTR, PTESTW", NULL, 25u, L(11u, 14u), TYP, true, false},
    {"RESET", NULL, 521u, F(521u), MIN, true, false},
    {"RTD", NULL, 6u, L(1u, 5u), EXACT, true, false},
    /* note a: every RTE row is a minimum. */
    {"RTE", "Stack Format $0", 2u, F(13u), MIN, true, false},
    {"RTE", "Stack Format $1", 4u, F(23u), MIN, true, false},
    {"RTE", "Stack Format $2", 2u, F(14u), MIN, true, false},
    {"RTE", "Stack Format $3", 3u, F(20u), MIN, true, false},
    {"RTE", "Stack Format $4", 2u, F(15u), MIN, true, false},
    {"RTE", "Stack Format $7", 4u, F(23u), MIN, true, false},
    {"RTR", NULL, 7u, L(1u, 6u), EXACT, true, false},
    {"RTS", NULL, 5u, F(5u), EXACT, true, false},
    {"SBCD", "Dy,Dx", 1u, F(3u), EXACT, false, false},
    {"SBCD", "-(Ay),-(Ax)", 3u, L(1u, 3u), EXACT, false, false},
    {"SUBX", "Dy,Dx", 1u, F(1u), EXACT, false, false},
    {"SUBX", "-(Ay),-(Ax)", 3u, L(1u, 2u), EXACT, false, false},
    {"SWAP", NULL, 1u, F(2u), EXACT, false, false},
    {"TRAP#", NULL, 16u, F(16u), MIN, true, false},
    /* note f: minimum only when the exception is taken. */
    {"TRAPcc", "Taken", 19u, F(19u), MIN, true, false},
    {"TRAPcc", "Not Taken", 5u, F(5u), EXACT, true, false},
    {"TRAPV", "Taken", 19u, F(19u), MIN, true, false},
    {"TRAPV", "Not Taken", 5u, F(5u), EXACT, true, false},
    {"UNLK", NULL, 2u, L(1u, 1u), EXACT, false, false},
    {"UNPK", "Dx,Dy,#", 1u, F(4u), EXACT, false, false},
    {"UNPK", "-(Ay),-(Ax),#", 3u, L(2u, 4u), EXACT, false, false},
};

const ap_m68040_misc_timing_t *ap_m68040_misc_timings(void) { return table; }

size_t ap_m68040_misc_timing_count(void) {
  return sizeof table / sizeof table[0];
}

const ap_m68040_misc_timing_t *
ap_m68040_misc_timing_find(const char *instruction, const char *condition) {
  for (size_t i = 0; i < ap_m68040_misc_timing_count(); i++) {
    if (strcmp(table[i].instruction, instruction) != 0) {
      continue;
    }
    if (condition == NULL) {
      return &table[i];
    }
    if (table[i].condition != NULL &&
        strcmp(table[i].condition, condition) == 0) {
      return &table[i];
    }
  }
  return NULL;
}

/* MC68030 addressing mode categories: Data, Memory, Control and Alterable.
 *
 * `M68000 Family Programmer's Reference Manual 1992` §2.3 and Table 2-4.
 *
 * These decide whether a decoded addressing mode is *legal* for an instruction.
 * `MOVE`'s destination must be data alterable, `LEA`'s source must be control,
 * the MMU instructions take only control alterable modes. Without them a
 * decoder accepts words the processor refuses, and the emulator runs programs
 * the hardware would fault on -- which is the wrong direction to be wrong in,
 * because a real program never contains them and only a broken one benefits.
 *
 * ## Table 2-4 is not transcribed here
 *
 * Its Alterable column does not survive the scan. The extraction reads:
 *
 *   - Absolute Short and Absolute Long: Alterable `—`
 *   - Program Counter Memory Indirect, both forms: Alterable `X`
 *
 * Both are impossible. `MOVE.W D0,$1234` is a legal instruction and `MOVE`'s
 * destination must be data alterable, so absolute addressing *is* alterable;
 * and nothing PC-relative is writable, so the PC memory indirect modes are
 * not. The column is shifted by two rows across the last four entries. The
 * same rows also give Absolute Long a register field of `000`, where every
 * other table in the manual -- `MOVEM`'s, `PMOVE`'s, `PFLUSH`'s -- gives `001`.
 *
 * So the table is *derived* from §2.3's definitions instead, which survive
 * intact:
 *
 *   "Data addressing modes refer to data operands. Memory addressing modes
 *    refer to memory operands. Alterable addressing modes refer to alterable
 *    (writable) operands. Control addressing modes refer to memory operands
 *    without an associated size."
 *
 * Each falls out of those four sentences:
 *
 *   Data       everything except `An` -- an address register holds an address,
 *              not a data operand.
 *   Memory     everything except `Dn` and `An`, which are not memory.
 *   Alterable  everything except the PC-relative modes and the immediate,
 *              which name operands that cannot be written.
 *   Control    the memory modes without an associated size: so not `(An)+` or
 *              `-(An)`, whose step *is* the operand size, and not the
 *              immediate, whose length is the operand size.
 *
 * and the results agree with every surviving cell of Table 2-4.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_CATEGORY_H
#define APOLLO_CPU_M68030_AP_M68030_CATEGORY_H

#include <stdbool.h>

#include "cpu/m68030/ap_m68030_ea.h"

[[nodiscard]] bool ap_m68030_ea_is_data(ap_m68030_ea_kind_t kind);
[[nodiscard]] bool ap_m68030_ea_is_memory(ap_m68030_ea_kind_t kind);
[[nodiscard]] bool ap_m68030_ea_is_control(ap_m68030_ea_kind_t kind);
[[nodiscard]] bool ap_m68030_ea_is_alterable(ap_m68030_ea_kind_t kind);

/* "Two combined classifications are alterable memory (addressing modes that are
 * both alterable and memory addresses) and data alterable (addressing modes
 * that are both alterable and data)." Control alterable is the third such
 * combination, and is what every MMU instruction takes -- `[030]` §9.6, "Only
 * Control-Alterable Addressing Modes Supported for MMU Instructions". */
[[nodiscard]] bool ap_m68030_ea_is_data_alterable(ap_m68030_ea_kind_t kind);
[[nodiscard]] bool ap_m68030_ea_is_memory_alterable(ap_m68030_ea_kind_t kind);
[[nodiscard]] bool ap_m68030_ea_is_control_alterable(ap_m68030_ea_kind_t kind);

#endif /* APOLLO_CPU_M68030_AP_M68030_CATEGORY_H */

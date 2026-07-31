/* MC68030 family 0101: ADDQ, SUBQ, Scc, DBcc and TRAPcc.
 *
 * `M68000 Family Programmer's Reference Manual 1992` §8.2 and the instruction
 * pages for each. Five instructions in one encoding space, separated by fields
 * that overlap rather than nest.
 *
 * ## How the five are told apart
 *
 * Bits 7-6 are the ADDQ/SUBQ **size** field, whose `11` encoding is not a legal
 * size. That spare encoding is what selects the conditional group, and bit 8 —
 * the ADDQ/SUBQ direction bit — becomes part of the condition there.
 *
 * Within the conditional group the effective address *mode* field separates the
 * remaining three, and it does so by reusing encodings `Scc` cannot legally
 * take. `Scc` writes a byte, so an address register destination (mode `001`) is
 * meaningless — and that is exactly the encoding `DBcc` occupies. Likewise mode
 * `111` with the low bits as an opmode is `TRAPcc`. Neither is a special case
 * bolted on: they are holes in `Scc`'s own address space.
 *
 * ## The quick data field's zero means eight
 *
 * "Three bits of immediate data; 1 - 7 represent immediate values of 1 - 7, and
 * zero represents eight." So `ADDQ #8` exists and `ADDQ #0` does not, and a
 * decoder that passes the field through unchanged silently turns every add-8
 * into an add-0 — an instruction that runs, touches the condition codes, and
 * does nothing.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_QUICK_H
#define APOLLO_CPU_M68030_AP_M68030_QUICK_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_cond.h"
#include "cpu/m68030/ap_m68030_ea.h"

typedef enum {
  AP_M68030_QUICK_ADDQ,
  AP_M68030_QUICK_SUBQ,
  AP_M68030_QUICK_SCC,
  AP_M68030_QUICK_DBCC,
  AP_M68030_QUICK_TRAPCC,
  AP_M68030_QUICK_INVALID,
} ap_m68030_quick_kind_t;

/* TRAPcc's operand form. "010 - Instruction is followed by one operand word.
 * 011 - Instruction is followed by two operand words. 100 - Instruction has no
 * following operand words." */
typedef enum {
  AP_M68030_TRAPCC_WORD = 0x2,
  AP_M68030_TRAPCC_LONG = 0x3,
  AP_M68030_TRAPCC_NONE = 0x4,
} ap_m68030_trapcc_form_t;

typedef struct {
  ap_m68030_quick_kind_t kind;

  /* ADDQ and SUBQ. */
  unsigned data;          /* 1-8, with the encoded zero already turned into 8 */
  unsigned size;          /* operand size in bytes: 1, 2 or 4 */
  ap_m68030_ea_t ea;      /* the destination */

  /* Scc, DBcc and TRAPcc. */
  ap_m68030_cond_t condition;
  unsigned reg;                  /* DBcc's counter register */
  ap_m68030_trapcc_form_t form;  /* TRAPcc only */
} ap_m68030_quick_t;

[[nodiscard]] ap_m68030_quick_t ap_m68030_quick_decode(uint16_t instruction);

/* Total instruction length in bytes, before any effective address extension
 * words: DBcc always carries a 16-bit displacement, and TRAPcc carries none,
 * one or two operand words. */
[[nodiscard]] unsigned ap_m68030_quick_length(const ap_m68030_quick_t *quick);

/* DBcc's loop rule, which is not simply "branch while the condition is false".
 * "If Condition False Then (Dn - 1 -> Dn; If Dn != -1 Then PC + dn -> PC)": the
 * counter is decremented *first* and the branch is taken only if it did not
 * pass -1, so a count of zero runs the body once more and terminates at -1
 * rather than looping 65536 times. Returns true when the branch is taken. */
[[nodiscard]] bool ap_m68030_dbcc_taken(bool condition_true,
                                        uint16_t counter_after_decrement);

#endif /* APOLLO_CPU_M68030_AP_M68030_QUICK_H */

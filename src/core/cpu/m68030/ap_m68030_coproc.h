/* MC68030 family 1111: the coprocessor interface, and the MMU instructions.
 *
 * `M68000 Family Programmer's Reference Manual 1992` §8.2 for the encoding, and
 * `[030]` §9.7.6 p. 9-64 for what the 68030 does with it. The last family of
 * the operation code map.
 *
 * ## cpID 0 is the 68030's own MMU
 *
 * "The MMU instructions use the same opcodes and coprocessor identification
 * (CpID) as the corresponding instructions of the MC68851." So `PMOVE`,
 * `PTEST` and `PFLUSH` are F-line instructions with coprocessor ID zero, and on
 * this machine that matters twice over: it is how the MMU registers this
 * project already models are reached, and the 68882 sits at a *different* ID
 * alongside it.
 *
 * ## The same word takes different vectors depending on privilege
 *
 * This is the unusual part, and it is stated plainly:
 *
 *   "All F-line instructions with CpID = 0 ... that the MC68030 does not
 *   support automatically cause F-line unimplemented instruction exceptions
 *   when their execution is attempted in the supervisor mode. If execution of
 *   an unimplemented F-line instruction with CpID = 0 is attempted in the user
 *   mode, the MC68030 takes a privilege violation exception."
 *
 * So one instruction word yields vector 11 from supervisor state and vector 8
 * from user state. Almost everywhere else in this architecture the exception a
 * word takes is a property of the word; here it is a property of the word *and*
 * the mode. Reporting F-line in both cases would let a user program distinguish
 * "unimplemented" from "not allowed", which is precisely what the privilege
 * violation exists to prevent.
 *
 * "F-line instructions with a CpID other than zero are executed as coprocessor
 * instructions by the MC68030" -- no privilege rule attaches to those.
 *
 * ## The interface registers are modelled, and nothing reaches them
 *
 * **Stated 2026-09-09, and the first draft of this note was wrong**, which is
 * worth keeping because the mistake is the standard one: it said the interface
 * "is not modelled", from grepping *this* directory and finding
 * `AP_M68030_FC_CPU_SPACE` only in `BKPT`'s acknowledge. The CIRs are modelled
 * -- in `cpu/m68882/ap_m68882_cir.h` and `cpu/m68851/ap_m68851_cir.h`, complete
 * with Table 7-2's don't-care select bits, the all-ones read of a write-only
 * register, and the two CIRs the part does not implement. Grepping a
 * subdirectory is not grepping `src/`.
 *
 * **What is true is narrower and worse.** Neither CIR module has a caller
 * outside its own test suite -- `check_what_is_called_by_nobody`'s exact
 * signature, and CLAUDE.md's first audit check. And nothing *could* call them:
 * `ap_machine.c`'s bus callbacks take a `function_code` and discard it,
 * `(void)function_code;` twice, so the machine cannot tell a CPU-space cycle
 * from a data cycle. A `MOVES` with `SFC`/`DFC` = 7 -- which `[030]` §7.4.3
 * makes the only way to reach a CIR outside the protocol -- lands on ordinary
 * memory.
 *
 * So `ap_m68030_step.c` dispatches family 1111 *functionally*: MMU instructions
 * to `execute_mmu`, everything else to the `ap_m68882` the CPU points at. Every
 * host that **executes** coprocessor instructions is served correctly, and only
 * one that drives the interface directly would notice. Nothing held does.
 *
 * `[020]` §8 specifies what the modules already hold and the wiring does not:
 * Table 8-1's address encoding (`A19:A16` = `0010` selects a coprocessor
 * operation, `A15:A13` the 3-bit `Cp-Id`, `A12:A5` zero, `A4:A0` the register),
 * Figure 8-4's register map, Figure 8-16's response register (`CA` 15, `PC` 14,
 * function 13-8, parameter 7-0), and Table 8-2's five primitive groups. §8.5's
 * defaults name the two IDs this core uses: **`000` the MC68851 PMMU**, which
 * "decodes CPU space $2, Cp-Id 0 on chip and thus **must** be coprocessor 0",
 * and **`001` the MC68881 floating point coprocessor** -- which is
 * `AP_M68882_DEFAULT_CPID`.
 *
 * *Cost to close*: carry the function code through `ap_machine`'s bus callbacks
 * instead of discarding it, decode CPU space in the board, and route type
 * `0010` with its `Cp-Id` to the matching CIR module. A named plan item carries
 * it.
  */

#ifndef APOLLO_CPU_M68030_AP_M68030_COPROC_H
#define APOLLO_CPU_M68030_AP_M68030_COPROC_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_ea.h"

/* Bits 8-6, the coprocessor operation type. */
typedef enum {
  AP_M68030_CP_GENERAL = 0x0,   /* cpGEN: the arithmetic and move forms */
  AP_M68030_CP_CONDITIONAL = 0x1, /* cpScc, cpDBcc, cpTRAPcc */
  AP_M68030_CP_BRANCH_WORD = 0x2, /* cpBcc with a word displacement */
  AP_M68030_CP_BRANCH_LONG = 0x3, /* cpBcc with a long displacement */
  AP_M68030_CP_SAVE = 0x4,
  AP_M68030_CP_RESTORE = 0x5,
  AP_M68030_CP_RESERVED = 0x6,
} ap_m68030_coproc_type_t;

/* The MMU is coprocessor zero on this part. */
#define AP_M68030_CPID_MMU 0u

typedef struct {
  bool valid;                   /* the word is in family 1111 at all */
  unsigned cpid;                /* bits 11-9 */
  ap_m68030_coproc_type_t type; /* bits 8-6 */
  bool is_mmu;                  /* cpID 0: the 68030's own MMU instructions */
  /* The MMU instructions put a six-bit effective address in the low bits of the
   * instruction word, where a general coprocessor instruction has one too. Only
   * "control alterable addressing modes" are legal there on this part, which is
   * a restriction the executor applies -- this reports what the field says. */
  ap_m68030_ea_t ea;
} ap_m68030_coproc_t;

[[nodiscard]] ap_m68030_coproc_t ap_m68030_coproc_decode(uint16_t instruction);

/* The vector an *unsupported* F-line instruction takes, which for coprocessor
 * zero depends on the privilege state it was attempted from. Returns the
 * exception vector number. */
[[nodiscard]] unsigned ap_m68030_coproc_unsupported_vector(
    const ap_m68030_coproc_t *coproc, bool supervisor);

/* The same question for a part whose MMU is not reached this way at all.
 *
 * `[040]` §3.7.3: "All MMU opcodes for the MC68030 and MC68851 cause F-line
 * unimplemented instruction exceptions if executed in **either supervisor or
 * user mode** by the M68040." So on that part the privilege distinction above
 * does not exist -- the 68040 reaches its MMU through `MOVEC` and this opcode
 * family is unimplemented on it, in both modes.
 *
 * That makes it the one divergence in the model table's list that a **user
 * program** can observe: on a 68030 row a user-mode `PMOVE` takes vector 8, on
 * a 68040 row it takes vector 11, and nothing privileged is needed to tell
 * them apart. */
[[nodiscard]] unsigned ap_m68030_coproc_unsupported_vector_for_part(
    const ap_m68030_coproc_t *coproc, bool supervisor,
    bool mmu_reached_by_movec);

#endif /* APOLLO_CPU_M68030_AP_M68030_COPROC_H */

/* MC68851 instruction decode.
 *
 * `MC68851 PMMU User's Manual, Third Edition` Appendix A, read from the page
 * images -- the extracted text renders these bit rows unusably, turning zeros
 * into letters and collapsing columns.
 *
 * ## The MMU is coprocessor zero
 *
 * Every instruction here has the operation word `1111 000 000` followed by an
 * effective address, so the 68020's F-line decoder routes it by cpID exactly as
 * it routes cpID 1 to the 68882. That is what lets a DN3000 carry both parts on
 * one coprocessor interface.
 *
 * ## The command word's top three bits are an opclass, and `001` holds four
 * different instructions
 *
 * This is the trap in the encoding, and it is only visible once every
 * instruction's page has been read:
 *
 *     001 | 000 | R/W | 0000  | FC     PLOAD
 *     001 | 001 | 0   | mask  | FC     PFLUSHA  (flush all)
 *     001 | 010 | 0000000000           PVALID   (against VAL)
 *     001 | 011 | 0000000  | Reg       PVALID   (against An)
 *     001 | 100 | 0   | mask  | FC     PFLUSH   by function code
 *     001 | 101 | 0   | mask  | FC     PFLUSHS  ... including shared
 *     001 | 110 | 0   | mask  | FC     PFLUSH   by function code and address
 *     001 | 111 | 0   | mask  | FC     PFLUSHS  ... including shared
 *
 * All eight values of the field are used, by three different instructions. An
 * earlier version of this module read only `PFLUSH`'s page, found five of the
 * eight listed there, and concluded the other three were undefined -- they are
 * `PLOAD` and the two `PVALID` forms. The lesson is in `CLAUDE.md` already:
 * one instruction's page is not the encoding.
 *
 * The other opclasses are each one instruction:
 *
 *     010 | PReg | R/W | 000000000     PMOVE  to/from TC, CRP, DRP, SRP,
 *                                             CAL, VAL, SCC, AC
 *     011 | PReg | R/W | 0000 | Num|00 PMOVE  to/from BADx, BACx
 *     011 | PReg | R/W | 000000000     PMOVE  to/from PSR, from PCSR
 *     100 | Level| R/W | AReg  | FC    PTEST
 *
 * `011` covers two `PMOVE` formats told apart by `PReg`: `000`/`001` are the
 * status registers and `100`/`101` the breakpoint registers.
 *
 * ## A function code is specified four ways, and three name something outside
 * the MMU
 *
 * The five-bit `FC` field, shared by `PLOAD`, `PFLUSH` and `PTEST`:
 *
 *     1DDDD   the function code is the four bits DDDD, immediate
 *     01RRR   it is in CPU data register RRR
 *     00000   it is in the CPU's SFC register
 *     00001   it is in the CPU's DFC register
 *
 * A prefix code, not a small integer: `00000` is the SFC form and `01000` is
 * data register 0, so a decoder that tested the top bit and then the next would
 * get SFC and DFC right by accident and register `R0` wrong. The manual also
 * notes what the CPU's register width costs -- "since the SFC of the MC68020
 * has only three implemented bits, only function codes $0 through $7 can be
 * specified in this manner" -- so only the immediate form can name a DMA
 * function code, and only it can flush DMA entries.
 */

#ifndef APOLLO_CPU_M68851_AP_M68851_DECODE_H
#define APOLLO_CPU_M68851_AP_M68851_DECODE_H

#include <stdbool.h>
#include <stdint.h>

/* The MMU's coprocessor ID: `1111 000` in the operation word. */
#define AP_M68851_CPID 0u

/* ---------------------------------------------------------------------------
 * The operation word's type field, bits 8-6.
 *
 * The same encoding the 68882 uses on the same interface -- which is what
 * "instruction extensions to M68000 Family processors using the M68000 Family
 * coprocessor interface" means concretely, and why one F-line decoder can serve
 * both parts by cpID alone.
 * ------------------------------------------------------------------------- */

typedef enum {
  AP_M68851_TYPE_GENERAL = 0,   /* PLOAD, PFLUSH, PVALID, PMOVE, PTEST */
  AP_M68851_TYPE_CONDITIONAL,   /* PDBcc, PScc, PTRAPcc */
  AP_M68851_TYPE_BRANCH_WORD,   /* PBcc with a 16-bit displacement */
  AP_M68851_TYPE_BRANCH_LONG,   /* PBcc with a 32-bit displacement */
  AP_M68851_TYPE_SAVE,          /* PSAVE */
  AP_M68851_TYPE_RESTORE,       /* PRESTORE */
  AP_M68851_TYPE_RESERVED_6,
  AP_M68851_TYPE_RESERVED_7,
} ap_m68851_type_t;

typedef struct {
  bool is_coprocessor;  /* bits 15-12 are 1111 */
  unsigned cpid;        /* bits 11-9 */
  ap_m68851_type_t type; /* bits 8-6 */
  unsigned effective_address; /* bits 5-0, meaning depends on the type */
} ap_m68851_operation_word_t;

[[nodiscard]] ap_m68851_operation_word_t
ap_m68851_decode_operation(uint16_t word);

/* ---------------------------------------------------------------------------
 * Conditions, `PBcc`'s and `PScc`'s tables.
 *
 * Sixteen conditions in a contiguous run from `000000`, eight PSR bits each
 * tested set and clear:
 *
 *     BS 000000  BC 000001    WS 001000  WC 001001
 *     LS 000010  LC 000011    IS 001010  IC 001011
 *     SS 000100  SC 000101    GS 001100  GC 001101
 *     AS 000110  AC 000111    CS 001110  CC 001111
 *
 * So the encoding is `2k + (clear ? 1 : 0)` over B, L, S, A, W, I, G, C -- and
 * the interesting part is what is *missing*. `PSR` has nine defined bits and
 * only eight are testable: **`M`, the modified bit, has no condition.** It is
 * the one `PSR` bit that reports a property of a page rather than the outcome
 * of a test, and a program wanting it uses `PTEST` and reads the register.
 * ------------------------------------------------------------------------- */

/* The eight testable bits, in condition order -- which is also their order in
 * `PSR` with `M` skipped. */
typedef enum {
  AP_M68851_COND_BUS_ERROR = 0,
  AP_M68851_COND_LIMIT_VIOLATION,
  AP_M68851_COND_SUPERVISOR_ONLY,
  AP_M68851_COND_ACCESS_LEVEL_VIOLATION,
  AP_M68851_COND_WRITE_PROTECTED,
  AP_M68851_COND_INVALID,
  AP_M68851_COND_GATE,
  AP_M68851_COND_GLOBALLY_SHARABLE,
  AP_M68851_COND_UNDEFINED,
} ap_m68851_condition_bit_t;

typedef struct {
  ap_m68851_condition_bit_t bit;
  /* False tests the bit set, true tests it clear -- the low bit of the
   * encoding, which is why every condition comes in a pair. */
  bool test_clear;
} ap_m68851_condition_t;

[[nodiscard]] ap_m68851_condition_t ap_m68851_decode_condition(unsigned field);

/* Evaluate a condition against a `PSR` value. Takes the register rather than a
 * decoded struct so this module stays independent of the register file. */
[[nodiscard]] bool ap_m68851_condition_true(ap_m68851_condition_t condition,
                                            uint16_t psr);

/* ---------------------------------------------------------------------------
 * PSAVE state frames, §6.2.7.3 by way of the PSAVE page.
 * ------------------------------------------------------------------------- */

/* "This state frame is 36 ($24) bytes long ... indicates that the MC68851 was
 * in an idle state with no coprocessor operations in progress, and no
 * breakpoints enabled." */
#define AP_M68851_FRAME_IDLE_BYTES 36u
/* "44 ($2C) bytes ... a coprocessor or module call operation in progress, and
 * no breakpoints enabled." */
#define AP_M68851_FRAME_MID_COPROCESSOR_BYTES 44u
/* "76 ($4C) bytes ... one or more breakpoints were enabled." Note that this
 * one says nothing about an operation in progress: "a coprocessor or module
 * call operation may or may not have been in progress", so the breakpoint
 * frame subsumes the other two rather than being a fourth combination. */
#define AP_M68851_FRAME_BREAKPOINTS_BYTES 76u

/* ---------------------------------------------------------------------------
 * The function code specification field.
 * ------------------------------------------------------------------------- */

typedef enum {
  AP_M68851_FC_IMMEDIATE,     /* 1DDDD */
  AP_M68851_FC_DATA_REGISTER, /* 01RRR */
  AP_M68851_FC_SFC,           /* 00000 */
  AP_M68851_FC_DFC,           /* 00001 */
  /* `00010` through `00111`: the manual lists four forms and no others. */
  AP_M68851_FC_UNDEFINED,
} ap_m68851_fc_source_t;

typedef struct {
  ap_m68851_fc_source_t source;
  unsigned immediate;     /* for 1DDDD */
  unsigned data_register; /* for 01RRR */
} ap_m68851_fc_spec_t;

[[nodiscard]] ap_m68851_fc_spec_t ap_m68851_decode_fc(unsigned field);

/* Whether this specification can name a function code with `FC3` set -- a DMA
 * master's. Only the immediate form carries all four bits in the instruction. */
[[nodiscard]] bool ap_m68851_fc_reaches_dma(const ap_m68851_fc_spec_t *spec);

/* ---------------------------------------------------------------------------
 * The instruction set.
 * ------------------------------------------------------------------------- */

typedef enum {
  AP_M68851_OP_UNDEFINED,
  AP_M68851_OP_PLOAD,
  AP_M68851_OP_PFLUSH,
  AP_M68851_OP_PVALID,
  AP_M68851_OP_PMOVE,
  AP_M68851_OP_PTEST,
} ap_m68851_opcode_t;

/* How the ATC is to be flushed. The names follow the assembler mnemonics rather
 * than the field values, because `PFLUSHA` and `PFLUSHS` are separate
 * instructions to a programmer and one field to the hardware. */
typedef enum {
  AP_M68851_PFLUSH_ALL = 1,          /* 001: PFLUSHA */
  AP_M68851_PFLUSH_FC = 4,           /* 100 */
  AP_M68851_PFLUSH_FC_SHARED = 5,    /* 101: PFLUSHS */
  AP_M68851_PFLUSH_FC_EA = 6,        /* 110 */
  AP_M68851_PFLUSH_FC_EA_SHARED = 7, /* 111: PFLUSHS */
} ap_m68851_pflush_mode_t;

/* `PMOVE`'s `PReg`, whose meaning depends on the opclass it appears in. */
typedef enum {
  AP_M68851_PREG_TC = 0,
  AP_M68851_PREG_DRP = 1,
  AP_M68851_PREG_SRP = 2,
  AP_M68851_PREG_CRP = 3,
  AP_M68851_PREG_CAL = 4,
  AP_M68851_PREG_VAL = 5,
  AP_M68851_PREG_SCC = 6,
  AP_M68851_PREG_AC = 7,
  /* Opclass `011`'s separate numbering. */
  AP_M68851_PREG_PSR = 8,
  AP_M68851_PREG_PCSR = 9,
  AP_M68851_PREG_BAD = 10,
  AP_M68851_PREG_BAC = 11,
  AP_M68851_PREG_UNDEFINED = 12,
} ap_m68851_preg_t;

typedef struct {
  ap_m68851_opcode_t opcode;

  /* "0 -- Transfer <ea> to MC68851 register; 1 -- Transfer MC68851 register to
   * <ea>". For `PLOAD` and `PTEST` the same bit distinguishes the R and W
   * forms, which differ in whether `U` alone or `U` and `M` are updated. */
  bool read_from_mmu;

  /* PFLUSH */
  ap_m68851_pflush_mode_t pflush_mode;
  unsigned mask;

  /* PLOAD, PFLUSH, PTEST */
  ap_m68851_fc_spec_t fc;

  /* PTEST */
  unsigned level;
  unsigned address_register;

  /* PMOVE */
  ap_m68851_preg_t preg;
  unsigned breakpoint_number;

  /* PVALID: true for the form that tests against an address register rather
   * than against `VAL`. */
  bool valid_against_register;
  unsigned valid_register;
} ap_m68851_instruction_t;

/* Decode a command word -- the word after the operation word. The operation
 * word carries only the cpID, the type and an effective address, so everything
 * that says *which* instruction this is lives here. */
[[nodiscard]] ap_m68851_instruction_t
ap_m68851_decode_command(uint16_t command);

/* Whether the operation word is an MMU coprocessor instruction: `1111 000 000`
 * followed by an effective address. */
[[nodiscard]] bool ap_m68851_is_mmu_operation_word(uint16_t word);

/* Whether a decoded instruction is well formed. Beyond the opcode being
 * defined, `PFLUSHA` carries two constraints from its field descriptions: "if
 * mode = 001 (flush all entries), mask must be 0000" and "function code must be
 * 00000". A flush-all that names a function code contradicts itself, so the
 * manual forbids the encoding rather than ignoring the fields. */
[[nodiscard]] bool
ap_m68851_instruction_is_valid(const ap_m68851_instruction_t *instruction);

/* Whether this flush mode reaches globally shared entries. "ATC entries whose
 * SG bit is set will not be invalidated unless the PFLUSHS is specified." */
[[nodiscard]] bool
ap_m68851_pflush_includes_shared(ap_m68851_pflush_mode_t mode);

/* Whether this flush mode also matches on the effective address. */
[[nodiscard]] bool ap_m68851_pflush_uses_address(ap_m68851_pflush_mode_t mode);

/* The flush's function code test: "(ATC function code bits and <mask>) =
 * (<fc> and <mask>)". Masked equality, so a zero mask matches every function
 * code and an all-ones mask exactly one. */
[[nodiscard]] bool ap_m68851_pflush_matches_fc(unsigned mask,
                                               unsigned instruction_fc,
                                               unsigned entry_fc);

/* Which registers a `PMOVE` may not reach through a register-direct mode.
 * Appendix A footnotes the addressing table: "PMOVE from CRP, SRP, DRP not
 * allowed with these modes" -- they are 64 bits and will not fit in one CPU
 * register. */
[[nodiscard]] bool ap_m68851_preg_is_64_bit(ap_m68851_preg_t preg);

#endif /* APOLLO_CPU_M68851_AP_M68851_DECODE_H */

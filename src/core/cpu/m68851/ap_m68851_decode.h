/* MC68851 instruction decode: the function code specification field, shared by
 * `PFLUSH`, `PLOAD`, `PTEST` and `PVALID`, and the `PFLUSH` family.
 *
 * `MC68851 PMMU User's Manual, Third Edition` Appendix A, read from the page
 * images -- the extracted text renders these bit rows unusably, turning zeros
 * into letters and collapsing columns.
 *
 * ## The MMU is coprocessor zero
 *
 * Every instruction here begins `1111 000`, so the F-line decoder in the 68020
 * sends it here by cpID exactly as it sends cpID 1 to the 68882. That is what
 * makes a DN3000 able to have both parts on one coprocessor interface.
 *
 * ## A function code is specified four ways, and two of them are the CPU's
 *
 * The five-bit `FC` field:
 *
 *     1DDDD   the function code is the four bits DDDD, immediate
 *     01RRR   it is in CPU data register RRR
 *     00000   it is in the CPU's SFC register
 *     00001   it is in the CPU's DFC register
 *
 * So three of the four encodings name something *outside* the MMU, which is
 * why this is a specification rather than a value: resolving it needs the CPU's
 * registers, and the MMU asks for them over the coprocessor interface. The
 * manual also notes the consequence of the CPU's register width -- "since the
 * SFC of the MC68020 has only three implemented bits, only function codes $0
 * through $7 can be specified in this manner" -- so the register forms cannot
 * reach the DMA function codes that have `FC3` set.
 *
 * The encoding is a prefix code and not a small integer: `00000` and `00001`
 * are two distinct meanings inside what the `01RRR` form would otherwise cover
 * if it were read one bit wider. A decoder that tested the top bit and then the
 * next would get SFC and DFC right by accident and register `R0` wrong.
 */

#ifndef APOLLO_CPU_M68851_AP_M68851_DECODE_H
#define APOLLO_CPU_M68851_AP_M68851_DECODE_H

#include <stdbool.h>
#include <stdint.h>

/* The MMU's coprocessor ID: `1111 000` in the operation word. */
#define AP_M68851_CPID 0u

/* Where a function code comes from. */
typedef enum {
  AP_M68851_FC_IMMEDIATE,   /* 1DDDD */
  AP_M68851_FC_DATA_REGISTER, /* 01RRR */
  AP_M68851_FC_SFC,         /* 00000 */
  AP_M68851_FC_DFC,         /* 00001 */
  /* `0001x` and `001xx` are named by no encoding above. The manual lists four
   * forms and no others, so these are undefined rather than aliases. */
  AP_M68851_FC_UNDEFINED,
} ap_m68851_fc_source_t;

typedef struct {
  ap_m68851_fc_source_t source;
  /* The immediate function code, for `1DDDD`. */
  unsigned immediate;
  /* The data register number, for `01RRR`. */
  unsigned data_register;
} ap_m68851_fc_spec_t;

[[nodiscard]] ap_m68851_fc_spec_t ap_m68851_decode_fc(unsigned field);

/* Whether this specification can name a function code with `FC3` set -- that
 * is, a DMA master's. Only the immediate form can: the CPU's SFC and DFC hold
 * three bits, and a data register form is described as "the lower four bits"
 * but is still routed through the CPU. Recorded because it is the difference
 * between an instruction that can flush DMA entries and one that cannot. */
[[nodiscard]] bool ap_m68851_fc_reaches_dma(const ap_m68851_fc_spec_t *spec);

/* ---------------------------------------------------------------------------
 * PFLUSH, PFLUSHA, PFLUSHS.
 *
 * Operation word: `1111 000 000` then an effective address.
 * Command word:   `001` | Mode(3) | 0 | Mask(4) | FC(5).
 * ------------------------------------------------------------------------- */

/* The mode field's five defined encodings. Note that `000`, `010` and `011`
 * are absent: the mode is not a bit-per-option field, so a value the manual
 * does not list is undefined rather than a combination. */
typedef enum {
  AP_M68851_PFLUSH_ALL = 1,             /* 001: flush all entries */
  AP_M68851_PFLUSH_FC = 4,              /* 100: by function code only */
  AP_M68851_PFLUSH_FC_SHARED = 5,       /* 101: ... including shared entries */
  AP_M68851_PFLUSH_FC_EA = 6,           /* 110: by function code and address */
  AP_M68851_PFLUSH_FC_EA_SHARED = 7,    /* 111: ... including shared entries */
  AP_M68851_PFLUSH_UNDEFINED = 8,
} ap_m68851_pflush_mode_t;

typedef struct {
  ap_m68851_pflush_mode_t mode;
  /* "Indicates which bits are significant in the function code compare. A zero
   * indicates that the bit position is not significant." So a flush names a
   * *set* of function codes, not one. */
  unsigned mask;
  ap_m68851_fc_spec_t fc;
} ap_m68851_pflush_t;

[[nodiscard]] ap_m68851_pflush_t ap_m68851_decode_pflush(uint16_t command);

/* Whether the command word is a well-formed `PFLUSH`. Two constraints beyond
 * the mode being defined, both from the field descriptions: "if mode = 001
 * (flush all entries), mask must be 0000" and "if mode = 001 (flush all
 * entries), function code must be 00000". A flush-all that names a function
 * code is contradictory, so the manual forbids the encoding rather than
 * ignoring the fields. */
[[nodiscard]] bool ap_m68851_pflush_is_valid(const ap_m68851_pflush_t *pflush);

/* Whether this mode flushes globally shared entries. "ATC entries whose SG bit
 * is set will not be invalidated unless the PFLUSHS is specified" -- so a
 * shared entry survives an ordinary flush, which is the point of sharing it. */
[[nodiscard]] bool
ap_m68851_pflush_includes_shared(ap_m68851_pflush_mode_t mode);

/* Whether this mode also matches on the effective address. */
[[nodiscard]] bool ap_m68851_pflush_uses_address(ap_m68851_pflush_mode_t mode);

/* The flush's function code test: "(ATC function code bits and <mask>) =
 * (<fc> and <mask>)". Masked equality, so a mask of zero matches every
 * function code and a mask of all ones matches exactly one. */
[[nodiscard]] bool ap_m68851_pflush_matches_fc(unsigned mask,
                                               unsigned instruction_fc,
                                               unsigned entry_fc);

#endif /* APOLLO_CPU_M68851_AP_M68851_DECODE_H */

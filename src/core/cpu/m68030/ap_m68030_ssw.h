/* The MC68030 special status word, and the layout of the two bus fault frames.
 *
 * `[030]` §8.2.1: "The internal SSW (see Figure 8-9) is one of several
 * registers saved as part of the bus fault exception stack frame. Both the
 * short bus cycle fault format and the long bus cycle fault format include this
 * word at offset $A."
 *
 * This is the register a bus error handler reads to find out *what* faulted --
 * the instruction stream, the data stream, or both -- and it is what makes a
 * fault repairable rather than merely fatal. Domain/OS's demand paging rests on
 * it, so it is worth building exactly rather than approximately.
 *
 * ## Which frame a fault gets is decided here, not by the caller
 *
 * §8.2.2, on repairing a data fault: "Data read faults only generate the long
 * bus fault frame and the handler must transfer properly sized data from the
 * location indicated by the fault address and address space to the image of the
 * data input buffer (DIB) at location SP + $2C of the long format stack frame."
 *
 * The reason is structural rather than arbitrary: the short frame has no data
 * input buffer, so there is nowhere for a handler to put the value a faulted
 * read was supposed to return. A read fault given the short frame is not a
 * smaller frame, it is an unrepairable one. That is why the frame choice is a
 * function of the SSW and not a parameter -- a caller cannot get it wrong by
 * asking for the wrong size.
 *
 * ## The rerun bits are not independent of the fault bits
 *
 * "A rerun bit is always set when the corresponding fault bit is set." An
 * encoder that let a caller set FB without RB would produce a word the real part
 * never emits, and a handler reading it would conclude that stage B was invalid
 * but needed no prefetch -- leaving a stale word in the pipe on RTE. The
 * encoder enforces it rather than trusting every call site to remember, for the
 * same reason the tape controller's exception and ready bits are set together:
 * an invariant that can be broken by omitting a line is not an invariant.
 *
 * The converse does not hold. "If an address error exception occurs, the fault
 * bits written to the stack frame are not set (they are only set due to a bus
 * error, as previously described), and the rerun bits alone show the cause of
 * the exception" -- so a rerun without a fault is exactly how an address error
 * is distinguished from a bus error, and must stay expressible.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_SSW_H
#define APOLLO_CPU_M68030_AP_M68030_SSW_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_exception.h"

/* Figure 8-9, from the top: FC, FB, RC, RB, three bits "for internal use only",
 * DF, RM, RW, a two-bit SIZE, one more internal bit, and FC2-FC0. */
#define AP_M68030_SSW_FC_BIT 15u /* fault on stage C of the instruction pipe */
#define AP_M68030_SSW_FB_BIT 14u /* fault on stage B */
#define AP_M68030_SSW_RC_BIT 13u /* rerun stage C */
#define AP_M68030_SSW_RB_BIT 12u /* rerun stage B */
#define AP_M68030_SSW_DF_BIT 8u  /* fault/rerun flag for the data cycle */
#define AP_M68030_SSW_RM_BIT 7u  /* read-modify-write on the data cycle */
#define AP_M68030_SSW_RW_BIT 6u  /* 1 = read, 0 = write */
#define AP_M68030_SSW_SIZE_SHIFT 4u
#define AP_M68030_SSW_SIZE_MASK 0x3u
#define AP_M68030_SSW_FUNCTION_CODE_MASK 0x7u

/* Table 7-3's SIZ1/SIZ0 encoding, which the SSW's SIZE field carries: the count
 * of bytes *remaining*, which is why a long word is zero and not four. */
typedef enum {
  AP_M68030_SSW_SIZE_LONG = 0u,
  AP_M68030_SSW_SIZE_BYTE = 1u,
  AP_M68030_SSW_SIZE_WORD = 2u,
  AP_M68030_SSW_SIZE_THREE_BYTE = 3u,
} ap_m68030_ssw_size_t;

typedef struct {
  /* The instruction stream half. "The fault bits (FB and FC) indicate that the
   * processor attempted to use a stage (B or C) and found it to be marked
   * invalid due to a bus error on the prefetch for that stage." */
  bool stage_c_fault;
  bool stage_b_fault;
  bool stage_c_rerun;
  bool stage_b_rerun;

  /* The data half, which "applies to data cycles only". */
  bool data_fault;
  bool read_modify_write;
  bool read; /* RW: 1 = read, 0 = write */
  ap_m68030_ssw_size_t size;
  uint8_t function_code; /* FC2-FC0: "address space for data cycle" */
} ap_m68030_ssw_t;

/* Build the word. Applies the rerun-implies-fault rule, so a caller that sets a
 * fault bit alone still produces a word the part could have emitted. */
[[nodiscard]] uint16_t ap_m68030_ssw_encode(const ap_m68030_ssw_t *ssw);

/* Read one back, as a handler does. The three internal-use bits and the fourth
 * at position 3 are dropped: they are "for internal use only", so reporting
 * them would invite a caller to depend on a value this model has no source
 * for. */
[[nodiscard]] ap_m68030_ssw_t ap_m68030_ssw_decode(uint16_t word);

/* The SIZE field for an operand of `bytes` bytes, and its inverse. */
[[nodiscard]] ap_m68030_ssw_size_t ap_m68030_ssw_size_for(unsigned bytes);
[[nodiscard]] unsigned ap_m68030_ssw_size_bytes(ap_m68030_ssw_size_t size);

/* Which of the two bus fault frames this fault requires.
 *
 * Long for a faulted data *read*, because only the long frame has the data
 * input buffer a handler must write the value into; short otherwise. See the
 * header comment -- this is a structural consequence, not a size preference. */
[[nodiscard]] ap_m68030_frame_format_t
ap_m68030_bus_fault_frame(const ap_m68030_ssw_t *ssw);

/* Field offsets within a bus fault frame, from Table 8-6. The first four --
 * status register, program counter, format word -- are common to every frame
 * and live with the frame code; these are the ones the fault frames add.
 *
 * Everything Table 8-6 labels INTERNAL REGISTER is deliberately absent. Those
 * are the processor's own microsequencer state, this model has no source for
 * them, and naming an offset for a field we would fill with a guess is how a
 * guess becomes load-bearing. */
#define AP_M68030_BUS_FAULT_SSW 0x0Au
#define AP_M68030_BUS_FAULT_STAGE_C 0x0Cu
#define AP_M68030_BUS_FAULT_STAGE_B 0x0Eu
#define AP_M68030_BUS_FAULT_ADDRESS 0x10u /* data cycle fault address, long */
#define AP_M68030_BUS_FAULT_DATA_OUTPUT 0x18u /* long */

/* Long frame only. */
#define AP_M68030_BUS_FAULT_STAGE_B_ADDRESS 0x24u /* long */
#define AP_M68030_BUS_FAULT_DATA_INPUT 0x2Cu      /* long */
#define AP_M68030_BUS_FAULT_VERSION 0x36u

#endif /* APOLLO_CPU_M68030_AP_M68030_SSW_H */

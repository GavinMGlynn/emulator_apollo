/* MC68020 module call and return: `CALLM` and `RTM`.
 *
 * `MC68020 32-Bit Microprocessor User's Manual` Appendix D, Figures D-1, D-2
 * and D-3; `M68000 Family Programmer's Reference Manual` pages 4-64 and 4-167.
 *
 * ## Two instructions that exist on exactly one part
 *
 * The PRM heads both entries "(MC68020)". They are not in the 68030, the 68040
 * or the 68000, so on a DN3500 their encodings are illegal instructions and on
 * a DN3000 they are not -- which makes this the sharpest CPU-family difference
 * in the core, because the same word must fault on one machine and execute on
 * another.
 *
 * ## The processor does not interpret access control
 *
 * "Although the MC68020 does not interpret the access control information, it
 * does communicate with external hardware when the access control is to be
 * changed, and relies on the external hardware to verify that the changes are
 * legal."
 *
 * So a type `$01` descriptor is not something the CPU can resolve alone: it
 * runs access-level bus cycles and believes the answer. A type `$00` descriptor
 * changes no rights and needs no such hardware, which is why the two types are
 * separated here rather than treated as a field.
 *
 * ## Validation is the interesting half
 *
 * Three separate statements make most encodings a format exception:
 *
 *   - "the MC68020 recognizes only the options of 000 and 100, all others cause
 *     a format exception";
 *   - "the MC68020 only recognizes descriptors of type $00 and $01, all others
 *     cause a format exception";
 *   - "all module descriptor types $10-$1F are reserved for user definition and
 *     cause a format error exception. This provides the user with a means of
 *     disabling any module by setting a single bit in its descriptor, without
 *     loss of any descriptor information."
 *
 * That last one is a *design intent*, not merely a reserved range: bit 4 of the
 * type field is a disable bit, and a model that folded `$10-$1F` in with every
 * other unrecognised type would still fault, but would lose the reason.
 */

#ifndef APOLLO_CPU_M68020_AP_M68020_MODULE_H
#define APOLLO_CPU_M68020_AP_M68020_MODULE_H

#include <stdbool.h>
#include <stdint.h>

/* `RTM Rn` is `0000 0110 1100 DRRR`. */
#define AP_M68020_RTM_BASE 0x06C0u
/* `CALLM #<data>,<ea>` is `0000 0110 11` followed by an effective address. RTM
 * occupies the low sixteen words of that space because CALLM's addressing is
 * restricted to control modes: modes 000 (Dn) and 001 (An) are illegal for
 * CALLM and so are free for RTM. The two instructions share an opcode prefix
 * and are told apart by a field CALLM cannot legally use. */
#define AP_M68020_CALLM_BASE 0x06C0u

typedef enum {
  AP_M68020_MODULE_NOT_A_MODULE_INSTRUCTION,
  AP_M68020_MODULE_CALLM,
  AP_M68020_MODULE_RTM,
} ap_m68020_module_opcode_t;

typedef struct {
  ap_m68020_module_opcode_t opcode;
  /* CALLM: the effective address mode and register from the operation word. */
  unsigned mode;
  unsigned reg;
  /* RTM: which register holds the module data area pointer. "D/A field --
   * specifies whether the module data pointer is in a data or an address
   * register." */
  bool rtm_address_register;
  unsigned rtm_register;
} ap_m68020_module_decode_t;

/* Decode an operation word. Returns NOT_A_MODULE_INSTRUCTION for anything
 * outside the shared prefix, and for a CALLM whose addressing mode is not a
 * control mode -- which is an illegal instruction, not a format error, because
 * the processor never gets as far as reading a descriptor. */
[[nodiscard]] ap_m68020_module_decode_t ap_m68020_module_decode(uint16_t word);

/* Figure D-1's first long word. */
typedef struct {
  unsigned opt;          /* bits 31-29 */
  unsigned type;         /* bits 28-24 */
  unsigned access_level; /* bits 23-16 */
} ap_m68020_module_control_t;

[[nodiscard]] ap_m68020_module_control_t
ap_m68020_module_control(uint32_t first_long_word);

/* Why a descriptor was refused, or that it was accepted. All three refusals are
 * the same vector -- a format exception -- and are distinguished here because
 * they are distinguished in the manual and because the disable case is a
 * documented technique rather than an error. */
typedef enum {
  AP_M68020_MODULE_OK,
  AP_M68020_MODULE_BAD_OPT,      /* not 000 or 100 */
  AP_M68020_MODULE_BAD_TYPE,     /* not $00 or $01, outside $10-$1F */
  AP_M68020_MODULE_DISABLED,     /* $10-$1F: "reserved for user definition" */
} ap_m68020_module_status_t;

[[nodiscard]] ap_m68020_module_status_t
ap_m68020_module_validate(const ap_m68020_module_control_t *control);

/* The two recognised option encodings, which decide whether arguments move. */
#define AP_M68020_MODULE_OPT_ON_STACK 0u   /* "000": copied if the SP changes */
#define AP_M68020_MODULE_OPT_INDIRECT 4u   /* "100": not copied */

/* The two recognised descriptor types. */
#define AP_M68020_MODULE_TYPE_NO_ACCESS_CHANGE 0x00u
#define AP_M68020_MODULE_TYPE_ACCESS_CHANGE 0x01u

/* Whether this descriptor calls for access-level bus cycles to external
 * hardware. "The access level field is used only with the type $01 descriptor,
 * and is passed to external hardware to change the access control." A type $00
 * call runs entirely inside the processor. */
[[nodiscard]] bool
ap_m68020_module_changes_access(const ap_m68020_module_control_t *control);

/* Whether CALLM must copy the caller's arguments to a new stack. "The 000
 * option indicates that the called module expects to find arguments from the
 * calling module on the stack just below the module stack frame. In cases where
 * there is a change of stack pointer during the call, the MC68020 will copy the
 * arguments from the old stack to the new stack." Both conditions -- option 000
 * *and* a stack change -- or nothing is copied. */
[[nodiscard]] bool ap_m68020_module_copies_arguments(
    const ap_m68020_module_control_t *control, bool stack_pointer_changes);

/* Figure D-1's offsets from the descriptor base. */
#define AP_M68020_DESCRIPTOR_CONTROL 0x00u
#define AP_M68020_DESCRIPTOR_ENTRY_WORD_POINTER 0x04u
#define AP_M68020_DESCRIPTOR_DATA_AREA_POINTER 0x08u
/* "Module Stack Pointer (Optional)": read only when the type calls for a stack
 * change, which is why it can be absent from a type $00 descriptor. */
#define AP_M68020_DESCRIPTOR_STACK_POINTER 0x0Cu

/* Figure D-3's offsets from the frame base, which is the new SP. */
#define AP_M68020_FRAME_OPT_TYPE 0x00u        /* opt, type, saved access level */
#define AP_M68020_FRAME_CCR 0x02u             /* zeros, then the CCR */
#define AP_M68020_FRAME_ARGUMENT_COUNT 0x04u  /* zeros, then the count */
#define AP_M68020_FRAME_DESCRIPTOR_POINTER 0x08u
#define AP_M68020_FRAME_SAVED_PC 0x0Cu
#define AP_M68020_FRAME_SAVED_DATA_AREA 0x10u
#define AP_M68020_FRAME_SAVED_SP 0x14u
#define AP_M68020_FRAME_BYTES 0x18u           /* arguments follow, optionally */

/* Figure D-2's module entry word: which register receives the data area
 * pointer. "The first word at the entry address specifies the register to be
 * saved in the module stack frame and then loaded with the module descriptor
 * data area pointer; the first instruction of the module starts with the next
 * word." */
typedef struct {
  bool address_register;
  unsigned reg;
} ap_m68020_module_entry_t;

[[nodiscard]] ap_m68020_module_entry_t ap_m68020_module_entry(uint16_t word);

#endif /* APOLLO_CPU_M68020_AP_M68020_MODULE_H */

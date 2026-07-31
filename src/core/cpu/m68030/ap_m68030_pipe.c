/* MC68030 instruction pipe and cache holding register.
 * See ap_m68030_pipe.h for the citations. */

#include "cpu/m68030/ap_m68030_pipe.h"

#include <stddef.h> /* NULL, for the optional out-parameters of _decoded() */

/* Instruction words are 16-bit and the holding register is a long word, so the
 * word an address selects is chosen by address bit 1. Bit 0 is not consulted:
 * an odd instruction address is an address error on this family and is the
 * caller's fault to detect, not something to silently round away here. */
#define HOLDING_MASK UINT32_C(0xFFFFFFFC)

static uint32_t holding_base(uint32_t address) { return address & HOLDING_MASK; }

/* True when `address` selects the high-order word of its long word -- the
 * "even-word (long-word aligned) prefetch" of §11.2.2. */
static bool selects_high_word(uint32_t address) {
  return (address & UINT32_C(2)) == 0;
}

static uint16_t word_from_holding(uint32_t longword, uint32_t address) {
  /* Big-endian order: the high-order word is the one at the lower address. The
   * holding register is a long word as the processor sees it, so this is a
   * property of the 68000 family's byte order and not of the host's. */
  return selects_high_word(address) ? (uint16_t)(longword >> 16)
                                    : (uint16_t)(longword & UINT32_C(0xFFFF));
}

void ap_m68030_pipe_reset(ap_m68030_pipe_t *pipe) {
  *pipe = (ap_m68030_pipe_t){0};
}

bool ap_m68030_pipe_holds(const ap_m68030_pipe_t *pipe, uint32_t address) {
  return pipe->holding_valid && holding_base(address) == pipe->holding_address;
}

void ap_m68030_pipe_fill(ap_m68030_pipe_t *pipe, uint32_t address,
                         uint32_t longword, bool abnormal) {
  pipe->holding_data = longword;
  pipe->holding_address = holding_base(address);
  pipe->holding_valid = true;
  pipe->holding_abnormal = abnormal;

  /* "...and the high-order word is also loaded into stage B of the pipe."
   * The manual describes the aligned case; a prefetch of the low-order word
   * loads that word, which is the same rule applied to the word actually
   * requested. */
  pipe->b.word = word_from_holding(longword, address);
  pipe->b.valid = true;
  pipe->b.abnormal = abnormal;
}

void ap_m68030_pipe_load_from_holding(ap_m68030_pipe_t *pipe, uint32_t address) {
  if (!ap_m68030_pipe_holds(pipe, address)) {
    /* A miss here is a caller error, not a condition to paper over: loading a
     * stale long word would put a wrong opcode into the pipe and the fault
     * would surface much later, somewhere else. Leave B empty so it is loud. */
    pipe->b = (ap_m68030_pipe_stage_t){0};
    return;
  }

  pipe->b.word = word_from_holding(pipe->holding_data, address);
  pipe->b.valid = true;
  /* The status bit follows the data it came with: a word taken from a holding
   * register filled by an abnormally terminated cycle is itself suspect. */
  pipe->b.abnormal = pipe->holding_abnormal;
}

void ap_m68030_pipe_advance(ap_m68030_pipe_t *pipe) {
  pipe->d = pipe->c;
  pipe->c = pipe->b;
  pipe->b = (ap_m68030_pipe_stage_t){0};
}

bool ap_m68030_pipe_decoded(const ap_m68030_pipe_t *pipe, uint16_t *word,
                            bool *abnormal) {
  if (!pipe->d.valid) {
    return false;
  }
  if (word != NULL) {
    *word = pipe->d.word;
  }
  if (abnormal != NULL) {
    *abnormal = pipe->d.abnormal;
  }
  return true;
}

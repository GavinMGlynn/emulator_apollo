/* Apollo Token Ring — media access control layer, symbol level.
 *
 * `[MAC]` = *Apollo Token Ring Media Access Control Layer and Physical Layer
 * Protocols*, 010005-00 rev 00, Oct 1987. Sections cited throughout.
 *
 * **There is no runnable oracle for any of this.** MAME's Apollo driver carries
 * Domain networking over an emulated 3c505 802.3 card and never touches the
 * ring, so every constant here cites a manual section and nothing is settled by
 * measurement against another emulator. `docs/references/RING.md` is the
 * standing record.
 *
 * ## Bit stuffing is what makes a control character recognisable
 *
 * `[MAC]` §2.2.1 p. 2-4: "Normally, a transmitting node inserts a Zero (this is
 * called bit-stuffing) into the serial bit stream after every five successive
 * Ones. Likewise, a receiving node extracts each Zero that follows five
 * successive Ones."
 *
 * So a run of **six** ones cannot occur in stuffed data, and that is the whole
 * trick: "The presence of the six successive Ones tells a receiver: 'the
 * bit-stuffing protocol has been intentionally violated by the transmitter.'
 * When the bit-stuffing protocol has been violated, a receiver knows that it is
 * seeing an out-of-band bit pattern (i.e., that this pattern contains control
 * information)."
 *
 * This is HDLC's flag trick with a longer run, and the consequence worth
 * stating is that **out-of-band characters are not byte values**. They are
 * nine-bit symbols on the wire, and a model that stored them as bytes would
 * have nowhere to put the leading zero that makes them work.
 *
 * ## The four characters, from the figures rather than from the text
 *
 * `[MAC]` §2.2.1 p. 2-4: "All out-of-band characters begin with a Zero; the
 * leading Zero exists to break a potential string of Ones. To determine what
 * the character means, the receiver then looks at the two least-significant
 * bits within the character (i.e., at the two bits immediately following the
 * six successive Ones)."
 *
 * So every out-of-band character is `0` + six `1`s + two type bits = nine bits,
 * transmitted most-significant-bit first (§2.2.1.1 and §2.2.1.2 both say so
 * explicitly of their characters). The type bits are Figures 2-2, 2-3 and 2-4:
 *
 * ```
 *   0 111111 00   separator      Figure 2-4, p. 2-5
 *   0 111111 01   frame start    Figure 2-3, p. 2-5
 *   0 111111 10   free token     Figure 2-2, p. 2-4
 *   0 111111 11   claimed token  Figure 2-2, p. 2-4
 * ```
 *
 * **Those four values were read off the page images, not the text layer.** The
 * figures are line drawings of a bit field with the cells labelled `0`, `1` and
 * `MUST BE ONES`, and `pdftotext` renders them as fragments like
 * `MSB L.1_o--L_--'_ _ ..L~_U_S_T_BLiE_O_N_E......F~_-,--_---,-_1---11----,1 LSB`
 * -- which contains a `1`, a `0` and no way to tell which cell either belongs
 * to. Every bit pattern in this file comes from `pdftoppm` output read as an
 * image. `CLAUDE.md` requires it and this is exactly the case it was written
 * for.
 *
 * The free and claimed tokens differ in the last bit alone, which the prose
 * confirms independently: §2.2.1.1 says a node claims the ring "by changing the
 * state of the character's last bit". A model that got the type-bit order
 * backwards would still pass that sentence, so it is a check and not a proof --
 * but it is the check that caught nothing here, since the figures agree.
 */

#ifndef APOLLO_RING_AP_RING_MAC_H
#define APOLLO_RING_AP_RING_MAC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Bit-stuffing thresholds. `[MAC]` §2.2.1 p. 2-4: a zero is inserted after
 * five successive ones, so six successive ones is the deliberate violation
 * that marks an out-of-band character. */
#define AP_RING_STUFF_AFTER_ONES 5u
#define AP_RING_OOB_ONES 6u

/* An out-of-band character is nine bits, not eight. See the header note. */
#define AP_RING_OOB_BITS 9u

/* The four out-of-band characters, as nine-bit values with the MSB at bit 8.
 * `[MAC]` Figures 2-2 (p. 2-4), 2-3 and 2-4 (p. 2-5). */
typedef enum {
  AP_RING_OOB_SEPARATOR = 0x0FC,    /* 0 111111 00 */
  AP_RING_OOB_FRAME_START = 0x0FD,  /* 0 111111 01 */
  AP_RING_OOB_FREE_TOKEN = 0x0FE,   /* 0 111111 10 */
  AP_RING_OOB_CLAIMED_TOKEN = 0x0FF /* 0 111111 11 */
} ap_ring_oob_t;

/* Whether a nine-bit value is a well-formed out-of-band character: leading
 * zero, six ones, any two type bits. Written as a predicate rather than a
 * comparison against the four constants because the *shape* is what the
 * receiver detects -- it finds six ones and then reads the type bits, so a
 * malformed symbol with the right shape is a different error from noise. */
[[nodiscard]] bool ap_ring_oob_well_formed(uint16_t symbol);

/* The two type bits of an out-of-band character. Undefined unless
 * `ap_ring_oob_well_formed`. */
[[nodiscard]] uint8_t ap_ring_oob_type(uint16_t symbol);

/* The free token becomes the claimed token by setting its last bit.
 * `[MAC]` §2.2.1.1: a node claims the ring "by changing the state of the
 * character's last bit". Modelled as its own operation because that is the one
 * ring event with no byte-level equivalent: the node rewrites a symbol *in
 * flight* rather than transmitting a new one. */
[[nodiscard]] uint16_t ap_ring_token_claim(uint16_t free_token);

/* Null separators. `[MAC]` §2.2.1.3 p. 2-5: the long null separator "consists
 * of a minimum of 8 bytes of Zeros" and precedes the packet; the short null
 * separator is "a byte of Zeros" and occurs within the frame start sequence and
 * within the frame check sequence. A *minimum* is modelled as a minimum: a
 * conforming transmitter may send more, and a receiver that demanded exactly
 * eight would reject legal traffic. */
#define AP_RING_NULL_SEPARATOR_SHORT_BYTES 1u
#define AP_RING_NULL_SEPARATOR_LONG_MIN_BYTES 8u

/* ## The bit stream
 *
 * A writer accumulates bits most-significant-first and performs the stuffing;
 * a reader undoes it. Both carry the run length of ones across calls, because
 * the rule spans symbol boundaries -- five ones ending one byte and a one
 * opening the next still forces a stuffed zero.
 */
typedef struct {
  uint8_t *bytes;
  size_t capacity;   /* in bytes */
  size_t bit_count;  /* bits written so far */
  unsigned ones_run; /* successive ones most recently emitted */
  bool overflow;     /* set once a write did not fit; sticky */
} ap_ring_bitwriter_t;

void ap_ring_bitwriter_init(ap_ring_bitwriter_t *w, uint8_t *bytes,
                            size_t capacity);

/* One raw bit, with no stuffing. For the out-of-band characters, which are
 * *defined* by violating the rule. */
void ap_ring_write_raw_bit(ap_ring_bitwriter_t *w, bool bit);

/* One data bit, stuffing a zero once five ones have been emitted. */
void ap_ring_write_data_bit(ap_ring_bitwriter_t *w, bool bit);

/* `bits` of `value`, most-significant first, as data. */
void ap_ring_write_data_bits(ap_ring_bitwriter_t *w, uint32_t value,
                             unsigned bits);

/* A whole out-of-band character, raw: nine bits, MSB first, and the ones-run
 * reset afterwards because the character ends the violation. */
void ap_ring_write_oob(ap_ring_bitwriter_t *w, uint16_t symbol);

typedef struct {
  const uint8_t *bytes;
  size_t bit_count; /* bits available */
  size_t bit_pos;   /* bits consumed */
  unsigned ones_run;
} ap_ring_bitreader_t;

void ap_ring_bitreader_init(ap_ring_bitreader_t *r, const uint8_t *bytes,
                            size_t bit_count);

/* Whether at least `bits` remain unread. */
[[nodiscard]] bool ap_ring_bitreader_has(const ap_ring_bitreader_t *r,
                                         size_t bits);

/* One raw bit. False if the stream is exhausted, with `*bit` untouched. */
[[nodiscard]] bool ap_ring_read_raw_bit(ap_ring_bitreader_t *r, bool *bit);

/* One data bit, discarding a stuffed zero that follows five ones. Returns
 * false at end of stream, and sets `*violation` when the bit that arrived
 * where a stuffed zero was required is a one -- which is not an error but the
 * receiver's signal that an out-of-band character has begun. */
[[nodiscard]] bool ap_ring_read_data_bit(ap_ring_bitreader_t *r, bool *bit,
                                         bool *violation);

/* `bits` data bits, most-significant first. */
[[nodiscard]] bool ap_ring_read_data_bits(ap_ring_bitreader_t *r, unsigned bits,
                                          uint32_t *value, bool *violation);

/* Nine raw bits as an out-of-band character. */
[[nodiscard]] bool ap_ring_read_oob(ap_ring_bitreader_t *r, uint16_t *symbol);

/* Whether an out-of-band character stands next in the stream, without consuming
 * it.
 *
 * A frame's sequences are delimited by separator *characters* and not by any
 * length field -- "a packet header can vary in size from 12 to 1024 bytes" and
 * nothing on the wire says which -- so a receiver decides "another data byte or
 * the end of this sequence" before every byte. That decision cannot be made by
 * reading: a symbol consumed is a symbol the caller must then put back, and the
 * reader carries a ones-run that makes putting it back more than a rewind.
 *
 * Safe to answer by looking because the stuffing guarantees it: data can never
 * present six consecutive ones, so `0` followed by six `1`s is an out-of-band
 * character and nothing else. False at end of stream, which a caller reads as
 * "no more sequences here" rather than as an error. */
[[nodiscard]] bool ap_ring_peek_oob(const ap_ring_bitreader_t *r,
                                    uint16_t *symbol);

#endif /* APOLLO_RING_AP_RING_MAC_H */

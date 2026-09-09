/* The DS5500's I/O protection map: `019411-A00` Table 2-5,
 * `07000000`-`0700FFFF`, 64 KB, named "I/O PROTECTION MAP".
 *
 * **What is documented is the range and the title, and nothing else.** Table
 * 2-5 is an address-space allocation table: it gives this region a name and a
 * 64 KB extent and says not one word about entry width, layout or what the
 * hardware does with an entry. The specification would be in the parent
 * handbook, `007861-A01`, whose Preface pins the order number and which is
 * **not scanned anywhere** -- searched at all three tiers during the
 * `019411-A00` walk, and recorded there as searched. So the *meaning* of a byte
 * in here is `PROVISIONAL` and this module invents none of it.
 *
 * **What is measured is that the region must answer.** The DS5500 boot PROM
 * clears the first four longwords at reset --
 *
 *     00692  clr.b  $11600.l          (the master request register)
 *     00698  clr.l  $7000000.l
 *     0069E  clr.l  $7000004.l
 *     006A4  clr.l  $7000008.l
 *     006AA  clr.l  $700000C.l
 *
 * -- and it does so **before it has a stack**. With the region unplaced the
 * write bus-errors, the handler at `000446` pushes to `FFFFFFFC` with `A7`
 * zero, and the machine is dead at **137 instructions**, which is what
 * `--service-mode` did on a DS5500 until this landed. Nothing in the PROM ever
 * reads the region back, so "it stores" is the honest model for a range the
 * manual calls a *map* rather than a measured requirement; the measured
 * requirement is only that a write is accepted.
 *
 * **Named and modelled, where DESKTOP VISUALIZATION SPACE is named and not.**
 * The difference is not inconsistency: that region has a title and no evidence
 * anything ever touches it, so declining is the truthful answer and a trace
 * saying *which* hole was reached is the whole benefit. Here the firmware
 * demonstrably writes, so declining is the answer that is wrong. */
#ifndef AP_IOPROT_H
#define AP_IOPROT_H

#include <stdbool.h>
#include <stdint.h>

/* Table 2-5's range, inclusive. */
#define AP_IOPROT_BASE 0x07000000u
#define AP_IOPROT_LIMIT 0x0700FFFFu
#define AP_IOPROT_BYTES (AP_IOPROT_LIMIT - AP_IOPROT_BASE + 1u)

typedef struct {
  uint8_t bytes[AP_IOPROT_BYTES];
  /* Zero on every model but the DS5500. One structure serves every board, and
   * this is what says whether the board *has* one -- the same idiom
   * `ap_cacheram_t::entries` and `ap_atmap_t::entries` use, and for the same
   * reason: the hasher walks the machine's own count, so a board without the
   * region contributes nothing at all to the digest rather than 65,536 zero
   * bytes. */
  unsigned size;
} ap_ioprot_t;

void ap_ioprot_init(ap_ioprot_t *map, bool present);

static inline bool ap_ioprot_present(const ap_ioprot_t *map) {
  return map->size != 0u;
}

/* `address` is anywhere in Table 2-5's range; the offset is taken from it. */
uint8_t ap_ioprot_read(const ap_ioprot_t *map, uint32_t address);
void ap_ioprot_write(ap_ioprot_t *map, uint32_t address, uint8_t value);

#endif /* AP_IOPROT_H */

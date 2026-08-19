/* MC68030 state hashing: the CPU's contribution to the whole-machine hash.
 *
 * The point of the whole-machine hash is stated in `CLAUDE.md`: "Optimization is
 * only safe under an identity harness", and the harness is probe goldens plus a
 * long-run state hash. A fast mode that diverges from the reference core is
 * caught here or it is not caught at all — so what this covers *is* the
 * definition of "the same machine state".
 *
 * ## Everything architectural, nothing of the host
 *
 * Every register, every flag, the pipe, the caches, the ATC and the accumulated
 * clock. Not the callback pointers, the access contexts, or anything else whose
 * value depends on where the process happened to put it: `ap_hash.h` has no
 * `ap_hash_ptr()` for exactly this reason, so host addresses are excluded by
 * construction rather than by remembering.
 *
 * ## Timing state counts
 *
 * The accumulated clock is hashed with the registers, not reported beside them.
 * Two runs that reach the same registers by different numbers of bus cycles are
 * *not* the same run on a machine whose whole claim is emergent timing, and a
 * hash that omitted the clock would call them equal.
 *
 * ## Adding a field
 *
 * A field added to `ap_m68030_cpu_t` and not added here is invisible: the hash
 * keeps matching while the state diverges, which is the one failure mode this
 * module must not have. `state_suite` therefore sweeps every field
 * individually — perturb it, and the hash must change. A new field without a
 * new sweep entry is a gap someone can see in the test, rather than one nobody
 * can see at all.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_STATE_H
#define APOLLO_CPU_M68030_AP_M68030_STATE_H

#include "cpu/m68030/ap_m68030_atc.h"
#include "cpu/m68030/ap_m68030_cache.h"
#include "cpu/m68030/ap_m68030_pipe.h"
#include "cpu/m68030/ap_m68030_step.h"
#include "state/ap_hash.h"

/* The programming model: data and address registers, all three stack pointers,
 * the PC, the whole status register, VBR and the function code registers.
 *
 * A7 is fed through the three stack pointers rather than through `ap_read_a7`,
 * so two states differing only in which stack is *active* still differ here. */
void ap_m68030_hash_regs(ap_hash_t *st, const ap_m68030_regs_t *regs);

/* The instruction pipe and the cache holding register. Both are architectural
 * on this part: a branch empties the pipe, and whether the holding register is
 * valid decides whether the next word costs a bus cycle. */
void ap_m68030_hash_pipe(ap_hash_t *st, const ap_m68030_pipe_t *pipe);

/* One cache, tag and valid bits and data. The valid bits are per *entry* rather
 * than per line, so a line whose tag matches and whose entries are all invalid
 * is a different state from an absent line, and both hash differently. */
void ap_m68030_hash_cache(ap_hash_t *st, const ap_m68030_cache_t *cache);

/* The address translation cache, every entry including the invalid ones: an
 * invalid entry still holds the history bit the replacement algorithm reads,
 * so two ATCs differing only there behave differently on the next miss. */
void ap_m68030_hash_atc(ap_hash_t *st, const ap_m68030_atc_t *atc);

/* The whole processor: the registers, the MMU registers, the cache control
 * registers, the pipe, the pending exception and interrupt state, the
 * accumulated clock, and whichever caches and ATC the access contexts reach.
 *
 * The instruction and data sides are fed separately and in that order, so a
 * machine with the two caches exchanged does not hash the same as one without.
 * A null access context feeds a marker rather than nothing, since "no data side
 * at all" and "a data side whose cache is empty" are different machines. */
/* The floating-point coprocessor's registers. Called by `ap_m68030_hash_cpu`
 * when one is fitted; there was no `cpu.fpu` scope at all until 2026-08-19, so
 * every 68882 register was outside the identity hash. */
void ap_m68030_hash_fpu(ap_hash_t *st, const ap_m68882_t *fpu);

void ap_m68030_hash_cpu(ap_hash_t *st, const ap_m68030_cpu_t *cpu);

/* The whole processor as one number, which is what a long run reports. */
[[nodiscard]] uint64_t ap_m68030_state_hash(const ap_m68030_cpu_t *cpu);

#endif /* APOLLO_CPU_M68030_AP_M68030_STATE_H */

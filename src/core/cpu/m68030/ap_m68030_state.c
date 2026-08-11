/* MC68030 state hashing. See ap_m68030_state.h for what is included and why the
 * host's pointers are not. */

#include "cpu/m68030/ap_m68030_state.h"

#include <stddef.h>
#include <stdio.h>

/* Booleans are fed as one tagged byte rather than as whatever the compiler
 * stores, so a `bool` that happens to hold a value other than 0 or 1 cannot
 * produce a hash the same state would not. */
static void hash_bool(ap_hash_t *st, bool value) {
  ap_hash_u8(st, value ? 1u : 0u);
}

void ap_m68030_hash_regs(ap_hash_t *st, const ap_m68030_regs_t *regs) {
  /* Scoped per group so a dump maps onto the oracle's registry, whose data and
   * address registers are separate arrays too. Positional within a group is
   * enough here and only here: `d[0..7]` in order is not a convention this core
   * chose, it is the register file. */
  ap_hash_scope(st, "cpu.d");
  for (unsigned i = 0; i < 8u; i++) {
    ap_hash_u32(st, regs->d[i]);
  }
  ap_hash_scope(st, "cpu.a");
  for (unsigned i = 0; i < 7u; i++) {
    ap_hash_u32(st, regs->a[i]);
  }
  ap_hash_scope(st, "cpu.sp");

  /* All three stack pointers, not A7 through the active one: two states
   * differing only in which stack is active must differ here, and the status
   * register below is what says which that is. */
  ap_hash_u32(st, regs->usp);
  ap_hash_u32(st, regs->isp);
  ap_hash_u32(st, regs->msp);

  /* **Which of the three A7 currently is**, derived, for the oracle diff only.
   *
   * MAME's Musashi keeps the *live* stack pointer in its `dar[15]` -- A7 -- and
   * `REG_USP()`/`REG_ISP()`/`REG_MSP()` are **spill slots**, written when the
   * status register switches away from that stack and stale until then. So
   * MAME's `REG_ISP()` reads 0 through the whole early boot while its A7 holds
   * the initialised supervisor stack, and pairing our `isp` with its `REG_ISP()`
   * reports a difference at every sync point where there is none.
   *
   * This core has no such slot: all three are always current and A7 is derived
   * from the status register instead. Emitting that derivation is what makes
   * the two models comparable -- it pairs with `REG_D().0.15` in every mode,
   * where no fixed pairing of the three could.
   *
   * `[M68030UM]` §1.3: S selects supervisor, and with S set M chooses the
   * master stack over the interrupt stack. */
  {
    const bool supervisor = (regs->sr & 0x2000u) != 0u;
    const bool master = (regs->sr & 0x1000u) != 0u;
    const uint32_t active = !supervisor ? regs->usp
                                        : (master ? regs->msp : regs->isp);
    ap_hash_note_u32(st, "active", active);
  }

  ap_hash_scope(st, "cpu.ctl");
  ap_hash_u32(st, regs->pc);
  ap_hash_u16(st, regs->sr);
  ap_hash_u32(st, regs->vbr);
  ap_hash_u8(st, regs->sfc);
  ap_hash_u8(st, regs->dfc);
}

static void hash_pipe_stage(ap_hash_t *st, const ap_m68030_pipe_stage_t *stage) {
  ap_hash_u16(st, stage->word);
  hash_bool(st, stage->valid);
  hash_bool(st, stage->abnormal);
}

void ap_m68030_hash_pipe(ap_hash_t *st, const ap_m68030_pipe_t *pipe) {
  hash_pipe_stage(st, &pipe->b);
  hash_pipe_stage(st, &pipe->c);
  hash_pipe_stage(st, &pipe->d);

  ap_hash_u32(st, pipe->holding_data);
  ap_hash_u32(st, pipe->holding_address);
  hash_bool(st, pipe->holding_valid);
  hash_bool(st, pipe->holding_abnormal);
}

void ap_m68030_hash_cache(ap_hash_t *st, const ap_m68030_cache_t *cache) {
  for (unsigned line = 0; line < AP_M68030_CACHE_LINES; line++) {
    /* Only what an access can *reach*. `ap_m68030_cache_clear` clears the valid
     * bits and leaves the tag and data behind, so an invalid entry holds
     * whatever the last occupant left -- and a lookup can never return it.
     * Hashing it anyway makes two machines that behave identically hash
     * differently, which is a false positive in the identity harness and worse
     * than a miss: a harness that rejects identical machines cannot be used at
     * all. It is how this was found -- two machines built the same way on two
     * different buffers disagreed.
     *
     * The tag is per line and shared by four entries, so it counts exactly when
     * some entry in the line is valid. */
    bool any_valid = false;
    for (unsigned entry = 0; entry < AP_M68030_CACHE_ENTRIES; entry++) {
      any_valid = any_valid || cache->line[line].valid[entry];
    }
    ap_hash_u32(st, any_valid ? cache->line[line].tag : 0u);

    for (unsigned entry = 0; entry < AP_M68030_CACHE_ENTRIES; entry++) {
      /* The valid bit is itself state, and per entry: a line with one valid
       * entry is not the same as a line with two. */
      hash_bool(st, cache->line[line].valid[entry]);
      ap_hash_u32(st, cache->line[line].valid[entry]
                          ? cache->line[line].entry[entry]
                          : 0u);
    }
  }
}

void ap_m68030_hash_atc(ap_hash_t *st, const ap_m68030_atc_t *atc) {
  for (unsigned i = 0; i < AP_M68030_ATC_ENTRIES; i++) {
    const ap_m68030_atc_entry_t *entry = &atc->entry[i];
    hash_bool(st, entry->valid);

    /* The history bit is fed even for an invalid entry: it is what the
     * replacement algorithm reads, so two ATCs differing only there choose
     * different victims on the next miss. `ap_m68030_atc_flush` clears it
     * along with the valid bit, which is what makes that well defined. */
    hash_bool(st, entry->history);

    /* Everything else only when the entry is valid. A flush clears the valid
     * bit and leaves the tag and translation behind, and no lookup can reach
     * them -- so hashing them would make two machines that behave identically
     * hash differently, which is a false positive the harness cannot afford. */
    if (!entry->valid) {
      continue;
    }
    ap_hash_u8(st, entry->function_code);
    ap_hash_u32(st, entry->logical);
    hash_bool(st, entry->bus_error);
    hash_bool(st, entry->cache_inhibit);
    hash_bool(st, entry->write_protect);
    hash_bool(st, entry->modified);
    ap_hash_u32(st, entry->physical);
  }
}

static void hash_tc(ap_hash_t *st, const ap_m68030_tc_t *tc) {
  hash_bool(st, tc->enable);
  hash_bool(st, tc->supervisor_root);
  hash_bool(st, tc->function_code_lookup);
  ap_hash_u8(st, tc->page_size_bits);
  ap_hash_u8(st, tc->initial_shift);
  for (unsigned level = 0; level < AP_M68030_TC_LEVELS; level++) {
    ap_hash_u8(st, tc->table_index[level]);
  }
}

static void hash_root(ap_hash_t *st, const ap_m68030_root_t *root) {
  ap_hash_u32(st, root->table_address);
  hash_bool(st, root->long_format);
  ap_hash_u16(st, root->limit);
  hash_bool(st, root->lower_limit);
  hash_bool(st, root->has_limit);
}

static void hash_tt(ap_hash_t *st, const ap_m68030_tt_t *tt) {
  ap_hash_u8(st, tt->logical_base);
  ap_hash_u8(st, tt->logical_mask);
  ap_hash_u8(st, tt->fc_base);
  ap_hash_u8(st, tt->fc_mask);
  hash_bool(st, tt->enabled);
  hash_bool(st, tt->cache_inhibit);
  hash_bool(st, tt->read_transparent);
  hash_bool(st, tt->ignore_read_write);
}

static void hash_cacr(ap_hash_t *st, const ap_m68030_cacr_t *cacr) {
  hash_bool(st, cacr->write_allocate);
  hash_bool(st, cacr->data_burst_enable);
  hash_bool(st, cacr->freeze_data);
  hash_bool(st, cacr->enable_data);
  hash_bool(st, cacr->instruction_burst_enable);
  hash_bool(st, cacr->freeze_instruction);
  hash_bool(st, cacr->enable_instruction);
}

/* One access context's *state*, which is its cache and ATC. The callbacks and
 * the context pointer are host addresses and are excluded; the enable and
 * disable flags are architectural, since they come from CACR and CDIS. */
/* `side` names this access path in the dump -- "i" or "d". It exists because
 * the cache and ATC walks below are **data dependent**: both skip fields for an
 * invalid entry, so the number of lines they emit varies with what the machine
 * happens to hold, and every field after them in the scope would be renumbered
 * by an ordinary run of the program. A map keyed on those indices silently
 * starts naming the wrong field.
 *
 * Collapsing each walk to one group line fixes that -- a group emits one named
 * line and does not advance the index -- and the group's running hash still
 * differs whenever any entry does. The names have to be distinct because this
 * function is called twice into the same scope. */
static void hash_access(ap_hash_t *st, const ap_m68030_access_ctx_t *access,
                        const char *side) {
  if (access == NULL) {
    /* A marker rather than nothing: "no data side at all" and "a data side
     * whose cache is empty" are different machines, and feeding nothing would
     * make them hash alike. */
    ap_hash_u8(st, 0xFFu);
    return;
  }
  ap_hash_u8(st, 0x01u);

  if (access->cache != NULL) {
    char name[16];
    snprintf(name, sizeof name, "%s_cache", side);
    ap_hash_group_begin(st, name);
    ap_m68030_hash_cache(st, access->cache);
    ap_hash_group_end(st);
  }
  if (access->atc != NULL) {
    char name[16];
    snprintf(name, sizeof name, "%s_atc", side);
    ap_hash_group_begin(st, name);
    ap_m68030_hash_atc(st, access->atc);
    ap_hash_group_end(st);
  }

  hash_bool(st, access->cache_enabled);
  hash_bool(st, access->cache_frozen);
  hash_bool(st, access->burst_enabled);
  hash_bool(st, access->write_allocate);
  hash_bool(st, access->cache_disable);
  /* Not translation: that is `TC`'s E bit and `hash_tc` already feeds it.
   * Hashing a second copy was hashing a field that could not change. */
}

void ap_m68030_hash_cpu(ap_hash_t *st, const ap_m68030_cpu_t *cpu) {
  ap_m68030_hash_regs(st, &cpu->regs);
  ap_hash_scope(st, "cpu.mmu");

  /* The MMU registers live on the CPU because there is one MMU and two access
   * paths through it. */
  hash_tc(st, &cpu->tc);
  hash_root(st, &cpu->crp);
  hash_root(st, &cpu->srp);
  hash_tt(st, &cpu->tt0);
  hash_tt(st, &cpu->tt1);
  ap_hash_u16(st, cpu->mmusr);

  /* The packed register words, for the oracle differential only -- derived, so
   * the hash is untouched (see `ap_hash_note_u32`).
   *
   * This core decomposes `TC` and the root pointers on the `PMOVE` that writes
   * them; MAME keeps the words. Decomposed against packed, no field of one
   * dump pairs with any field of the other, and the MMU is what a page fault
   * has to be read from -- so the packing that `PMOVE` already inverts is
   * emitted beside the fields, where it lines up with `m_mmu_tc`,
   * `m_mmu_crp_limit`/`_aptr` and `m_mmu_srp_limit`/`_aptr` one for one.
   *
   * MAME's `_limit` is the **upper** long word and `_aptr` the lower
   * (`m68kmmu.h`, `PMOVE from CRP`), which is the opposite of what the names
   * suggest to a reader who has not checked. */
  ap_hash_note_u32(st, "tc_packed", ap_m68030_tc_encode(&cpu->tc));
  ap_hash_note_u32(st, "crp_upper", ap_m68030_root_pack_upper(&cpu->crp));
  ap_hash_note_u32(st, "crp_lower", cpu->crp.table_address);
  ap_hash_note_u32(st, "srp_upper", ap_m68030_root_pack_upper(&cpu->srp));
  ap_hash_note_u32(st, "srp_lower", cpu->srp.table_address);

  ap_hash_scope(st, "cpu.cache");
  hash_cacr(st, &cpu->cacr);
  ap_hash_u32(st, cpu->caar);

  ap_hash_scope(st, "cpu.exec");
  hash_bool(st, cpu->stopped);
  ap_hash_u32(st, (uint32_t)cpu->external_resets);
  ap_hash_u32(st, (uint32_t)cpu->interrupt_level);
  ap_hash_u32(st, (uint32_t)cpu->previous_interrupt_level);
  ap_hash_u32(st, (uint32_t)cpu->extension_words);
  ap_hash_u32(st, (uint32_t)cpu->pending_vector);

  /* The instruction side: the pipe, where it is fetching from, and in which
   * address space. */
  ap_hash_scope(st, "cpu.fetch");
  ap_m68030_hash_pipe(st, &cpu->fetch.pipe);
  ap_hash_u32(st, cpu->fetch.address);
  ap_hash_u8(st, cpu->fetch.function_code);
  /* What prefetching has cost, which is timing state in the same sense as
   * `cpu->clocks` and is not derivable from it: two runs reaching the same
   * total by different splits between instruction and operand cycles are
   * different runs, and the split is exactly what §11.6's model turns on. */
  ap_hash_u64(st, cpu->fetch.bus_clocks);
  ap_hash_u8(st, cpu->data_function_code);

  /* Instruction side then data side, in that order, so a machine with the two
   * caches exchanged does not hash the same as one without. */
  hash_access(st, cpu->fetch.access, "i");
  hash_access(st, cpu->data, "d");

  /* Timing is state. Two runs reaching the same registers by different numbers
   * of bus cycles are not the same run on a machine whose whole claim is
   * emergent timing, and a hash omitting the clock would call them equal. */
  ap_hash_u64(st, cpu->clocks);
}

uint64_t ap_m68030_state_hash(const ap_m68030_cpu_t *cpu) {
  ap_hash_t st = ap_hash_begin();
  ap_m68030_hash_cpu(&st, cpu);
  return ap_hash_end(&st);
}

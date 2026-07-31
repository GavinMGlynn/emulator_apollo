/* MC68030 state hashing. See ap_m68030_state.h for what is included and why the
 * host's pointers are not. */

#include "cpu/m68030/ap_m68030_state.h"

#include <stddef.h>

/* Booleans are fed as one tagged byte rather than as whatever the compiler
 * stores, so a `bool` that happens to hold a value other than 0 or 1 cannot
 * produce a hash the same state would not. */
static void hash_bool(ap_hash_t *st, bool value) {
  ap_hash_u8(st, value ? 1u : 0u);
}

void ap_m68030_hash_regs(ap_hash_t *st, const ap_m68030_regs_t *regs) {
  for (unsigned i = 0; i < 8u; i++) {
    ap_hash_u32(st, regs->d[i]);
  }
  for (unsigned i = 0; i < 7u; i++) {
    ap_hash_u32(st, regs->a[i]);
  }

  /* All three stack pointers, not A7 through the active one: two states
   * differing only in which stack is active must differ here, and the status
   * register below is what says which that is. */
  ap_hash_u32(st, regs->usp);
  ap_hash_u32(st, regs->isp);
  ap_hash_u32(st, regs->msp);

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
    ap_hash_u32(st, cache->line[line].tag);
    for (unsigned entry = 0; entry < AP_M68030_CACHE_ENTRIES; entry++) {
      /* The valid bit is per entry, so a line whose tag matches and whose
       * entries are all invalid is not the same state as an absent line. */
      hash_bool(st, cache->line[line].valid[entry]);
      ap_hash_u32(st, cache->line[line].entry[entry]);
    }
  }
}

void ap_m68030_hash_atc(ap_hash_t *st, const ap_m68030_atc_t *atc) {
  for (unsigned i = 0; i < AP_M68030_ATC_ENTRIES; i++) {
    const ap_m68030_atc_entry_t *entry = &atc->entry[i];
    hash_bool(st, entry->valid);
    ap_hash_u8(st, entry->function_code);
    ap_hash_u32(st, entry->logical);
    hash_bool(st, entry->bus_error);
    hash_bool(st, entry->cache_inhibit);
    hash_bool(st, entry->write_protect);
    hash_bool(st, entry->modified);
    ap_hash_u32(st, entry->physical);
    /* The history bit is fed even for an invalid entry: it is what the
     * replacement algorithm reads, so two ATCs differing only there choose
     * different victims on the next miss. */
    hash_bool(st, entry->history);
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
static void hash_access(ap_hash_t *st, const ap_m68030_access_ctx_t *access) {
  if (access == NULL) {
    /* A marker rather than nothing: "no data side at all" and "a data side
     * whose cache is empty" are different machines, and feeding nothing would
     * make them hash alike. */
    ap_hash_u8(st, 0xFFu);
    return;
  }
  ap_hash_u8(st, 0x01u);

  if (access->cache != NULL) {
    ap_m68030_hash_cache(st, access->cache);
  }
  if (access->atc != NULL) {
    ap_m68030_hash_atc(st, access->atc);
  }

  hash_bool(st, access->cache_enabled);
  hash_bool(st, access->cache_frozen);
  hash_bool(st, access->burst_enabled);
  hash_bool(st, access->write_allocate);
  hash_bool(st, access->cache_disable);
  hash_bool(st, access->translation_enabled);
}

void ap_m68030_hash_cpu(ap_hash_t *st, const ap_m68030_cpu_t *cpu) {
  ap_m68030_hash_regs(st, &cpu->regs);

  /* The MMU registers live on the CPU because there is one MMU and two access
   * paths through it. */
  hash_tc(st, &cpu->tc);
  hash_root(st, &cpu->crp);
  hash_root(st, &cpu->srp);
  hash_tt(st, &cpu->tt0);
  hash_tt(st, &cpu->tt1);
  ap_hash_u16(st, cpu->mmusr);

  hash_cacr(st, &cpu->cacr);
  ap_hash_u32(st, cpu->caar);

  hash_bool(st, cpu->stopped);
  ap_hash_u32(st, (uint32_t)cpu->external_resets);
  ap_hash_u32(st, (uint32_t)cpu->interrupt_level);
  ap_hash_u32(st, (uint32_t)cpu->previous_interrupt_level);
  ap_hash_u32(st, (uint32_t)cpu->extension_words);
  ap_hash_u32(st, (uint32_t)cpu->pending_vector);

  /* The instruction side: the pipe, where it is fetching from, and in which
   * address space. */
  ap_m68030_hash_pipe(st, &cpu->fetch.pipe);
  ap_hash_u32(st, cpu->fetch.address);
  ap_hash_u8(st, cpu->fetch.function_code);
  ap_hash_u8(st, cpu->data_function_code);

  /* Instruction side then data side, in that order, so a machine with the two
   * caches exchanged does not hash the same as one without. */
  hash_access(st, cpu->fetch.access);
  hash_access(st, cpu->data);

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

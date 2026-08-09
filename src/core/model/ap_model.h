/* The model table.
 *
 * Built the way the PDP-11 project handled its model range: describe the
 * superset machine, then express every other member as a subset of it, from one
 * table rather than from scattered conditionals. DN3500 is the reference
 * superset here -- it is the model with the most complete runnable oracle
 * coverage in MAME and the one whose boot and ring firmware are both dumped.
 *
 * Every field is either confirmed against a cited source or marked PROVISIONAL
 * in ap_model_t::provisional. A PROVISIONAL figure is a named plan item in
 * docs/COMPLETION_PLAN.md, never a number invented to fill a gap.
 */

#ifndef APOLLO_MODEL_AP_MODEL_H
#define APOLLO_MODEL_AP_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  AP_MODEL_DN2500,
  AP_MODEL_DN3000,
  AP_MODEL_DN3500,
  AP_MODEL_DN4500,
  AP_MODEL_DN5500,
  AP_MODEL_DSP3000,
  AP_MODEL_DSP3500,
  AP_MODEL_DSP4500,
  AP_MODEL_DSP5500,
  AP_MODEL_COUNT
} ap_model_id_t;

typedef enum {
  AP_CPU_M68020,
  AP_CPU_M68030,
  AP_CPU_M68040
} ap_cpu_t;

/* Memory management: the 68020 needs an external PMMU, the 68030 and 68040
 * have one on chip. This distinction is a whole subsystem, not a flag. */
typedef enum {
  AP_MMU_M68851,    /* external PMMU, DN3000 */
  AP_MMU_M68030,    /* on-chip, DN2500/DN3500/DN4500 */
  AP_MMU_M68040     /* on-chip, different descriptor format, DN5500 */
} ap_mmu_t;

typedef enum {
  AP_FPU_M68881,
  AP_FPU_M68882,
  AP_FPU_M68040     /* integrated */
} ap_fpu_t;

/* A DSP ("Domain Server Processor") is the same board without the display or
 * keyboard: it is a headless node. These are the cheap nodes to run many of on
 * an emulated ring. */
/* The base (monochrome) configuration of each model. Colour and larger-monitor
 * variants are real orderable options -- Series 3500/3550/4500 all offer 19"
 * colour 1280x1024 and 1024x800 -- but a display variant is a graphics-phase
 * concern, so the table names only the base panel each model ships with. */
typedef enum {
  AP_DISPLAY_NONE,
  AP_DISPLAY_MONO_1024X800,
  AP_DISPLAY_MONO_1280X1024,
  AP_DISPLAY_COLOR_1024X800
} ap_display_t;

/* How well this model's behaviour can be checked. Recorded in the table so the
 * status doc cannot drift from it. */
typedef enum {
  AP_ORACLE_MAME,       /* runnable oracle exists in ext/mame */
  AP_ORACLE_PAPER_ONLY  /* manuals and firmware only; no runnable reference */
} ap_oracle_t;

typedef struct {
  ap_model_id_t id;
  const char *name;          /* short key, e.g. "dn3500" */
  const char *description;

  /* The model whose **board** this one is, or itself.
   *
   * A DSP variant is a workstation without a display -- the table's own
   * descriptions say so, "DN3500 board without display" -- so everything
   * decided by the board rather than by the machine follows the workstation it
   * is derived from. The memory configuration strap is the first thing to need
   * it: the byte is a property of the board and the firmware that reads it, and
   * keying it on the model left every DSP variant unstrapped and failing its
   * memory self-test, exactly as an unlisted DN3000 size did.
   *
   * A field rather than a `name`-prefix rule, because "dsp3500 is a dn3500" is
   * machine variance and this table is where machine variance is allowed to
   * live. */
  ap_model_id_t board_of;
  ap_cpu_t cpu;
  uint32_t cpu_hz;
  ap_mmu_t mmu;
  ap_fpu_t fpu;
  ap_display_t display;
  ap_oracle_t oracle;

  /* Physical address of main memory and the largest supported size, from the
   * model's address space allocation table. */
  uint32_t ram_base;
  uint32_t ram_max_bytes;

  /* Apollo Token Ring controller fitted. Every model in this table supports
   * one; a node may still be configured without it. */
  bool has_ring;

  /* Board-level address translation map at `017000`, between the AT bus and
   * physical memory. See `board/ap_atmap.h`.
   *
   * Genuinely a model difference rather than a superset feature: `008778-03`
   * §1.2 says "The Series 4000, unlike the Series 3000, incorporates an address
   * translation map in its architecture", so a DN3000 has none and its DMA
   * reaches physical memory directly. `019411-A00` §4.2.1.4 replaces that
   * section and names the models that do: DS3500, DS4000, DS4500, DS5500.
   *
   * The DN2500 is on neither list. Set false by that absence rather than by a
   * statement -- `019411-A00`'s enumeration reads as exhaustive, but it is
   * still an argument from silence and is the one entry here that a Series 2500
   * hardware reference could overturn. */
  bool has_address_translation_map;

  /* Whether the CPU control register's four parity-lane bits are **active
   * low**, so `08` means "force bad parity on all four lanes" rather than `F8`.
   *
   * The boot PROMs settle it without needing a manual, because both families
   * run the same self-test and write complementary values into it: `08` in
   * `3500_BOOT_12191_7` at `00744E`, `4500_BOOT_13167_02` at `00746E` and
   * `5500_BOOT_A1631-80046` at `007BFE`; `F8` in `3000_BOOT_8475_4` at `0067F4`
   * and `3000_BOOT_8475_7` at `006848`. Each then requires all four status bits
   * back, so the two values mean one thing and the families differ by an
   * inversion.
   *
   * The DN2500's PROM contains **no** forced-parity write at all, so nothing in
   * hand tests it either way; it follows the Series 4000 here on the oracle's
   * split, which is the weakest entry in this field and is named as such in its
   * `provisional` string. */
  bool has_active_low_parity_lanes;

  /* Non-NULL when one or more fields above are not yet confirmed against a
   * cited source. The string names exactly which. */
  const char *provisional;
} ap_model_t;

/* ---------------------------------------------------------------------------
 * What a CPU family means for the core.
 *
 * These are *derived* from `ap_cpu_t`, not stored per model, because they are
 * facts about the part and not about the machine it was soldered into: every
 * 68020 has a 64-entry instruction cache whatever board it is on. Deriving them
 * keeps the rule in CLAUDE.md -- all machine variance in one table -- without
 * inviting a DN3000 entry that claims a 68020 with a data cache.
 *
 * Sources: `MC68020 User's Manual` §7.1.1 and §1 for the 68020's cache and the
 * absence of an on-chip MMU; `MC68030 User's Manual` §6 and §9 for the 68030's
 * two caches and burst; the `MC68020` PRM entries for `CALLM`/`RTM`, which are
 * marked "(MC68020)" and exist on no later part.
 * ------------------------------------------------------------------------- */
typedef struct {
  /* Instruction cache size in bytes, and the number of long words per line.
   * Both parts hold 256 bytes and they are not the same cache: the 68020 is
   * "a direct-mapped cache of 64 long word entries" (one long word per line),
   * the 68030 is 16 lines of four. The line length is what decides how much a
   * single fill covers and therefore the hit rate on straight-line code. */
  unsigned instruction_cache_bytes;
  unsigned instruction_cache_line_longs;

  /* Zero on the 68020: it caches instructions only, so every operand access
   * goes to the bus. A model that gave it a data cache would make data cheaper
   * than the hardware's. */
  unsigned data_cache_bytes;

  /* The 68030 and 68040 translate on chip; the 68020 drives an external 68851
   * over the coprocessor interface, so its MMU is a coprocessor and its
   * translation is not in the CPU's own bus cycle. */
  bool has_onchip_mmu;

  /* Synchronous termination and burst filling arrived with the 68030: the
   * 68020 has `DSACK` only, so it cannot burst and its cache fills one long
   * word per bus cycle. */
  bool has_synchronous_bus;
  bool has_burst_fill;

  /* `CALLM` and `RTM`, the module call instructions. Present on the 68020
   * alone -- the PRM marks both "(MC68020)" -- and removed from the 68030
   * onward, where their encodings take an F-line/illegal path instead. */
  bool has_module_calls;
} ap_cpu_features_t;

/* Derive the features of a CPU family. Total: every `ap_cpu_t` has an entry. */
[[nodiscard]] ap_cpu_features_t ap_cpu_features(ap_cpu_t cpu);

/* Look up by id. Returns NULL for AP_MODEL_COUNT or out-of-range input. */
[[nodiscard]] const ap_model_t *ap_model_by_id(ap_model_id_t id);

/* Look up by short name, case-sensitive. Returns NULL if unknown. */
[[nodiscard]] const ap_model_t *ap_model_by_name(const char *name);

/* Number of entries in the table; equals AP_MODEL_COUNT. */
[[nodiscard]] size_t ap_model_count(void);

#endif /* APOLLO_MODEL_AP_MODEL_H */

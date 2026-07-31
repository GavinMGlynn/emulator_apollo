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

  /* Non-NULL when one or more fields above are not yet confirmed against a
   * cited source. The string names exactly which. */
  const char *provisional;
} ap_model_t;

/* Look up by id. Returns NULL for AP_MODEL_COUNT or out-of-range input. */
[[nodiscard]] const ap_model_t *ap_model_by_id(ap_model_id_t id);

/* Look up by short name, case-sensitive. Returns NULL if unknown. */
[[nodiscard]] const ap_model_t *ap_model_by_name(const char *name);

/* Number of entries in the table; equals AP_MODEL_COUNT. */
[[nodiscard]] size_t ap_model_count(void);

#endif /* APOLLO_MODEL_AP_MODEL_H */

#include "model/ap_model.h"

#include <string.h>

/* Sources for this table:
 *
 *  [S3K]  Domain Series 3000/4000 Technical Reference (008778-03, Aug 87),
 *         Table 2-8 "64-MB Physical Address Space Allocation" and section
 *         1.5.4 "Network Controller".
 *  [CFG]  HP-Apollo Products Configuration Guide (Dec 89). Cited by its
 *         page-level "Product Summary" sections, which give ordering-level CPU
 *         and FPU part numbers and clocks, and by the "HP-Apollo Workstation
 *         Specifications and Graphics Options" overview table at p. A-11.
 *  [QREF] Apollo Quick-Reference Configuration Guide (5952-2149, Jul 90).
 *  [MAME] ext/mame src/mame/apollo/apollo.cpp machine configurations, used as
 *         a cross-check only -- the oracle is a model, not the hardware.
 *
 * DN4500 and DN2500 have no runnable oracle, so every figure for them comes from
 * [CFG] rather than from measurement. */
static const ap_model_t k_models[AP_MODEL_COUNT] = {
    [AP_MODEL_DN2500] = {
        .id = AP_MODEL_DN2500,
        .name = "dn2500",
        .description = "DN2500 low-cost integrated workstation",
        /* "32-bit MC68030 20 MHz CPU with MC68882 20 MHz Floating Point
         * Processor ... On-board monochrome graphics ... SCSI Bus supporting up
         * to 7 devices" -- [CFG] Series 2500 Product Summary. RAM 4-16 MB and
         * the 15" mono 1024x800 / 19" mono 1280x1024 panels -- [CFG] p. A-11. */
        .cpu = AP_CPU_M68030,
        .cpu_hz = 20000000u,
        .mmu = AP_MMU_M68030,
        .fpu = AP_FPU_M68882,
        .display = AP_DISPLAY_MONO_1024X800,
        .oracle = AP_ORACLE_PAPER_ONLY,
        .ram_base = 0x1000000u,
        .ram_max_bytes = 0x1000000u, /* 4-16 MB [CFG] p. A-11 */
        .has_ring = true,
        .provisional = "ram_base: assumed to match the other 68030 models at "
                       "0x1000000, but the DN2500 is a single integrated board "
                       "with its own memory design and no address-space table "
                       "for it has been found. Close it from the 2500 boot PROM "
                       "or a Series 2500 hardware reference",
    },
    [AP_MODEL_DN3000] = {
        .id = AP_MODEL_DN3000,
        .name = "dn3000",
        .description = "DN3000 workstation, 68020 with external PMMU",
        .cpu = AP_CPU_M68020,
        .cpu_hz = 12000000u,
        .mmu = AP_MMU_M68851,
        .fpu = AP_FPU_M68881,
        .display = AP_DISPLAY_MONO_1024X800,
        .oracle = AP_ORACLE_MAME,
        .ram_base = 0x100000u,
        .ram_max_bytes = 0x800000u, /* 0x100000-0x8fffff = 8 MB [S3K] */
        .has_ring = true,
        .provisional = nullptr,
    },
    [AP_MODEL_DN3500] = {
        .id = AP_MODEL_DN3500,
        .name = "dn3500",
        .description = "DN3500 workstation, 68030 (reference superset)",
        .cpu = AP_CPU_M68030,
        .cpu_hz = 25000000u,
        .mmu = AP_MMU_M68030,
        .fpu = AP_FPU_M68882,
        .display = AP_DISPLAY_MONO_1024X800,
        .oracle = AP_ORACLE_MAME,
        .ram_base = 0x1000000u,
        .ram_max_bytes = 0x2000000u, /* 8-32 MB supported [CFG] */
        .has_ring = true,
        .provisional = nullptr,
    },
    [AP_MODEL_DN4500] = {
        .id = AP_MODEL_DN4500,
        .name = "dn4500",
        .description = "DN4500 workstation, faster 68030 with Matrox graphics",
        /* "32-bit MC68030 33 MHz CPU with MC68882 33 MHz Floating Point
         * Processor" -- [CFG] Series 4500 Product Summary p. D-108, corroborated
         * by [CFG]'s narrative "the 33MHz MC68030".
         *
         * [CFG]'s own overview table at p. A-11 says "MC68030@30MHZ" for Series
         * 4500. 33 MHz is taken as correct: two independent statements against
         * one, the ordering-level summary outranks the marketing summary, and
         * Motorola never binned a 30 MHz 68030 (16/20/25/33/40/50). Recorded as
         * a resolved discrepancy in docs/PROJECT_STATUS.md rather than silently
         * dropped -- if a probe ever contradicts 33 MHz, that table is the
         * reason to revisit. */
        .cpu = AP_CPU_M68030,
        .cpu_hz = 33000000u,
        .mmu = AP_MMU_M68030,
        .fpu = AP_FPU_M68882,
        .display = AP_DISPLAY_MONO_1280X1024, /* Series 4500 mono panel [CFG] */
        .oracle = AP_ORACLE_PAPER_ONLY,
        .ram_base = 0x1000000u,
        .ram_max_bytes = 0x2000000u, /* 4-32 MB [CFG]; Table 2-8 DS4000 [S3K] */
        .has_ring = true,
        .provisional = nullptr,
    },
    [AP_MODEL_DN5500] = {
        .id = AP_MODEL_DN5500,
        .name = "dn5500",
        .description = "DN5500 workstation, 68040",
        .cpu = AP_CPU_M68040,
        .cpu_hz = 25000000u,
        .mmu = AP_MMU_M68040,
        .fpu = AP_FPU_M68040,
        .display = AP_DISPLAY_MONO_1024X800,
        .oracle = AP_ORACLE_MAME,
        .ram_base = 0x1000000u,
        .ram_max_bytes = 0x2000000u, /* 16-32 MB [CFG] */
        .has_ring = true,
        .provisional = nullptr,
    },
    [AP_MODEL_DSP3000] = {
        .id = AP_MODEL_DSP3000,
        .name = "dsp3000",
        .description = "DSP3000 headless server, DN3000 board without display",
        .cpu = AP_CPU_M68020,
        .cpu_hz = 12000000u,
        .mmu = AP_MMU_M68851,
        .fpu = AP_FPU_M68881,
        .display = AP_DISPLAY_NONE,
        .oracle = AP_ORACLE_MAME,
        .ram_base = 0x100000u,
        .ram_max_bytes = 0x800000u,
        .has_ring = true,
        .provisional = nullptr,
    },
    [AP_MODEL_DSP3500] = {
        .id = AP_MODEL_DSP3500,
        .name = "dsp3500",
        .description = "DSP3500 headless server, DN3500 board without display",
        .cpu = AP_CPU_M68030,
        .cpu_hz = 25000000u,
        .mmu = AP_MMU_M68030,
        .fpu = AP_FPU_M68882,
        .display = AP_DISPLAY_NONE,
        .oracle = AP_ORACLE_MAME,
        .ram_base = 0x1000000u,
        .ram_max_bytes = 0x2000000u,
        .has_ring = true,
        .provisional = nullptr,
    },
    [AP_MODEL_DSP4500] = {
        .id = AP_MODEL_DSP4500,
        .name = "dsp4500",
        .description = "DSP4500 headless server, DN4500 board without display",
        /* Same "MC68030 33 MHz CPU with MC68882 33 MHz" processor-I/O board as
         * the DN4500 -- [CFG] Series 4500 server Product Summary. Its heading
         * reads "DSP4500 Monochrome Workstation", which is a copy-paste of the
         * DN4500 page: the DSP4500 country kit (DSPCK-*) contains only a power
         * cord, where the DN4500's (DN3CK-*) includes keyboard, keyboard cable
         * and mouse. Headless, in line with every other DSP. */
        .cpu = AP_CPU_M68030,
        .cpu_hz = 33000000u,
        .mmu = AP_MMU_M68030,
        .fpu = AP_FPU_M68882,
        .display = AP_DISPLAY_NONE,
        .oracle = AP_ORACLE_PAPER_ONLY,
        .ram_base = 0x1000000u,
        .ram_max_bytes = 0x2000000u,
        .has_ring = true,
        .provisional = nullptr,
    },
    [AP_MODEL_DSP5500] = {
        .id = AP_MODEL_DSP5500,
        .name = "dsp5500",
        .description = "DSP5500 headless server, DN5500 board without display",
        .cpu = AP_CPU_M68040,
        .cpu_hz = 25000000u,
        .mmu = AP_MMU_M68040,
        .fpu = AP_FPU_M68040,
        .display = AP_DISPLAY_NONE,
        .oracle = AP_ORACLE_MAME,
        .ram_base = 0x1000000u,
        .ram_max_bytes = 0x2000000u,
        .has_ring = true,
        .provisional = nullptr,
    },
};

const ap_model_t *ap_model_by_id(ap_model_id_t id) {
  if ((size_t)id >= (size_t)AP_MODEL_COUNT) {
    return nullptr;
  }
  return &k_models[id];
}

const ap_model_t *ap_model_by_name(const char *name) {
  if (name == nullptr) {
    return nullptr;
  }
  for (size_t i = 0; i < (size_t)AP_MODEL_COUNT; ++i) {
    if (strcmp(k_models[i].name, name) == 0) {
      return &k_models[i];
    }
  }
  return nullptr;
}

size_t ap_model_count(void) { return (size_t)AP_MODEL_COUNT; }

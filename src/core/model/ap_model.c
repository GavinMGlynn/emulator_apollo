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
        .board_of = AP_MODEL_DN2500,
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
        .ram_base = 0x4000000u,
        .ram_max_bytes = 0x1000000u, /* 4-16 MB [CFG] p. A-11 */
        .has_ring = true,
        .has_active_low_parity_lanes = true,
        .provisional = "has_active_low_parity_lanes: true, on the oracle's "
                       "split alone. Both DN3000 PROMs write F8 to force bad "
                       "parity and the three Series 4000 PROMs write 08, so "
                       "the two families are settled from firmware; "
                       "2500_BOOT_16182_8 makes no such write anywhere, so "
                       "nothing here tests it. "
                       "ram_base and ram_max_bytes are no longer provisional: "
                       "the Series 2500 boot PROM sizes its own memory and "
                       "both constants are in that code. 2500_BOOT_16182_8 "
                       "resets to PC 0001F040, and its address-line walk ORs "
                       "the base into each address it probes -- OR.L "
                       "#$04000000,D1 at 1F49A -- then masks the walking "
                       "pattern with ANDI.L #$04FFFFFF,D1 at 1F4CE and again "
                       "at 1F4FA. A base of 04000000 with a 00FFFFFF offset "
                       "mask is a 16 MB region at 04000000, which is what the "
                       "table holds and what [CFG] p. A-11's 4-16 MB says "
                       "independently. The reset SSP 040007D0 agrees a third "
                       "time. No Series 2500 allocation table exists on disk "
                       "or on the web, and the oracle has no 2500 driver, so "
                       "the firmware is the primary source here rather than a "
                       "fallback",
    },
    [AP_MODEL_DN3000] = {
        .id = AP_MODEL_DN3000,
        .board_of = AP_MODEL_DN3000,
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
        .board_of = AP_MODEL_DN3500,
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
        .has_address_translation_map = true,
        .has_active_low_parity_lanes = true,
        .provisional = nullptr,
    },
    [AP_MODEL_DN3550] = {
        .id = AP_MODEL_DN3550,
        /* Its own board. `board_of` is not "shares a board family" -- it is
         * "is the headless variant of", which `model_suite` asserts outright:
         * a machine with a display that pointed elsewhere would be saying its
         * own row is not the authority on itself. The first draft of this entry
         * borrowed the DN3500's board to inherit its memory strap and the
         * invariant caught it; the strap rows are added to
         * `ap_sio_ram_config_byte` instead, which is what the DN4500 needed for
         * the same reason. */
        .board_of = AP_MODEL_DN3550,
        .name = "dn3550",
        .description = "DN3550 workstation, DN3500 board with the 19-inch panel",
        /* "CPU: MC68030, clocked at 25 MHz" and "Floating Point Processor:
         * MC68882 clocked at 25 MHz, is standard" -- [CFG] Model 3550
         * Monochrome Workstation, p. D-77, and its Product Summary p. D-78
         * repeats both in the ordering line "32-bit MC68030 25 MHz CPU with
         * MC68882 25 MHz Floating Point Processor". Identical to the DN3500. */
        .cpu = AP_CPU_M68030,
        .cpu_hz = 25000000u,
        .mmu = AP_MMU_M68030,
        .fpu = AP_FPU_M68882,
        /* **The one thing that differs from a DN3500**, and it is the reason
         * this model waited for Phase 5: "Monitor: 19-inch 1280 by 1024 inch,
         * Monochrome Monitor" (p. D-77), with Opt. DM0 "1280 by 1024
         * monochrome graphics controller" and Opt. FM2 the 19-inch display.
         * The DN3500's base panel is the 15-inch 1024x800. */
        .display = AP_DISPLAY_MONO_1280X1024,
        /* MAME registers no 3550 of any kind, so there is nothing to diff
         * against and every figure here is the configuration guide's. */
        .oracle = AP_ORACLE_PAPER_ONLY,
        .ram_base = 0x1000000u,
        /* "RAM: 8-MB or 16-MB parity, expandable to 32-MB" -- p. D-77, with
         * Opt. H02 and H04 as the two base sizes. */
        .ram_max_bytes = 0x2000000u,
        /* Opt. G01 "Apollo Token Ring network controller" is on its options
         * list, exactly as the DN3500's is. */
        .has_ring = true,
        .has_address_translation_map = true,
        .has_active_low_parity_lanes = true,
        .provisional = nullptr,
    },
    [AP_MODEL_DN4500] = {
        .id = AP_MODEL_DN4500,
        .board_of = AP_MODEL_DN4500,
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
        .has_address_translation_map = true,
        .has_active_low_parity_lanes = true,
        .provisional = nullptr,
    },
    [AP_MODEL_DN5500] = {
        .id = AP_MODEL_DN5500,
        .board_of = AP_MODEL_DN5500,
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
        .has_address_translation_map = true,
        .has_active_low_parity_lanes = true,
        .provisional = nullptr,
    },
    [AP_MODEL_DSP3000] = {
        .id = AP_MODEL_DSP3000,
        .board_of = AP_MODEL_DN3000,
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
        .board_of = AP_MODEL_DN3500,
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
        .has_address_translation_map = true,
        .has_active_low_parity_lanes = true,
        .provisional = nullptr,
    },
    [AP_MODEL_DSP3550] = {
        .id = AP_MODEL_DSP3550,
        .board_of = AP_MODEL_DN3550,
        .name = "dsp3550",
        .description = "DSP3550 headless server, DN3550 board without display",
        /* `[CFG]` Model 3550 Server, p. D-96: the same processor-I/O board as
         * the workstation -- "MC68030, clocked at 25 MHz", "MC68882 clocked at
         * 25 MHz, is standard", "8-MB or 16-MB parity, expandable to 32-MB" --
         * and its Product Summary p. D-97 repeats the ordering line. The page
         * lists no monitor at all, which is what makes it the server. */
        .cpu = AP_CPU_M68030,
        .cpu_hz = 25000000u,
        .mmu = AP_MMU_M68030,
        .fpu = AP_FPU_M68882,
        .display = AP_DISPLAY_NONE,
        .oracle = AP_ORACLE_PAPER_ONLY,
        .ram_base = 0x1000000u,
        .ram_max_bytes = 0x2000000u,
        .has_ring = true,
        .has_address_translation_map = true,
        .has_active_low_parity_lanes = true,
        .provisional = nullptr,
    },
    [AP_MODEL_DSP4500] = {
        .id = AP_MODEL_DSP4500,
        .board_of = AP_MODEL_DN4500,
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
        .has_address_translation_map = true,
        .has_active_low_parity_lanes = true,
        .provisional = nullptr,
    },
    [AP_MODEL_DSP5500] = {
        .id = AP_MODEL_DSP5500,
        .board_of = AP_MODEL_DN5500,
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
        .has_address_translation_map = true,
        .has_active_low_parity_lanes = true,
        .provisional = nullptr,
    },
};

ap_cpu_features_t ap_cpu_features(ap_cpu_t cpu) {
  switch (cpu) {
  case AP_CPU_M68020:
    return (ap_cpu_features_t){
        /* "A direct-mapped cache of 64 long word entries": 256 bytes, one long
         * word to a line, and no data cache at all. */
        .instruction_cache_bytes = 256,
        .instruction_cache_line_longs = 1,
        .data_cache_bytes = 0,
        .has_onchip_mmu = false,   /* external 68851 */
        .has_synchronous_bus = false,
        .has_burst_fill = false,
        .has_module_calls = true,  /* CALLM and RTM */
    };
  case AP_CPU_M68030:
    return (ap_cpu_features_t){
        /* 256 bytes each, sixteen lines of four long words, and a burst that
         * fills a whole line in one bus tenure. */
        .instruction_cache_bytes = 256,
        .instruction_cache_line_longs = 4,
        .data_cache_bytes = 256,
        .has_onchip_mmu = true,
        .has_synchronous_bus = true,
        .has_burst_fill = true,
        .has_module_calls = false,
    };
  case AP_CPU_M68040:
    return (ap_cpu_features_t){
        /* Four kilobytes each, four-long-word lines, four-way set associative.
         * The organisation beyond size is a Phase 2b concern; the sizes are
         * here so a DN5500 does not silently inherit the 68030's. */
        .instruction_cache_bytes = 4096,
        .instruction_cache_line_longs = 4,
        .data_cache_bytes = 4096,
        .has_onchip_mmu = true,
        .has_synchronous_bus = true,
        .has_burst_fill = true,
        .has_module_calls = false,
    };
  }
  /* Unreachable for a valid `ap_cpu_t`; the 68030 is the reference superset. */
  return ap_cpu_features(AP_CPU_M68030);
}

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

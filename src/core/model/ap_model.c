#include "model/ap_model.h"

#include <string.h>
#include "cpu/m68030/ap_m68030_cache.h"
#include "cpu/m68030/ap_m68030_ssw.h"

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
        /* **`1024 x 800`, and the citation is here because its absence nearly
         * cost a wrong change.** `[S3K]` §11's three monitors are "15-inch
         * colour 1024 x 800 at 60 Hz, 19-inch colour 1024 x 800 at 60 Hz" and
         * §10.1's 4-plane controller is "1024 x 800 x 4" -- so **1024 x 800 is
         * this board family's resolution and 15 or 19 inches is a size**.
         *
         * `[CFG]`'s Series 3500 description block reads "Monitor: 19-inch, 1280
         * by 1024, 64-Hz Monochrome Monitor", which looks like a contradiction
         * and is not: the same guide's Series 3500 options list carries
         * `Opt. FM2` "19" monochrome graphics display ... (**Requires option
         * DM0**)", and DM0 is the *1280 by 1024 graphics controller*. The
         * description block describes a configured system, and the thing that
         * changes the resolution is a different controller -- which is exactly
         * what the `DN3550` row below is, and why it cites both options.
         *
         * A hardware manual beats a configuration guide for a hardware fact,
         * and an uncited field in the reference row is what invites a
         * plausible wrong correction. */
        .display = AP_DISPLAY_MONO_1024X800,
        .oracle = AP_ORACLE_MAME,
        .ram_base = 0x1000000u,
        /* "RAM: 4-MB or 8-MB parity, expandable to 32-MB" -- `[CFG]`'s Series
         * 3500 description block; the two base sizes are Opt. H01 and H02. */
        .ram_max_bytes = 0x2000000u,
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
        /* **Two DN4500 hardware features this table has no field for**, named
         * by `Apollo_Price_List_Jul88`'s model block and by nothing else on the
         * shelf: a **64 KB physical cache** and **8-32 MB two-way interleaved**
         * memory.
         *
         * `has_virtual_cache` above is the DS4000's *virtual* cache, argued
         * from `[S3K]`'s block diagrams and bus topology because no sentence
         * names the models; a **physical** cache on a later board is a
         * different part in a different place. Neither it nor the interleave is
         * modelled.
         *
         * **Recorded as a named gap rather than added**, because a field with
         * no behaviour behind it is worse than an absence: both of these are
         * *timing* features, and this core's timing work is on the DN3500,
         * where the oracle is. The cost to close is a cache model at the board
         * level and an interleaved memory timing path, and the trigger is
         * anyone measuring a DN4500.
         *
         * *A price list is a selling document*, which is why this is a gap and
         * not a figure. What would settle both is `007861-A01`, the same
         * unobtainable handbook `has_virtual_cache` names above — and which
         * `005809-A00` §1.4.2 and `000959-A00`'s Related Manuals both cite by
         * order number, so it exists and is not public. */
        .oracle = AP_ORACLE_PAPER_ONLY,
        .ram_base = 0x1000000u,
        .ram_max_bytes = 0x2000000u, /* 4-32 MB [CFG]; Table 2-8 DS4000 [S3K] */
        .has_ring = true,
        .has_address_translation_map = true,
        .has_active_low_parity_lanes = true,
        .provisional = nullptr,
    },
    [AP_MODEL_DN4000] = {
        .id = AP_MODEL_DN4000,
        .board_of = AP_MODEL_DN4000,
        .name = "dn4000",
        .description = "DN4000 (DS4000) workstation, 25 MHz 68020 with 68851",
        /* ## Two names, and the sources use different ones
         *
         * `002398-04` Figure 1-2 is headed "**DS4000** Functional Block Diagram"
         * and `019411-A00` §4.2.1.4 groups "DS3500, DS4000, DS4500, DS5500";
         * Datapro's 1988 report calls the same machine the "**DN4000** Personal
         * Super Workstation". This table uses `dn` throughout -- the DS3500 is
         * `dn3500` here -- so the row is `dn4000` and the description carries
         * both.
         *
         * ## Where each field comes from, because they are not all equal
         *
         * `cpu`, `mmu` and `fpu` are `002398-04` **Figure 1-2**, which names the
         * parts: MC68020, MC68851 PMMU, MC68881 FPU. That figure names parts and
         * not figures, so it supplies nothing numeric.
         *
         * `cpu_hz` and `ram_max_bytes` have **two independent sources that
         * agree**: §1's prose gives the DS4000 a 25-MHz 68020 and 4-32 MB, and
         * Datapro's per-model table gives the DN4000 `25MHz` and `4M`/`32M`. It
         * is that agreement, on fields both documents state, that makes the
         * third field below usable at all.
         *
         * `display` is **Datapro alone** -- 1,280 x 1,024, colour, "16.7
         * million/256" -- and is the weakest thing in this row. No hardware
         * manual states a display per model: `008778-03` §1.5.3 describes three
         * graphics boards without saying which machine takes which, which is
         * why this row was blocked rather than merely unwritten. A market report
         * is weaker evidence than a manual and its purpose was selling machines,
         * so this is the field to revisit first if a probe or a manual ever
         * disagrees.
         *
         * **Not modelled, and deliberately**: Datapro gives "3 serial RS-232-C
         * standard" where Figure 1-2 shows four lines. Those reconcile as three
         * user ports plus the keyboard's -- this core's two 2681s carry four
         * with line 0 the keyboard -- but the reconciliation is a reading and
         * the table has no serial-count field to put it in. Recorded here
         * instead of encoded. */
        .cpu = AP_CPU_M68020,
        .cpu_hz = 25000000u,
        .mmu = AP_MMU_M68851,
        .fpu = AP_FPU_M68881,
        .display = AP_DISPLAY_COLOR_1280X1024,
        .oracle = AP_ORACLE_PAPER_ONLY,
        .ram_base = 0x1000000u,
        .ram_max_bytes = 0x2000000u, /* 4-32 MB, Figure 1-2 and Datapro agree */
        .has_ring = true,
        .has_address_translation_map = true,
        .has_active_low_parity_lanes = true,
        /* The one model with `[S3K]` §1.3.1's virtual cache: Figure 1-2 draws
         * it and the write buffer onto this machine's logical bus, beside the
         * separate 68851 that a 68030 model does not have. See
         * `ap_model.h`'s field and `board/ap_cacheram.h`. */
        .has_virtual_cache = true,
        .provisional = "display resolution is a 1988 market report's, not a "
                       "manual's -- no hardware document states one per model",
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
        /* **64 MB, and the source is the release that introduced the machine.**
         * This was `0x2000000u` citing `[CFG]` -- the *HP-Apollo Products
         * Configuration Guide* of **December 1989** and its July 1990 quick
         * reference, both of which predate the DS5500 by two years and cannot
         * be describing its final configuration.
         *
         * `018901-A00`, the SR10.4 release notes of March 1992, §1.4.1: "All
         * memory modules shipped with the DN3500, DN3550 and DN4500
         * workstations are also supported. In addition, a **new 16-MB memory
         * module has been added which gives the DN5500 a total memory capacity
         * of 64 MB**." SR10.4 is the release that added DS5500 support, so this
         * is the machine's own document rather than a guide that predates it.
         *
         * Nothing in this core defaults to the maximum -- a DS5500 boot runs
         * with 16 Mbyte -- so this raises a ceiling rather than changing a
         * machine. */
        .ram_max_bytes = 0x4000000u,
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
        /* 64 MB, its workstation sibling's -- see the DN5500 row. */
        .ram_max_bytes = 0x4000000u,
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
        .has_cache_maintenance = false,
        .has_68040_mmu_registers = false,
        .has_cache_address_register = true,
        /* `[020]` Figure 7-2: C, CE, F, E at 3-0 and zero above. The four
         * actions of the one cache this part has. */
        .cacr_implemented_mask = AP_M68030_CACR_MASK_68020,
        /* `[020]` Figure 6-8 and Figure 6-9's summary: "1011  MC68020 Long Bus
         * Fault (44 Words)". */
        .long_bus_fault_frame_words = 44u,
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
        .has_cache_maintenance = false,
        .has_68040_mmu_registers = false,
        .has_cache_address_register = true,
        /* `[030]` Figure 6-3: the 68020's four with an `I` suffix, plus a
         * data-cache set at 13-8 and a burst enable at 4. */
        .cacr_implemented_mask = AP_M68030_CACR_MASK_68030,
        .long_bus_fault_frame_words = 46u,
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
        .has_cache_maintenance = true,
        .has_68040_mmu_registers = true,
        /* Lost, not gained: `M68000PRM`'s MOVEC table footnotes CAAR "For the
         * MC68020 and MC68030 only". */
        .has_cache_address_register = false,
        /* `[040]` Figure 4-4: `DE` at 31 and `IE` at 15, everything else
         * undefined. No clear and no freeze -- `CINV` and `CPUSH` do that,
         * which is why §4.2 says the caches must be cleared before enabling. */
        .cacr_implemented_mask = AP_M68030_CACR_MASK_68040,
        /* **Zero, because the 68040 has no format `$A` or `$B` at all** --
         * `[040]` §8.1 gives it five formats against the 68020/68030's six, and
         * §8.3/§8.4 give it a **format `$7` access error frame** instead. This
         * row carried the 68030's 46 "until the 68040 exception item reaches
         * it"; it has. Zero is what selects the 68040's frame set, and a
         * caller that asks a 68040 for a long frame's size gets an answer that
         * says the question was wrong. */
        .long_bus_fault_frame_words = 0u,
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

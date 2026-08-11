/* Deterministic frontend: no wall clock, no host input, no threads.
 *
 * Everything shared with the interactive frontend -- naming, the model table
 * report, and the options both accept -- lives in ../common. This file holds
 * only what is specific to running a machine headlessly.
 *
 * Presently it can only report the machine configuration it would build. The
 * flags that make this the engine of the verification methodology (run N
 * cycles, dump state, --dump-mem, screenshots, scripted input, ring trace) are
 * added alongside the subsystems they observe. */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "ap_frontend.h"
#include "probe/ap_probe.h"

#include <stdlib.h>

#include "device/ap_qic.h"
#include "image/ap_ct.h"
#include "image/ap_afd.h"
#include "image/ap_awd.h"
#include "image/ap_volume.h"
#include "board/ap_board.h"
#include "board/ap_sio.h"
#include "device/ap_mc68681.h"
#include "board/ap_graphics.h"
#include "ap_png.h"
#include "device/ap_bt458.h"
#include "device/ap_kbd.h"
#include "machine/ap_machine.h"
#include "model/ap_quirk.h"
#include "ring/ap_ring_probe.h"

static void print_usage(const char *program_name) {
  ap_print_common_usage(stdout, program_name);
  /* Headless-only flags are listed here as they are implemented. */
  fprintf(stdout,
          "  --run-probes          run the built-in probe suite and report\n"
          "  --run-ring-probes     run the cross-node ring probes and report:\n"
          "                        token round trip, latency per node, contention\n"
          "  --probe-file PATH     run one probe described by a file, so a\n"
          "                        probe can come from outside this binary\n"
          "  --time-instructions   report per-instruction clocks, for oracle\n"
          "                        comparison\n"
          "  --boot-limit N        stop a boot after N instructions, to find\n"
          "                        where one goes wrong\n"
          "  --boot-stop-physical-pc ADDR[:LEN]\n"
          "                        the same stop, matched on the address the\n"
          "                        bus carried -- for code found by searching\n"
          "                        memory, whose logical address is unknown\n"
          "  --boot-stop-on-watch-write N\n"
          "                        end the run on the Nth write to the watched\n"
          "                        address, so a ring holds the instruction\n"
          "  --dump-logical ADDR[:LEN]\n"
          "                        the same dump, of the address the *program*\n"
          "                        named: translated as an access would be\n"
          "  --boot-watch-write ADDR\n"
          "                        remember the last write to ADDR and which\n"
          "                        instruction made it, and report both\n"
          "  --boot-watch-read ADDR\n"
          "                        the same on the read side, with the value\n"
          "                        the machine answered -- \"it read there and\n"
          "                        got zero\" and \"it never read there\" are the\n"
          "                        two hypotheses a count alone cannot separate\n"
          "  --boot-stop-on-watch-read N\n"
          "                        end the run on the Nth read of the watched\n"
          "                        address, so a kept trace holds what led there\n"
          "  --boot-stop-on-disk-refusal\n"
          "                        end the run the first time the disk\n"
          "                        controller refuses an address, so a trace\n"
          "                        ring holds what computed it\n"
          "  --service-mode        set the Normal/Service switch to Service, which\n"
          "                        is what reaches the Mnemonic Debugger\n"
          "  --boot-progress N     report the step count and the program\n"
          "                        counter to stderr every N instructions, so\n"
          "                        a run that says nothing for ten minutes can\n"
          "                        be told from one that is stuck\n"
          "  --boot-stop-pc ADDR[:LEN]\n"
          "                        stop the run the first time the program\n"
          "                        counter is ADDR, so a kept trace holds what\n"
          "                        led there rather than what followed\n"
          "");
  /* Split because a single literal outgrew C99's guaranteed 4095 characters,
   * which `-Werror` catches. Two calls, one list. */
  fprintf(stdout,
          "  --boot-stop-on-mmu-fault-at ADDR\n"
          "                        end the run when translation *refuses* this\n"
          "                        logical address. An instruction that faults,\n"
          "                        recovers and faults again later cannot be\n"
          "                        caught by counting visits to its PC: the\n"
          "                        visits that succeed look identical until the\n"
          "                        access is made\n"
          "  --boot-progress-from ADDR\n"
          "                        count --boot-progress from the first time\n"
          "                        ADDR executes rather than from reset, so two\n"
          "                        machines that reach the same code at\n"
          "                        different absolute counts sample the *same*\n"
          "                        instants and their PCs can be compared\n"
          "  --boot-stop-pc-then N run N more instructions after the stop\n"
          "                        address is reached, then end. With a trace\n"
          "                        ring this holds the window *after* an event\n"
          "                        rather than the one before it, which is what\n"
          "                        a comparison against another machine's run\n"
          "                        from the same point needs\n"
          "");
  /* Split again, for the same reason and by the same rule. */
  fprintf(stdout,
          "");
  /* Split again, by the same 4095-character rule. */
  fprintf(stdout,
          "  --oracle-quirk NAME   run one deliberate divergence the *oracle's*\n"
          "                        way instead of the reference documentation's,\n"
          "                        so a state comparison can be carried past a\n"
          "                        difference instead of drowning in its\n"
          "                        consequences. The default is always the\n"
          "                        documented behaviour\n"
          "  --list-oracle-quirks  name each one, with what the reference says\n"
          "                        and what MAME does instead\n"
          "");
  /* And once more. */
  fprintf(stdout,
          "  --dump-state FILE     write every field of machine state the hash\n"
          "                        covers, one line per field, and print the\n"
          "                        hash of that same walk beside it. The two\n"
          "                        numbers agreeing is what makes the dump\n"
          "                        trustworthy: it is the identity harness's\n"
          "                        own traversal with an output attached, not a\n"
          "                        second walker that could visit different\n"
          "                        fields and show two machines agreeing while\n"
          "                        their hashes differ\n"
          "  --boot-type-await-pushback\n"
          "                        hold each typed character until the boot\n"
          "                        PROM's one-byte type-ahead slot is empty.\n"
          "                        The firmware drains a character from the\n"
          "                        receive FIFO before every character it\n"
          "                        prints, looking for an XOFF, and drops what\n"
          "                        it pushed back if that slot is full -- so a\n"
          "                        sender pacing on an empty receive buffer\n"
          "                        outruns it and loses characters silently\n"
          "  --boot-type-then TEXT\n"
          "  --boot-type-then-after-pc ADDR\n"
          "                        a second typed phase with its own arming\n"
          "                        address, sent once the first is spent. The\n"
          "                        Mnemonic Debugger needs two characters\n"
          "                        *before* its banner and the command *after*\n"
          "                        it, and the firmware discards whatever is\n"
          "                        typed while it prints -- so one string with\n"
          "                        one gate cannot express the dialogue\n"
          "  --boot-type-after-pc ADDR\n"
          "                        hold the typed dialogue until the program\n"
          "                        counter first reaches ADDR, then send it. A\n"
          "                        one-shot trigger, not a threshold, because a\n"
          "                        prompt inside the boot PROM sits *below*\n"
          "                        every threshold rather than above one: the\n"
          "                        Mnemonic Debugger runs at 0000xxxx. Without\n"
          "                        it a keyboard dialogue is delivered in the\n"
          "                        first tenth of an emulated second, long\n"
          "                        before the firmware selects a console, and\n"
          "                        every character but the last is gone\n"
          "  --boot-stop-pc-skip N ignore the first N times the stop address\n"
          "                        is reached. An address executed once on a\n"
          "                        path that recovers and again on one that\n"
          "                        does not is two events, and only the second\n"
          "                        is usually the question\n"
          "  --boot-trace-last N   keep the last N steps and print them when\n"
          "                        the run ends. A fault half a billion\n"
          "                        instructions in cannot be reached by\n"
          "                        printing every step\n"
          "  --boot-trace          report pc and a7 per step: a7 is where a\n"
          "                        stack goes wrong, pc only where it shows\n"
          "  --boot-watch ADDR     with --boot-trace, report the long word at\n"
          "                        ADDR after every step. MEMORY ONLY: it reads\n"
          "                        through the board, so a device register\n"
          "                        would be perturbed by being watched\n"
          "  --boot-input TEXT     deliver TEXT to serial port 2 as the\n"
          "                        firmware reads it; scripted, not host input\n"
          "  --boot-script FILE    a console dialogue: lines of `expect TEXT`\n"
          "                        and `send TEXT`, in order. Waits for what the\n"
          "                        machine says before answering, which a fixed\n"
          "                        --boot-input cannot do\n"
          "");
  /* Split here only because ISO C99 guarantees just 4095 characters in a
   * string literal, and this list passed it. No flag is grouped by meaning
   * across the break. */
  fprintf(stdout,
          "  --boot-console        print what the machine transmits on either\n"
          "                        serial port: its own console output\n"
          "  --boot-input-port N   which serial port --boot-input feeds, 1 or\n"
          "                        2 (default 2)\n"
          "  --boot-input-rate CSR clock select the scripted terminal sends at\n"
          "                        (default 0xBB, 9600 baud, which is what\n"
          "                        makes the boot PROM answer)\n"
          "  --boot-input-interval US  emulated microseconds between scripted\n"
          "                        characters; the wire's own floor if 0\n"
          "  --boot-type TEXT      type TEXT on the keyboard, one character each\n"
          "  --boot-report         print the input path end to end at exit: the\n"
          "                        port, the receiver, the interrupt and its mask,\n"
          "                        the controller and the keyboard. Every question\n"
          "                        of the form \"why did my keystroke do nothing\"\n"
          "                        this session was answerable from these lines\n"
          "  --ram MB              megabytes of main memory to fit. Defaults to\n"
          "                        16, or the model's maximum if that is less --\n"
          "                        a size the model cannot be built in leaves the\n"
          "                        boot PROM's sizing strap unset\n"
          "  --clock DATE          the instant the machine powers on, as\n"
          "                        YYYY-MM-DD or YYYY-MM-DDTHH:MM:SS. A volume\n"
          "                        written later than the clock makes Domain/OS stop\n"
          "                        and say the calendar is slow -- and it is right\n"
          "  --boot-type-after-os  hold --boot-type until code above the firmware and\n"
          "                        its loaded diagnostics is running: the PROM is at\n"
          "                        0000xxxx and SELF_TEST at 0100xxxx, so this types\n"
          "                        at the operating system, not into a self-test\n"
          "                        time the machine settles into an input poll --\n"
          "                        which is what a prompt deep inside an operating\n"
          "                        system needs and a step number cannot give\n"
          "  --boot-key N          press and release keyboard key N (a matrix\n"
          "                        index 0-7F, not a character). Delivered when the\n"
          "                        firmware is polling for input, not merely able\n"
          "                        to take it\n"
          "  --screen KIND         fit a display: c4p, c8p, 19i or 15i\n"
          "  --screenshot FILE     scan the fitted screen out to a PNG\n"
          "  --disk FILE           fit a Winchester (.awd) to the boot\n"
          "  --calendar-ram FILE   the calendar's battery-backed RAM, loaded at\n"
          "                        reset and written back at the end: fifty bytes\n"
          "                        of configuration, and not the clock. A missing\n"
          "                        file is a machine that has never been configured\n"
          "  --dump-mem A[:L]      dump memory through the board after a\n"
          "                        run; hex address, hex length (default 100)\n"
          "  --floppy FILE         read an .afd through the reader and\n"
          "                        report its geometry\n"
          "  --boot-input-channel C  which channel, A or B (default A). The\n"
          "                        keyboard is port 1 channel A; a terminal is\n"
          "                        port 1 channel B\n");
}

/* The probes' RAM. Static rather than automatic because it is large, and
 * supplied by the caller because the core allocates nothing. */
#define PROBE_RAM_BYTES 0x00010000u
static uint8_t probe_ram[PROBE_RAM_BYTES];

/* The MMU register a `PMOVE` addressed, for the report. Named rather than
 * numbered because the whole value of an MMU load log is being able to read
 * "CRP <- ..." at a glance and see which of the two root pointers moved. */
static const char *ap_mmu_register_name(uint8_t which) {
  switch ((ap_m68030_mmu_register_t)which) {
  case AP_M68030_MMU_TC: return "TC";
  case AP_M68030_MMU_SRP: return "SRP";
  case AP_M68030_MMU_CRP: return "CRP";
  case AP_M68030_MMU_TT0: return "TT0";
  case AP_M68030_MMU_TT1: return "TT1";
  case AP_M68030_MMU_MMUSR: return "MMUSR";
  case AP_M68030_MMU_NONE: break;
  }
  return "?";
}

/* Which empty-slot addresses a run touched, distinct and in the order first
 * seen. The count alone cannot tell a driver polling one status register from
 * a scan across a card's whole window, and those are different findings: the
 * first says a card that should be fitted is missing, the second says the
 * firmware looked and correctly found nothing.
 *
 * The overflow is printed rather than swallowed. A list that has silently
 * stopped growing reads as a complete inventory, which is how "it only ever
 * touched these addresses" gets believed about a run that touched hundreds. */
static void print_atbus_empty_addresses(const ap_board_t *board) {
  if (board->atbus_empty_distinct == 0u) {
    return;
  }
  printf("    first seen");
  for (unsigned i = 0; i < board->atbus_empty_distinct; i++) {
    printf(" %08X", board->atbus_empty_addresses[i]);
  }
  if (board->atbus_empty_addresses_dropped > 0u) {
    printf("  (+%u more, not recorded)", board->atbus_empty_addresses_dropped);
  }
  printf("\n");
  /* And the far end of the run. The list above fills with whatever looked
   * first -- on this machine the PROM's expansion-ROM scan, every time -- so
   * without these two a boot whose driver polled one register seven million
   * times reported the same addresses as one that never polled at all. */
  if (board->atbus_empty_reads > 0u) {
    printf("    last read  %08X\n", board->last_atbus_empty_read);
  }
  if (board->atbus_empty_writes > 0u) {
    printf("    last write %08X\n", board->last_atbus_empty_write);
  }
}

/* Per-instruction clocks, measured the way tools/mame-oracle/steptime.lua
 * measures the oracle's -- consecutive deltas after a discarded first step, so
 * neither side charges one instruction for filling the pipe.
 *
 * This report is *not* a claim about the hardware. It is one half of a
 * comparison, and the other half is the oracle's; the manual arbitrates. The
 * header says so, because a table of numbers with no such note is exactly what
 * gets quoted later as if it were measured silicon. */
static void time_instructions(FILE *out) {
  unsigned count = 0;
  const ap_probe_timing_t *timed = ap_probe_timed_instructions(&count);

  fprintf(out, "# apollo per-instruction clocks\n");
  fprintf(out,
          "# BUS AND CACHE TIME ONLY. Instruction execution time -- the\n"
          "# microcode clocks between the bus cycles -- is not yet modelled, so\n"
          "# every figure here is a lower bound. Compare against the oracle with\n"
          "# tools/mame-oracle/steptime.lua, and against MC68030 User's Manual\n"
          "# ch. 11; classify each disagreement rather than moving these to fit.\n"
          "#\n"
          "# The alternation below is expected, not noise. The cache holding\n"
          "# register is a long word, so one external fetch serves two\n"
          "# instruction words: [030] 11.3.3, \"one external bus cycle per two\n"
          "# instruction prefetches\". The published tables average the odd- and\n"
          "# even-aligned cases; this reports both.\n");
  fprintf(out, "%-6s %-18s %-7s %s\n", "word", "instruction", "steady",
          "clocks per step");

  for (unsigned i = 0; i < count; i++) {
    const ap_probe_timing_t result = ap_probe_time_instruction(
        timed[i].word, timed[i].mnemonic, probe_ram, PROBE_RAM_BYTES);

    fprintf(out, "%04X   %-18s ", result.word, result.mnemonic);
    if (!result.ok) {
      fprintf(out, "%-7s (did not execute)\n", "-");
      continue;
    }
    fprintf(out, "%-7s", result.steady ? "yes" : "NO");
    for (unsigned s = 0; s < result.samples; s++) {
      fprintf(out, " %u", result.delta[s]);
    }
    fprintf(out, "\n");
  }
}

/* The ring's own probe block. Separate from the instruction probes above
 * because it measures a different thing in a different unit: those report
 * clocks for one machine, these report bit times for a whole ring. Folding
 * them together would put two unrelated units in one golden.
 *
 * Fixed width for the same reason as that block -- it is diffed by a person. */
static void run_ring_probes(FILE *out) {
  unsigned count = 0u;
  const ap_ring_probe_result_t *results = ap_ring_probe_all(&count);

  fprintf(out, "# apollo ring probes\n");
  fprintf(out,
          "# Bit times on a 12 Mbit/s ring, from [MAC] 010005-00. There is no\n"
          "# runnable oracle for any of this: every figure is a structural\n"
          "# consequence of a cited section, not a measurement against another\n"
          "# emulator. round trip grows by exactly one bit per node because a\n"
          "# station transceives with the elastic store's nominal one-bit\n"
          "# delay ([MAC] 3.2, 3.3.2); the fixed offset is the token's own\n"
          "# nine bits, since the lap is measured to the last of them.\n");
  fprintf(out, "%-14s %5s %10s %9s %8s %6s %5s %s\n", "probe", "nodes",
          "roundtrip", "bittimes", "claimedby", "claims", "err", "done");

  for (unsigned i = 0; i < count; i++) {
    const ap_ring_probe_result_t *r = &results[i];
    fprintf(out, "%-14s %5u %10llu %9llu %8d %6u %5s %s\n", r->name, r->nodes,
            (unsigned long long)r->round_trip_bits,
            (unsigned long long)r->bit_times, r->claimed_by, r->claims,
            r->biphase_error ? "YES" : "no", r->completed ? "yes" : "NO");
  }

  /* The slope, stated rather than left to the reader to subtract. This is the
   * quantity `[MAC]` predicts and the one a defect would move; the absolute
   * figures carry the token width with them. */
  if (count >= 2u && results[0].completed && results[3].completed) {
    const unsigned long long span =
        results[3].round_trip_bits - results[0].round_trip_bits;
    const unsigned nodes = results[3].nodes - results[0].nodes;
    fprintf(out, "\nlatency per node inserted: %llu bit time(s)\n",
            nodes > 0u ? span / nodes : 0ull);
  }
}

/* A fixed-width report, because it is read as a golden diff by a person: a
 * column that moves when one field widens turns a one-line change into a
 * whole-file one. */
static void run_probes(FILE *out, ap_model_id_t model) {
  unsigned count = 0;
  const ap_probe_t *probes = ap_probe_all(&count);

  fprintf(out, "# apollo probe results\n");
  fprintf(out,
          "# clocks cover bus and cache time only: instruction execution time\n"
          "# is not yet modelled, so these are lower bounds rather than\n"
          "# figures to compare against MC68030 User's Manual ch. 11.\n");
  fprintf(out, "%-18s %4s %-14s %-8s %-8s %8s %4s %-16s\n", "probe", "ran",
          "status", "d0", "pc", "clocks", "berr", "hash");

  for (unsigned i = 0; i < count; i++) {
    const ap_probe_result_t result =
        ap_probe_run(&probes[i], probe_ram, PROBE_RAM_BYTES, model);
    fprintf(out, "%-18s %4u %-14s %08X %08X %8llu %4u %016llX\n",
            probes[i].name, result.executed,
            ap_probe_status_name(result.status), (unsigned)result.d0,
            (unsigned)result.pc, (unsigned long long)result.clocks,
            result.bus_errors, (unsigned long long)result.hash);
  }

  /* What each probe is for, printed once below the table rather than in it:
   * a golden diff shows the numbers, and the purposes are what make a moved
   * number interpretable by someone who did not write it. */
  fprintf(out, "\n# what each probe covers\n");
  for (unsigned i = 0; i < count; i++) {
    fprintf(out, "# %-18s %s\n", probes[i].name, probes[i].purpose);
  }
}

/* Run one probe described by a file, so a probe can come from outside this
 * binary.
 *
 * `tools/mame-oracle/encoder.py` hand-assembles the words and
 * `tools/mame-oracle/probe.lua` writes the same words into the oracle's RAM, so
 * the two sides run the identical program by construction rather than by two
 * people transcribing it. Neither path needs firmware or a boot.
 *
 * The sentinel is read back from the RAM this frontend owns rather than
 * returned by `ap_probe_run`: a probe result reports registers, and a probe
 * that proves a *store* has to be checked where the store landed.
 */
/* Dump a range of the machine's memory, through the **board** rather than out of
 * the buffer.
 *
 * Through the board because that is what a program sees: an address the board
 * decodes to a device answers with the device's value, and one it decodes to
 * nothing is *reported* rather than silently read as zero. A dump that indexed
 * the RAM array would show a frame buffer as blank and an unmapped hole as
 * plausible data.
 *
 * The format is one line per sixteen bytes: address, the bytes in hex, then the
 * printable characters. Bytes nothing answered print as `--` rather than `00`,
 * because "the board declined this address" and "the board answered zero" are
 * different facts and a dump that spelt them alike would be lying in the one
 * place a dump is read most carefully. */
static void dump_memory(FILE *out, ap_board_t *board, uint32_t address,
                        uint32_t length) {
  for (uint32_t offset = 0; offset < length; offset += 16u) {
    const uint32_t base = address + offset;
    const uint32_t run = (length - offset) < 16u ? (length - offset) : 16u;
    uint8_t bytes[16];
    bool answered[16];
    for (uint32_t i = 0; i < run; i++) {
      bool ok = false;
      bytes[i] = ap_board_read(board, base + i, &ok);
      answered[i] = ok;
    }
    fprintf(out, "%08X ", base);
    for (uint32_t i = 0; i < 16u; i++) {
      if (i == 8u) { fputc(' ', out); }
      if (i < run) {
        if (answered[i]) {
          fprintf(out, " %02X", bytes[i]);
        } else {
          fputs(" --", out);
        }
      } else {
        fputs("   ", out);
      }
    }
    fputs("  ", out);
    for (uint32_t i = 0; i < run; i++) {
      const uint8_t b = bytes[i];
      fputc(answered[i] && b >= 0x20u && b < 0x7Fu ? (int)b : '.', out);
    }
    fputc('\n', out);
  }
}

/* `ADDR` or `ADDR:LEN`, both hexadecimal. Returns false for a spec that is not
 * one, so a mistyped flag is refused rather than dumping from address zero.
 *
 * ## The default length is the caller's, and it used to be 256 for everyone
 *
 * A *dump* wants a block: `--dump-mem 1000` meaning one byte would be useless,
 * so 256 is right there. A *stop* wants an address, and `--boot-stop-pc 1794`
 * silently meant "anywhere in `1794`-`1893`" -- a 256-byte window, wider than
 * most subroutines. `boot_stop_pc_length` was initialised to 1 and then
 * overwritten by this function, so the intent was there and the code undid it.
 *
 * What that cost is real and was visible in the output the whole time: a run
 * asked to stop at `1794` reported `stopped at PC 000017A2`, fourteen bytes
 * past a branch it had *not* taken, and "the address was reached" is exactly
 * the wrong conclusion to draw from that. The report printed the true PC, so
 * every affected reading is recoverable -- but only by rereading it. */
/* How many `--dump-logical` requests one run will honour. */
#define AP_MAX_LOGICAL_DUMPS 4u

static bool parse_spec(const char *spec, uint32_t *address, uint32_t *length,
                       uint32_t default_length) {
  char *end = NULL;
  const unsigned long start = strtoul(spec, &end, 16);
  if (end == spec) {
    return false;
  }
  *address = (uint32_t)start;
  *length = default_length;
  if (*end == ':') {
    const char *rest = end + 1;
    const unsigned long count = strtoul(rest, &end, 16);
    if (end == rest || count == 0u) {
      return false;
    }
    *length = (uint32_t)count;
  } else if (*end != '\0') {
    return false;
  }
  return true;
}

/* A dump: a block by default. */
static bool parse_dump_spec(const char *spec, uint32_t *address,
                            uint32_t *length) {
  return parse_spec(spec, address, length, 256u);
}

/* A stop: **one address** by default. A range is still available as `ADDR:LEN`,
 * which is what the physical-PC stop wants when the logical address of a piece
 * of code is unknown and only its page is. */
static bool parse_stop_spec(const char *spec, uint32_t *address,
                            uint32_t *length) {
  return parse_spec(spec, address, length, 1u);
}

static int run_probe_file(FILE *out, ap_model_id_t model,
                          const char *program_name, const char *path,
                          const char *dump_spec) {
  FILE *file = fopen(path, "r");
  if (file == NULL) {
    fprintf(stderr, "%s: cannot open probe file %s\n", program_name, path);
    return 2;
  }

  uint16_t words[256];
  unsigned word_count = 0;
  uint32_t load = 0, entry = 0, stack = 0, read_at = 0;
  unsigned limit = 64;
  bool have_read = false;
  /* Run against a whole core board rather than flat RAM. A probe that only
   * touches memory does not care; one that touches a device register cannot
   * work without it, because on flat RAM the register is simply unmapped and
   * the probe faults where the oracle's machine answers. That is what kept
   * every device verification line -- interrupt ordering, DMA transfers, timer
   * self-timing -- from having a route.
   *
   * No boot PROM is attached: `ap_board_init` does not need one, and a probe is
   * side-loaded precisely so that no firmware runs. */
  bool on_board = false;

  char line[1024];
  while (fgets(line, (int)sizeof line, file) != NULL) {
    char *cursor = line;
    while (*cursor == ' ' || *cursor == '\t') { cursor++; }
    if (*cursor == '#' || *cursor == '\n' || *cursor == '\0') { continue; }

    if (strncmp(cursor, "words", 5) == 0) {
      cursor += 5;
      while (*cursor != '\0' && *cursor != '\n') {
        while (*cursor == ' ' || *cursor == '\t') { cursor++; }
        if (*cursor == '\0' || *cursor == '\n') { break; }
        char *end = NULL;
        const unsigned long value = strtoul(cursor, &end, 16);
        if (end == cursor) { break; }
        if (word_count >= sizeof words / sizeof words[0]) {
          fprintf(stderr, "%s: probe has more than %zu words\n", program_name,
                  sizeof words / sizeof words[0]);
          fclose(file);
          return 2;
        }
        words[word_count++] = (uint16_t)(value & 0xFFFFu);
        cursor = end;
      }
      continue;
    }

    char key[32];
    unsigned long value = 0;
    if (sscanf(cursor, "%31s %lx", key, &value) != 2) { continue; }
    if (strcmp(key, "load") == 0) { load = (uint32_t)value; }
    else if (strcmp(key, "entry") == 0) { entry = (uint32_t)value; }
    else if (strcmp(key, "stack") == 0) { stack = (uint32_t)value; }
    else if (strcmp(key, "limit") == 0) { limit = (unsigned)value; }
    else if (strcmp(key, "read") == 0) { read_at = (uint32_t)value; have_read = true; }
    else if (strcmp(key, "board") == 0) { on_board = value != 0u; }
  }
  fclose(file);

  if (word_count == 0) {
    fprintf(stderr, "%s: probe file %s has no words\n", program_name, path);
    return 2;
  }

  const ap_probe_t probe = {
      .name = "file",
      .purpose = "a probe supplied from outside this binary",
      .words = words,
      .word_count = word_count,
      .load_address = load,
      .entry = entry,
      .stack = stack,
      .limit = limit,
  };
  ap_probe_result_t result;
  /* The board machine owns its RAM for the whole run, so both live until the
   * result has been reported. */
  uint8_t *board_ram = nullptr;
  ap_board_t *board = nullptr;
  ap_machine_t board_machine;

  if (!on_board) {
    result = ap_probe_run(&probe, probe_ram, PROBE_RAM_BYTES, model);
  } else {
    const ap_model_t *entry_model = ap_model_by_id(model);
    const uint32_t ram_bytes = 0x400000u;
    board_ram = calloc(1, ram_bytes);
    board = calloc(1, sizeof *board);
    /* The same epoch the boot path uses, so a probe that reads the calendar
     * gets a stated time rather than whatever the host clock says. */
    static const ap_mc146818_time_t epoch = {
        .year = 1987u, .month = 7u, .day = 31u, .day_of_week = 6u,
        .hour = 21u, .minute = 9u, .second = 21u,
    };
    if (entry_model == nullptr || board_ram == nullptr || board == nullptr ||
        !ap_board_init(board, board_ram, ram_bytes, &epoch, 0x012345u)) {
      free(board);
      free(board_ram);
      fprintf(stderr, "%s: cannot build the core board\n", program_name);
      return 2;
    }

    ap_machine_init_model(&board_machine, board_ram, ram_bytes, model);
    ap_machine_set_board(&board_machine, board);

    /* Through the *board*, not `ap_machine_write`: that is the operator's view
     * of flat RAM and knows nothing of where a model puts its memory. A
     * DN3500's RAM begins at `01000000`, so a board probe loads where the
     * oracle's does — which is the point, since both sides then run the same
     * addresses and the diff stops needing a base offset. */
    for (unsigned i = 0; i < word_count; i++) {
      bool hi = false, lo = false;
      ap_board_write(board, load + i * 2u, (uint8_t)(words[i] >> 8), &hi);
      ap_board_write(board, load + i * 2u + 1u, (uint8_t)words[i], &lo);
      if (!hi || !lo) {
        free(board);
        free(board_ram);
        fprintf(stderr, "%s: probe does not fit the board's RAM at %08X\n",
                program_name, (unsigned)load);
        return 2;
      }
    }
    ap_machine_reset(&board_machine, entry, stack);

    const ap_machine_run_t run = ap_machine_run(&board_machine, limit);
    result = (ap_probe_result_t){
        .executed = run.executed,
        .status = run.status,
        .d0 = board_machine.cpu.regs.d[0],
        .pc = board_machine.cpu.regs.pc,
        .bus_errors = board_machine.bus_errors,
        .hash = ap_machine_hash(&board_machine),
    };
  }

  fprintf(out, "# apollo probe file result\n");
  fprintf(out, "words     %u\n", word_count);
  fprintf(out, "ran       %u\n", result.executed);
  fprintf(out, "status    %s\n", ap_probe_status_name(result.status));
  fprintf(out, "d0        %08X\n", (unsigned)result.d0);
  fprintf(out, "pc        %08X\n", (unsigned)result.pc);
  fprintf(out, "berr      %u\n", result.bus_errors);

  if (on_board && dump_spec != NULL) {
    /* The same dump the boot path offers, on the board a probe built. Available
     * here because `board 1` makes a whole machine **without firmware**, which
     * is what lets the flag be exercised where `roms/` is absent -- and CI is
     * exactly that place. */
    uint32_t at = 0, length = 0;
    if (!parse_dump_spec(dump_spec, &at, &length)) {
      fprintf(stderr, "%s: --dump-mem wants ADDR or ADDR:LEN in hex, not %s\n",
              program_name, dump_spec);
    } else {
      fprintf(out, "memory %08X, %u byte(s), through the board\n", at, length);
      dump_memory(out, board, at, length);
    }
  }

  if (have_read && on_board) {
    uint32_t stored = 0;
    bool all = true;
    for (unsigned k = 0; k < 4u; k++) {
      bool ok = false;
      const uint8_t byte = ap_board_read(board, read_at + k, &ok);
      all = all && ok;
      stored = (stored << 8) | byte;
    }
    if (!all) {
      free(board);
      free(board_ram);
      fprintf(stderr, "%s: read address %08X is not memory on this board\n",
              program_name, (unsigned)read_at);
      return 2;
    }
    fprintf(out, "read      %08X %08X\n", (unsigned)read_at, (unsigned)stored);
    free(board);
    free(board_ram);
    return 0;
  }
  if (on_board) {
    free(board);
    free(board_ram);
    return 0;
  }

  if (have_read) {
    if ((uint64_t)read_at + 4u > (uint64_t)PROBE_RAM_BYTES) {
      fprintf(stderr, "%s: read address %08X is outside the probe RAM\n",
              program_name, (unsigned)read_at);
      return 2;
    }
    /* Big-endian, as the part stores it. */
    const uint32_t stored = (uint32_t)((uint32_t)probe_ram[read_at] << 24 |
                                       (uint32_t)probe_ram[read_at + 1u] << 16 |
                                       (uint32_t)probe_ram[read_at + 2u] << 8 |
                                       (uint32_t)probe_ram[read_at + 3u]);
    fprintf(out, "read      %08X %08X\n", (unsigned)read_at, (unsigned)stored);
  }
  return 0;
}


/* Read a whole file into memory. The frontend does this because `src/core` has
 * no file I/O by design. */
static uint8_t *read_file(const char *path, long *size_out) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    return NULL;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return NULL;
  }
  long size = ftell(file);
  rewind(file);
  if (size <= 0) {
    fclose(file);
    return NULL;
  }
  uint8_t *bytes = malloc((size_t)size);
  if (bytes == NULL || fread(bytes, 1, (size_t)size, file) != (size_t)size) {
    free(bytes);
    fclose(file);
    return NULL;
  }
  fclose(file);
  *size_out = size;
  return bytes;
}

/* The node ID a machine should present, taken from the volume it boots.
 *
 * `board/ap_nodeid.h` takes its identifier from a caller because "a device whose
 * purpose is to be unique per machine must not be identical on every one", and
 * this is the source that caller was always meant to have. A Domain volume
 * records the node that initialised it, and a machine booting that volume has to
 * present the same one: the file system's object identifiers carry it, so a node
 * that disagreed with its own disk would create objects attributed to a machine
 * that is not there.
 *
 * Refuses rather than defaults when the file is not a volume. A node ID invented
 * from an arbitrary file configures a machine to lie about its identity, and
 * every object it then creates carries the lie -- which outlives the run and
 * cannot be traced back to the moment it was chosen. */
static bool node_id_from_volume(const char *path, uint32_t *out) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "apollo: cannot open %s\n", path);
    return false;
  }
  uint8_t blocks[AP_VOLUME_LABEL_BYTES];
  const size_t got = fread(blocks, 1u, sizeof blocks, file);
  fclose(file);

  ap_volume_label_t label;
  if (!ap_volume_read_label(blocks, got, &label)) {
    fprintf(stderr, "apollo: %s is not a Domain volume\n", path);
    return false;
  }
  printf("volume %s\n", path);
  printf("  name         %s\n", label.name);
  printf("  creator UID  %08X%08X\n", label.creator.high, label.creator.low);
  printf("  node ID      %05X\n", label.node_id);
  *out = label.node_id;
  return true;
}

/* The machine's clock is not set here, and there is nothing to set it with.
 * `ap_machine_init` reads the rate from the model row, so a run through
 * `board/ap_board.c` -- the DN3500's core board -- keeps time at the DN3500's
 * rate because the machine is a DN3500, not because this file looked the model
 * up by name and said so. That is `CLAUDE.md`'s rule made true of the frontend
 * as well as the core, and it means the clock follows the table when the board
 * becomes model-driven rather than needing to be found and edited here. */

/* The whole machine as one number, with the numbers that localise a
 * disagreement beside it.
 *
 * A hash answers "are these two runs the same" and nothing else: when it says
 * no, it says nothing about where they parted. The clock and the PC are what
 * turn that into a place to look, which is why `ap_machine_state` reports them
 * together and why every run here prints the block rather than the hash alone.
 *
 * The bus-error count is here rather than in the hash deliberately -- it is our
 * record of watching the machine, not state the machine has. `ap_machine.h` has
 * the reasoning; this is the "reported beside it" half of it. */
/* What a fitted display is called, for the configuration line below. */
static const char *screen_kind_name(ap_screen_kind_t screen) {
  switch (screen) {
  case AP_SCREEN_NONE:
    return "none";
  case AP_SCREEN_COLOUR_4_PLANE:
    return "c4p (4-plane colour)";
  case AP_SCREEN_COLOUR_8_PLANE:
    return "c8p (8-plane colour)";
  case AP_SCREEN_MONO_19_INCH:
    return "19i (19-inch monochrome)";
  case AP_SCREEN_MONO_15_INCH:
    return "15i (15-inch monochrome)";
  }
  return "unknown";
}

/* `--dump-state`'s target. File scope because the dump belongs beside the hash
 * it must agree with, and `report_state` is where that is printed -- threading
 * a path through every reporting call to reach one line would be worse. */
static const char *g_dump_state_path = NULL;

/* The selected oracle divergences, at file scope for the same reason: they are
 * applied where the board is built, which is not where arguments are read. */
static ap_quirks_t g_quirks;

static void report_state(ap_machine_t *machine) {
  const ap_machine_state_t state = ap_machine_state(machine);
  printf("  state hash   %016llX\n", (unsigned long long)state.hash);
  if (g_dump_state_path != NULL) {
    FILE *f = fopen(g_dump_state_path, "w");
    if (f == NULL) {
      fprintf(stderr, "apollo: cannot write %s\n", g_dump_state_path);
    } else {
      /* The returned hash is the check that this is the **same walk** the
       * identity harness takes. If it differs from the line above, the dump is
       * of a different traversal and nothing in it can be trusted as a
       * comparison -- so it is printed rather than asserted, because a caller
       * diffing two machines wants to see both numbers agree first. */
      const uint64_t walked = ap_machine_dump_state(machine, f);
      fclose(f);
      printf("  state dump   %s, walk hash %016llX%s\n", g_dump_state_path,
             (unsigned long long)walked,
             walked == state.hash ? "" : "   <- DIFFERS, dump is untrustworthy");
    }
  }

  /* The configuration the hash covers, printed beside the number it changes.
   *
   * A fitted display puts its image memory into the hash and leaves **no other
   * trace in this report**: the region counters count by address, so they
   * report the display controller's reads whether or not a board answers them,
   * and two runs differing only in `--screen` produce reports identical but for
   * the hash. That is exactly how a reference hash comes to be irreproducible
   * -- ours was taken with a display fitted, and rerunning it without one gave
   * a different number with every other line byte-identical, which reads as a
   * broken change rather than a different machine. Memory's extent is here for
   * the same reason: `ap_board_hash` feeds it and nothing else prints it. */
  if (machine->board != NULL) {
    printf("  fitted       display %s, %u Mbyte main memory\n",
           screen_kind_name(machine->board->graphics.screen),
           (unsigned)(machine->board->ram_bytes / (1024u * 1024u)));
  }
  /* The region of the address the *bus* would have seen. Naming the region of
   * the logical PC was the same error the trace's opcode column made: with the
   * MMU on it reports where that number would land if nothing translated it,
   * so an operating system running perfectly well at `3FFA24FC` is described
   * as `unmapped` and reads as a machine that has crashed. */
  uint32_t pc_physical = state.pc;
  const bool pc_mapped =
      ap_machine_translate(machine, state.pc, 6u, &pc_physical);
  printf("  final PC     %08X", state.pc);
  if (pc_mapped && pc_physical != state.pc) {
    printf(" -> %08X", pc_physical);
  }
  printf(" (%s)\n",
         !pc_mapped ? "no translation"
                    : (machine->board != NULL
                           ? ap_board_region_name(
                                 ap_board_region(machine->board, pc_physical))
                           : "no board"));
  /* Every register, because which one matters is not known in advance.
   *
   * The trace ring keeps six of them, chosen when the questions were about the
   * firmware's own conventions. The question after that turned out to be about
   * `a3`, computed as `3C42BCC0 - a4 + d7`, and none of those three were kept
   * -- so the run that finally stopped on the right instruction could not say
   * where it had been reading from. Sixteen numbers at the end of a run cost
   * nothing next to another fourteen-minute boot. */
  printf("  d0-d7       ");
  for (unsigned r = 0; r < 8u; r++) {
    printf(" %08X", machine->cpu.regs.d[r]);
  }
  printf("\n  a0-a7       ");
  for (unsigned r = 0; r < 7u; r++) {
    printf(" %08X", machine->cpu.regs.a[r]);
  }
  printf(" %08X\n", ap_m68030_read_a7(&machine->cpu.regs));
  printf("  clocks       %llu\n", (unsigned long long)state.clocks);
  /* In AP_TIME_BASE_HZ units, never CPU cycles: several nodes of different
   * models share one ring, so no CPU's cycle is a legal unit of account. A
   * machine whose clock rate was never set has produced no time at all, which
   * is visibly wrong here rather than quietly approximate. */
  printf("  elapsed      %llu base units\n", (unsigned long long)state.now);
  {
    /* Exceptions by vector, which is the machine's own account of what went
     * wrong: the firmware prints at most the one it stopped on, and a handler
     * that reports one exception as another is invisible without this. */
    bool any = false;
    for (unsigned v = 0; v < 256u; v++) {
      if (machine->cpu.exceptions_taken[v] == 0u) {
        continue;
      }
      if (!any) {
        printf("  exceptions  ");
        any = true;
      }
      printf(" %u x vector %u", machine->cpu.exceptions_taken[v], v);
    }
    if (any) {
      printf("\n");
    }
  }
  {
    /* Whether translation is on, and the two windows that bypass it.
     *
     * A descriptor-fetch count says the MMU has been *used*; it does not say
     * whether a given access went through it, because a transparent window is
     * consulted first and answers without a table search. A run that reports
     * translation enabled and a window covering all of memory is describing a
     * machine whose page tables cannot matter, and that is not visible from any
     * other number here. */
    const ap_m68030_cpu_t *cpu = &machine->cpu;
    printf("  translation  %s", cpu->tc.enable ? "enabled" : "off");
    for (unsigned t = 0; t < 2u; t++) {
      const ap_m68030_tt_t *tt = t == 0u ? &cpu->tt0 : &cpu->tt1;
      if (!tt->enabled) {
        continue;
      }
      printf(", tt%u base %02X mask %02X", t, tt->logical_base,
             tt->logical_mask);
    }
    if (cpu->tc.enable) {
      /* Which root a supervisor access uses is `TC`'s SRE, and the two roots
       * are loaded by separate `PMOVE`s -- so a program that loads one and not
       * the other translates through a table that was never filled in. */
      printf(", %s root", cpu->tc.supervisor_root ? "split supervisor" :
                                                    "one");
      printf(", crp %08X limit %04X, srp %08X limit %04X",
             cpu->crp.table_address, cpu->crp.limit, cpu->srp.table_address,
             cpu->srp.limit);
    }
    printf("\n");
  }
  printf("  atc fills    %u descriptor fetch(es), %u history update(s)\n",
         state.table_fetches, state.table_updates);
  /* The observer's own table searches, on their own line and never added to the
   * machine's. This frontend reads one word back per stepped instruction to
   * fill the trace's instruction column, and each of those is a full walk of
   * whatever tree is loaded -- so a three-level tree charged three fetches per
   * instruction to a counter above that reads as the MMU's behaviour. It was
   * read that way, and "the ATC is not retaining entries" was written down from
   * it before the arithmetic gave it away: the excess was exactly three per
   * *step*, not per access. Printed always, including zero, because a reader
   * comparing two runs needs to know whether the probe was running at all. */
  printf("  probe walks  %u descriptor fetch(es) by --dump-logical and the\n"
         "               per-step trace read-back, not by the machine\n",
         state.probe_fetches);
  /* Every MMU register load, in order, with the instruction that made it.
   *
   * The register dump above is the *last* state; this is how it got there, and
   * the two answer different questions. "Which tree did the operating system
   * install" is not visible in a final `CRP` when the answer is that it
   * installed none and inherited the firmware's -- those two look identical at
   * exit and differ entirely here. */
  if (machine->mmu_writes_total > 0u) {
    printf("  mmu loads    %u PMOVE(s)", machine->mmu_writes_total);
    if (machine->mmu_writes_total > machine->mmu_write_count) {
      printf(", first %u shown", machine->mmu_write_count);
    }
    printf("\n");
    for (unsigned i = 0; i < machine->mmu_write_count; i++) {
      printf("    %-5s <- %08X %08X  by PC %08X\n",
             ap_mmu_register_name(machine->mmu_writes[i].which),
             machine->mmu_writes[i].high, machine->mmu_writes[i].low,
             machine->mmu_writes[i].pc);
    }
  }
  /* Reads, always -- **including when there are none**, because "the program
   * never looked at the MMU" is a finding and a line that only appears on a
   * non-zero count cannot report it. The registers read are named rather than
   * counted apart: which ones were inspected is the interesting half. */
  {
    printf("  mmu reads    %u PMOVE(s) out of a register", machine->mmu_reads_total);
    if (machine->mmu_reads_total > 0u) {
      printf(":");
      for (unsigned r = 0; r < 8u; r++) {
        if ((machine->mmu_reads_mask & (1u << r)) != 0u) {
          printf(" %s", ap_mmu_register_name((uint8_t)r));
        }
      }
    }
    printf("\n");
    for (unsigned i = 0; i < machine->mmu_read_count; i++) {
      printf("    %-5s ->          by PC %08X\n",
             ap_mmu_register_name(machine->mmu_reads[i].which),
             machine->mmu_reads[i].pc);
    }
  }
  if (machine->distinct_fault_count > 0u) {
    /* One line per faulting *instruction*, with the span it reached over and
     * how often. As a bare list of addresses this said which places went
     * unanswered and nothing about whether they were probed or stumbled into,
     * which is the distinction a boot ending in a fault turns on -- and the
     * PROM's own device scan then filled the list before anything interesting
     * happened. */
    printf("  fault sites  %u instruction(s)", machine->distinct_fault_count);
    if (machine->fault_sites_dropped > 0u) {
      printf(", %u fault(s) from further PCs not recorded",
             machine->fault_sites_dropped);
    }
    printf("\n");
    for (unsigned i = 0; i < machine->distinct_fault_count; i++) {
      const ap_fault_site_t *site = &machine->fault_sites[i];
      printf("    PC %08X  %u time(s)  %08X", site->pc, site->count,
             site->first_address);
      if (site->last_address != site->first_address) {
        printf("-%08X", site->last_address);
      }
      printf("\n");
    }
  }
  /* The MMU's refusals, always -- including when there are none. "Translation
   * never refused anything" is a finding, and a line that appears only on a
   * non-zero count cannot report it. Kept apart from the board's list because
   * the two are the same vector 2 to the program and different events to
   * whoever is reading the boot. */
  {
    static const char *const reason_name[] = {
        "cached fault", "invalid", "limit", "protection", "search bus error"};
    printf("  mmu faults   %u", machine->mmu_faults);
    if (machine->mmu_fault_sites_dropped > 0u) {
      printf(", %u from further PCs not recorded",
             machine->mmu_fault_sites_dropped);
    }
    printf("\n");
    for (unsigned i = 0; i < machine->mmu_fault_site_count; i++) {
      const ap_mmu_fault_site_t *site = &machine->mmu_fault_sites[i];
      printf("    PC %08X  %u time(s)  %08X", site->pc, site->count,
             site->first_address);
      if (site->last_address != site->first_address) {
        printf("-%08X", site->last_address);
      }
      printf("  %s on %s\n", reason_name[site->reason],
             site->write ? "write" : "read");
    }
  }
  if (machine->watch_read_address != 0u) {
    printf("  watch read   %08X read %u time(s)", machine->watch_read_address,
           machine->watch_reads);
    if (machine->watch_reads > 0u) {
      /* The value as well as the count: "it read there and got zero" and "it
       * never read there" are the two hypotheses this flag exists to separate,
       * and a count alone cannot. */
      printf(", last %0*X by PC %08X", (int)(machine->watch_read_size * 2u),
             machine->watch_read_value, machine->watch_read_pc);
    }
    printf("\n");
  }
  if (machine->watch_write_address != 0u) {
    printf("  watch        %08X written %u time(s)", machine->watch_write_address,
           machine->watch_writes);
    if (machine->watch_writes > 0u) {
      printf(", last %0*X by PC %08X", (int)(machine->watch_write_size * 2u),
             machine->watch_write_value, machine->watch_write_pc);
    }
    printf("\n");
  }
  printf("  bus errors   %u", state.bus_errors);
  if (state.bus_errors > 0u) {
    printf(", first %08X, last %08X from PC %08X", state.first_bus_error,
           state.last_bus_error, state.last_bus_error_pc);
  }
  printf("\n");

  /* The two addresses those numbers are *not*.
   *
   * `last_bus_error` is what the bus carried, because that is where an access
   * is refused; the program named something else and the difference is the
   * whole of the MMU. The processor already keeps the logical one -- it has to,
   * since the bus-error frame reports it to the handler -- so this is a print
   * rather than new bookkeeping.
   *
   * `VBR` earns a line because a vector table the tables do not map turns every
   * exception into a double fault, and it is invisible in every other number
   * here: the run reports the handler it reached, never the address it read to
   * find it. */
  {
    const struct {
      const char *name;
      uint32_t logical;
      bool interesting;
    } pointers[] = {
        {"fault addr  ", machine->cpu.fault_address,
         machine->cpu.access_faulted},
        {"vbr         ", machine->cpu.regs.vbr, true},
    };
    for (unsigned i = 0; i < sizeof pointers / sizeof pointers[0]; i++) {
      if (!pointers[i].interesting) {
        continue;
      }
      uint32_t physical = 0;
      const bool mapped =
          ap_machine_translate(machine, pointers[i].logical,
                               AP_M68030_FC_SUPERVISOR_DATA, &physical);
      printf("  %s %08X", pointers[i].name, pointers[i].logical);
      if (!mapped) {
        printf(" -> no translation\n");
        continue;
      }
      printf(" -> %08X (%s)\n", physical,
             machine->board != NULL
                 ? ap_board_region_name(ap_board_region(machine->board,
                                                        physical))
                 : "no board");
    }
  }
}

/* Run the machine from its boot PROM, which is how a DN3500 actually starts.
 *
 * The side-loading route (`--boot-tape`) puts an image at an address it names
 * and jumps there. That works only while memory answers everywhere: the real
 * machine has no physical memory at the boot image's load address, and the
 * addresses in its header are logical (`FINDINGS.md` C28). The PROM is what
 * enables translation, so it is what has to run first. */
/* Scan the fitted screen out and write it as a PNG.
 *
 * The plan's verification line for the drawing engine asks for exactly this and
 * for nothing weaker: "verify on a decoded PNG rather than on register
 * round-trips — a controller that passes register tests and draws nothing is
 * the standard way this goes wrong". A mirrored, sheared or blank image passes
 * every register identity in `graphics_suite`.
 *
 * ## The palette, and what this cannot yet claim
 *
 * A monochrome screen has a real one: the bitmap stores **ink**, so a set bit
 * is black and a clear bit white. That is the whole colour model and it is
 * exact.
 *
 * A colour screen does not, here. The 8-plane board's lookup table is a Bt458
 * and `ap_bt458` models it completely — but it is **not wired to the board**,
 * so an index cannot yet become a colour; the 4-plane board's own 16-entry
 * table is not modelled at all. Rather than invent colours, a colour screenshot
 * is written as an *index map* under an even grey ramp, and the console says
 * so. It still catches every geometric failure, which is what the verification
 * is for; it does not claim to be what the monitor showed. */
static int write_screenshot(const char *path, const ap_graphics_t *graphics,
                            uint8_t cr1) {
  ap_graphics_geometry_t geometry;
  if (!ap_graphics_geometry(graphics->screen, &geometry)) {
    fprintf(stderr, "apollo: no screen fitted, nothing to capture\n");
    return 1;
  }
  if (!ap_png_available()) {
    fprintf(stderr, "apollo: %s\n", ap_png_status_name(AP_PNG_UNSUPPORTED));
    return 1;
  }

  const uint32_t pixels = (uint32_t)geometry.width * geometry.height;
  uint8_t *image = calloc(1, pixels);
  if (image == NULL) {
    fprintf(stderr, "apollo: cannot allocate the screenshot\n");
    return 1;
  }
  if (ap_graphics_scanout(graphics, cr1, image, pixels) != pixels) {
    free(image);
    fprintf(stderr, "apollo: the screen could not be scanned out\n");
    return 1;
  }

  const unsigned colours = 1u << geometry.planes;
  uint8_t palette[256][3];
  bool real_palette = false;
  if (geometry.planes == 1u) {
    /* Ink, not light: a set bit is black. That is the whole colour model for a
     * monochrome screen and it is exact. */
    palette[0][0] = palette[0][1] = palette[0][2] = 0xFFu;
    palette[1][0] = palette[1][1] = palette[1][2] = 0x00u;
    real_palette = true;
  } else if (graphics->screen == AP_SCREEN_COLOUR_8_PLANE) {
    /* The Bt458's own, as the firmware loaded it. */
    real_palette = true;
    for (unsigned i = 0; i < colours; i++) {
      uint8_t rgb[3] = {0u, 0u, 0u};
      (void)ap_bt458_palette(&graphics->lut, i, rgb);
      palette[i][0] = rgb[0];
      palette[i][1] = rgb[1];
      palette[i][2] = rgb[2];
    }
  } else {
    /* A 4-plane board's lookup table is sixteen entries written through three
     * registers of the controller's own, and is not modelled. An even ramp,
     * labelled as an index map rather than passed off as colours. */
    for (unsigned i = 0; i < colours; i++) {
      const uint8_t level = (uint8_t)(i * 255u / (colours - 1u));
      palette[i][0] = palette[i][1] = palette[i][2] = level;
    }
  }

  const ap_png_status_t status =
      ap_png_write_indexed(path, image, geometry.width, geometry.height,
                           palette, colours);
  free(image);
  if (status != AP_PNG_OK) {
    fprintf(stderr, "apollo: cannot write %s: %s\n", path,
            ap_png_status_name(status));
    return 1;
  }

  printf("  screenshot   %s, %ux%u, %u plane(s)\n", path, geometry.width,
         geometry.height, geometry.planes);
  if (!ap_graphics_display_enabled(cr1)) {
    /* Reported rather than painted black, so that "the firmware never enabled
     * the display" and "the firmware drew nothing" stay different answers. */
    printf("               DISP_EN is clear: the monitor would show black\n");
  }
  if (!real_palette) {
    printf("               index map under a grey ramp, not the screen's"
           " colours -- a\n"
           "               4-plane board's lookup table is not modelled\n");
  } else if (geometry.planes > 1u) {
    printf("               palette is the Bt458's, as the firmware loaded"
           " it\n");
  }
  return 0;
}

/* ## A console script: wait for what the machine says, then answer it
 *
 * `--boot-input` sends a fixed string, paced by a timer. That is right for the
 * one thing it was built for -- autobauding the port with a carriage return --
 * and wrong for a dialogue: the boot PROM asks questions at times that depend
 * on how long a disk took, so a fixed script answers the wrong prompt. Feeding
 * `ex domain_os` that way put an `o` into "Do you wish to continue (y,n)?".
 *
 * A line is `expect <text>` or `send <text>`, in order. `expect` waits until
 * that text has appeared in what the machine has transmitted; `send` delivers
 * its text, with `\r` for a carriage return. Matching is a plain substring
 * search over the whole console stream so far, which is what the oracle
 * harness's own procedures use and is enough for every prompt the PROM asks.
 *
 * This is the "scripted input" half of the frontend-flags item, and it is what
 * the item meant: input *at the machine's pace* rather than at ours. */
typedef struct {
  bool send;      /* false for expect */
  char text[128];
} ap_console_step_t;

#define AP_CONSOLE_SCRIPT_STEPS 64u

typedef struct {
  ap_console_step_t step[AP_CONSOLE_SCRIPT_STEPS];
  unsigned steps;
  unsigned at;        /* which step is current */
  unsigned sent;      /* how far through the current send */
  /* What the machine has transmitted, kept only as far as the longest pattern
   * needs. A whole boot's console is megabytes and matching wants a tail. */
  char seen[512];
  unsigned seen_len;
} ap_console_script_t;

/* `\r` and `\n` are the only escapes: a prompt answer is a line, and anything
 * richer would be a language rather than a script. */
static void console_script_unescape(char *text) {
  char *out = text;
  for (const char *in = text; *in != '\0'; in++) {
    if (*in == '\\' && in[1] == 'r') {
      *out++ = '\r';
      in++;
    } else if (*in == '\\' && in[1] == 'n') {
      *out++ = '\n';
      in++;
    } else {
      *out++ = *in;
    }
  }
  *out = '\0';
}

[[nodiscard]] static bool console_script_load(ap_console_script_t *script,
                                              const char *path) {
  FILE *file = fopen(path, "r");
  if (file == NULL) {
    fprintf(stderr, "apollo: cannot read console script %s\n", path);
    return false;
  }
  char line[192];
  bool ok = true;
  while (fgets(line, (int)sizeof line, file) != NULL) {
    size_t length = strlen(line);
    while (length > 0u && (line[length - 1u] == '\n' || line[length - 1u] == '\r')) {
      line[--length] = '\0';
    }
    if (line[0] == '\0' || line[0] == '#') {
      continue;
    }
    if (script->steps >= AP_CONSOLE_SCRIPT_STEPS) {
      fprintf(stderr, "apollo: console script has more than %u steps\n",
              AP_CONSOLE_SCRIPT_STEPS);
      ok = false;
      break;
    }
    ap_console_step_t *step = &script->step[script->steps];
    if (strncmp(line, "send ", 5u) == 0) {
      step->send = true;
      snprintf(step->text, sizeof step->text, "%s", line + 5);
    } else if (strncmp(line, "expect ", 7u) == 0) {
      step->send = false;
      snprintf(step->text, sizeof step->text, "%s", line + 7);
    } else {
      fprintf(stderr, "apollo: console script line is not send or expect: %s\n",
              line);
      ok = false;
      break;
    }
    console_script_unescape(step->text);
    script->steps++;
  }
  fclose(file);
  return ok;
}

/* A byte the machine transmitted. Kept in a sliding tail, and an `expect` that
 * matches advances the script. */
static void console_script_saw(ap_console_script_t *script, uint8_t byte) {
  if (script->steps == 0u) {
    return;
  }
  if (script->seen_len + 1u >= sizeof script->seen) {
    /* Keep the tail: a pattern longer than what is kept could never match, and
     * the buffer is far larger than any prompt. */
    const unsigned keep = (unsigned)(sizeof script->seen) / 2u;
    memmove(script->seen, script->seen + script->seen_len - keep, keep);
    script->seen_len = keep;
  }
  script->seen[script->seen_len++] = (char)byte;
  script->seen[script->seen_len] = '\0';

  while (script->at < script->steps && !script->step[script->at].send &&
         strstr(script->seen, script->step[script->at].text) != NULL) {
    /* Matched: consume the stream so the next `expect` cannot be satisfied by
     * the same text, which is how a script silently skips a prompt. */
    script->seen_len = 0u;
    script->seen[0] = '\0';
    script->at++;
    script->sent = 0u;
  }
}

/* The next byte to deliver, or -1 when the script is waiting or finished. */
[[nodiscard]] static int console_script_next(ap_console_script_t *script) {
  if (script->at >= script->steps || !script->step[script->at].send) {
    return -1;
  }
  const char *text = script->step[script->at].text;
  const unsigned length = (unsigned)strlen(text);
  if (script->sent >= length) {
    script->at++;
    script->sent = 0u;
    return -1;
  }
  return (unsigned char)text[script->sent++];
}

/* One step, kept rather than printed.
 *
 * `--boot-trace` prints every step, which answers "what did the machine do" for
 * a run of a few thousand instructions and nothing at all for a run of five
 * hundred million: the fault this was built for is half a billion steps in, and
 * the output would be terabytes of a file nobody can open.
 *
 * `--boot-trace-last N` keeps the last N steps in a ring and prints them when
 * the run ends. The registers are the ones a failure is read from -- `d0` and
 * `d1` because the PROM's own reporter prints them as "Expected" and "Actual",
 * `a0` because it prints that as "Address", and `a6`/`a7` because the firmware
 * bases its data on one and its calls on the other. */
typedef struct {
  unsigned step;
  uint32_t pc;
  /* Every data and address register, because which one matters is not knowable
   * in advance and each miss costs a nine-minute run. This ring has been
   * widened three times mid-investigation -- for `a1`, which the PROM's disk
   * service takes its block number through; for `a3`, `a4` and `d7`, which
   * compute a rebased pointer; and for `d6`, which held the status the failing
   * routine returns. Sixteen columns of hex is a wide line and nothing else. */
  uint32_t d[8];
  uint32_t a[8];

  /* And A1, because the PROM's disk service takes its block number through it:
   * `movea.l 8(a7),a1` then `move.l (a1),$17e(a6)`, so the pointer that decides
   * which sector is read is only ever in this register. */

  uint16_t instruction;
  ap_m68030_step_status_t status;
} ap_trace_ring_t;


/* One place that fills a ring slot, because there are three call sites and the
 * last time they were edited by hand one of them ended up with the register
 * loop three times over. */
static void ap_trace_record(ap_trace_ring_t *slot, unsigned step, uint32_t pc,
                            const ap_m68030_cpu_t *cpu, uint16_t instruction,
                            ap_m68030_step_status_t status) {
  slot->step = step;
  slot->pc = pc;
  for (unsigned ri = 0; ri < 8u; ri++) {
    slot->d[ri] = cpu->regs.d[ri];
    /* A7 is not `regs.a[7]`: the stack pointer lives in whichever of USP, ISP
     * or MSP the mode selects, and the array slot reads zero. */
    slot->a[ri] = ri == 7u ? ap_m68030_read_a7(&cpu->regs) : cpu->regs.a[ri];
  }
  slot->instruction = instruction;
  slot->status = status;
}

/* How many reads of a channel's status register mark it as *waiting* rather
 * than merely running.
 *
 * Firmware that is executing reads a status register a handful of times.
 * Firmware sitting at a prompt reads it in a tight loop -- the boot PROM's own
 * input poll bounds itself at 65,536 tries -- so any threshold well above the
 * first and well below the second separates them. Two thousand is that, with
 * more than an order of magnitude either side, and it is deliberately not
 * tuned finely: a number that had to be exact would be measuring something
 * this is only meant to detect.
 */
#define AP_BOOT_KEY_POLLS 2000u

/* How hard the machine is polling for input, whichever register it polls.
 *
 * The threshold above was written against the **boot PROM**, which waits on
 * `SRA`, and it named that register. Domain/OS waits on the **ISR** instead:
 * measured over one 1.2-billion-instruction boot sitting at its calendar
 * prompt, `SRA` was read 1,468 times and the ISR 203,699. So a condition naming
 * one register is a condition about one *program*, and `--boot-type` delivered
 * nothing to an operating system that was polling as hard as it knows how.
 *
 * Both are status reads on the keyboard's port and either is the same evidence,
 * so they are summed. */
static unsigned input_polls(const ap_board_t *board) {
  return board->sio.register_reads[0][AP_MC68681_SR_CSR_A] +
         board->sio.register_reads[0][AP_MC68681_ISR_IMR];
}

/* How many instructions the fast path runs between typed-input checks. Coarse
 * on purpose: the condition it samples -- a configured port and a machine
 * polling for input -- holds for millions of consecutive instructions, so the
 * only requirement is to be far finer than that. */
/* Above the boot PROM (`0000xxxx`) and above the diagnostics it loads into low
 * main memory (`0100xxxx`). Domain/OS runs in the high supervisor space --
 * `3C43F5AC` at its calendar prompt. Measured, and a bound rather than a point:
 * it says the operating system is running without claiming when it started. */
/* `YYYY-MM-DD` or `YYYY-MM-DDTHH:MM:SS`, into the calendar's own structure.
 *
 * Exposed because the instant a machine powers on with is a *property of the
 * run*, not of this core, and hard-coding one had a consequence nobody had
 * connected: booting a 1992 volume with a 1987 clock makes Domain/OS say "The
 * calendar is more than a minute slow" and stop to ask about it. It is right.
 *
 * Still no wall clock -- `CLAUDE.md` forbids one and `ap_calendar_reset`
 * refuses to pick an instant for the caller. This takes a written-down date, so
 * two runs of the same command line are the same machine. */
static bool parse_clock(const char *spec, ap_mc146818_time_t *out) {
  unsigned y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
  const int n = sscanf(spec, "%u-%u-%uT%u:%u:%u", &y, &mo, &d, &h, &mi, &sec);
  if ((n != 3 && n != 6) || mo < 1u || mo > 12u || d < 1u || d > 31u ||
      h > 23u || mi > 59u || sec > 59u) {
    return false;
  }
  /* Sakamoto, so the day of week is derived rather than asked for: a caller
   * that had to supply it could supply a wrong one, and the calendar's own
   * register would then disagree with its date. */
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  unsigned yy = y - (mo < 3u ? 1u : 0u);
  const unsigned dow =
      (yy + yy / 4u - yy / 100u + yy / 400u + (unsigned)t[mo - 1u] + d) % 7u;
  *out = (ap_mc146818_time_t){.year = y,
                              .month = (uint8_t)mo,
                              .day = (uint8_t)d,
                              /* the register is 1-7 with Sunday as 1 */
                              .day_of_week = (uint8_t)(dow + 1u),
                              .hour = (uint8_t)h,
                              .minute = (uint8_t)mi,
                              .second = (uint8_t)sec};
  return true;
}

#define AP_BOOT_TYPE_OS_PC 0x02000000u

/* The boot PROM's one-byte type-ahead slot, as an offset from its globals base
 * in `A6`.
 *
 * `$272E` runs before **every character the firmware prints** -- from `0026A0`
 * in the serial output path and `00479A` in the display one -- and drains a
 * character out of the receive FIFO to look for an XOFF. Anything that was not
 * flow control is pushed back here, and `002726` **drops it when the slot is
 * already occupied**, silently. So the firmware can hold exactly one character
 * of type-ahead while it is printing, and a sender whose readiness test is "the
 * receive buffer is empty" outruns it: the buffer is empty the instant the
 * firmware drains it, including when it drains it into a slot it then discards.
 *
 * Measured: a keyboard dialogue lost five characters this way, with the port
 * reporting every one delivered and nothing discarded. A person at a terminal
 * never outruns a one-character slot, which is why the firmware is right and
 * only the harness was wrong. */
#define AP_BOOT_TYPE_PUSHBACK_OFFSET 0x14Cu
#define AP_BOOT_TYPE_PUSHBACK_EMPTY 0xFFFFu

#define AP_BOOT_TYPE_CHUNK 4096u


/* Deliver the next typed character if the machine is ready for it. Shared by
 * the step loop and the chunked fast path, because the *condition* is the same
 * and only the sampling rate differs. */
static void typed_deliver(ap_machine_t *machine, ap_board_t *board,
                          const char *typed, size_t typed_length,
                          size_t *typed_sent, ap_time_t *typed_at,
                          unsigned *flushed_was, unsigned *reads_was,
                          bool *pending, bool type_after_os, bool armed,
                          bool await_pushback, bool *first_done) {
  if (typed == NULL || *typed_sent >= typed_length) {
    return;
  }
  /* ## Re-deliver what the machine threw away, rather than predict when it
   * will stop throwing things away
   *
   * Five rules were tried for *when* a character may be typed: the machine is
   * polling; the MMU is on; code above the diagnostics is running; the
   * receive buffer is empty; the command register has been quiet. Each was a
   * true statement about the machine and none of them kept the `y`.
   *
   * The trace says why, and it is not a timing miss. Domain/OS takes the
   * console over by rewriting the port -- `CRA` `05 0A 2A 3A 10 45` -- and
   * `2A`'s miscellaneous command is **reset receiver**, §4.2.7.2, which
   * destroys the FIFO's contents. It does this **twice**, with a settled
   * screen and a quiet command register in between, so no predicate evaluated
   * before the first burst can know the second is coming. A rule that waits
   * for the reconfiguration to finish cannot be written, because from inside
   * the machine there is no such moment.
   *
   * So this does not predict. It observes: `rx_flushed` counts characters the
   * channel discarded unread, and a rise in it while ours is the character in
   * flight means ours was destroyed. Then it is sent again -- which is what a
   * person does when a keystroke produces nothing. The counter cannot mistake
   * a consumed character for a discarded one; a FIFO emptied by a read is not
   * counted. */
  if (*pending) {
    if (ap_sio_receiver_reads(&board->sio, 0u, 0u) != *reads_was) {
      /* Read out of the FIFO: it arrived, whatever the machine then did with
       * it. This is checked first because a read and a reset can both have
       * happened since the last look, and in that order the character was not
       * lost. */
      *pending = false;
    } else if (ap_sio_receiver_flushed(&board->sio, 0u, 0u) != *flushed_was) {
      *pending = false;
      (*typed_sent)--;
      fprintf(stderr,
              "apollo: --boot-type: '%c' was flushed unread; sending again\n",
              typed[*typed_sent]);
    }
  }
  /* **And not before the operating system is running, when asked.**
   *
   * "The machine is polling hard" does not distinguish a prompt from a
   * self-test that polls harder. `KEYBOARD TEST # 0` waits on the ISR
   * 65,536 times per exchange, which trips any threshold a prompt would,
   * and a `y` delivered into it lands in the receive FIFO in the middle of
   * the loopback echo comparison: measured, `SELF TEST FAILED ... ACTUAL=
   * 0000FF11, ADDRESS= 00010407, PC= 0000732E`. The typed character became
   * the byte the firmware was comparing.
   *
   * The gate is a machine *state* rather than a tuned instruction count:
   * The first attempt at it was **"the MMU is enabled"**, on the reasoning that
   * the boot PROM runs with translation off and an operating system turns it
   * on. True, and useless: `SELF_TEST`'s own `CPU (MMU) TEST #0` turns it on
   * hundreds of millions of instructions earlier. Measured -- the two
   * characters went immediately after the PROM's keyboard tests, into a machine
   * still running diagnostics, and the prompt they were meant for never saw
   * them.
   *
   * What separates them is **which address space is executing**: the PROM is at
   * `0000xxxx`, `SELF_TEST` is loaded at `0100xxxx`, and Domain/OS runs high --
   * `3C43F5AC` at the calendar prompt. A threshold above the diagnostics says
   * the operating system is running without claiming when it started, which a
   * count could not. */
  /* `armed` is `--boot-type-after-pc`'s half of the same question. The
   * threshold above cannot express "after the Mnemonic Debugger's banner",
   * because MD runs in the *boot PROM* at `0000xxxx` -- **below** every
   * threshold, not above one. So the second gate is a one-shot trigger on a PC
   * having been *reached*, which is what a prompt inside the firmware needs.
   *
   * Measured, and it is why this exists: typing `EX DOMAIN_OS\r` at the
   * keyboard delivers all thirteen characters within ~125 ms of emulated time,
   * while the PROM does not reach its console-selection poll until after the
   * display and memory tests. Every character but the last is gone before
   * anything is listening, and the run parks in MD's read poll at `002684`
   * having typed everything and submitted nothing. */
  const bool typing_allowed =
      armed && (!type_after_os || machine->cpu.regs.pc >= AP_BOOT_TYPE_OS_PC);
  /* **The poll threshold gates the first character only.** Rearming it per
   * character was measured wrong twice, with the same shortfall both times:
   * `polls 217931 (need 219228)`, 1,297 short after seven hundred million
   * further instructions. Once Domain/OS has taken a character it stops
   * polling the status registers -- it waits by some other means -- so a
   * rule that demands two thousand *more* polls waits for something that
   * will not happen.
   *
   * What says the machine is ready for the next one is that it took the
   * last: `ap_sio_receiver_ready` false means the receive buffer is empty
   * again. After that the only bound is the wire's, one character time,
   * which is also the fastest a person could type. */
  /* **`*typed_sent == 0` is the wrong test for "the first character ever" once
   * there are two phases**, because the phase switch resets the index. The
   * second phase's first character then took the poll rule instead of the
   * character-time rule and went the instant its gate armed, without the wire's
   * own spacing after the phase before it -- measured as one character lost at
   * exactly that boundary, and not fixed by moving the gate, which is what
   * showed the rule rather than the moment was wrong. `first` is carried across
   * phases so the poll threshold applies once, to the first character of the
   * run, which is what it was written for. */
  const bool typed_due =
      !*first_done ? input_polls(board) >= AP_BOOT_KEY_POLLS
                   : machine->now >= *typed_at + ap_sio_character_time(
                                                 &board->sio, 0u, 0u,
                                                 AP_SIO_KEYBOARD_BAUD);
  /* The firmware's own condition, when asked for. `A6` is the PROM's globals
   * base while PROM code is running, which is the only time this matters. */
  bool pushback_clear = true;
  if (await_pushback) {
    uint32_t slot = 0u;
    const uint32_t at = machine->cpu.regs.a[6] + AP_BOOT_TYPE_PUSHBACK_OFFSET;
    pushback_clear =
        ap_board_peek_ram(board, at, 2u, &slot) &&
        (uint16_t)slot == AP_BOOT_TYPE_PUSHBACK_EMPTY;
  }
  if (*typed_sent < typed_length && typing_allowed && typed_due &&
      pushback_clear && !ap_sio_receiver_ready(&board->sio, 0u, 0u) &&
      ap_sio_character_bits(&board->sio, 0u, 0u) == 8u &&
      ap_sio_receiver_enabled(&board->sio, 0u, 0u)) {
    if (ap_board_key_type(board, typed[*typed_sent])) {
      (*typed_sent)++;
      *first_done = true;
      *typed_at = machine->now;
      /* Armed against this character, from now: see above. */
      *flushed_was = ap_sio_receiver_flushed(&board->sio, 0u, 0u);
      *reads_was = ap_sio_receiver_reads(&board->sio, 0u, 0u);
      *pending = true;
    } else {
      /* A character this keyboard cannot produce. Skipped rather than
       * retried for ever, and said out loud -- a silently dropped character
       * makes a script that never worked look like a machine that ignored
       * it. */
      fprintf(stderr, "apollo: --boot-type: no key produces %c\n",
              typed[*typed_sent]);
      (*typed_sent)++;
      *first_done = true;
    }
  }
}

static int boot_from_prom(const char *path, unsigned limit, bool trace,
                          uint32_t watch, const char *input, unsigned input_unit,
                          unsigned input_channel, uint8_t input_rate,
                          unsigned input_interval_us,
                          unsigned key, const char *typed, bool type_after_os,
                          uint32_t type_after_pc, const char *typed2,
                          uint32_t type2_after_pc,
                          bool type_await_pushback,
                          const ap_mc146818_time_t *clock, bool boot_report,
                          bool console,
                          ap_screen_kind_t screen, uint32_t node_id,
                          ap_model_id_t model, const char *screenshot,
                          unsigned trace_last, uint32_t stop_pc,
                          const char *console_script_path,
                          const char *disk_path, const char *battery_path,
                          unsigned ram_megabytes,
                          const char *dump_spec,
                          unsigned progress_every, bool stop_on_refusal,
                          uint32_t watch_write, unsigned stop_on_watch,
                          uint32_t watch_read, unsigned stop_on_watch_read,
                          uint32_t stop_pc_length,
                          const char *const *dump_logical,
                          unsigned dump_logical_count,
                          uint32_t stop_physical_pc,
                          uint32_t stop_physical_length, bool service_mode,
                          unsigned stop_pc_skip, uint32_t stop_mmu_fault_at,
                          unsigned stop_pc_then, uint32_t progress_from) {
  /* Before the PROM is even opened: a script that does not parse is the
   * caller's mistake and should be reported as one, not hidden behind whichever
   * file happens to be missing first. */
  static ap_console_script_t script;
  if (console_script_path != NULL &&
      !console_script_load(&script, console_script_path)) {
    return 1;
  }

  long size = 0;
  uint8_t *prom = read_file(path, &size);
  if (prom == NULL) {
    fprintf(stderr, "apollo: cannot read boot PROM %s\n", path);
    return 1;
  }

  /* 16 MB, which is a size the RAM configuration table covers -- the boot PROM
   * reads that strap to size memory and a machine whose size is not in the
   * table cannot tell it anything. It was 4 MB, which is not a configuration a
   * DN3500 can be built in: four banks of 4 MB is the smallest the byte
   * describes at all.
   *
   * **Capped at what the model can take**, which is machine variance and so
   * comes out of the model table rather than from a constant here. Sixteen
   * megabytes on a DN3000 is not a small error: its maximum is eight, the
   * strap table has no entry for a size the machine cannot be built in, so the
   * board goes out unstrapped, and the firmware's memory test fails with
   * `E0060882` before it reaches anything else. `--ram` overrides. */
  uint32_t ram_bytes = ram_megabytes * 1024u * 1024u;
  uint8_t *ram = calloc(1, ram_bytes);
  ap_board_t *board = calloc(1, sizeof *board);
  /* The instant this machine powers on with. A default rather than a constant:
   * `--clock` overrides it, because a volume written later than the clock makes
   * an operating system stop and say so. */
  const ap_mc146818_time_t epoch =
      clock != NULL ? *clock
                    : (ap_mc146818_time_t){.year = 1987u,
                                           .month = 7u,
                                           .day = 31u,
                                           .day_of_week = 6u,
                                           .hour = 21u,
                                           .minute = 9u,
                                           .second = 21u};
  /* The memory array's parity RAM: one bit per byte, allocated here because the
   * core allocates nothing. A real board has it, so a booted machine gets it --
   * self-test 7 forces bad parity and expects the level 7 interrupt back. */
  const uint32_t parity_bytes = (ram_bytes + 7u) / 8u;
  uint8_t *parity = calloc(1, parity_bytes);
  ap_trace_ring_t *trace_ring =
      trace_last > 0u ? calloc(trace_last, sizeof *trace_ring) : NULL;
  unsigned trace_ring_used = 0;
  if (trace_last > 0u && trace_ring == NULL) {
    fprintf(stderr, "apollo: cannot keep %u trace step(s)\n", trace_last);
    return 1;
  }
  if (ram == NULL || board == NULL || parity == NULL ||
      !ap_board_init_model(board, ram, ram_bytes, &epoch, node_id, model) ||
      !ap_board_attach_parity(board, parity, parity_bytes)) {
    free(trace_ring);
    free(parity);
    free(board);
    free(ram);
    free(prom);
    fprintf(stderr, "apollo: cannot build the core board\n");
    return 1;
  }
  /* Before anything runs: the set is configuration, and it is hashed.
   *
   * **This was missing on the boot path.** `--oracle-quirk` was applied only
   * where a board is built for a probe run, so on a boot it parsed the name,
   * refused a bad one, and then silently changed nothing -- a comparison run
   * would report the reference machine's behaviour while claiming the oracle's.
   * Found because selecting a quirk left the state hash byte-identical, which
   * it cannot do when the set is hashed. */
  ap_board_set_quirks(board, g_quirks);

  /* Fit a display, if one was asked for. The memories are allocated here rather
   * than in the core, which allocates nothing -- and only when a screen is
   * fitted, so a machine without one has no frame buffer rather than an empty
   * one. */
  uint8_t *colour_memory = NULL;
  uint8_t *mono_memory = NULL;
  if (screen != AP_SCREEN_NONE) {
    /* The card's whole image memory, which is **not** the size of the CPU's
     * window onto it. An 8-plane board carries 1 MB in eight planes and the
     * window at `0A0000-0BFFFF` is 128 KB -- one plane -- which is why the
     * plane-select registers exist at all. Allocating the window's size, which
     * is what this did first, gives a card with one eighth of its memory and a
     * scanout that correctly refuses to run.
     *
     * The window is **exactly one plane** -- 128 KB is 1024x1024 bits, and the
     * 1280x1024 monochrome board's 256 KB is 2048x1024 -- so an offset in it is
     * a word offset *within* a plane and there is no plane selector to model.
     * Which planes an access reaches is `CR2`'s, applied by the blitter's plane
     * loop. This carried a documented approximation until the two sizes were
     * put beside each other; the arithmetic is the proof. */
    ap_graphics_geometry_t geometry;
    const bool fitted = ap_graphics_geometry(screen, &geometry);
    const uint32_t image_bytes =
        fitted ? geometry.plane_words * 2u * geometry.planes : 0u;
    const uint32_t window_colour =
        AP_GRAPHICS_COLOUR_MEMORY_END - AP_GRAPHICS_COLOUR_MEMORY_ADDR + 1u;
    const uint32_t window_mono =
        AP_GRAPHICS_MONO_MEMORY_END - AP_GRAPHICS_MONO_MEMORY_ADDR + 1u;
    /* Whichever is larger: the memory must hold the picture, and the window
     * must not address past the end of the buffer it decodes into. */
    const bool is_colour = ap_graphics_is_colour(screen);
    const uint32_t colour_bytes =
        (is_colour && image_bytes > window_colour) ? image_bytes : window_colour;
    const uint32_t mono_bytes =
        (!is_colour && image_bytes > window_mono) ? image_bytes : window_mono;
    colour_memory = calloc(1, colour_bytes);
    mono_memory = calloc(1, mono_bytes);
    if (colour_memory == NULL || mono_memory == NULL) {
      free(colour_memory);
      free(mono_memory);
      free(board);
      free(trace_ring);
      free(parity);
      free(ram);
      free(prom);
      fprintf(stderr, "apollo: cannot allocate the graphics memories\n");
      return 1;
    }
    ap_graphics_init(&board->graphics, screen);
    ap_graphics_attach_memory(&board->graphics, colour_memory, colour_bytes,
                              mono_memory, mono_bytes);
  }

  /* Fit a Winchester, if one was named. The controller and the image reader
   * have both been complete for a while and nothing ever handed one to the
   * other, so every boot so far has run on a machine with **no disk**: the
   * firmware finds no drive, which is a different failure from finding a broken
   * one and was being read as the latter.
   *
   * The image is owned here, as every image in this core is, and stays mapped
   * for the whole run. 348 MB is the reference drive; a shorter file is opened
   * against the same geometry and reads past its end fail, which is what a
   * partly written image should do. */
  uint8_t *disk_bytes = NULL;
  ap_awd_t disk_image;
  if (disk_path != NULL) {
    long disk_size = 0;
    disk_bytes = read_file(disk_path, &disk_size);
    if (disk_bytes == NULL) {
      free(colour_memory);
      free(mono_memory);
      free(board);
      free(trace_ring);
      free(parity);
      free(ram);
      free(prom);
      fprintf(stderr, "apollo: cannot read disk image %s\n", disk_path);
      return 1;
    }
    /* **Writable**, and the file is not: `disk_bytes` is a private copy read
     * into memory above and freed at exit, and nothing here ever writes it
     * back. So the drive the machine sees behaves like a drive, and the user's
     * image on disk is untouched either way.
     *
     * It was opened read-only, which looked like the careful choice and was
     * not. An operating system cannot reach a login prompt on a disk it may not
     * write to, and Domain/OS's first write -- a `1F WRITE DATA FROM BUFFER` to
     * cylinder 0, head 0, sector 1, the second sector of the disk -- failed for
     * that reason and only that reason. Protecting the file is right; making
     * the machine believe its disk is write-protected is a different thing, and
     * this was doing the second while meaning the first. */
    if (!ap_awd_open(&disk_image, disk_bytes, (size_t)disk_size,
                     ap_awd_geometry_for(AP_AWD_DRIVE_348MB), true)) {
      free(disk_bytes);
      free(colour_memory);
      free(mono_memory);
      free(board);
      free(trace_ring);
      free(parity);
      free(ram);
      free(prom);
      fprintf(stderr, "apollo: %s is not an Apollo Winchester image\n",
              disk_path);
      return 1;
    }
    ap_omti_attach(&board->disk.controller, &disk_image);
    printf("disk %s, %ld bytes\n", disk_path, disk_size);
  }

  /* The calendar's battery. `008778-03` §3.6: the chip "has a backup battery to
   * ensure that no data is lost when the ac power is removed", and every run of
   * this core until now has been a machine whose battery is flat -- which is
   * what the boot PROM means by "Configuration information is not initialized".
   *
   * A missing file is not an error: it is a machine that has never been
   * configured, which is the state to start from and the one every previous run
   * has had. The file holds the fifty battery bytes and **not** the clock, for
   * the reason `ap_calendar.h` gives -- persisting the time would make a run's
   * starting instant depend on when the last one ended. */
  if (battery_path != NULL) {
    long battery_size = 0;
    uint8_t *battery = read_file(battery_path, &battery_size);
    if (battery != NULL) {
      ap_calendar_load_battery(&board->calendar, battery,
                               (unsigned)battery_size);
      printf("calendar ram %s, %ld byte(s) loaded\n", battery_path,
             battery_size);
      free(battery);
    } else {
      printf("calendar ram %s, absent -- an unconfigured machine\n",
             battery_path);
    }
  }

  if (!ap_board_load_prom(board, prom, (uint32_t)size)) {
    free(board);
    free(trace_ring);
    free(parity);
    free(ram);
    free(prom);
    fprintf(stderr, "apollo: %s does not fit the boot PROM region\n", path);
    return 1;
  }

  uint32_t stack = 0;
  uint32_t pc = 0;
  /* The Normal/Service switch, applied after the board is up and before the
   * first instruction runs. It is an input the machine samples, not a condition
   * the board raises, so it is set once here rather than maintained. */
  ap_boardreg_set_normal_mode(&board->registers, !service_mode);

  if (!ap_board_reset_vector(board, &stack, &pc)) {
    free(board);
    free(trace_ring);
    free(parity);
    free(ram);
    free(prom);
    fprintf(stderr, "apollo: %s carries no reset vector\n", path);
    return 1;
  }

  /* A watch address must name memory. This reads through `ap_board_read` every
   * step, so watching a device register would *perturb the machine*: reading a
   * DUART's receive buffer pops its FIFO, and every read inflates the
   * per-region counters. An instrument that changes what it measures is worse
   * than none, and a comment saying so is not a guard -- the mistake is one
   * keystroke away and its symptom is a run that is merely *different*, with
   * nothing to say it was the watching that changed it. */
  if (watch != 0u) {
    const ap_board_region_t region = ap_board_region(board, watch);
    if (region != AP_BOARD_REGION_RAM && region != AP_BOARD_REGION_PROM) {
      fprintf(stderr,
              "apollo: --boot-watch %08X is in %s, not memory. Watching a "
              "device register would change the run it is measuring.\n",
              watch, ap_board_region_name(region));
      free(board);
      free(trace_ring);
      free(parity);
      free(ram);
      free(prom);
      return 1;
    }
  }

  printf("boot PROM %s\n", path);
  printf("  size         %lu\n", (unsigned long)size);
  printf("  reset SSP    %08X (%s)\n", stack,
         ap_board_region_name(ap_board_region(board, stack)));
  printf("  reset PC     %08X (%s)\n", pc,
         ap_board_region_name(ap_board_region(board, pc)));
  /* The instant the machine powered on, for the same reason the display is
   * reported beside the hash: the calendar's registers are hashed, `--clock`
   * moves them, and nothing else in a run's output says which epoch it used. */
  printf("  power-on     %04u-%02u-%02uT%02u:%02u:%02u\n", epoch.year,
         epoch.month, epoch.day, epoch.hour, epoch.minute, epoch.second);

  ap_machine_t machine;
  /* The same model as the board, or the processor would run at one machine's
   * clock over another machine's address space. */
  ap_machine_init_model(&machine, ram, ram_bytes, model);
  ap_machine_set_board(&machine, board);
  machine.watch_write_address = watch_write;
  machine.mmu_fault_stop_address = stop_mmu_fault_at;
  machine.watch_read_address = watch_read;
  ap_machine_reset(&machine, pc, stack);

  /* Scripted serial input, delivered a byte at a time as the firmware takes the
   * previous one. Not host input: a byte sequence decided before the run
   * starts, so the run stays reproducible. That is the whole reason this
   * frontend exists, and a getchar() here would end it.
   *
   * The port *and channel* are choices, not constants. The oracle settles
   * which is which: the keyboard drives serial 1 channel A and a terminal
   * drives serial 1 channel B, so ASCII belongs on B and scan codes on A.
   *
   * The port is a choice, not a constant: the PROM's poll loop tests *both*
   * DUARTs and branches differently for each, so which one carries the console
   * is a question the firmware answers rather than one to assume. */
  unsigned key_state = 0u; /* 0 press, 1 release, 2 done */
  /* Typed text, and where the poll counter stood when the last character went.
   * The *rearm* is the point: a cumulative count past the threshold stays past
   * it for ever, so one baseline per character is what makes the second wait
   * for a fresh poll rather than going out in the same instant as the first. */
  size_t typed_sent = 0u;
  /* ## The dialogue is two phases, because one gate cannot express it
   *
   * Measured: driving the Mnemonic Debugger from the keyboard needs two
   * characters *before* its banner -- one to select the console, one carriage
   * return to enter it -- and the command *after*. The boot PROM reads
   * everything typed while it is printing that banner and discards it, which is
   * ordinary type-ahead handling and not a defect: a run reported 15 characters
   * typed, `sio1 reg 3` read 15 times, `rx_flushed` never moved, and MD
   * received `AIN_OS` out of `EX DOMAIN_OS`. Six characters read by the
   * firmware and dropped.
   *
   * So `--boot-type` gets a second string with its own arming address, and the
   * cursor moves to it once the first is spent. Two phases and not N because
   * two is what the machine needs and a general list would be untested
   * generality. */
  const char *typed_phase[2] = {typed, typed2};
  const uint32_t typed_phase_pc[2] = {type_after_pc, type2_after_pc};
  unsigned typed_phase_at = 0u;
  const char *typed_now = typed_phase[0];
  size_t typed_length = typed_now != NULL ? strlen(typed_now) : 0u;
  ap_time_t typed_at = 0u;
  unsigned typed_flushed_was = 0u;
  unsigned typed_reads_was = 0u;
  bool typed_pending = false;
  /* `--boot-type-after-pc`: unset means armed from reset. */
  bool typed_armed = typed_phase_pc[0] == 0u;
  /* Carried across the phase switch, unlike `typed_sent`. */
  bool typed_first_done = false;
  size_t input_sent = 0;
  const size_t input_length = input != NULL ? strlen(input) : 0u;

  /* One character time at the line's own rate, which is the floor the *wire*
   * imposes: a character cannot be delivered closer together than its own
   * framing takes, whatever the far end is doing.
   *
   * **Ten bit times was the assumption here, and it is only right for 8N1.**
   * The frame is a start bit, the data bits `MR1[1:0]` asks for, a parity bit
   * when `MR1[2]` is clear, and a stop length from `MR2[3:0]` -- which is
   * sixteen encodings from 0.5 to 2 bits, not a one-or-two flag. So the figure
   * comes from the mode registers now, through
   * `ap_mc68681_character_time`, and a link running with parity or a long stop
   * is paced at the rate it actually runs at rather than a tenth faster.
   *
   * `MD.md`'s capture used 0.4 s between carriage returns, which is a person
   * typing and is four hundred times this. The requirement it was recording is
   * satisfied by any gap long enough for the firmware to rewrite a clock select
   * between characters; the physical floor is the one this can justify without
   * choosing a number, and it is three orders of magnitude cheaper to run.
   *
   * Zero for the rates that are not a fixed rate -- the timer and the two
   * external clocks -- where this cannot know the character time and so imposes
   * no gap. */
  const unsigned input_baud =
      ap_mc68681_baud((uint8_t)(input_rate & 0x0Fu), true);
  /* How long to leave between scripted characters.
   *
   * The floor is one character time -- ten bit times at the line's own rate --
   * because a byte cannot be delivered faster than the wire carries it, and
   * this defaulted to exactly that. `docs/references/MD.md` records a different
   * figure from the other side: "one carriage return every 0.4 s on standard
   * input, not a pipe delivered at once". Those are not the same requirement
   * and the code claimed the second while implementing the first, which is 400
   * times faster at 9600 baud.
   *
   * So the interval is settable, and the wire's floor is enforced under
   * whatever is asked for -- a caller cannot request something the line cannot
   * carry. `input_interval_us` of zero keeps the floor, which is what every
   * existing use expects. */
  const ap_time_t wire_floor = ap_sio_character_time(
      &machine.board->sio, input_unit, input_channel, input_baud);
  ap_time_t requested = 0u;
  if (input_interval_us > 0u) {
    requested = (ap_time_t)input_interval_us * (AP_TIME_BASE_HZ / 1000000u);
  }
  const ap_time_t input_interval =
      requested > wire_floor ? requested : wire_floor;
  ap_time_t input_next_at = 0u;

  ap_machine_run_t run;
  /* **`typed_length` belongs in this list, and leaving it out is the trap this
   * file has now been caught by three times.** The step-by-step loop is where
   * every per-instruction check lives -- the PC stops, the scripted input, the
   * keypress, and now the typed text -- and a run that enters none of the
   * conditions above skips all of them silently. `--boot-progress` was found
   * mute for the same reason, and two "never reached" conclusions about PC
   * stops were artefacts of it.
   *
   * `--boot-type` with every delivery condition satisfied typed nothing, and
   * the diagnostic printed `port: 8 bit(s), receiver enabled, free, polls
   * 203,962 (need 2000)` -- all four conditions true, and the code testing them
   * never ran. */
  /* **Every per-step feature, and the list had less than half of them.** The
   * stops, the progress report and the watch limits all live inside this loop
   * and none of them was a reason to enter it -- so `--boot-progress` was mute,
   * `--boot-stop-pc` did nothing, and two conclusions of the form "that address
   * is never executed" were drawn from runs that were never watching. Every
   * measurement in this session had to carry a `--boot-console` it did not
   * otherwise need, to force the loop on.
   *
   * The contract is that anything checked per instruction belongs here. It is
   * still a hand-maintained list, which is the honest state of it -- but it is
   * now complete, and the `boot type` diagnostic reports when a flag was asked
   * for and did nothing, which is the failure this class produces. */
  const bool wants_steps =
      trace || trace_last > 0u || input_length > 0u || console ||
      script.steps > 0u || key < AP_KBD_KEYS ||
      progress_every > 0u || stop_pc != 0u || stop_physical_pc != 0u ||
      stop_mmu_fault_at != 0u ||
      stop_on_watch != 0u || stop_on_watch_read != 0u || stop_on_refusal;
  if (wants_steps) {
    /* Step by step, reporting the program counter and the active stack pointer.
     *
     * A7 is the observable this exists for. A wrong PC is where damage becomes
     * visible; a stack pointer that stops matching the call depth is where it
     * happens, and the two can be thousands of instructions apart. Printing
     * both together is what lets one be found from the other. */
    /* How many times the stop address has been reached, so a run can be told
     * to ignore the first N. An address executed once on a path that recovers
     * and again on one that does not is one address and two events, and only
     * the second is the question. */
    unsigned stop_pc_seen = 0u;
    unsigned stop_pc_countdown = 0u;
    bool progress_started = false;
    unsigned progress_base = 0u;
    bool stop_pc_armed = false;
    run = (ap_machine_run_t){.status = AP_M68030_STEP_EXECUTED};
    if (trace && trace_last == 0u) {
      printf("# step pc a7 a6 a0 instruction status%s\n",
             watch != 0u ? " watched" : "");
    }
    for (unsigned i = 0; i < limit; i++) {
      /* A heartbeat, on stderr so it cannot be mistaken for something the
       * machine said. A boot that has printed nothing for ten minutes is either
       * an operating system initialising quietly or a program going round a
       * loop, and the console cannot tell those apart -- both are silence. The
       * program counter can: a run reporting the same address every time is
       * stuck, and one reporting different addresses is working. */
      if (progress_from != 0u && !progress_started &&
          machine.cpu.regs.pc == progress_from) {
        /* Zero the counter here: the interesting instant is when this code was
         * reached, not when the machine was switched on, and two machines that
         * arrive at different absolute counts must sample the same deltas for
         * their PCs to be comparable at all. */
        progress_started = true;
        progress_base = i;
      }
      if (progress_every != 0u && (progress_from == 0u || progress_started) &&
          i > progress_base && ((i - progress_base) % progress_every) == 0u) {
        const uint32_t here = machine.cpu.regs.pc;
        uint32_t physical = here;
        const bool mapped =
            ap_machine_translate(&machine, here, 6u, &physical);
        fprintf(stderr, "  progress     %u instruction(s), pc %08X", i, here);
        if (mapped && physical != here) {
          fprintf(stderr, " -> %08X", physical);
        }
        fprintf(stderr, "%s\n", mapped ? "" : " (no translation)");
        (void)fflush(stderr);
      }
      /* Feed the next byte only once the program has taken the last **and** a
       * terminal's worth of time has passed since the one before.
       *
       * "The program has taken the last" alone is not a terminal, it is a pipe:
       * it delivers the next character microseconds later, and the firmware's
       * console negotiation needs the gap. Measured, the two are 35 instructions
       * apart -- about 5.6 us against a real terminal's 0.4 s, some seventy
       * thousand times too fast.
       *
       * What that costs is not a lost byte but a *wrong* one. The autobaud arms
       * itself on a mis-framed character and reprograms the port; the character
       * that follows is meant to arrive at the new rate and be the clean one.
       * Delivered before the firmware has rewritten the clock select, it is the
       * old rate's garbage instead, it consumes the armed state, and the clean
       * character that comes next has nothing left to consume it. The
       * negotiation cycles forever making progress it immediately loses.
       *
       * `docs/references/MD.md` recorded the requirement from the other side --
       * "one carriage return every 0.4 s on standard input, not a pipe
       * delivered at once" -- and this could not honour it until the machine
       * advanced time at all, which is a Phase 3 tick-loop item away. */
      if (input_sent < input_length &&
          ap_machine_now(&machine) >= input_next_at &&
          !ap_sio_receiver_ready(&board->sio, input_unit, input_channel) &&
          ap_sio_character_bits(&board->sio, input_unit, input_channel) == 8u &&
          ap_sio_receiver_enabled(&board->sio, input_unit, input_channel)) {
        /* The same three conditions `--boot-key` needs, and for the same
         * reason: `MR1` resets to a five-bit link and a disabled receiver drops
         * what arrives, so a script that sent as soon as the FIFO was free was
         * sending into a port that could neither carry nor keep the byte.
         *
         * It matters more here than it looks. The firmware's autobaud
         * identifies the sender's rate from *what the wrong rate did to the
         * character*, so a byte truncated to five bits first arrives as a shape
         * the autobaud has no case for -- and the negotiation cannot even
         * begin. */
        /* Sent at the rate the terminal is set to. `77` is what the DN3500's
         * own firmware configures both ports to at reset, measured off the
         * oracle -- so a scripted terminal that used anything else would be
         * modelling a misconfigured cable rather than a console. */
        ap_sio_receive_at(&board->sio, input_unit, input_channel,
                          (uint8_t)input[input_sent], input_rate);
        input_next_at = ap_machine_now(&machine) + input_interval;
        /* Advance only if the receiver actually took it. A DUART whose receiver
         * is still disabled drops the byte, and the firmware enables it long
         * after reset -- so a script that advanced regardless would deliver its
         * whole text into a switched-off port and then wait forever for the
         * first character. Retrying costs nothing and cannot lose a byte. */
        if (ap_sio_receiver_ready(&board->sio, input_unit, input_channel)) {
          input_sent++;
        }
      }
      /* The script's own bytes, under exactly the same conditions -- the port
       * has to be able to carry them for the same reasons. It runs after the
       * fixed script so that `--boot-input`'s carriage return can still do the
       * autobaud before a dialogue begins. */
      if (input_sent >= input_length && script.steps > 0u &&
          ap_machine_now(&machine) >= input_next_at &&
          !ap_sio_receiver_ready(&board->sio, input_unit, input_channel) &&
          ap_sio_character_bits(&board->sio, input_unit, input_channel) == 8u &&
          ap_sio_receiver_enabled(&board->sio, input_unit, input_channel)) {
        const int byte = console_script_next(&script);
        if (byte >= 0) {
          ap_sio_receive_at(&board->sio, input_unit, input_channel,
                            (uint8_t)byte, input_rate);
          input_next_at = ap_machine_now(&machine) + input_interval;
          if (!ap_sio_receiver_ready(&board->sio, input_unit, input_channel)) {
            /* Not taken: put it back, the same retry the fixed script does. */
            script.sent--;
          }
        }
      }
      /* Drain both ports' transmitters every step, so nothing is lost when the
       * firmware writes two characters before we look again. Written straight
       * to stdout: this is the machine's console, and the whole point is to see
       * what it says rather than to interpret it. */
      for (unsigned unit = 0; unit < 2u; unit++) {
        for (unsigned channel = 0; channel < 2u; channel++) {
          uint8_t out_byte = 0;
          while (ap_board_transmitted(board, unit, channel, &out_byte)) {
            if (console) {
              fputc((int)out_byte, stdout);
              /* Flushed per character, not per line.
               *
               * A boot that runs for ten minutes writes its console into a
               * block buffer and shows nothing until it exits -- so a run that
               * has to be interrupted, or that is simply being watched, has
               * produced nothing at all. One such run was killed at ten
               * minutes and lost every byte it had printed.
               *
               * Per character rather than per line because the question the
               * machine asks is `Do you wish to continue (y,n)? `, with no
               * newline: line buffering would hold exactly the prompt a reader
               * is waiting on. The cost is one `write` per console byte, on a
               * stream that carries a few thousand of them in a whole boot. */
              (void)fflush(stdout);
            }
            /* The script watches the same stream the console prints, so what it
             * matches is exactly what a reader sees. */
            console_script_saw(&script, out_byte);
          }
        }
      }
      /* Press the key once the firmware is *waiting* for one, then release on
       * the next opportunity -- self-timed, because a fixed step number would
       * be a guess about how long the firmware takes to get there and would
       * silently do nothing if it took longer.
       *
       * **"Ready" and "waiting" are different, and this used to press on the
       * first.** The port is configured early, long before anything asks for
       * input, so a key delivered then is consumed by whatever the firmware
       * does next and is gone by the time a prompt appears. That is why a
       * `--boot-key` run against a display-fitted machine looked exactly like a
       * run with no key at all: the press happened, hundreds of millions of
       * instructions too soon.
       *
       * What distinguishes a prompt is *polling*. Firmware that is merely
       * running reads the status register a handful of times; firmware sitting
       * at a prompt reads it thousands of times over, which is visible in the
       * per-register read counters the board already keeps. So the condition is
       * a configured port **and** a long run of status reads with nothing
       * delivered. */
      if (key < AP_KBD_KEYS && key_state < 2u &&
          !ap_sio_receiver_ready(&board->sio, 0u, 0u) &&
          ap_sio_character_bits(&board->sio, 0u, 0u) == 8u &&
          ap_sio_receiver_enabled(&board->sio, 0u, 0u) &&
          input_polls(board) >= AP_BOOT_KEY_POLLS) {
        /* Eight bits, not merely a free receiver.
         *
         * `MR1` resets to a **five-bit** link, and this sent as soon as the
         * receiver was free -- which is immediately, thousands of instructions
         * before the firmware programs the port. A make code arrived with its
         * top three bits missing and a release code, which is the make code with
         * bit 7 set, could not arrive at all: both became `00`. So every
         * `--boot-key` run this project has taken delivered nothing, and looked
         * exactly like a run where the firmware ignored the keyboard.
         *
         * And enabled as well as eight bits wide: a disabled receiver drops
         * what arrives, so waiting only for the width delivered into a port
         * that was not yet listening -- measured, the byte went in at step 1223
         * and the FIFO stayed empty.
         *
         * The port's own configuration is the condition to wait on, which is
         * what "self-timed" was always supposed to mean here. */
        const bool moved = (key_state == 0u) ? ap_board_key_press(board, key)
                                             : ap_board_key_release(board, key);
        if (moved) {
          key_state++;
        }
      }

      /* `--boot-type`, on the same trigger and for the same reason: a character
       * goes when the machine is *waiting* for one. The difference is that a
       * string needs the condition to rearm, so the threshold is measured from
       * the last delivery rather than from zero.
       *
       * This is what a prompt deep inside an operating system needs and a fixed
       * step number cannot give: Domain/OS's calendar question arrives some
       * seven hundred million instructions in, and any constant chosen for it
       * would be a measurement of one boot rather than a condition. */
      /* Arm this phase, then move to the next once it is spent. The gate is
       * re-evaluated per phase, so the command waits for MD's prompt while the
       * two characters that produce that prompt waited only for the console
       * poll. */
      if (!typed_armed && machine.cpu.regs.pc == typed_phase_pc[typed_phase_at]) {
        typed_armed = true;
      }
      if (typed_sent >= typed_length && typed_phase_at == 0u &&
          typed_phase[1] != NULL) {
        typed_phase_at = 1u;
        typed_now = typed_phase[1];
        typed_length = strlen(typed_now);
        typed_sent = 0u;
        typed_pending = false;
        typed_armed = typed_phase_pc[1] == 0u;
      }
      typed_deliver(&machine, board, typed_now, typed_length, &typed_sent,
                    &typed_at, &typed_flushed_was, &typed_reads_was,
                    &typed_pending, type_after_os, typed_armed,
                    type_await_pushback, &typed_first_done);
      const uint32_t step_pc = machine.cpu.regs.pc;
      /* One instruction through the *machine*, not through the processor.
       *
       * This called `ap_m68030_step` directly, which is the CPU and nothing
       * else: no interrupt sampling, no bus tick, no stall for another master,
       * and no device advanced. So the boot -- the most important path this
       * frontend has -- ran on a machine where no time passed at all, and every
       * timer the firmware programmed stood still while it waited for one. The
       * `elapsed` line reported zero for exactly that reason and nobody read
       * it as the symptom it was.
       *
       * `ap_machine_run` with a limit of one is the whole of the machine's own
       * loop, once. Keeping the frontend's stepping on that rather than beside
       * it is what stops the two diverging again. */
      /* So a watch can name the instruction rather than a byte inside it. */
      machine.executing_address = step_pc;
      const ap_machine_run_t one = ap_machine_run(&machine, 1u);
      /* The instruction word is read back from where it executed, since the
       * machine's loop reports why a run ended and not which word did it.
       *
       * Read as the *processor* addressed it. This was `ap_machine_read` of the
       * PC, which is a physical read of a logical address: correct while the
       * MMU is off, and once Domain/OS turns it on, a read of a number no
       * memory answers. Every word in the trace came back `0000` and the column
       * read like a machine executing zeros. */
      /* **Only when something consumes it.** The read-back translates a
       * logical address, and with the MMU on that is a full walk of the tree --
       * three descriptor fetches, each four byte-wide board reads. Done
       * unconditionally it was twelve board accesses per instruction for a
       * column nobody had asked to see.
       *
       * Measured, which is how it was found: at 350 M instructions a stepped
       * boot performed 266,700,639 probe walks against the machine's own
       * 56,688 -- **99.98% of all table walks in the run were this line** --
       * and `perf` put `ap_board_read` at 28% of the whole profile, the single
       * largest entry by a factor of four. The Phase 7 item that asks where the
       * time goes was reading a profile dominated by its own instrument.
       *
       * The word is still always available when it matters. A trace or a ring
       * wants it every step; a run that ends abnormally reports it once, and
       * that ending is not a step that executed -- so the core's own
       * `one.instruction` covers it, which is the value the branch below
       * already preferred in exactly that case. */
      const bool want_word = trace || trace_last > 0u;
      uint32_t executed_word = 0;
      if (want_word) {
        (void)ap_machine_read_logical(&machine, step_pc, 6u, 2u,
                                      &executed_word);
      }
      const ap_m68030_step_result_t r = {
          .status = one.status,
          /* The core's own word when it stopped on one, since a read-back
           * cannot see what the caches decoded; the read-back otherwise, which
           * is what fills the column for every step that ran. */
          .instruction = want_word && (one.status == AP_M68030_STEP_EXECUTED ||
                                       one.status == AP_M68030_STEP_EXCEPTION)
                             ? (uint16_t)executed_word
                             : one.instruction};
      /* Recorded **before** any stop is considered, so the instruction that
       * triggers a stop is in the ring rather than the one before it. It was
       * after, and the off-by-one was invisible until a watch reported a write
       * by `3C49EE46` while the ring's last entry was `3C49EC48` -- two
       * addresses that are not even adjacent, and a chain of reasoning was
       * built on the wrong one. */
      if (trace_last > 0u) {
        ap_trace_record(&trace_ring[trace_ring_used % trace_last], i, step_pc,
                        &machine.cpu, r.instruction, r.status);
        trace_ring_used++;
      }
      /* The refusal, rather than an address.
       *
       * The instructions that computed a wild disk address are the ones just
       * before the controller refuses it, and the run ends four hundred million
       * instructions later -- so a ring kept to the end of the run holds a
       * different program entirely. Stopping on the event puts the arithmetic
       * in the ring. Checked *after* the step, because the command is issued by
       * the write the step just performed. */
      /* The write itself, so the ring holds the instruction that made it
       * rather than whatever ran between it and a later event. Numbered
       * because a value written ten times is interesting on the tenth: the
       * early ones are a loader doing its job. */
      if (stop_on_watch_read != 0u && machine.watch_reads >= stop_on_watch_read) {
        printf("  stopped on   read %u of %08X after %u instruction(s)\n",
               machine.watch_reads, machine.watch_read_address, i);
        run.executed++;
        break;
      }
      if (stop_on_watch != 0u && machine.watch_writes >= stop_on_watch) {
        printf("  stopped on   write %u to %08X, after %u instruction(s)\n",
               machine.watch_writes, machine.watch_write_address, i);
        run.executed++;
        break;
      }
      if (stop_on_refusal &&
          ap_omti_refusals(&board->disk.controller) > 0u) {
        printf("  stopped on   the disk controller refusing an address, after "
               "%u instruction(s)\n",
               i);
        run.executed++;
        break;
      }
      /* The same stop against the address the *bus* carried. Code found by
       * searching memory is found at a physical address, and recovering its
       * logical one means knowing which page it is in -- which the translation
       * is under no obligation to make guessable, as one wrong derivation here
       * has already shown. */
      if (stop_physical_pc != 0u) {
        uint32_t physical = step_pc;
        if (ap_machine_translate(&machine, step_pc, 6u, &physical) &&
            physical >= stop_physical_pc &&
            physical < stop_physical_pc + stop_physical_length) {
          printf("  stopped at   PC %08X -> %08X after %u instruction(s)\n",
                 step_pc, physical, i);
          run.executed++;
          break;
        }
      }
      if (machine.mmu_fault_stopped && !stop_pc_armed) {
        if (stop_pc_then > 0u) {
          /* The same "window after the event" the PC stop takes: what a fault
           * handler *does* is the question, and that is entirely after. */
          stop_pc_countdown = stop_pc_then;
          stop_pc_armed = true;
        } else {
          printf(
              "  stopped on   the MMU refusing %08X, after %u instruction(s)\n",
              machine.mmu_fault_stop_address, i);
          run.executed++;
          break;
        }
      }
      if (stop_pc_countdown > 0u && --stop_pc_countdown == 0u) {
        printf("  stopped      %u instruction(s) after %s, at %u\n",
               stop_pc_then,
               stop_mmu_fault_at != 0u ? "the MMU refusal" : "the stop PC", i);
        run.executed++;
        break;
      }
      if (stop_pc != 0u && stop_pc_countdown == 0u && !stop_pc_armed &&
          step_pc >= stop_pc && step_pc < stop_pc + stop_pc_length &&
          stop_pc_seen++ >= stop_pc_skip) {
        if (stop_pc_then > 0u) {
          /* Not a stop but a start: the window this run wants is the one that
           * follows the event. */
          stop_pc_countdown = stop_pc_then;
          stop_pc_armed = true;
        } else {
        /* Checked **before** the fast path below, not after it.
         *
         * It was after, so the stop only happened when a trace or a ring was
         * also asked for -- and a run without one reported nothing and looked
         * exactly like a run whose address was never reached. That is worse
         * than not having the flag: it answers a question it did not test, and
         * I read two such runs as evidence that an address was never executed.
         *
         * The run ends here so a ring kept alongside it holds the steps that
         * led to this rather than the ones that came after. */
        printf("  stopped at   PC %08X after %u instruction(s)\n", step_pc, i);
        run.executed++;
        break;
        }
      }
      if (!trace && trace_last == 0u) {
        run.status = r.status;
        run.instruction = r.instruction;
        if (r.status != AP_M68030_STEP_EXECUTED &&
            r.status != AP_M68030_STEP_EXCEPTION) {
          break;
        }
        run.executed++;
        continue;
      }
      if (trace_last > 0u) {
        /* Kept, not printed. See `trace_last`'s declaration: a fault half a
         * billion instructions in cannot be reached by printing every step. */
        run.status = r.status;
        run.instruction = r.instruction;
        if (r.status != AP_M68030_STEP_EXECUTED &&
            r.status != AP_M68030_STEP_EXCEPTION) {
          break;
        }
        run.executed++;
        continue;
      }
      printf("%u %08X %08X %08X %08X %04X %s\n", i, step_pc,
             ap_m68030_read_a7(&machine.cpu.regs), machine.cpu.regs.a[6],
             machine.cpu.regs.a[0], r.instruction,
             ap_probe_status_name(r.status));
      if (watch != 0u) {
        /* One named location's contents, per step. Three hypotheses about this
         * value were each refuted by an observable rather than by argument, so
         * the cheapest thing left is to stop reasoning about what writes it and
         * simply watch it change.
         *
         * **Memory only.** This reads through `ap_board_read`, so pointing it
         * at a device register would *perturb the machine*: reading a DUART's
         * receive buffer pops its FIFO, and every read here inflates the
         * per-region counters below. An instrument that changes what it
         * measures is worse than none, and this one gives no warning -- the run
         * simply becomes a different run. */
        uint32_t held = 0;
        bool all = true;
        for (unsigned k = 0; k < 4u; k++) {
          bool byte_ok = false;
          const uint8_t byte = ap_board_read(board, watch + k, &byte_ok);
          all = all && byte_ok;
          held = (held << 8) | byte;
        }
        printf("  watch %08X = %08X%s\n", watch, held, all ? "" : " (unmapped)");
      }
      run.status = r.status;
      run.instruction = r.instruction;
      if (r.status != AP_M68030_STEP_EXECUTED &&
          r.status != AP_M68030_STEP_EXCEPTION) {
        break;
      }
      run.executed++;
    }
  } else if (typed_length > 0u || typed_phase[1] != NULL) {
    /* ## Typed input does not need a step loop, and paying for one made it
     * unusable
     *
     * Every other per-instruction feature genuinely inspects each step: a trace
     * prints one, a stop compares one, a watch counts one. Typing does not. Its
     * condition -- a configured port and a machine polling for input -- is true
     * for *millions* of consecutive instructions, so sampling it a few thousand
     * apart sees exactly the same thing.
     *
     * What the step loop cost is the whole reason this matters. Reaching
     * Domain/OS's calendar prompt is about a billion instructions, and stepping
     * one at a time from the frontend turned a ten-minute experiment into a
     * two-hour one. Two attempts at that prompt were abandoned mid-run for
     * budget rather than for anything the machine did.
     *
     * So this runs the machine in chunks and checks between them. The chunk is
     * far finer than the condition it samples and far coarser than the cost it
     * was paying. */
    run = (ap_machine_run_t){.status = AP_M68030_STEP_EXECUTED};
    while (run.executed < limit) {
      const unsigned remaining = limit - run.executed;
      const unsigned chunk = remaining < AP_BOOT_TYPE_CHUNK ? remaining
                                                            : AP_BOOT_TYPE_CHUNK;
      const ap_machine_run_t part = ap_machine_run(&machine, chunk);
      run.executed += part.executed;
      run.status = part.status;
      if (part.status != AP_M68030_STEP_EXECUTED &&
          part.status != AP_M68030_STEP_EXCEPTION) {
        break;
      }
      /* Arm this phase, then move to the next once it is spent. The gate is
       * re-evaluated per phase, so the command waits for MD's prompt while the
       * two characters that produce that prompt waited only for the console
       * poll. */
      if (!typed_armed && machine.cpu.regs.pc == typed_phase_pc[typed_phase_at]) {
        typed_armed = true;
      }
      if (typed_sent >= typed_length && typed_phase_at == 0u &&
          typed_phase[1] != NULL) {
        typed_phase_at = 1u;
        typed_now = typed_phase[1];
        typed_length = strlen(typed_now);
        typed_sent = 0u;
        typed_pending = false;
        typed_armed = typed_phase_pc[1] == 0u;
      }
      typed_deliver(&machine, board, typed_now, typed_length, &typed_sent,
                    &typed_at, &typed_flushed_was, &typed_reads_was,
                    &typed_pending, type_after_os, typed_armed,
                    type_await_pushback, &typed_first_done);
    }
  } else {
    run = ap_machine_run(&machine, limit);
  }
  if (trace_last > 0u && trace_ring_used > 0u) {
    /* Oldest first, so it reads forwards like the run did. A ring that has not
     * filled yet starts at zero; one that has starts at the next slot to be
     * overwritten. */
    const unsigned kept =
        trace_ring_used < trace_last ? trace_ring_used : trace_last;
    const unsigned first = trace_ring_used < trace_last
                               ? 0u
                               : trace_ring_used % trace_last;
    printf("# last %u step(s): step pc d0-d7 a0-a7 instruction status\n",
           kept);
    for (unsigned k = 0; k < kept; k++) {
      const ap_trace_ring_t *e = &trace_ring[(first + k) % trace_last];
      printf("%u %08X", e->step, e->pc);
      for (unsigned ri = 0; ri < 8u; ri++) {
        printf(" %08X", e->d[ri]);
      }
      for (unsigned ri = 0; ri < 8u; ri++) {
        printf(" %08X", e->a[ri]);
      }
      printf(" %04X %s\n", e->instruction, ap_probe_status_name(e->status));
    }
  }
  printf("  executed     %u instruction(s)\n", run.executed);
  printf("  stopped      %s", ap_probe_status_name(run.status));
  /* And on which word, when the word is the point. A run that ends `ILLEGAL` is
   * a report that an opcode is missing, and the opcode is the one part of that
   * a reader cannot recover afterwards. */
  if (run.status != AP_M68030_STEP_EXECUTED &&
      run.status != AP_M68030_STEP_EXCEPTION) {
    printf(" on %04X", run.instruction);
  }
  printf("\n");
  report_state(&machine);
  printf("  unmapped     %u read, %u written\n", board->unmapped_reads,
         board->unmapped_writes);
  if (board->unmapped_reads > 0u) {
    printf("    first read %08X (%s)\n", board->first_unmapped_read,
           ap_board_region_name(ap_board_region(board, board->first_unmapped_read)));
  }
  if (board->unmapped_writes > 0u) {
    printf("    first write %08X (%s)\n", board->first_unmapped_write,
           ap_board_region_name(ap_board_region(board, board->first_unmapped_write)));
  }
  /* Reported separately because it is not an error: a read-only memory absorbs
   * a write, and this firmware is known to make one. Folded into the unmapped
   * total it would read as a fault that never happened. */
  printf("  rom writes   %u\n", board->rom_writes);
  if (board->rom_writes > 0u) {
    printf("    first      %08X\n", board->first_rom_write);
  }
  printf("  empty slot   %u read, %u written\n", board->atbus_empty_reads,
         board->atbus_empty_writes);
  if (board->atbus_empty_reads > 0u) {
    printf("    first read %08X\n", board->first_atbus_empty_read);
  }
  if (board->atbus_empty_writes > 0u) {
    printf("    first write %08X\n", board->first_atbus_empty_write);
  }
  print_atbus_empty_addresses(board);
  /* The two registers Table 2-8 names and this core declines. Printed even at
   * zero for the same reason: "the firmware never touched task alias" is an
   * answer, and the only one available until a handbook turns up. */
  printf("  declined     task alias %u/%u, master request %u/%u (read/write)\n",
         board->task_alias_reads, board->task_alias_writes,
         board->master_request_reads, board->master_request_writes);
  /* The part of the translation map's region no manual describes. Reported even
   * at zero, because zero is the informative answer here: it says the run never
   * went anywhere our assumed decode could be wrong. */
  printf("  atmap undoc  %u read, %u written\n",
         board->atmap_undescribed_reads, board->atmap_undescribed_writes);
  if (board->atmap_undescribed_reads > 0u) {
    printf("    first read %08X\n", board->first_atmap_undescribed_read);
  }
  if (board->atmap_undescribed_writes > 0u) {
    printf("    first write %08X\n", board->first_atmap_undescribed_write);
  }
  /* The machine's own account of what went wrong. A self-test failure posts a
   * diagnostic code to the control register -- the LEDs -- rather than to any
   * console, and then flashes it for ever. Discarding those values threw away
   * the one thing the firmware says about the failure. `FINDINGS.md` C109 has
   * the post routine and the codes seen so far. */
  if (board->registers.posted_count > 0u) {
    printf("  posted codes ");
    for (unsigned i = 0; i < board->registers.posted_count; i++) {
      printf("%s%02X", i == 0u ? "" : " ", board->registers.posted[i]);
    }
    printf("  (%u write(s), distinct in order, as written)\n",
           board->registers.posted_total);
  }
  /* What the machine told its firmware about its own memory. Printed even when
   * known, because a wrong configuration byte is a machine that sizes memory it
   * does not have and finds out mid-self-test. */
  if (board->ram_config_known) {
    printf("  ram config   %02X strapped on serial 1's input port\n",
           board->ram_config);
  } else {
    printf("  ram config   **not strapped**: no table entry for this model at"
           " this size,\n"
           "               so the firmware reads zero and sizes no memory at"
           " all\n");
  }
  /* Why scripted input did or did not arrive, which a serial read count cannot
   * say. Delivery is gated on the port being programmed to eight bits and its
   * receiver enabled -- `MR1` resets to a five-bit link and a disabled receiver
   * drops what arrives -- and a script blocked on either looks exactly like a
   * firmware that is ignoring the console. Printed whenever a script was given,
   * including when all of it went, because "all delivered and still silent" is
   * a different finding from "none delivered". */
  if (input_length > 0u) {
    printf("  input        %zu of %zu character(s) delivered, sent at %u baud"
           " (CSR %02X)\n",
           input_sent, input_length,
           ap_mc68681_baud((uint8_t)(input_rate >> 4), false), input_rate);
    for (unsigned unit = 0; unit < 2u; unit++) {
      for (unsigned ch = 0; ch < 2u; ch++) {
        /* The rate the *machine* is listening on, beside what the terminal
         * sent at. Those are the two numbers the console negotiation is
         * about, and until they were printed together the only way to compare
         * them was to infer one from whether a character survived. A `CR` is
         * `0D` and forgiving of being sampled at the wrong instants; a letter
         * is not -- so a mismatch here shows as carriage returns working and
         * everything else vanishing. */
        const uint8_t csr = ap_sio_clock_select(&board->sio, unit, ch);
        printf("    sio%u %c      %u bits, receiver %s, listening at %u baud"
               " (CSR %02X)\n",
               unit + 1u, ch == 0u ? 'A' : 'B',
               ap_sio_character_bits(&board->sio, unit, ch),
               ap_sio_receiver_enabled(&board->sio, unit, ch) ? "enabled"
                                                             : "disabled",
               ap_mc68681_baud((uint8_t)(csr >> 4), false), csr);
      }
    }
  }
  /* Which of the disk controller's registers a run touched. A region total
   * says the firmware talked to it and cannot say *what it asked*, and six
   * million reads against seven writes is a poll whose target is the whole
   * question. */
  if (board->disk.command_total > 0u) {
    printf("  disk commands %u issued:", board->disk.command_total);
    for (unsigned c = 0; c < 256u; c++) {
      if (board->disk.commands[c] != 0u) {
        printf(" %02X x%u", c, board->disk.commands[c]);
      }
    }
    printf("\n");
    /* And how the last one ended. A command histogram says what was asked and
     * a `03 REQUEST SENSE` beside a `08 READ` says the read failed, but neither
     * says *why the controller refused* -- which is the one thing the sense
     * bytes exist to answer and the one thing a run could not report. */
    const ap_omti_t *omti = &board->disk.controller;
    printf("  disk last     %02X, %s, sense %02X %02X %02X %02X, next lba %u\n",
           omti->last_command,
           omti->completion == 0u ? "completed" : "error",
           omti->sense[0], omti->sense[1], omti->sense[2], omti->sense[3],
           omti->next_lba);
    /* And *which* address, when one was refused. "Illegal disk address" names
     * a class of fault and not a fact: a driver asking for one sector past the
     * geometry and a driver asking for a thousand read identically without
     * this, and the geometry is printed beside it because the refusal is a
     * statement about the pair. */
    if (ap_omti_refusals(omti) > 0u) {
      const ap_awd_geometry_t geometry = ap_awd_geometry_for(AP_AWD_DRIVE_348MB);
      printf("  disk refused  %u address(es), last c%u h%u s%u / lba %u, "
             "against %u x %u x %u\n",
             ap_omti_refusals(omti), ap_omti_refused_cylinder(omti),
             ap_omti_refused_head(omti), ap_omti_refused_sector(omti),
             ap_omti_refused_lba(omti), geometry.cylinders, geometry.heads,
             geometry.sectors);
      const uint8_t *cdb = ap_omti_refused_cdb(omti);
      printf("  disk refused cdb %02X %02X %02X %02X %02X %02X\n", cdb[0],
             cdb[1], cdb[2], cdb[3], cdb[4], cdb[5]);
      /* And what it read to arrive at that address. A wild block number came
       * out of a sector the driver had just read, so the sectors immediately
       * before the refusal are the ones to compare against the image. */
      printf("  disk last read");
      for (unsigned k = 0; k < 2048u; k++) {
        uint32_t lba = 0;
        if (!ap_omti_recent_read(omti, k, &lba)) {
          break;
        }
        printf(" %u", lba);
      }
      printf("  (newest first, of %u)\n", ap_omti_reads(omti));
    }
  }
  for (unsigned r = 0; r < AP_OMTI_DISK_REGISTERS; r++) {
    if (board->disk.disk_reads[r] == 0u &&
        board->disk.disk_writes[r] == 0u) {
      continue;
    }
    printf("    disk reg %u  %8u read(s) %8u write(s)\n", r,
           board->disk.disk_reads[r],
           board->disk.disk_writes[r]);
  }
  for (unsigned r = 0; r < AP_OMTI_FLOPPY_REGISTERS; r++) {
    if (board->disk.floppy_reads[r] == 0u &&
        board->disk.floppy_writes[r] == 0u) {
      continue;
    }
    printf("    fdc reg %u   %8u read(s) %8u write(s)\n", r,
           board->disk.floppy_reads[r],
           board->disk.floppy_writes[r]);
  }
  /* The lookup table's third chip select is an **A/D converter**, and what it
   * converts is the controller's own video output -- the level on one gun at
   * wherever the beam is.
   * Reported because the firmware range-checks the answers and posts a
   * diagnostic code if either is outside `[52, 70)`. */
  if (board->graphics.lut_ad_accesses > 0u) {
    printf("  lut a/d      %u access(es) (channel selects and conversions)\n",
           board->graphics.lut_ad_accesses);
  }
  /* The diagnostic memory-refresh trigger, on every board but the 8-plane.
   * What a refresh does is not modelled -- this core's graphics memory does not
   * decay -- so the request is reported instead of being silently dropped. */
  if (board->graphics.diag_refresh_requests > 0u) {
    printf("  gfx refresh  %u diagnostic request(s), last %02X\n",
           board->graphics.diag_refresh_requests,
           board->graphics.diag_refresh_request);
  }
  /* The display controller's *memory*, apart from its registers -- which
   * `region_writes` counts together and so cannot tell apart. "The firmware
   * never wrote a pixel" and "it wrote and nothing drew" are different answers
   * and only the second is a defect. Printed even at zero, for that reason. */
  if (board->parity.forced_writes > 0u || board->parity.errors > 0u ||
      board->parity.unstorable_writes > 0u) {
    printf("  parity       %u forced write(s), %u error(s)",
           board->parity.forced_writes, board->parity.errors);
    if (board->parity.errors > 0u) {
      printf(", first at %08X",
             board->map->ram_base + board->parity.first_error_offset);
    }
    if (board->parity.unstorable_writes > 0u) {
      /* A board asked to force bad parity with no parity RAM fitted. Said out
       * loud, because a self-test that passes on such a board passes for the
       * wrong reason. */
      printf(", %u with no parity RAM fitted",
             board->parity.unstorable_writes);
    }
    printf("\n");
  }
  for (unsigned u = 0; u < 2u; u++) {
    /* The four registers that decide whether a transfer can happen at all.
     * A software request is non-maskable and a **pin** is not -- and the
     * cascade reaches controller 2 as a pin, so a masked channel 0 there stops
     * every transfer on controller 1 without either controller looking wrong on
     * its own. That is invisible from a transfer count. */
    const ap_i8237_t *c = &board->dma.controller[u];
    /* Both, always. Skipping a controller that looks untouched hid *which* of
     * the two a program had reached, which is the question when a register
     * decode is in doubt -- and it made a report that named one controller read
     * as though the other did not exist. */
    printf("  dma%u regs    command %02X, mask %01X, request %01X, status %02X\n",
           u + 1u, c->command, c->mask & 0x0Fu, c->request & 0x0Fu, c->status);
    for (unsigned ch = 0; ch < 2u; ch++) {
      /* The two channels a memory-to-memory service uses, with both the base
       * and the current values: a current address that has advanced past its
       * base is a transfer that ran, and a base that is not what the program
       * wrote is a decode fault. Neither shows in a translated address alone. */
      printf("  dma%u ch%u      mode %02X, address %04X (base %04X),"
             " count %04X (base %04X)\n",
             u + 1u, ch, c->channel[ch].mode, c->channel[ch].current_address,
             c->channel[ch].base_address, c->channel[ch].current_count,
             c->channel[ch].base_count);
    }
  }
  printf("  core status  %04X, control %04X, cache %02X\n",
         board->registers.cpu_status | AP_BOARDREG_STATUS_ALWAYS_SET,
         board->registers.cpu_control, board->registers.cache_control);
  if (board->core_register_write_count > 0u) {
    printf("  core writes  ");
    for (unsigned i = 0; i < board->core_register_write_count; i++) {
      printf(" %06X", board->core_register_writes[i]);
    }
    printf("\n");
  }
  if (board->dma_register_write_count > 0u) {
    printf("  dma writes   ");
    for (unsigned i = 0; i < board->dma_register_write_count; i++) {
      printf(" %06X", board->dma_register_writes[i]);
    }
    printf("\n");
  }
  {
    const ap_mc146818_t *rtc = &board->calendar.rtc;
    printf("  calendar     %u update cycle(s), seconds register %02X\n",
           rtc->update_cycles, rtc->ram[0]);
  }
  printf("  dma bus      %u bus tick(s), %u asking, %u holding\n",
         board->bus_ticks, board->dma_bus_requests, board->dma_bus_held);
  if (board->dma_transfers > 0u || board->dma_unwired_transfers > 0u) {
    /* Whether the second bus master ever moved anything, and how much of it
     * went to a channel with no peripheral wired. A run that programs a
     * transfer and reports zero here has an arbitration problem rather than a
     * controller one, and nothing else distinguishes those. */
    printf("  dma          %u transfer(s), %u to an unwired channel,"
           " last read %08X wrote %08X\n",
           board->dma_transfers, board->dma_unwired_transfers,
           board->dma_last_read, board->dma_last_write);
  }
  printf("  blit cycles  %u, %u plane write(s)\n", board->graphics_cycles,
         board->graphics_planes_written);
  if (board->graphics_unknown_mode_cycles > 0u) {
    printf("    undescribed CR0 mode %u time(s), first at %08X\n",
           board->graphics_unknown_mode_cycles,
           board->first_graphics_unknown_mode);
  }
  /* Every region the firmware touched, and every one it did not. The zeros are
   * the informative half: a device with no accesses is one the firmware never
   * reached for, which a total cannot say. */
  printf("  regions      reads / writes\n");
  for (unsigned r = 0; r < AP_BOARD_REGIONS; r++) {
    if (board->region_reads[r] == 0u && board->region_writes[r] == 0u) {
      continue;
    }
    printf("    %-22s %8u %8u\n",
           ap_board_region_name((ap_board_region_t)r), board->region_reads[r],
           board->region_writes[r]);
  }
  /* Which serial registers, not just how many. A transmit that never happened
   * and one dropped at the register look identical from a total. */
  for (unsigned unit = 0; unit < 2u; unit++) {
    for (unsigned reg = 0; reg < AP_MC68681_REGISTERS; reg++) {
      if (board->sio.register_writes[unit][reg] == 0u &&
          board->sio.register_reads[unit][reg] == 0u) {
        continue;
      }
      printf("    sio%u reg %-2u %8u write(s) %8u read(s)\n", unit + 1u, reg,
             board->sio.register_writes[unit][reg],
             board->sio.register_reads[unit][reg]);
    }
  }
  /* Characters the ports threw away, by *which* of the two ways. A run that
   * delivered a dialogue and saw none of it arrive cannot tell "the machine
   * took it and reset the FIFO" from "the machine was not listening", and the
   * two have different fixes -- the first is re-sent automatically, the second
   * means the sender chose the wrong moment. Printed only when non-zero, so a
   * clean run says nothing. */
  for (unsigned unit = 0; unit < 2u; unit++) {
    for (unsigned ch = 0; ch < 2u; ch++) {
      const unsigned flushed = ap_sio_receiver_flushed(&board->sio, unit, ch);
      const unsigned deaf =
          ap_sio_receiver_disabled_drops(&board->sio, unit, ch);
      if (flushed == 0u && deaf == 0u) {
        continue;
      }
      printf("    sio%u %c    %8u discarded unread, %8u dropped with the"
             " receiver disabled\n",
             unit + 1u, (char)('A' + ch), flushed, deaf);
    }
  }

  if (dump_spec != NULL) {
    uint32_t at = 0, length = 0;
    if (!parse_dump_spec(dump_spec, &at, &length)) {
      fprintf(stderr, "apollo: --dump-mem wants ADDR or ADDR:LEN in hex, not"
                      " %s\n", dump_spec);
    } else {
      printf("memory %08X, %u byte(s), through the board\n", at, length);
      dump_memory(stdout, board, at, length);
    }
  }

  /* The same dump of the address the *program* named. Once an operating system
   * is running, every address worth looking at is a logical one -- an
   * instruction stream at `3C456A0C`, a stack frame at `3C4F9BF0` -- and
   * translating each by hand from a reported physical PC is exactly the kind of
   * arithmetic that quietly goes wrong once. */
  for (unsigned k = 0; k < dump_logical_count; k++) {
    uint32_t at = 0, length = 0;
    if (!parse_dump_spec(dump_logical[k], &at, &length)) {
      fprintf(stderr, "apollo: --dump-logical wants ADDR or ADDR:LEN in hex,"
                      " not %s\n", dump_logical[k]);
    } else {
      uint32_t physical = 0;
      if (!ap_machine_translate(&machine, at, AP_M68030_FC_SUPERVISOR_DATA,
                                &physical)) {
        printf("logical %08X does not translate\n", at);
      } else {
        printf("logical %08X -> %08X, %u byte(s)\n", at, physical, length);
        dump_memory(stdout, board, physical, length);
      }
    }
  }

  /* Last, so the run's own report is complete first and a failed capture
   * cannot cost the measurements that were already taken. `CR1` is the one the
   * *firmware* programmed, now that the register file stores it -- so `INV` and
   * `DISP_EN` are the machine's own answers rather than the harness's
   * assumption. */
  /* What `--boot-type` managed, and *why* if it managed less than all of it.
   *
   * A flag that is accepted and then does nothing is the defect class this
   * frontend has already been caught by twice -- `--boot-progress` silent
   * outside the step loop, and `--boot-key` delivering into a five-bit port.
   * Both looked exactly like a machine ignoring the input. So the condition is
   * reported rather than left to be inferred from a screen that did not
   * change. */
  if (typed_length > 0u) {
    printf("  boot type    %u of %u character(s) typed\n",
           (unsigned)typed_sent, (unsigned)typed_length);
    if (typed_sent < typed_length) {
      printf("               port: %u bit(s), receiver %s, %s, polls %u "
             "(need %u)\n",
             ap_sio_character_bits(&board->sio, 0u, 0u),
             ap_sio_receiver_enabled(&board->sio, 0u, 0u) ? "enabled"
                                                          : "**disabled**",
             ap_sio_receiver_ready(&board->sio, 0u, 0u)
                 ? "**holding an unread byte**"
                 : "free",
             input_polls(board), AP_BOOT_KEY_POLLS);
    }
  }

  /* ## `--boot-report`: the input path, end to end, in one place
   *
   * Five separate investigations into "the machine ignored what I typed" this
   * session were each a *harness* fault, and each was diagnosed by adding a
   * temporary probe, running for twenty minutes, reading three lines and
   * reverting it. Every one of those facts was already in the machine at the
   * moment the run ended. This prints them.
   *
   * The order is the path a character takes: the port it arrives at, the
   * receiver that latches it, the interrupt that announces it, the controller
   * that delivers that, and the keyboard at the far end. A reader looking for
   * why a keystroke did nothing walks it downwards and stops at the first line
   * that is not what they expected. */
  if (boot_report) {
    const ap_mc68681_t *duart = &board->sio.port[0];
    const ap_mc68681_channel_t *ch = &duart->channel[0];
    printf("  --- input path, serial 1 channel A (the keyboard) ---\n");
    printf("  port         MR1 %02X (%u bit(s)), MR2 %02X, CSR %02X, SR %02X\n",
           ch->mr[0], ap_sio_character_bits(&board->sio, 0u, 0u), ch->mr[1],
           ch->csr, ch->sr);
    printf("  receiver     %s, FIFO %u deep, %s\n",
           ch->rx_enabled ? "enabled" : "**disabled**", ch->fifo_count,
           (ch->sr & AP_MC68681_SR_OVERRUN) ? "**overrun**" : "no overrun");
    printf("  transmitter  %s, holding register %s\n",
           ch->tx_enabled ? "enabled" : "**disabled**",
           ch->tx_holding_full ? "**full**" : "empty");
    /* The two that decide whether a received character is *announced*. A
     * driver that reads by interrupt and never set its mask bit looks exactly
     * like a machine ignoring the keyboard, and the difference is here. */
    printf("  interrupt    ISR %02X, IMR %02X, line %s\n", duart->isr,
           duart->imr, ap_sio_irq(&board->sio) ? "asserted" : "not asserted");
    printf("  controller   IRQ%u %s, master IRR %02X IMR %02X, %s\n",
           AP_SIO_IRQ,
           (board->interrupts.master.imr & (1u << AP_SIO_IRQ)) ? "**masked**"
                                                               : "unmasked",
           board->interrupts.master.irr, board->interrupts.master.imr,
           ap_intr_pending(&board->interrupts) ? "pending" : "nothing pending");
    printf("  keyboard     %s, %s set, %u byte(s) still on the wire\n",
           board->keyboard.loopback ? "in loopback" : "out of loopback",
           board->keyboard.keystate_mode ? "keystate" : "compatibility",
           board->kbd_reply.count);
    printf("  traffic      data register %u write(s) %u read(s), "
           "command register %u write(s)\n",
           board->sio.register_writes[0][AP_MC68681_RB_TB_A],
           board->sio.register_reads[0][AP_MC68681_RB_TB_A],
           board->sio.register_writes[0][AP_MC68681_CR_A]);
  }

  int status = 0;
  if (screenshot != NULL) {
    status = write_screenshot(screenshot, &board->graphics,
                              board->graphics.reg.cr1);
  }

  /* The battery keeps its charge: whatever the machine left in the fifty bytes
   * is what the next run starts with. Written unconditionally when a path was
   * given, including when the machine wrote nothing -- a file that appears only
   * once a run happens to store something would make "the table is empty" and
   * "the run did not save" look identical, which is the confusion this whole
   * item has been made of. */
  if (battery_path != NULL) {
    uint8_t battery[AP_CALENDAR_BATTERY_BYTES];
    const unsigned kept =
        ap_calendar_save_battery(&board->calendar, battery, sizeof battery);
    FILE *out = fopen(battery_path, "wb");
    if (out == NULL || fwrite(battery, 1u, kept, out) != kept) {
      fprintf(stderr, "apollo: cannot write calendar ram %s\n", battery_path);
      status = 1;
    } else {
      printf("  calendar ram %s, %u byte(s) kept\n", battery_path, kept);
    }
    if (out != NULL) {
      fclose(out);
    }
  }

  free(disk_bytes);
  free(colour_memory);
  free(mono_memory);
  free(board);
  free(trace_ring);
  free(parity);
  free(ram);
  free(prom);
  return status;
}

/* Load a cartridge and run its boot image.
 *
 * The file reading lives here and not in `src/core`, which has no file I/O by
 * design: a deterministic core cannot have a device reaching for a path at run
 * time. The core is handed bytes and never learns where they came from.
 *
 * This is also why the cartridge is a command-line argument rather than a
 * default path. `media/` is gitignored -- Apollo distribution media is not ours
 * to redistribute -- so a build that looked for a cartridge would work on one
 * machine and fail on every other. */
/* Read an `.afd` through the reader, as a driver would.
 *
 * The counterpart of `--tape` for the cartridge and of `--volume` for the disk,
 * and the *reading* path rather than the booting one. Every sector goes through
 * `ap_afd_read` rather than being indexed out of the buffer, which is what makes
 * this a check of the addressing instead of a check of `memcpy`: a wrong
 * geometry, a zero-based sector number or a head-major layout all fail here and
 * none of them fails a size comparison.
 *
 * The image is walked in **cylinder, head, sector** order and the linear
 * numbers it produces must come out 0, 1, 2 ... with no gap and no repeat. That
 * is the whole claim a blank image can support, and it is worth stating plainly
 * that it is not the same claim as "the same image reads identically under
 * both" -- for that the image has to have something in it. */
static int report_floppy(const char *path) {
  long size = 0;
  uint8_t *bytes = read_file(path, &size);
  if (bytes == NULL) {
    fprintf(stderr, "apollo: cannot read floppy image %s\n", path);
    return 1;
  }

  ap_afd_t image;
  if (!ap_afd_open(&image, bytes, (size_t)size, false)) {
    free(bytes);
    fprintf(stderr,
            "apollo: %s is %ld bytes; an Apollo floppy is exactly %u"
            " (%u cylinders x %u heads x %u sectors x %u)\n",
            path, size, (unsigned)AP_AFD_BYTES, AP_AFD_CYLINDERS, AP_AFD_HEADS,
            AP_AFD_SECTORS, AP_AFD_SECTOR_BYTES);
    return 1;
  }

  printf("floppy %s\n", path);
  printf("  bytes        %ld\n", size);
  printf("  geometry     %u cylinders x %u heads x %u sectors x %u bytes\n",
         AP_AFD_CYLINDERS, AP_AFD_HEADS, AP_AFD_SECTORS, AP_AFD_SECTOR_BYTES);
  printf("  sectors      one-based, 1 to %u\n", AP_AFD_SECTORS);

  uint32_t expected = 0;
  uint32_t nonzero = 0;
  bool self_identifying = true;
  uint64_t sum = 0;
  uint8_t sector[AP_AFD_SECTOR_BYTES];
  for (unsigned cyl = 0; cyl < AP_AFD_CYLINDERS; cyl++) {
    for (unsigned head = 0; head < AP_AFD_HEADS; head++) {
      for (unsigned s = 1; s <= AP_AFD_SECTORS; s++) {
        uint32_t lba = 0;
        if (!ap_afd_lba((uint16_t)cyl, (uint8_t)head, (uint8_t)s, &lba)) {
          free(bytes);
          fprintf(stderr, "apollo: %u/%u/%u is not addressable\n", cyl, head, s);
          return 1;
        }
        /* Cylinder-major, then head, then sector: the numbers must come out
         * consecutive. A head-major layout produces every number exactly once
         * too, so a set comparison would pass and this does not. */
        if (lba != expected) {
          free(bytes);
          fprintf(stderr,
                  "apollo: %u/%u/%u is sector %u, expected %u -- the layout is"
                  " not cylinder-major\n",
                  cyl, head, s, lba, expected);
          return 1;
        }
        expected++;
        if (!ap_afd_read(&image, lba, sector)) {
          free(bytes);
          fprintf(stderr, "apollo: sector %u could not be read\n", lba);
          return 1;
        }
        for (unsigned b = 0; b < AP_AFD_SECTOR_BYTES; b++) {
          if (sector[b] != 0u) { nonzero++; }
          sum += sector[b];
        }
        /* An image whose every sector opens with its own linear number checks
         * the mapping rather than merely exercising it: a reader that returned
         * the neighbouring sector would return a wrong *number* instead of
         * plausible bytes. Detected rather than required, so an ordinary image
         * still reports normally. */
        const uint32_t stamp = ((uint32_t)sector[0] << 24) |
                               ((uint32_t)sector[1] << 16) |
                               ((uint32_t)sector[2] << 8) | sector[3];
        if (stamp != lba) { self_identifying = false; }
      }
    }
  }
  printf("  read         %u sectors through the reader, %u non-zero byte(s)\n",
         expected, nonzero);
  printf("  checksum     %016llX\n", (unsigned long long)sum);
  if (self_identifying && nonzero > 0u) {
    printf("               every sector opens with its own linear number, so"
           " the mapping is\n"
           "               checked and not merely exercised\n");
  }
  if (nonzero == 0u) {
    /* Said plainly rather than left to be inferred from a checksum of zero. */
    printf("               the image is blank: this pins the addressing and"
           " nothing about\n"
           "               content, since every sector reads the same under"
           " any geometry\n");
  }
  free(bytes);
  return 0;
}

/* Report a cartridge without requiring it to boot.
 *
 * `--boot-tape` refuses an image with no boot record, which is right for a boot
 * and wrong for the question "does this reader read the distribution media".
 * Four of the five SR10.x cartridges carry data and no `SYSBOOT` header, so the
 * boot path rejects them correctly and says nothing about whether their blocks
 * are readable. This is the reading path, and it is the counterpart of
 * `--volume` for the disk. */
static int report_tape(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "apollo: cannot open cartridge %s\n", path);
    return 1;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    fprintf(stderr, "apollo: cannot size cartridge %s\n", path);
    return 1;
  }
  const long size = ftell(file);
  rewind(file);
  if (size <= 0) {
    fclose(file);
    fprintf(stderr, "apollo: empty cartridge %s\n", path);
    return 1;
  }
  uint8_t *bytes = malloc((size_t)size);
  if (bytes == NULL || fread(bytes, 1, (size_t)size, file) != (size_t)size) {
    free(bytes);
    fclose(file);
    fprintf(stderr, "apollo: cannot read cartridge %s\n", path);
    return 1;
  }
  fclose(file);

  ap_qic_t drive;
  ap_qic_init(&drive);
  if (!ap_qic_load(&drive, bytes, (size_t)size, AP_QIC_CARTRIDGE_DC600A,
                   /* Writable, and the file is not: `bytes` is a private copy
                    * freed at exit, exactly as the disk image is. See the disk
                    * path above for the argument. */
                   true)) {
    free(bytes);
    fprintf(stderr, "apollo: %s is not a whole number of 512-byte blocks\n",
            path);
    return 1;
  }

  printf("cartridge %s\n", path);
  printf("  bytes        %ld\n", size);
  printf("  blocks       %" PRIu64 "\n", ap_ct_blocks(&drive.image));

  /* Read every block through the drive, as a driver would -- not by indexing
   * the buffer, which would test nothing but `memcpy`. A short or misaddressed
   * image fails here rather than at whatever later step first noticed. */
  if (!ap_qic_command(&drive, AP_QIC_CMD_SELECT) ||
      !ap_qic_command(&drive, AP_QIC_CMD_READ)) {
    free(bytes);
    fprintf(stderr, "apollo: %s could not be selected for reading\n", path);
    return 1;
  }
  uint8_t block[AP_CT_BLOCK_SIZE];
  uint64_t read = 0u;
  while (ap_qic_read_block(&drive, block)) {
    read++;
  }
  printf("  blocks read  %" PRIu64 "%s\n", read,
         read == ap_ct_blocks(&drive.image) ? " (all)" : " -- SHORT");

  ap_ct_boot_image_t boot;
  if (ap_ct_boot_image(&drive.image, &boot)) {
    printf("  boot record  load %08X entry %08X length %u\n",
           boot.load_address, boot.entry_point, boot.length);
  } else {
    printf("  boot record  none -- a data cartridge\n");
  }

  free(bytes);
  return read == ap_ct_blocks(&drive.image) ? 0 : 1;
}

static int boot_from_tape(const char *path, unsigned limit) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "apollo: cannot open cartridge %s\n", path);
    return 1;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    fprintf(stderr, "apollo: cannot size cartridge %s\n", path);
    return 1;
  }
  long size = ftell(file);
  rewind(file);
  if (size <= 0) {
    fclose(file);
    fprintf(stderr, "apollo: empty cartridge %s\n", path);
    return 1;
  }

  uint8_t *bytes = malloc((size_t)size);
  if (bytes == NULL || fread(bytes, 1, (size_t)size, file) != (size_t)size) {
    free(bytes);
    fclose(file);
    fprintf(stderr, "apollo: cannot read cartridge %s\n", path);
    return 1;
  }
  fclose(file);

  ap_ct_t cartridge;
  if (!ap_ct_open(&cartridge, bytes, (size_t)size, true)) {
    free(bytes);
    fprintf(stderr,
            "apollo: %s is not a whole number of %u-byte blocks\n",
            path, AP_CT_BLOCK_SIZE);
    return 1;
  }

  ap_ct_boot_image_t image;
  if (!ap_ct_boot_image(&cartridge, &image)) {
    free(bytes);
    fprintf(stderr, "apollo: %s carries no bootable M68K image\n", path);
    return 1;
  }

  printf("cartridge %s\n", path);
  printf("  blocks       %llu\n", (unsigned long long)ap_ct_blocks(&cartridge));
  printf("  load address %08X\n", image.load_address);
  printf("  entry point  %08X\n", image.entry_point);
  printf("  length       %u\n", image.length);

  /* Enough RAM to hold the image where it belongs, rounded up. The machine
   * takes flat memory from zero; a real DN3500 has its main memory elsewhere,
   * and wiring that is the boot-PROM route this deliberately does not need. */
  /* Main memory's size, not an address: with the board's map the image's load
   * address is in the machine's low regions, not in RAM. */
  uint32_t ram_bytes = 0x400000u;
  uint8_t *ram = calloc(1, ram_bytes);
  if (ram == NULL) {
    free(bytes);
    fprintf(stderr, "apollo: cannot allocate %u bytes of RAM\n", ram_bytes);
    return 1;
  }

  /* The DN3500's own address map, so the firmware meets the machine it was
   * written for: main memory at 1000000, devices at their measured addresses,
   * and an unclaimed address reported rather than answered with zero. */
  static const ap_mc146818_time_t epoch = {
      .year = 1987u, .month = 7u, .day = 31u, .day_of_week = 6u,
      .hour = 21u, .minute = 9u, .second = 21u,
  };
  ap_board_t *board = calloc(1, sizeof *board);
  if (board == NULL || !ap_board_init(board, ram, ram_bytes, &epoch,
                                      0x012345u)) {
    free(board);
    free(ram);
    free(bytes);
    fprintf(stderr, "apollo: cannot build the core board\n");
    return 1;
  }
  /* Before anything runs: the set is configuration, and it is hashed. */
  ap_board_set_quirks(board, g_quirks);

  ap_machine_t machine;
  ap_machine_init(&machine, ram, ram_bytes);
  ap_machine_set_board(&machine, board);
  for (uint32_t i = 0; i < image.length; i++) {
    if (!ap_machine_write(&machine, image.load_address + i, 1u,
                          image.data[i])) {
      free(ram);
      free(bytes);
      fprintf(stderr, "apollo: cannot place the image at %08X\n",
              image.load_address);
      return 1;
    }
  }

  /* A stack below the image, which is a choice and not a measurement: the boot
   * PROM would set one and this core is not running the PROM. Reported so that
   * a run which depends on it is visibly depending on a chosen figure. */
  uint32_t stack = image.load_address;
  ap_machine_reset(&machine, image.entry_point, stack);
  printf("  stack        %08X (chosen, not from the cartridge)\n", stack);

  ap_machine_run_t run = ap_machine_run(&machine, limit);
  printf("  executed     %u instruction(s)\n", run.executed);
  printf("  stopped      %s", ap_probe_status_name(run.status));
  /* And on which word, when the word is the point. A run that ends `ILLEGAL` is
   * a report that an opcode is missing, and the opcode is the one part of that
   * a reader cannot recover afterwards. */
  if (run.status != AP_M68030_STEP_EXECUTED &&
      run.status != AP_M68030_STEP_EXCEPTION) {
    printf(" on %04X", run.instruction);
  }
  printf("\n");
  report_state(&machine);
  printf("  unmapped     %u read, %u written\n", board->unmapped_reads,
         board->unmapped_writes);
  if (board->unmapped_reads > 0u) {
    printf("    first read %08X (%s)\n", board->first_unmapped_read,
           ap_board_region_name(ap_board_region(board, board->first_unmapped_read)));
  }
  if (board->unmapped_writes > 0u) {
    printf("    first write %08X (%s)\n", board->first_unmapped_write,
           ap_board_region_name(ap_board_region(board, board->first_unmapped_write)));
  }
  /* Reported separately because it is not an error: a read-only memory absorbs
   * a write, and this firmware is known to make one. Folded into the unmapped
   * total it would read as a fault that never happened. */
  printf("  rom writes   %u\n", board->rom_writes);
  if (board->rom_writes > 0u) {
    printf("    first      %08X\n", board->first_rom_write);
  }
  printf("  empty slot   %u read, %u written\n", board->atbus_empty_reads,
         board->atbus_empty_writes);
  if (board->atbus_empty_reads > 0u) {
    printf("    first read %08X\n", board->first_atbus_empty_read);
  }
  if (board->atbus_empty_writes > 0u) {
    printf("    first write %08X\n", board->first_atbus_empty_write);
  }
  print_atbus_empty_addresses(board);
  /* The two registers Table 2-8 names and this core declines. Printed even at
   * zero for the same reason: "the firmware never touched task alias" is an
   * answer, and the only one available until a handbook turns up. */
  printf("  declined     task alias %u/%u, master request %u/%u (read/write)\n",
         board->task_alias_reads, board->task_alias_writes,
         board->master_request_reads, board->master_request_writes);
  /* The part of the translation map's region no manual describes. Reported even
   * at zero, because zero is the informative answer here: it says the run never
   * went anywhere our assumed decode could be wrong. */
  printf("  atmap undoc  %u read, %u written\n",
         board->atmap_undescribed_reads, board->atmap_undescribed_writes);
  if (board->atmap_undescribed_reads > 0u) {
    printf("    first read %08X\n", board->first_atmap_undescribed_read);
  }
  if (board->atmap_undescribed_writes > 0u) {
    printf("    first write %08X\n", board->first_atmap_undescribed_write);
  }
  printf("  final region %s\n",
         ap_board_region_name(ap_board_region(board, machine.cpu.regs.pc)));

  /* Where it stopped matters more than that it stopped. A fault outside the
   * image is the firmware reaching for hardware that is not mapped, which is
   * expected and is the thing to fix; a fault *inside* the image would mean
   * this core mis-executed something, which is not. */
  if (machine.cpu.regs.pc >= image.load_address &&
      machine.cpu.regs.pc < image.load_address + image.length) {
    printf("  note         PC is inside the loaded image\n");
  } else {
    printf("  note         PC is outside the loaded image\n");
  }

  free(board);
  free(ram);
  free(bytes);
  return 0;
}

int main(int argc, char **argv) {
  const char *program_name = argc > 0 ? argv[0] : "apollo-headless";
  ap_common_options_t opt;
  ap_common_options_init(&opt);

  bool boot_trace = false;
  unsigned boot_trace_last = 0;
  unsigned boot_progress = 0;
  /* The CPU status register's bit 0, the Normal/Service switch. An *input* -- a
   * physical switch on the machine -- so it is a caller's setting and not state
   * the board evolves. `ap_boardreg_set_normal_mode` has existed since the bit
   * was modelled and nothing had ever called it, which left a modelled input
   * unreachable: the one configuration this core could not be put in was the
   * one the boot PROM behaves most differently in.
   *
   * Normal is the default, because that is what a workstation is. Service is
   * what reaches the Mnemonic Debugger, and MD is what runs the stand alone
   * utilities the machine asks for by name -- `EX CONFIG`, `EX CALENDAR`. */
  bool service_mode = false;
  bool boot_stop_on_refusal = false;
  uint32_t boot_watch_write = 0;
  uint32_t boot_watch_read = 0;
  const char *boot_typed = NULL;
  ap_mc146818_time_t boot_clock;
  bool boot_clock_set = false;
  bool boot_report = false;
  bool boot_type_after_os = false;
  uint32_t boot_type_after_pc = 0;
  const char *boot_typed2 = NULL;
  uint32_t boot_type2_after_pc = 0;
  bool boot_type_await_pushback = false;
  unsigned boot_stop_on_watch_read = 0;
  unsigned boot_stop_on_watch = 0;
  uint32_t boot_stop_pc_length = 1u;
  unsigned boot_stop_pc_skip = 0u;
  uint32_t boot_stop_mmu_fault_at = 0u;
  unsigned boot_stop_pc_then = 0u;
  uint32_t boot_progress_from = 0u;
  uint32_t boot_stop_physical_pc = 0;
  uint32_t boot_stop_physical_length = 1u;
  const char *dump_logical_specs[AP_MAX_LOGICAL_DUMPS] = {0};
  unsigned dump_logical_count = 0;
  uint32_t boot_stop_pc = 0;
  uint32_t boot_watch = 0;
  const char *boot_input = NULL;
  bool boot_console = false;
  const char *boot_script = NULL;
  unsigned boot_input_unit = 1u; /* SIO2 */
  unsigned boot_input_channel = 0u;
  /* 9600 baud. **Not** what the firmware configures its own ports to -- that is
   * `77`, 1050 baud, and it is what this defaulted to for a long time on the
   * reasoning that a scripted terminal should match the machine. It should not:
   * the firmware *autobauds*, so the terminal sends at the terminal's rate and
   * the PROM works out which it was. At `77` the negotiation never completes
   * and the machine stays silent; at `BB` it prints its banner. `FINDINGS.md`
   * C113. */
  unsigned boot_input_rate = 0xBBu;
  unsigned boot_input_interval_us = 0u;
  unsigned boot_key = AP_KBD_KEYS; /* none */
  ap_screen_kind_t boot_screen = AP_SCREEN_NONE;
  const char *screenshot = NULL;
  bool run_probe_suite = false;
  bool run_ring_probe_suite = false;
  const char *probe_file_path = nullptr;
  bool report_timing = false;
  const char *boot_tape = NULL;
  const char *boot_prom = NULL;
  unsigned ram_megabytes = 0u; /* 0 = take the default from the model */
  const char *tape_path = NULL;
  /* The node this machine presents. `012345` is what every board in this
   * project has been built with; `--volume` replaces it with the identity the
   * disk itself records, which is the only source that can make a machine and
   * its file system agree. */
  const char *volume_path = NULL;
  const char *floppy_path = NULL;
  const char *disk_path = NULL;
  const char *battery_path = NULL;
  const char *dump_spec = NULL;
  uint32_t node_id = 0x012345u;
  unsigned boot_limit = 100000u;

  for (int i = 1; i < argc;) {
    if (strcmp(argv[i], "--volume") == 0 && i + 1 < argc) {
      volume_path = argv[i + 1];
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-input-interval") == 0 && i + 1 < argc) {
      boot_input_interval_us = (unsigned)strtoul(argv[i + 1], NULL, 0);
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--dump-mem") == 0 && i + 1 < argc) {
      dump_spec = argv[i + 1];
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--dump-logical") == 0 && i + 1 < argc) {
      /* Accepted more than once. A single dump per run answers one question,
       * and the questions come in pairs -- an instruction stream *and* the
       * address it names -- at ten minutes a run. */
      if (dump_logical_count < AP_MAX_LOGICAL_DUMPS) {
        dump_logical_specs[dump_logical_count++] = argv[i + 1];
      }
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--calendar-ram") == 0 && i + 1 < argc) {
      battery_path = argv[i + 1];
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--disk") == 0 && i + 1 < argc) {
      disk_path = argv[i + 1];
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--floppy") == 0 && i + 1 < argc) {
      floppy_path = argv[i + 1];
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--tape") == 0 && i + 1 < argc) {
      tape_path = argv[i + 1];
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--ram") == 0 && i + 1 < argc) {
      char *end = NULL;
      unsigned long mb = strtoul(argv[i + 1], &end, 10);
      if (end == argv[i + 1] || *end != '\0' || mb == 0u || mb > 4096u) {
        fprintf(stderr, "apollo: --ram wants a size in megabytes\n");
        return 2;
      }
      ram_megabytes = (unsigned)mb;
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-prom") == 0 && i + 1 < argc) {
      boot_prom = argv[i + 1];
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-tape") == 0 && i + 1 < argc) {
      boot_tape = argv[i + 1];
      i += 2;
      continue;
    }
    /* Stopping a boot short is how you find where it went wrong. Without it the
     * only observable is the end state, and an end state cannot say which
     * instruction produced it -- a wild PC looks the same however far back the
     * mistake was made. */
    if (strcmp(argv[i], "--boot-limit") == 0 && i + 1 < argc) {
      boot_limit = (unsigned)strtoul(argv[i + 1], NULL, 0);
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--service-mode") == 0) {
      service_mode = true;
      i += 1;
      continue;
    }
    if (strcmp(argv[i], "--boot-progress") == 0 && i + 1 < argc) {
      boot_progress = (unsigned)strtoul(argv[i + 1], NULL, 0);
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-watch-write") == 0 && i + 1 < argc) {
      boot_watch_write = (uint32_t)strtoul(argv[i + 1], NULL, 16);
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-watch-read") == 0 && i + 1 < argc) {
      boot_watch_read = (uint32_t)strtoul(argv[i + 1], NULL, 16);
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-stop-on-watch-read") == 0 && i + 1 < argc) {
      boot_stop_on_watch_read = (unsigned)strtoul(argv[i + 1], NULL, 0);
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-stop-physical-pc") == 0 && i + 1 < argc) {
      if (!parse_stop_spec(argv[i + 1], &boot_stop_physical_pc,
                           &boot_stop_physical_length)) {
        fprintf(stderr,
                "%s: --boot-stop-physical-pc wants ADDR or ADDR:LEN in hex\n",
                program_name);
        return 2;
      }
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-stop-on-watch-write") == 0 && i + 1 < argc) {
      boot_stop_on_watch = (unsigned)strtoul(argv[i + 1], NULL, 0);
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-stop-on-disk-refusal") == 0) {
      boot_stop_on_refusal = true;
      i += 1;
      continue;
    }
    if (strcmp(argv[i], "--boot-report") == 0) {
      boot_report = true;
      i += 1;
      continue;
    }
    if (strcmp(argv[i], "--clock") == 0 && i + 1 < argc) {
      if (!parse_clock(argv[i + 1], &boot_clock)) {
        fprintf(stderr,
                "%s: --clock wants YYYY-MM-DD or YYYY-MM-DDTHH:MM:SS\n",
                program_name);
        return 2;
      }
      boot_clock_set = true;
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-type-after-os") == 0) {
      boot_type_after_os = true;
      i += 1;
      continue;
    }
    if (strcmp(argv[i], "--boot-type-after-pc") == 0 && i + 1 < argc) {
      /* **Hex, like every other address flag.** `--boot-watch-write`,
       * `--boot-watch-read` and `--boot-stop-pc` all take an address as hex
       * without a prefix; this one took base 0, so `--boot-type-after-pc 2670`
       * armed at decimal 2670 and the trigger never fired. The failure is
       * silent -- the run completes and reports `0 of 2 character(s) typed` --
       * which is exactly the shape that wastes a fifteen-minute boot. */
      boot_type_after_pc = (uint32_t)strtoul(argv[i + 1], NULL, 16);
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-type-then") == 0 && i + 1 < argc) {
      boot_typed2 = argv[i + 1];
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-type-then-after-pc") == 0 && i + 1 < argc) {
      boot_type2_after_pc = (uint32_t)strtoul(argv[i + 1], NULL, 16);
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--list-oracle-quirks") == 0) {
      for (unsigned q = 0; q < AP_QUIRK_COUNT; q++) {
        printf("  %-28s %s\n", ap_quirk_name((ap_quirk_t)q),
               ap_quirk_description((ap_quirk_t)q));
      }
      return 0;
    }
    if (strcmp(argv[i], "--oracle-quirk") == 0 && i + 1 < argc) {
      ap_quirk_t q = AP_QUIRK_GRAPHICS_ID_ALWAYS_COLOUR;
      if (!ap_quirk_by_name(argv[i + 1], &q)) {
        /* Refused rather than ignored: a typo that ran the reference machine
         * while the report claimed an oracle comparison would invalidate the
         * comparison silently, which is the failure mode this whole exercise
         * exists to avoid. */
        fprintf(stderr, "apollo: unknown oracle quirk %s;"
                        " --list-oracle-quirks shows them\n", argv[i + 1]);
        return 2;
      }
      ap_quirk_select(&g_quirks, q);
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--dump-state") == 0 && i + 1 < argc) {
      g_dump_state_path = argv[i + 1];
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-type-await-pushback") == 0) {
      boot_type_await_pushback = true;
      i += 1;
      continue;
    }
    if (strcmp(argv[i], "--boot-type") == 0 && i + 1 < argc) {
      boot_typed = argv[i + 1];
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-key") == 0 && i + 1 < argc) {
      /* A matrix index, not a character: this keyboard reports keys, and the
       * firmware's own table turns them into characters. */
      boot_key = (unsigned)strtoul(argv[i + 1], NULL, 0);
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-input-rate") == 0 && i + 1 < argc) {
      boot_input_rate = (unsigned)strtoul(argv[i + 1], NULL, 0);
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-input-channel") == 0 && i + 1 < argc) {
      const char c = argv[i + 1][0];
      boot_input_channel = (c == 'b' || c == 'B' || c == '1') ? 1u : 0u;
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-input-port") == 0 && i + 1 < argc) {
      /* 1 or 2 as the board names them; 0-based inside. */
      const unsigned port = (unsigned)strtoul(argv[i + 1], NULL, 0);
      boot_input_unit = (port >= 2u) ? 1u : 0u;
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--screen") == 0 && i + 1 < argc) {
      /* The four the firmware knows, by the names its own probe order
       * implies -- not by ours. */
      const char *name = argv[i + 1];
      if (strcmp(name, "c4p") == 0) {
        boot_screen = AP_SCREEN_COLOUR_4_PLANE;
      } else if (strcmp(name, "c8p") == 0) {
        boot_screen = AP_SCREEN_COLOUR_8_PLANE;
      } else if (strcmp(name, "19i") == 0) {
        boot_screen = AP_SCREEN_MONO_19_INCH;
      } else if (strcmp(name, "15i") == 0) {
        boot_screen = AP_SCREEN_MONO_15_INCH;
      } else {
        fprintf(stderr, "apollo: unknown screen %s (c4p, c8p, 19i, 15i)\n",
                name);
        return 1;
      }
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
      screenshot = argv[i + 1];
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-script") == 0 && i + 1 < argc) {
      boot_script = argv[i + 1];
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-console") == 0) {
      boot_console = true;
      i += 1;
      continue;
    }
    if (strcmp(argv[i], "--boot-input") == 0 && i + 1 < argc) {
      boot_input = argv[i + 1];
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-watch") == 0 && i + 1 < argc) {
      boot_watch = (uint32_t)strtoul(argv[i + 1], NULL, 0);
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-stop-on-mmu-fault-at") == 0 && i + 1 < argc) {
      boot_stop_mmu_fault_at =
          (uint32_t)strtoul(argv[i + 1], NULL, 16);
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-progress-from") == 0 && i + 1 < argc) {
      boot_progress_from = (uint32_t)strtoul(argv[i + 1], NULL, 16);
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-stop-pc-then") == 0 && i + 1 < argc) {
      boot_stop_pc_then = (unsigned)strtoul(argv[i + 1], NULL, 0);
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-stop-pc-skip") == 0 && i + 1 < argc) {
      boot_stop_pc_skip = (unsigned)strtoul(argv[i + 1], NULL, 0);
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-stop-pc") == 0 && i + 1 < argc) {
      /* `ADDR` or `ADDR:LEN`, the same shape `--dump-mem` takes. A range
       * because the address worth stopping at is not always one you can name:
       * a crash routine's *return* address is printed in the message and never
       * executed, and the call that pushed it is two, four or six bytes before
       * it depending on how it was made. */
      if (!parse_stop_spec(argv[i + 1], &boot_stop_pc, &boot_stop_pc_length)) {
        fprintf(stderr, "%s: --boot-stop-pc wants ADDR or ADDR:LEN in hex\n",
                program_name);
        return 2;
      }
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-trace-last") == 0 && i + 1 < argc) {
      boot_trace_last = (unsigned)strtoul(argv[i + 1], NULL, 10);
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-trace") == 0) {
      boot_trace = true;
      i += 1;
      continue;
    }
    if (strcmp(argv[i], "--run-probes") == 0) {
      run_probe_suite = true;
      i += 1;
      continue;
    }
    if (strcmp(argv[i], "--run-ring-probes") == 0) {
      run_ring_probe_suite = true;
      i += 1;
      continue;
    }
    if (strcmp(argv[i], "--probe-file") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "%s: --probe-file needs a path\n", program_name);
        return 2;
      }
      probe_file_path = argv[i + 1];
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--time-instructions") == 0) {
      report_timing = true;
      i += 1;
      continue;
    }

    int consumed = 0;
    const char *err = nullptr;
    switch (ap_common_parse_arg(&opt, argc, argv, i, &consumed, &err)) {
      case AP_ARG_CONSUMED:
        i += consumed;
        break;
      case AP_ARG_ERROR:
        fprintf(stderr, "%s: %s: %s\n", program_name, argv[i], err);
        return 2;
      case AP_ARG_UNKNOWN:
        fprintf(stderr, "%s: unrecognised option: %s\n", program_name, argv[i]);
        print_usage(program_name);
        return 2;
    }
  }

  if (opt.help) {
    print_usage(program_name);
    return 0;
  }

  /* How much memory to fit, resolved before anything is built and against the
   * model table, because memory size is machine variance.
   *
   * The default is sixteen megabytes **capped at what the model can take**. A
   * size the model cannot be built in is not a small error: the boot PROM sizes
   * memory from a strap byte, `ap_sio_ram_config_byte` has no entry for an
   * impossible configuration, so the board goes out unstrapped and the firmware
   * fails its memory test rather than saying what is wrong. A DN3000 fitted
   * with sixteen megabytes -- twice its maximum -- did exactly that, and it was
   * this sweep of every firmware revision that found it. */
  {
    const uint32_t model_max = opt.model->ram_max_bytes;
    const unsigned model_max_mb = (unsigned)(model_max / (1024u * 1024u));
    if (ram_megabytes == 0u) {
      ram_megabytes = model_max_mb < 16u ? model_max_mb : 16u;
    } else if (ram_megabytes > model_max_mb) {
      fprintf(stderr,
              "apollo: %s takes at most %u MB of memory, not %u\n",
              opt.model->name, model_max_mb, ram_megabytes);
      return 2;
    }
  }

  if (opt.list_models) {
    ap_print_model_table(stdout);
    return 0;
  }

  /* Read before anything is built, so a machine is never constructed with an
   * identity that is about to be replaced -- and so a volume that is not one
   * fails before a run rather than during it. */
  if (volume_path != NULL && !node_id_from_volume(volume_path, &node_id)) {
    return 1;
  }

  if (floppy_path != NULL) {
    return report_floppy(floppy_path);
  }

  if (tape_path != NULL) {
    return report_tape(tape_path);
  }

  if (boot_prom != NULL) {
    return boot_from_prom(boot_prom, boot_limit, boot_trace, boot_watch,
                          boot_input, boot_input_unit, boot_input_channel,
                          (uint8_t)boot_input_rate, boot_input_interval_us,
                          boot_key, boot_typed, boot_type_after_os,
                          boot_type_after_pc, boot_typed2, boot_type2_after_pc,
                          boot_type_await_pushback,
                          boot_clock_set ? &boot_clock : NULL, boot_report,
                          boot_console,
                          boot_screen, node_id, opt.model->id, screenshot,
                          boot_trace_last, boot_stop_pc, boot_script,
                          disk_path, battery_path, ram_megabytes,
                          dump_spec, boot_progress,
                          boot_stop_on_refusal, boot_watch_write,
                          boot_stop_on_watch, boot_watch_read,
                          boot_stop_on_watch_read, boot_stop_pc_length,
                          dump_logical_specs, dump_logical_count,
                          boot_stop_physical_pc, boot_stop_physical_length,
                          service_mode, boot_stop_pc_skip,
                          boot_stop_mmu_fault_at, boot_stop_pc_then,
                          boot_progress_from);
  }

  if (boot_tape != NULL) {
    return boot_from_tape(boot_tape, boot_limit);
  }

  if (probe_file_path != nullptr) {
    return run_probe_file(stdout, opt.model->id, program_name,
                          probe_file_path, dump_spec);
  }

  if (run_probe_suite) {
    run_probes(stdout, opt.model->id);
    return 0;
  }

  if (run_ring_probe_suite) {
    run_ring_probes(stdout);
    return 0;
  }

  if (report_timing) {
    time_instructions(stdout);
    return 0;
  }

  /* A CPU exists and probes run on it, but no *machine* of this model does:
   * there is no I/O, no device and no bus arbitration point, so `--model` has
   * nothing to build yet. Say which is missing rather than implying the whole
   * emulator is absent. */
  fprintf(stderr,
          "%s: cannot run %s yet -- the CPU works, but there are no devices to\n"
          "build a machine from. See docs/PROJECT_STATUS.md; --list-models and\n"
          "--run-probes work now.\n",
          program_name, opt.model->name);
  return 2;
}

#!/usr/bin/env python3
"""Exercise the headless frontend's flags, each one, in CTest.

Phase 5 asks for "headless frontend flags that earn their keep ... *Verification:
each flag exercised in CTest*", and until this existed not one of them was. The
flags are the project's own instruments -- every campaign in `FINDINGS.md` since
the console was reached was driven by one -- and an instrument nothing checks is
one that breaks silently and takes a measurement with it.

## What can be checked here, and what cannot

`roms/` and `media/` are gitignored: Apollo firmware and Domain/OS media are not
this project's to redistribute, so CI has neither. Every flag that needs a boot
PROM is therefore unreachable *here* and is listed as skipped with that reason
rather than quietly omitted -- a list of what is not covered is worth as much as
the coverage.

What is reachable is more than it looks, because `--probe-file` takes `board 1`
and builds a whole machine with **no firmware at all**. Flags that only need a
machine work under it.
"""

from __future__ import annotations

import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# A probe that stores a sentinel, on a board, so a machine exists to interrogate.
PROBE = """load  1001000
entry 1001000
stack 1002000
limit 20
read  1001800
board 1
words 7005 23C0 0100 1800 4E72 2700
"""

failures = 0
skipped: list[tuple[str, str]] = []


def find_headless() -> Path:
    for preset in ("linux-debug", "linux-release", "linux-ci", "windows-ci",
                   "macos-ci"):
        for name in ("apollo-headless", "apollo-headless.exe"):
            candidate = REPO / "build" / preset / "src" / "frontend" / "headless" / name
            if candidate.is_file():
                return candidate
    sys.stderr.write("check_frontend_flags: no apollo-headless built\n")
    raise SystemExit(2)


def run(args: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run([str(find_headless()), *args], capture_output=True,
                          text=True, timeout=300)


def source_check(name: str, held: bool) -> None:
    """Assert something about the frontend's source rather than its output.

    For properties that need firmware to observe, which CI has none of. Weaker
    than running the binary, and named so that is visible in the log.
    """
    global failures
    if held:
        sys.stdout.write("ok   %s (source)\n" % name)
        return
    failures += 1
    sys.stderr.write("FAIL %s (source)\n" % name)


def check(name: str, args: list[str], expect: str, want_ok: bool = True) -> None:
    """Run the binary and require `expect` in its output.

    The pattern matters more than the exit code: a flag that is accepted and does
    nothing exits zero, which is the failure this test exists to catch.
    """
    global failures
    proc = run(args)
    ok = (proc.returncode == 0) == want_ok
    found = re.search(expect, proc.stdout + proc.stderr) is not None
    if ok and found:
        sys.stdout.write("ok   %s\n" % name)
        return
    failures += 1
    sys.stderr.write("FAIL %s\n  args: %s\n  exit: %d (wanted %s)\n"
                     "  pattern %r %s\n"
                     % (name, " ".join(args), proc.returncode,
                        "0" if want_ok else "non-zero", expect,
                        "found" if found else "NOT found"))
    sys.stderr.write("  output: %s\n" % (proc.stdout + proc.stderr)[:600])


def skip(name: str, why: str) -> None:
    skipped.append((name, why))


def main() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        spec = work / "probe.spec"
        spec.write_text(PROBE)

        # ---- flags that need only the binary ----
        check("--help lists the flags", ["--help"], r"--dump-mem")
        check("--list-models prints the table and the time base",
              ["--list-models"], r"time base: \d+ Hz")
        check("--model selects a machine", ["--model", "dn3000", "--list-models"],
              r"dn3000")
        check("--model refuses a machine that does not exist",
              ["--model", "dn9999", "--list-models"], r"unknown model name",
              want_ok=False)

        # Memory size is machine variance, so it is checked against the model
        # table rather than against a constant. A DN3000 fitted with sixteen
        # megabytes -- twice its maximum -- leaves the boot PROM's sizing strap
        # unset, and the firmware fails its memory test instead of saying so.
        check("--ram accepts a size the model can be built in",
              ["--model", "dn3000", "--ram", "8", "--list-models"],
              r"time base: \d+ Hz")
        check("--ram refuses more memory than the model takes",
              ["--model", "dn3000", "--ram", "64", "--list-models"],
              r"dn3000 takes at most 8 MB", want_ok=False)
        check("--ram refuses a size that is not one",
              ["--ram", "nonsense", "--list-models"],
              r"--ram wants a size in megabytes", want_ok=False)

        # A bound above the core's 32-bit instruction counter is **refused**, not
        # wrapped. `--boot-limit 6000000000` used to parse into an `unsigned` and
        # become 1,705,032,704 -- `6e9 mod 2^32` -- so runs that asked for six
        # billion instructions stopped at 1.7 billion and reported the bound they
        # were given as if it had been honoured. Three measurements were taken
        # that way before the identical instruction counts gave it away.
        #
        # No second flag: `--list-models` is handled in an earlier pass and
        # would exit 0 before the argument loop ran, so a check that paired them
        # would pass whatever the guard did.
        check("--boot-limit refuses a bound the core cannot count to",
              ["--boot-limit", "6000000000"],
              r"exceeds this core's \d+-instruction ceiling", want_ok=False)

        # ---- flags that need a machine, which `board 1` builds with no ROM ----
        # `moveq` is the first probe the suite reports; matching a probe's own
        # line rather than the header is what makes this a check that the suite
        # *ran* rather than that the flag was accepted.
        check("--run-probes runs the built-in suite", ["--run-probes"],
              r"moveq\s+\d+ STOPPED")
        check("--probe-file runs a probe from outside the binary",
              ["--probe-file", str(spec)], r"read\s+01001800 00000005")
        check("--dump-mem dumps through the board",
              ["--probe-file", str(spec), "--dump-mem", "1001800:10"],
              r"01001800  00 00 00 05")
        # The distinction the dump exists to draw: an address nothing answers
        # prints `--`, not `00`.
        check("--dump-mem marks what the board did not answer",
              ["--probe-file", str(spec), "--dump-mem", "FFF90000:10"],
              r"FFF90000  -- -- -- --")
        check("--dump-mem refuses a spec that is not one",
              ["--probe-file", str(spec), "--dump-mem", "nonsense"],
              r"wants ADDR or ADDR:LEN")

        # ---- media, which needs a file but not firmware ----
        floppy = work / "blank.afd"
        floppy.write_bytes(b"\x00" * (77 * 2 * 8 * 1024))
        check("--floppy reads an image through the reader",
              ["--floppy", str(floppy)],
              r"read\s+1232 sectors through the reader")
        short = work / "short.afd"
        short.write_bytes(b"\x00" * 4096)
        check("--floppy refuses an image that is not one",
              ["--floppy", str(short)], r"an Apollo floppy is exactly",
              want_ok=False)

        # ---- the console script, whose parsing needs no machine ----
        bad = work / "bad.script"
        bad.write_text("wait for something\n")
        check("--boot-script refuses a line that is not send or expect",
              ["--boot-prom", "/nonexistent", "--boot-script", str(bad)],
              r"not send or expect", want_ok=False)
        missing = work / "absent.script"
        check("--boot-script says so when the file is not there",
              ["--boot-prom", "/nonexistent", "--boot-script", str(missing)],
              r"cannot read console script", want_ok=False)

        # ---- two whole machines on one ring segment ----
        #
        # Needs no firmware, which is why it is checked here rather than
        # skipped: the mode's own work is building two boards, giving them
        # distinct node IDs and joining both to one scheduler, and all of that
        # happens before a single instruction runs. A PROM only decides what
        # the processors then do.
        check("--ring-two-node builds two nodes on one segment",
              ["--ring-two-node", "2000"],
              r"node 0 .*ring slot 0(.|\n)*node 1 .*ring slot 1")
        # And the ring's phase hash is reported, which is what makes a
        # multi-node run comparable across builds at all.
        check("--ring-two-node reports the ring's scheduling hash",
              ["--ring-two-node", "2000"], r"ring +hash [0-9A-F]{16}")
        # **The configuration table must describe the machine that was built.**
        # This mode used to seal "a ring and nothing else" into every node's
        # battery RAM while fitting a Winchester and an FPU, and -- with no
        # option ROM -- no findable ring at all, so the firmware failed its own
        # self-test naming all three discrepancies before it reached the loader
        # (FINDINGS.md C186). The bits are FLOPPY|CTAPE|WINCHESTER|FPU = 0x0F,
        # with RING (0x10) added only when `--ring-rom` gives the card a ROM for
        # the expansion scan to find. Checked without firmware because that is
        # what CI has: the table is built before any instruction runs.
        # **A machine takes its node from the volume it is given**, and for
        # four sessions `--disk` did not: it presented the compiled-in 12345
        # whatever the disk recorded, which is invisible while the only
        # installed volume happens to *be* 12345 and is a node that shuts itself
        # down when it is not (FINDINGS.md C199). The label is synthesised here
        # -- `media/` is gitignored and CI has none -- and it exercises the same
        # reader both flags now use: the creator UID at block 0 0x48, whose low
        # three bytes are the node.
        label = Path(tmp) / "node.awd"
        blob = bytearray(2048)
        blob[0x418:0x41C] = (0xFEDCA986).to_bytes(4, "big")     # the magic
        blob[0x48:0x50] = bytes.fromhex("A45AA673") + bytes([0x10, 0x03, 0x33, 0x33])
        label.write_bytes(bytes(blob))
        # `want_ok=False`: `--volume` reports a label and then declines to
        # build a machine, because without a boot PROM there is nothing to run.
        # The report is the point; the refusal afterwards is the same one every
        # PROM-less invocation gives.
        check("a volume's node comes from the creator UID",
              ["--volume", str(label)], r"node ID\s+33333", want_ok=False)

        check("a ring node's configuration table lists the devices it was given",
              ["--ring-two-node", "2000"],
              r"node 0  calendar ram: dev bits 0000000F"
              r"(.|\n)*node 1  calendar ram: dev bits 0000000F")

        # ---- what only the source can assert, and why ----
        #
        # The run header's `console` line needs a boot PROM to print, so CI --
        # which has no `roms/` -- cannot observe it. It is checked here anyway,
        # in the source, because the thing it guards against is precise: two
        # runs printing identical headers while one boots and one does not.
        # `--boot-console`, `--boot-input` and `--boot-script` were invisible in
        # the header for as long as they existed, and the silent run was read as
        # a device regression before the flags were noticed.
        main_c = (REPO / "src/frontend/headless/main.c").read_text()
        for fragment, what in (
                ('printf("  console      %s"', "the header's console line"),
                ("g_boot_console = boot_console;", "--boot-console reaches it"),
                ("g_boot_script_path = boot_script;", "--boot-script reaches it"),
                ("g_boot_input_text = boot_input;", "--boot-input reaches it")):
            source_check("the run header records how the console was driven: "
                         "%s" % what, fragment in main_c)

        # `--boot-script-line` needs a booted machine to observe, so it is
        # checked in the source for the same reason as the header line above.
        # It exists because the console's *output* drains all four serial
        # channels and its *input* went to exactly one: a login offered on
        # line 2 could be seen and never answered, and a working `siologin`
        # waiting for a carriage return was indistinguishable from a failing
        # one (`FINDINGS.md` C219).
        for fragment, what in (
                ('strcmp(argv[i], "--boot-script-line")', "the flag is parsed"),
                ("g_script_line_set ? g_script_unit : input_unit",
                 "the single-node path honours it"),
                ("g_script_line_set ? g_script_unit : AP_SIO_CONSOLE_UNIT",
                 "the two-node path honours it")):
            source_check("the script can type at a line other than the "
                         "console's: %s" % what, fragment in main_c)

        # ---- the model table's fields must be consulted, not just declared ----
        #
        # `model/`'s own rule is "all machine variance lives here, and every
        # other model is expressed as a subset from the one table". A field that
        # nothing outside the table reads is variance the machine does not
        # honour, and it reads exactly like variance it does.
        #
        # Found by audit on 2026-08-19 (`FINDINGS.md` C227, C228): `has_ring`
        # was set on eleven rows and read by nothing in `src/` at all -- the
        # card is fitted by `--ring` instead -- while `.mmu` was consumed only
        # by a hash-scope *name* and a line in a report, so `AP_MMU_M68851` on
        # the DN3000 rows named a part `ap_machine` never builds. Both are
        # recorded and neither is closed; this guard exists so the *next* one
        # is caught by CI rather than by someone happening to look.
        model_h = (REPO / "src/core/model/ap_model.h").read_text()
        struct = re.search(r"typedef struct \{(.*?)\} ap_model_t;", model_h,
                           re.S)
        fields = re.findall(r"^\s+(?:const\s+)?[A-Za-z_][A-Za-z0-9_ \*]*?"
                            r"\b([a-z_][a-z0-9_]*)\s*(?:\[[^\]]*\])?;",
                            struct.group(1), re.M) if struct else []
        sources = []
        for path in (REPO / "src").rglob("*.c"):
            if path.name != "ap_model.c":
                sources.append(path.read_text())
        blob = "\n".join(sources)
        # Known and recorded, so the guard reports the count rather than failing
        # a tree whose gaps are already written down.
        recorded = {"has_ring", "mmu"}
        for field in sorted(set(fields)):
            seen = re.search(r"[.\->]%s\b" % re.escape(field), blob) is not None
            if seen or field in recorded:
                continue
            fail("every `ap_model_t` field is consulted somewhere in src/: %s"
                 % field,
                 "declared in the model table and read by nothing outside it")
        source_check("every `ap_model_t` field is consulted somewhere in src/, "
                     "or is one of the %d already recorded as not"
                     % len(recorded), True)

        # ---- what needs firmware, named rather than omitted ----
        for flag in ("--boot-prom", "--boot-limit", "--boot-trace",
                     "--boot-watch", "--boot-console", "--boot-input",
                     "--boot-input-rate", "--boot-input-interval",
                     "--boot-key", "--screen", "--screenshot", "--disk",
                     "--disk-meta", "--diskette", "--cartridge",
                     "--option-rom-entry", "--option-rom-text",
                     "--boot-trace-last", "--boot-stop-pc", "--dump-mem",
                     "--boot-script (a dialogue, as opposed to its parsing)"):
            skip(flag, "needs a boot PROM; roms/ is gitignored and CI has none")

    for name, why in skipped:
        sys.stdout.write("skip %s -- %s\n" % (name, why))
    if failures:
        sys.stderr.write("\n%d flag check(s) failed\n" % failures)
        return 1
    sys.stdout.write("\nall reachable flags exercised; %d need firmware\n"
                     % len(skipped))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

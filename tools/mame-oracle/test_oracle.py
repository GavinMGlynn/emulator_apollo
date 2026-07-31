#!/usr/bin/env python3
"""Test oracle.py's driving logic against a stub MAME.

oracle.py's job splits in two. One half needs a real emulator: whether the
oracle's *numbers* are right is what FINDINGS.md campaigns settle. The other
half is ordinary program logic -- pull the dump out of a noisy stdout, notice
when two runs disagree, fail loudly when a run produces nothing -- and that half
does not need MAME at all. A stub that prints canned output exercises it in
milliseconds, so it is tested here rather than left to be discovered during a
multi-minute emulator run.

The stubs deliberately include the failure shapes that matter:

  - a *noisy* oracle, printing its own diagnostics around the dump, because
    real MAME does exactly that and an extra warning line must not make two
    identical runs look different;
  - a *nondeterministic* oracle, whose second run differs, because catching
    that is the entire reason the verify mode exists;
  - a *silent* oracle that emits no dump, because a run that produced nothing
    must fail rather than be mistaken for a pass.

    python3 tools/mame-oracle/test_oracle.py
"""

from __future__ import annotations

import os
import stat
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ORACLE = HERE / "oracle.py"

failures = 0


def check(name: str, actual, expected) -> None:
    global failures
    if actual != expected:
        failures += 1
        sys.stderr.write("FAIL %s\n  expected: %r\n  actual:   %r\n"
                         % (name, expected, actual))
    else:
        sys.stdout.write("ok   %s\n" % name)


def check_in(name: str, needle: str, haystack: str) -> None:
    global failures
    if needle not in haystack:
        failures += 1
        sys.stderr.write("FAIL %s\n  %r not found in:\n%s\n" % (name, needle, haystack))
    else:
        sys.stdout.write("ok   %s\n" % name)


DUMP = """# apollo oracle dump
machine dn3500
at 1.000000
[cpu :maincpu]
D0       DEADBEEF
PC       00010000
# end
"""


def write_stub(path: Path, body: str) -> None:
    path.write_text("#!/usr/bin/env python3\nimport sys, os\n" + body)
    path.chmod(path.stat().st_mode | stat.S_IEXEC | stat.S_IXGRP | stat.S_IXOTH)


def run_oracle(args: list, cwd: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, str(ORACLE)] + args,
        capture_output=True, text=True, cwd=str(cwd),
    )


def main() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        roms = root / "roms"
        (roms / "dn3500").mkdir(parents=True)
        rundir = root / "run"

        # A well-behaved but noisy oracle: MAME prints its own chatter, and the
        # dump has to be extracted from between the markers regardless.
        good = root / "mame_good"
        write_stub(good, (
            "sys.stdout.write('Average speed: 102.32%% (1 seconds)\\n')\n"
            "sys.stdout.write(%r)\n"
            "sys.stdout.write('Exiting...\\n')\n"
        ) % DUMP)

        # Differs on the second run. Uses a marker file rather than a random
        # value so the test itself stays deterministic.
        flaky = root / "mame_flaky"
        write_stub(flaky, (
            "marker = os.path.join(os.path.dirname(os.path.abspath(__file__)), '.ran')\n"
            "second = os.path.exists(marker)\n"
            "open(marker, 'w').close()\n"
            "d = %r\n"
            "if second: d = d.replace('DEADBEEF', 'CAFEBABE')\n"
            "sys.stdout.write(d)\n"
        ) % DUMP)

        silent = root / "mame_silent"
        write_stub(silent, "sys.stdout.write('nothing useful here\\n')\n")

        failing = root / "mame_failing"
        write_stub(failing, "sys.stderr.write('boom\\n')\nsys.exit(3)\n")

        common = ["--roms", str(roms), "--rundir", str(rundir), "--machine", "dn3500"]

        # ---- run mode -------------------------------------------------------
        proc = run_oracle(["run", "--mame", str(good)] + common, root)
        check("a clean run exits 0", proc.returncode, 0)
        check("the dump is extracted from noisy output", proc.stdout, DUMP)
        check_in("MAME's own chatter is stripped", "# apollo oracle dump", proc.stdout)
        global failures
        if "Average speed" in proc.stdout:
            failures += 1
            sys.stderr.write("FAIL surrounding chatter leaked into the dump\n")
        else:
            sys.stdout.write("ok   surrounding chatter does not leak into the dump\n")

        # ---- run mode, --out ------------------------------------------------
        out_file = root / "dump.txt"
        proc = run_oracle(["run", "--mame", str(good), "--out", str(out_file)] + common, root)
        check("--out exits 0", proc.returncode, 0)
        check("--out writes the dump verbatim", out_file.read_text(), DUMP)

        # ---- verify mode, deterministic -------------------------------------
        proc = run_oracle(["verify", "--mame", str(good)] + common, root)
        check("verify passes when two runs agree", proc.returncode, 0)
        check_in("verify says what it proved", "byte-identical", proc.stdout)

        # ---- verify mode, nondeterministic ----------------------------------
        proc = run_oracle(["verify", "--mame", str(flaky)] + common, root)
        check("verify fails when two runs differ", proc.returncode, 1)
        check_in("the mismatch is shown as a diff", "-D0       DEADBEEF", proc.stderr)
        check_in("and names why it matters", "Until this is explained", proc.stderr)

        # ---- no dump at all --------------------------------------------------
        proc = run_oracle(["run", "--mame", str(silent)] + common, root)
        check("a run producing no dump fails", proc.returncode, 1)
        check_in("and says the script did not complete", "no dump in", proc.stderr)

        # ---- oracle exits non-zero -------------------------------------------
        proc = run_oracle(["run", "--mame", str(failing)] + common, root)
        check("a non-zero exit from MAME fails", proc.returncode, 1)
        check_in("and reports MAME's own stderr", "boom", proc.stderr)

        # ---- environment problems are distinguished from run failures --------
        proc = run_oracle(["run", "--mame", str(root / "nope")] + common, root)
        check("a missing binary is exit 2, not 1", proc.returncode, 2)

        proc = run_oracle(
            ["run", "--mame", str(good), "--roms", str(root / "absent"),
             "--rundir", str(rundir), "--machine", "dn3500"], root)
        check("missing ROM sets is exit 2, not 1", proc.returncode, 2)
        check_in("and points at romset.py", "romset.py", proc.stderr)

        # ---- the run directory is wiped between runs -------------------------
        # NVRAM and cfg persist across MAME runs by design, which is precisely
        # what would make run 2 start from a different machine than run 1.
        stale = rundir / "nvram" / "stale.nv"
        stale.parent.mkdir(parents=True, exist_ok=True)
        stale.write_text("stale")
        run_oracle(["run", "--mame", str(good)] + common, root)
        check("a stale run directory is wiped, not reused", stale.exists(), False)

    sys.stdout.write("\n%s: %d failure(s)\n" % ("PASS" if not failures else "FAIL", failures))
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())

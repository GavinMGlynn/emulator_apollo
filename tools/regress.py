#!/usr/bin/env python3
"""Golden result-block regression checker.

Runs a command, captures its stdout, and compares it against a checked-in
golden file. Exit status 0 on match, 1 on mismatch, 2 on a usage or execution
problem.

Why a golden per result block rather than diffing two builds against each
other: comparing -O0 output with -O3 output only proves the two agree, which is
also true when both have drifted together. Checking each against a committed
golden catches that, and it catches a platform that quietly differs from the
other three -- which is the property this project actually claims.

Line endings are normalised before comparison. On Windows, C stdio in text mode
turns every \\n into \\r\\n, so the same emulator legitimately emits different
bytes there. That is a host artefact, not emulated state, and normalising it is
the only way one golden can serve all three platforms. Nothing else is
normalised: trailing whitespace, blank lines and column alignment are all
significant, because a report's layout is part of what the golden pins.
"""

from __future__ import annotations

import argparse
import difflib
import subprocess
import sys
from pathlib import Path


def normalise(text: str) -> str:
    """Collapse CRLF and lone CR to LF. See the module docstring."""
    return text.replace("\r\n", "\n").replace("\r", "\n")


def capture(command: list) -> str:
    try:
        proc = subprocess.run(command, capture_output=True, text=True)
    except OSError as exc:
        sys.stderr.write("regress: cannot run %s: %s\n" % (command[0], exc))
        raise SystemExit(2)
    if proc.returncode != 0:
        sys.stderr.write(
            "regress: %s exited %d\n%s\n" % (command[0], proc.returncode, proc.stderr)
        )
        raise SystemExit(2)
    return normalise(proc.stdout)


def report_mismatch(name: str, golden_path: Path, expected: str, actual: str) -> None:
    sys.stderr.write("regress: %s does not match its golden\n" % name)
    sys.stderr.write("  golden: %s\n\n" % golden_path)
    diff = difflib.unified_diff(
        expected.splitlines(keepends=True),
        actual.splitlines(keepends=True),
        fromfile="golden",
        tofile="actual",
        n=2,
    )
    sys.stderr.writelines(diff)
    sys.stderr.write(
        "\nIf this change is intended, re-run with 'update' and commit the "
        "golden alongside the change that caused it. A golden updated in its "
        "own commit hides what moved.\n"
    )


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=["check", "update"])
    ap.add_argument("golden", type=Path, help="path to the golden file")
    ap.add_argument("command", nargs=argparse.REMAINDER,
                    help="-- followed by the command to run")
    args = ap.parse_args(argv)

    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        sys.stderr.write("regress: no command given (use -- before it)\n")
        return 2

    actual = capture(command)
    name = args.golden.name

    if args.mode == "update":
        args.golden.parent.mkdir(parents=True, exist_ok=True)
        # newline="\n" so a golden written on Windows is byte-identical to one
        # written on Linux; the file itself is the cross-platform artefact.
        with open(args.golden, "w", newline="\n") as fh:
            fh.write(actual)
        sys.stdout.write("regress: wrote %s (%d bytes)\n" % (args.golden, len(actual)))
        return 0

    if not args.golden.exists():
        sys.stderr.write(
            "regress: no golden at %s.\nCreate it with:\n  %s update %s -- %s\n"
            % (args.golden, Path(__file__).name, args.golden, " ".join(command))
        )
        return 2

    with open(args.golden, "r", newline="") as fh:
        expected = normalise(fh.read())

    if expected != actual:
        report_mismatch(name, args.golden, expected, actual)
        return 1

    sys.stdout.write("regress: %s matches (%d bytes)\n" % (name, len(actual)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

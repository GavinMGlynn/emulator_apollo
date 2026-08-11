#!/usr/bin/env python3
"""The state dump must be a function of the machine state and nothing else.

`--dump-state` is one half of the oracle differential, and every conclusion it
supports rests on an assumption nothing else checks: that two runs of the same
machine to the same point produce the *same file*. The state hash is already
pinned by the goldens, but the hash and the dump are different artefacts — the
dump carries field names, indices and derived lines that the hash never sees, so
a hash can stay constant while the dump's *shape* moves underneath a field map.

That is not hypothetical. Field indices were found to depend on how full the
DUART's receive FIFO happened to be, because the walk hashes only the occupied
entries. The hash was correct throughout; the dump renumbered every field after
it, and a map keyed on those indices would have compared unrelated values while
still looking right. This checks the property that fix established.

Two things are asserted, and the second is the one with teeth:

  * **the same point twice is byte-identical** — no host addresses, no
    timestamps, no iteration order leaking in;
  * **a different point differs** — so the first assertion cannot be passing
    because the dump is blind or empty, which is how a determinism test quietly
    becomes a tautology.

Skips, rather than fails, when the boot PROM or the disk image is absent: both
are gitignored because they are not ours to redistribute, so CI has neither.
"""

import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

PROM = os.path.join(REPO, "roms", "firmware", "3500_BOOT_12191_7.bin")
DISK = os.path.join(REPO, "media", "dn3500-sr10.4-installed.awd")

FAILURES = []


def binary():
    for build in ("linux-release", "linux-debug"):
        path = os.path.join(REPO, "build", build, "src", "frontend", "headless",
                            "apollo-headless")
        if os.access(path, os.X_OK):
            return path
    return None


def dump(bin_path, out_path, writes):
    """Boot to the Nth write of the diagnostic register and dump there.

    The stop is a *posted diagnostic code*, not an instruction count: it is the
    same sync point the oracle differential uses, so this exercises the dump at
    the place it is actually taken.
    """
    proc = subprocess.run(
        [bin_path,
         "--boot-prom", PROM,
         "--disk", DISK,
         "--boot-watch-write", "10100",
         "--boot-stop-on-watch-write", str(writes),
         "--boot-limit", "50000000",
         "--dump-state", out_path],
        capture_output=True, text=True)
    return proc.returncode, proc.stdout


def check(name, cond, detail=""):
    if cond:
        print(f"  ok    {name}")
    else:
        FAILURES.append(name)
        print(f"  FAIL  {name}  {detail}")


def main():
    print("dump determinism:")

    bin_path = binary()
    if bin_path is None:
        print("  skip  no apollo-headless built")
        return 0
    for needed in (PROM, DISK):
        if not os.path.exists(needed):
            print(f"  skip  {os.path.basename(needed)} not present "
                  f"(gitignored, not ours to redistribute)")
            return 0

    with tempfile.TemporaryDirectory() as d:
        first = os.path.join(d, "a.txt")
        second = os.path.join(d, "b.txt")
        later = os.path.join(d, "c.txt")

        for path, writes in ((first, 3), (second, 3), (later, 5)):
            code, out = dump(bin_path, path, writes)
            check(f"the boot to write {writes} completes", code == 0, out[-400:])
            if code != 0:
                return 1

        a = open(first).read()
        b = open(second).read()
        c = open(later).read()

        check("the same point twice is byte-identical", a == b,
              f"{len(a)} vs {len(b)} bytes")
        # Not a tautology: the dump has to be capable of differing at all.
        check("a different point differs", a != c, "identical, so blind")
        check("the dump is not empty", len(a.splitlines()) > 100,
              f"{len(a.splitlines())} lines")

        # The walk hash printed beside the dump is the check that the dump is
        # the *same traversal* the identity harness takes. If those disagree the
        # dump is of something else, and nothing in it can be compared.
        code, out = dump(bin_path, first, 3)
        hashes = [line for line in out.splitlines()
                  if "state hash" in line or "walk hash" in line]
        check("the walk hash and the state hash agree",
              "DIFFERS" not in out, "\n".join(hashes))

    print(f"\n{len(FAILURES)} failure(s)")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())

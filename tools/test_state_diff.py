#!/usr/bin/env python3
"""`state-diff.py`'s own tests.

The tool decides what counts as a difference between this core and the oracle,
so a fault in it is a wrong answer about the emulator wearing the authority of a
measurement. Two of its behaviours are load-bearing and neither is obvious:

  * an **unmapped** field is not a difference -- the mapping is expected to be
    partial for a long time, and reporting the gap as a finding would bury the
    real ones;
  * a **positional** scope mapping is refused when the two scopes differ in
    length, because then the orders cannot be assumed to correspond.

Both are checked here against constructed inputs rather than against the tool's
own output, which is the same rule `test_encoder.py` follows.
"""

import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "state-diff.py")

FAILURES = []


def run(ours, theirs, mapping, *args):
    with tempfile.TemporaryDirectory() as d:
        paths = {}
        for name, text in (("a", ours), ("b", theirs), ("m", mapping)):
            paths[name] = os.path.join(d, name)
            with open(paths[name], "w") as f:
                f.write(text)
        p = subprocess.run(
            [sys.executable, TOOL, paths["a"], paths["b"], "--map", paths["m"],
             *args], capture_output=True, text=True)
        return p.returncode, p.stdout


def check(name, cond, detail=""):
    if cond:
        print(f"  ok    {name}")
    else:
        FAILURES.append(name)
        print(f"  FAIL  {name}  {detail}")


def main():
    print("state-diff:")

    # A mapped pair whose values agree is not reported, and one that differs is.
    code, out = run("x.000 u32 00000000000000AA\nx.001 u32 00000000000000BB\n",
                    "y.0.0 u32 00000000000000AA\ny.0.1 u32 00000000000000CC\n",
                    "x.000\ty.0.0\nx.001\ty.0.1\n")
    check("a differing mapped field is reported", "DIFFERS" in out and
          "x.001" in out, out)
    check("an agreeing mapped field is not", out.count("DIFFERS") == 1, out)
    check("differences make the exit status non-zero", code != 0, str(code))

    # An unmapped field is not a difference. This is the one that matters most:
    # for most of this work most fields will be unmapped.
    code, out = run("x.000 u32 0000000000000001\nz.000 u32 0000000000000009\n",
                    "y.0.0 u32 0000000000000001\n",
                    "x.000\ty.0.0\n")
    check("an unmapped field is not a difference", code == 0, out)
    check("unmapped fields are counted", "1 unmapped here" in out, out)

    # A mapped field that is absent is a real finding: one core models something
    # the other does not, or a name moved under us.
    code, out = run("x.000 u32 0000000000000001\n",
                    "y.0.0 u32 0000000000000001\n",
                    "x.000\ty.0.0\nx.001\ty.0.1\n")
    check("a mapped but absent field is reported", "MISSING" in out, out)
    check("a missing field fails the run", code != 0, str(code))

    # Positional scope mapping: sound when the lengths agree, refused when they
    # do not. Refusing is the point -- equal lengths are weak evidence that the
    # orders correspond, and unequal lengths are proof they do not.
    code, out = run("x.000 u32 0000000000000001\nx.001 u32 0000000000000002\n",
                    "y.0.0 u32 0000000000000001\ny.0.1 u32 0000000000000002\n",
                    "x.*\ty.*\n")
    check("equal scopes map positionally", "2 matched" in out, out)

    code, out = run("x.000 u32 0000000000000001\nx.001 u32 0000000000000002\n",
                    "y.0.0 u32 0000000000000001\n",
                    "x.*\ty.*\n")
    check("unequal scopes are refused, not guessed",
          "SCOPE SIZE" in out and "0 matched" in out, out)

    print(f"\n{len(FAILURES)} failure(s)")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())

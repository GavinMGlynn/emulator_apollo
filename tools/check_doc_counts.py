#!/usr/bin/env python3
"""Check the test counts `PROJECT_STATUS.md` claims against the suites.

The subsystem table's Verification column names its evidence as, for example,
"`step_suite`, 175 tests". That is a *claim about the tree*, and until this
existed nothing checked it: every count went stale the moment a test was added,
silently, ninety of them at once.

They had all drifted the same way -- understating, because suites only grow --
which is the harmless direction and exactly why it survived. `step_suite` said
175 against an actual 270. A reader deciding whether a subsystem was worth
trusting was reading a number from whenever that row was last touched.

This project has spent a long campaign finding claims that were true when
written and never re-run (`FINDINGS.md` C88, C90, C94, C103). A count is the
cheapest kind of claim to verify and the easiest to let rot, so it is checked
here rather than remembered.

## Why RUN_TEST and not the binaries

Unity reports the number of `RUN_TEST` invocations, so counting them in the
source is the same number the suite prints -- verified against four suites when
this was written. Reading the source means the check needs no build, so it runs
in a second and can be a CTest entry that never has to be skipped.

The cost is that a test *defined* but never registered is invisible to both this
and the suite itself, which is a different defect and one `-Wunused-function`
already catches.

    python3 tools/check_doc_counts.py
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
STATUS = REPO / "docs" / "PROJECT_STATUS.md"
TESTS = REPO / "tests"

# "`name_suite`, N tests" or "`name_suite`, N test" inside a table row. Only
# table rows: the prose says things like "`step_suite` +4", which is a delta and
# not a claim about the total.
CLAIM = re.compile(r"`([a-z0-9_]+_suite)`,\s*(\d+)\s+tests?\b")


def registered(suite: str) -> int | None:
    source = TESTS / f"{suite}.c"
    if not source.is_file():
        return None
    return len(re.findall(r"\bRUN_TEST\(", source.read_text()))


def main() -> int:
    if not STATUS.is_file():
        sys.stderr.write("check_doc_counts: no PROJECT_STATUS.md\n")
        return 2

    problems = []
    checked = 0
    for line in STATUS.read_text().splitlines():
        if not line.startswith("| "):
            continue
        for suite, claimed in CLAIM.findall(line):
            actual = registered(suite)
            if actual is None:
                problems.append(f"{suite}: named in the table, no such suite")
                continue
            checked += 1
            if actual != int(claimed):
                problems.append(
                    f"{suite}: the table says {claimed}, the suite registers {actual}")

    for problem in sorted(set(problems)):
        print(problem)
    if problems:
        print(f"\n{len(set(problems))} stale count(s) in PROJECT_STATUS.md's "
              f"subsystem table")
        return 1
    print(f"{checked} test-count claims in PROJECT_STATUS.md all match")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

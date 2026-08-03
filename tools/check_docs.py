#!/usr/bin/env python3
"""Check what the living documents claim about the tree, against the tree.

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

## Three claims, all mechanical

Test counts, cited source paths, and cited `ap_*` symbols. All three are claims
a reader takes on trust and none was checked until the counts turned out to be
twenty-three-for-ninety wrong.

Paths and symbols were clean when this was written -- 78 and 97 of them -- which
is worth having a check for anyway: they are clean *now*, and the counts were
clean once too.

A path named only by an unticked plan item is allowed not to exist. Those items
are frequently "write this document", and requiring the artefact before the work
would make the plan unable to describe its own future.

    python3 tools/check_docs.py
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
STATUS = REPO / "docs" / "PROJECT_STATUS.md"
PLAN = REPO / "docs" / "COMPLETION_PLAN.md"
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


# A completed plan item is a summary. Sixteen lines is not the rule -- the rule
# is one line of what was done, its verification, and "Detail in
# PROJECT_STATUS.md" -- it is the point past which an item is certainly carrying
# detail that belongs in the status document. Items ran to forty and eighty-one
# lines before anything looked, because nothing about a long item looks wrong
# from inside it. In-progress items are exempt: they keep their detail while
# they are live, and are compressed in the commit that ticks them.
ITEM_LINES = 16
ITEM = re.compile(r"^\s*- \[([x~ ])\]")

PATH = re.compile(r"`((?:src|tools|tests|docs)/[A-Za-z0-9_./-]+)`")
SYMBOL = re.compile(r"`(ap_[a-z0-9_]+)`")


def tree_symbols() -> set[str]:
    found: set[str] = set()
    for root in ("src", "tests", "tools"):
        for path in (REPO / root).rglob("*"):
            if path.suffix not in (".c", ".h", ".py"):
                continue
            found.update(re.findall(r"\bap_[a-z0-9_]+\b", path.read_text()))
    return found


def check_item_length(problems: list[str]) -> int:
    """Completed plan items, against the summarise-on-completion rule."""
    if not PLAN.is_file():
        return 0
    lines = PLAN.read_text().splitlines()
    items, cur, start = [], None, 0
    for i, line in enumerate(lines):
        if ITEM.match(line):
            if cur:
                items.append((start, cur))
            cur, start = [line], i + 1
        elif cur is not None:
            # A heading or unindented paragraph ends the item.
            if line and not line.startswith((" ", "\t")) and not line.lstrip().startswith("-"):
                items.append((start, cur))
                cur = None
                continue
            cur.append(line)
    if cur:
        items.append((start, cur))

    checked = 0
    for line_no, body in items:
        while body and not body[-1].strip():
            body.pop()
        if ITEM.match(body[0]).group(1) != "x":
            continue
        checked += 1
        if len(body) > ITEM_LINES:
            title = body[0].strip()[:56]
            problems.append(
                f"COMPLETION_PLAN.md:{line_no}: completed item is {len(body)} lines "
                f"(limit {ITEM_LINES}) -- move the detail to PROJECT_STATUS.md: {title}")
    return checked


# Directories the repository deliberately does not carry. `CLAUDE.md`: the
# vendor manuals are "reference only, vendor copyright" and the Apollo firmware
# and media "are not ours to redistribute". They exist on a machine that has
# done the work and in no fresh clone, so citing them is correct and requiring
# them to exist is not.
#
# An explicit list rather than a rule. Two cleverer tests were tried and both
# were wrong: `git check-ignore` says these directories are not ignored (their
# *contents* are), and "does git track anything here" skips a mistyped path
# too, which made the check vacuous — it stopped catching the very thing it
# exists for. A short list of known-absent prefixes catches typos and says why
# each one is exempt.
USER_SUPPLIED = (
    "docs/references/archive/",
    "docs/references/intel/",
    "docs/references/motorola/",
    "docs/references/omti/",
    "docs/references/bitsavers/",
    "tools/mame-oracle/out/",
    "roms/",
    "media/",
)


def deliberately_absent(cited: str) -> bool:
    return cited.startswith(USER_SUPPLIED)


def check_references(problems: list[str]) -> int:
    """Paths and symbols the documents name, against what exists."""
    symbols = tree_symbols()
    checked = 0
    for document in (STATUS, PLAN):
        if not document.is_file():
            continue
        for line in document.read_text().splitlines():
            # An unticked item may name an artefact it exists to create.
            planned = line.lstrip().startswith("- [ ]")
            for cited in PATH.findall(line):
                checked += 1
                if (not (REPO / cited).exists() and not planned
                        and not deliberately_absent(cited)):
                    problems.append(f"{document.name}: names {cited}, which does not exist")
            for symbol in SYMBOL.findall(line):
                checked += 1
                if symbol not in symbols:
                    problems.append(
                        f"{document.name}: names `{symbol}`, which is nowhere in the tree")
    return checked


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

    checked += check_references(problems)
    checked += check_item_length(problems)

    for problem in sorted(set(problems)):
        print(problem)
    if problems:
        print(f"\n{len(set(problems))} stale claim(s) in the living documents")
        return 1
    print(f"{checked} claims in the living documents all check out")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

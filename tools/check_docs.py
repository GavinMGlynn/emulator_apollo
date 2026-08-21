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

import functools
import pathlib
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


# The same drift, in the same direction, for the one check that is a script
# rather than a CTest suite. A commit claimed "`check_frontend_flags` 16 -> 17"
# when the script ran 19 checks and then 20: **neither number had been
# measured**, and nothing could catch it, because this file verified suite
# counts, paths and symbols and this was prose with numbers in it.
#
# The claim is written as a delta -- "N -> M" -- so only the number *after* the
# arrow is a claim about the tree; the one before it is history and is left
# alone. A bare "`check_frontend_flags` N" is checked too.
FLAGS_CLAIM = re.compile(
    r"`check_frontend_flags`[^0-9\n]*(?:(\d+)\s*(?:->|→)\s*)?(\d+)")
FLAGS_SCRIPT = REPO / "tools" / "check_frontend_flags.py"


def flag_checks() -> int | None:
    """How many checks `check_frontend_flags.py` runs.

    Counted from the source rather than by running it, so this stays a document
    check and not a test run: `check(` at statement position, which is how every
    one of them is written. `skip(` is deliberately not counted -- a skipped
    flag is named in the output but is not a check that passed.
    """
    if not FLAGS_SCRIPT.is_file():
        return None
    return len(re.findall(r"^\s+check\(", FLAGS_SCRIPT.read_text(), re.MULTILINE))


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


def check_stray_items(problems: list[str]) -> int:
    """Items adrift of the document's structure.

    A whole plan item once landed *above* the file's own title, and every check
    here passed: the item was well-formed, its parent was intact, and the counts
    it claimed were right. What was wrong was where it sat -- an edit that meant
    to replace a block and instead prepended a copy of it, leaving the same item
    twice in a document read forwards to choose the next thing.

    Two cheap invariants catch that class. The plan starts with its heading, and
    no item title appears twice.
    """
    if not PLAN.is_file():
        return 0
    lines = PLAN.read_text().splitlines()

    checked = 0
    for i, line in enumerate(lines):
        if not line.strip():
            continue
        checked += 1
        if not line.startswith("#"):
            problems.append(
                f"COMPLETION_PLAN.md:{i + 1}: content before the document's own "
                f"heading -- an edit landed adrift: {line.strip()[:56]}")
        break

    seen: dict[str, int] = {}
    for i, line in enumerate(lines):
        if not ITEM.match(line):
            continue
        title = line.split("]", 1)[1].strip()
        if len(title) < 24:
            continue  # too short to be distinctive on its own
        checked += 1
        if title in seen:
            problems.append(
                f"COMPLETION_PLAN.md:{i + 1}: this item is already at line "
                f"{seen[title]} -- one of the two is a stray copy: {title[:56]}")
        else:
            seen[title] = i + 1
    return checked


def check_parent_residue(problems: list[str]) -> int:
    """A parent whose children are all done must tick, or say what it awaits.

    A top-level plan item carries a verification line of its own, and it is
    deliberately not the sum of its children's: the children are "built, and
    unit-tested against the manual", and the parent ticks when the oracle
    comparison it names has actually been run. So a parent sitting unticked
    over a complete implementation is legitimate -- and indistinguishable, to
    anyone reading the plan forwards to choose the next thing, from one that has
    simply been forgotten.

    Both happened. The core-register item's verification was `FINDINGS.md` C10,
    which had run: the children were ticked one at a time and nobody went back
    to the parent, so a finished item advertised itself as open for months. Two
    others were genuinely waiting and said nothing about what for.

    So the rule is a convention with a check behind it, as the sixteen-line
    limit is: state the residue with `**Awaiting:**`, or tick.
    """
    if not PLAN.is_file():
        return 0

    parent = re.compile(r"^- \[( |x)\] ")
    child = re.compile(r"^ +- \[( |x)\] ")

    lines = PLAN.read_text().splitlines()
    checked = 0
    open_parents = []  # (line number, title, body lines, child states)
    cur = None
    for i, line in enumerate(lines, start=1):
        if line.startswith("## "):
            if cur:
                open_parents.append(cur)
            cur = None
            continue
        m = parent.match(line)
        if m:
            if cur:
                open_parents.append(cur)
            cur = None if m.group(1) == "x" else [i, line.strip()[:56], [line], []]
            continue
        if cur is None:
            continue
        cur[2].append(line)
        c = child.match(line)
        if c:
            cur[3].append(c.group(1) == "x")
    if cur:
        open_parents.append(cur)

    for line_no, title, body, children in open_parents:
        if not children or not all(children):
            continue
        checked += 1
        if "**Awaiting:**" in "\n".join(body):
            continue
        problems.append(
            f"COMPLETION_PLAN.md:{line_no}: every child is done and the parent "
            f"is not -- tick it, or say what it awaits with `**Awaiting:**`: "
            f"{title}")
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


def check_parent_subject(problems: list[str]) -> int:
    """A `###` section heading must not sit inside a top-level item's children.

    Plan items are appended to, session after session, as `  - [ ]` children at
    a fixed indent. Nothing about that syntax says which parent a child belongs
    to -- it belongs to whichever parent happens to sit above it. So a new line
    of enquiry, written up as children without a parent of its own, silently
    becomes part of whatever item was last worked on.

    That is not hypothetical. "Archive SC-499 cartridge tape" reached 85 ticked
    children and 12 open ones spanning 622 lines, of which seven of the open
    twelve were the FPA address space, the display controller, the blitter,
    keyboard scan codes, SIO1, the tick loop and the 68030's stack frames. The
    item could not be completed, because completing it required finishing work
    that had nothing to do with tape. The MC68882 was nested under an oracle
    *probe task*; the MMU and the caches under "Exceptions, traps, interrupt
    priority".

    Subject drift cannot be detected mechanically in general, and a child-count
    threshold would fire on the 68030 integer core, which legitimately has
    forty-seven. But every real case here announced itself the same way: a `###`
    heading, written to introduce the new subject, left stranded inside the
    previous item's children. A heading is document structure; children cannot
    span one. So this is the narrow, exact rule rather than the heuristic --
    when it fires, the children after the heading want a parent of their own.
    """
    if not PLAN.is_file():
        return 0

    parent = re.compile(r"^- \[( |x)\] ")
    child = re.compile(r"^ +- \[( |x)\] ")

    lines = PLAN.read_text().splitlines()
    checked = 0
    cur = None  # line number of the item currently accumulating children
    pending = None  # a heading seen since the last parent, if any
    for i, line in enumerate(lines, start=1):
        if line.startswith("## "):
            cur, pending = None, None
            continue
        if parent.match(line):
            # A heading that introduces the next item is exactly right, so it
            # is forgotten here rather than reported.
            cur, pending = i, None
            checked += 1
            continue
        if line.startswith("### "):
            if cur is not None:
                pending = (i, line[4:][:44])
            continue
        if child.match(line) and pending is not None:
            problems.append(
                f"{PLAN.name}:{pending[0]}: `### {pending[1]}` has children under "
                f"it but no parent of its own -- they belong to the item at line "
                f"{cur}, which is not what the heading says"
            )
            pending = None
    return checked


def deliberately_absent(cited: str) -> bool:
    return cited.startswith(USER_SUPPLIED)


@functools.lru_cache(maxsize=1)
def tracked_paths() -> frozenset[str]:
    """Every path git tracks, and every directory implied by one.

    Existence was checked against the *filesystem*, which is true of the machine
    running the check and not of anyone else's. Reference PDFs are gitignored,
    so a document naming one passed here and failed in CI -- twice, because the
    fix for the file was itself verified locally, where the file exists. Git's
    index is the only view of the tree that every machine shares.
    """
    try:
        listed = subprocess.run(
            ["git", "-C", str(REPO), "ls-files", "-z"],
            capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return frozenset()          # not a checkout: fall back to the disk
    paths: set[str] = set()
    for entry in listed.split("\0"):
        if not entry:
            continue
        paths.add(entry)
        parent = pathlib.PurePosixPath(entry).parent
        while str(parent) != ".":
            paths.add(str(parent))
            paths.add(str(parent) + "/")
            parent = parent.parent
    return frozenset(paths)


def present(cited: str) -> bool:
    """Whether a cited path is one every checkout has."""
    tracked = tracked_paths()
    if not tracked:
        return (REPO / cited).exists()
    return cited in tracked or cited.rstrip("/") in tracked


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
                if (not present(cited) and not planned
                        and not deliberately_absent(cited)):
                    problems.append(f"{document.name}: names {cited}, which does not exist")
            for symbol in SYMBOL.findall(line):
                checked += 1
                if symbol not in symbols:
                    problems.append(
                        f"{document.name}: names `{symbol}`, which is nowhere in the tree")
    return checked


# A subsystem row that opens with a bare completeness claim, and a phrase that
# takes it back. `CLAUDE.md`'s *FINISH THE MODULE* requires the qualifier to
# come first, so the reader learns the module is partial from the first three
# words rather than from a subordinate clause forty lines in.
OPENS_COMPLETE = re.compile(r"^\**\s*(working|complete)\b", re.IGNORECASE)
TAKES_IT_BACK = re.compile(
    r"\bnot (?:yet )?(?:implemented|modelled|modeled|started|wired|decoded|joined)\b"
    r"|\breports? unimplemented\b",
    re.IGNORECASE,
)
# The rewrite that fixes such a row usually wants to *say* what it used to
# claim, which would trip the check forever. A row that quotes the old wording
# is exempt, because a quotation is the correction rather than the claim.
QUOTES_THE_OLD_CLAIM = re.compile(r"(?:the row|this row) (?:said|used to)", re.IGNORECASE)


def check_completeness_claims(problems: list[str]) -> int:
    """Rows that call a module working and then admit it is not.

    The rule this enforces is the one most often broken here: "working" means
    100% of the part's documented behaviour. A row that opens "working" and
    closes "not yet wired to the board" tells a reader skimming the first
    column that a subsystem is done when it is not -- and three rows were doing
    exactly that when this check was written, two of them stale in the *other*
    direction, still disclaiming instructions the CPU had executed for months.

    Nothing here can tell whether a module is genuinely complete; no check can.
    What it can enforce is that a row does not claim both things at once.
    """
    checked = 0
    if not STATUS.is_file():
        return 0
    for number, line in enumerate(STATUS.read_text().splitlines(), 1):
        if not line.startswith("| "):
            continue
        cells = [cell.strip() for cell in line.split("|")]
        if len(cells) < 4:
            continue
        status = cells[2]
        checked += 1
        if (OPENS_COMPLETE.match(status) and TAKES_IT_BACK.search(status)
                and not QUOTES_THE_OLD_CLAIM.search(status)):
            problems.append(
                f"PROJECT_STATUS.md:{number}: {cells[1][:48]!r} opens with a "
                "completeness claim and then denies it -- lead with the gap "
                "(CLAUDE.md, FINISH THE MODULE)")
    return checked


# A walk record's tag, as its own coverage table declares it: `| `[OMTI]` | ...`.
WALK_TAG = re.compile(r"^\|\s*`\[([A-Za-z0-9-]+)\]`\s*\|")
# The phrases a record uses to say a document has not been read.
UNWALKED = re.compile(r"none (?:is |are )?walked|not walked|still owed|"
                      r"unwalked", re.IGNORECASE)


def check_walk_coverage(problems: list[str]) -> int:
    """A walk record that calls a document unread while the code derives it.

    **This is the one class of claim nothing here could check, and it drifted.**
    Test counts, cited paths and cited symbols are all verifiable against the
    tree, and the checks above have kept them honest for months. *Document
    coverage* was not: `OMTI_WALK.md` said "§5 and §6.1-6.3 are still owed" and
    the plan item said "220 pages, none walked", while `ap_omti_cdb.h`,
    `ap_omti.h` and `ap_omti.c` cited **37 distinct §5 subsections** between
    them, the whole §5.4.3-§5.4.29 command run included. A reader following
    either document would have re-read a chapter already in the model, and the
    only reason one did not is that the citations were checked first.

    So the check is the cheapest thing that would have caught it: a record whose
    table declares a tag, whose own prose calls that document unread, and whose
    tag the tree cites anyway. It cannot tell *derived* from *walked* -- nothing
    can, that is a judgement about field-by-field coverage -- but it can tell
    "unread" from "cited forty times", which is the error that actually
    happened.

    Bounded deliberately: only a record that says both things is flagged. A
    record honestly listing which sections remain is the normal case and must
    stay quiet.
    """
    checked = 0
    references = REPO / "docs" / "references"
    if not references.is_dir():
        return 0
    sources = " ".join(
        path.read_text(errors="ignore")
        for path in sorted((REPO / "src").rglob("*.[ch]")))
    for record in sorted(references.glob("*_WALK.md")):
        text = record.read_text()
        tags = {match.group(1) for match in
                (WALK_TAG.match(line) for line in text.splitlines()) if match}
        for tag in sorted(tags):
            checked += 1
            cites = sources.count(f"[{tag}]")
            if cites < 10:
                continue
            # Only the record's own summary lines, not its per-page rows: a row
            # saying one section is unread is exactly what a record is for.
            summary = "\n".join(line for line in text.splitlines()
                                if line.startswith("#") or line.startswith("**"))
            # **A record correcting itself quotes the claim it is retracting**,
            # and that must not read as the claim. The same exemption
            # `QUOTES_THE_OLD_CLAIM` makes for subsystem rows, made here by
            # dropping quoted spans before matching -- a retraction names the
            # old words in quotes, a live claim states them plainly.
            summary = re.sub(r'"[^"]*"', "", summary)
            summary = re.sub(r"\u201c[^\u201d]*\u201d", "", summary)
            if UNWALKED.search(summary):
                problems.append(
                    f"{record.name}: calls `[{tag}]` unread in its summary "
                    f"while the tree cites it {cites} times -- audit the "
                    "citations before believing the coverage")
    return checked


# A walk record's coverage table row: | `[TAG]` | `dir/file.pdf` | 219 | ...
# "READ WHOLE -- 330 of 330 pages", or "4 of 722".
PROSE_PAGES = re.compile(r"([0-9]+) of ([0-9]+)(?: PDF)? pages?")
WALK_FILE = re.compile(
    r"^\|\s*`?\[?([A-Za-z0-9-]+)\]?`?\s*\|\s*`([^`]+\.pdf)`\s*\|\s*([0-9]+)\s*\|")


def check_walk_page_counts(problems: list[str]) -> int:
    """The page count a walk record claims, against the PDF it names.

    "Read whole -- 330 of 330 pages" is a claim with two halves, and the second
    is checkable: the document either has 330 pages or it does not. A record
    that walks a 209-page manual and calls it 190 is claiming completeness it
    has not reached, and one that calls it 250 is claiming pages that do not
    exist -- both of which read as diligence.

    **This can only run where the PDFs are.** `docs/references/**/*.pdf` is
    gitignored for the same reason `roms/` is -- the documents are not ours to
    redistribute -- so CI never sees them and this check skips there. That is
    not a weakness: the record is *written* on a machine that has the document,
    which is exactly when the number would be got wrong, and a check that runs
    then is worth more than one that runs in CI a day later.

    Skips silently when the file is absent or `pdfinfo` is not installed,
    because a missing artefact is not a false claim.
    """
    checked = 0
    references = REPO / "docs" / "references"
    if not references.is_dir():
        return 0
    for record in sorted(references.glob("*_WALK.md")):
        for line in record.read_text().splitlines():
            match = WALK_FILE.match(line)
            if match is None:
                continue
            tag, relative, claimed = match.groups()
            document = references / relative
            if not document.is_file():
                continue
            try:
                out = subprocess.run(["pdfinfo", str(document)],
                                     capture_output=True, text=True,
                                     check=False, timeout=30).stdout
            except (OSError, subprocess.SubprocessError):
                return checked
            pages = [word.split()[1] for word in out.splitlines()
                     if word.startswith("Pages:")]
            if not pages:
                continue
            checked += 1
            if pages[0] != claimed:
                problems.append(
                    f"{record.name}: `[{tag}]` is claimed at {claimed} pages "
                    f"and {relative} has {pages[0]}")

        # **The finished records state it in prose, not in a table.** "READ
        # WHOLE -- 29 of 29 pages" names no file, so the document is found the
        # only other way it can be: a record is named for the order number it
        # walks, and the PDF carries the same prefix. That covers every
        # whole-document walk this project has, which is the half of the corpus
        # where a wrong page count would read as completeness.
        order = record.name.removesuffix("_WALK.md")
        document = next(
            (path for path in sorted(references.rglob(f"{order}*.pdf"))), None)
        if document is None:
            continue
        # **Headings only, and a false positive is why.** `002398-04_WALK.md`
        # cross-references the `008778-03` walk as "finished at 209 of 209
        # pages" in its opening prose, and a search of the whole file takes
        # that for the record's own count -- 209 against a 330-page document.
        # A record's claim about *itself* is its status heading; a number in
        # its body may belong to any manual it mentions.
        prose = next(
            (found for found in
             (PROSE_PAGES.search(line) for line in record.read_text().splitlines()
              if line.startswith("#")) if found), None)
        if prose is None:
            continue
        try:
            out = subprocess.run(["pdfinfo", str(document)],
                                 capture_output=True, text=True, check=False,
                                 timeout=30).stdout
        except (OSError, subprocess.SubprocessError):
            return checked
        pages = [word.split()[1] for word in out.splitlines()
                 if word.startswith("Pages:")]
        if not pages:
            continue
        checked += 1
        if pages[0] != prose.group(1):
            problems.append(
                f"{record.name}: claims {prose.group(1)} pages and "
                f"{document.name} has {pages[0]}")
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

    # Not restricted to table rows: this claim is always made in prose, in the
    # verification line of whatever landed the check.
    actual_flags = flag_checks()
    for doc in (STATUS, PLAN):
        if not doc.is_file():
            continue
        for _before, claimed in FLAGS_CLAIM.findall(doc.read_text()):
            if actual_flags is None:
                problems.append(
                    "check_frontend_flags: claimed in the docs, no such script")
                continue
            checked += 1
            if int(claimed) != actual_flags:
                problems.append(
                    f"check_frontend_flags: {doc.name} says {claimed}, "
                    f"the script runs {actual_flags}")

    checked += check_references(problems)
    checked += check_item_length(problems)
    checked += check_stray_items(problems)
    checked += check_parent_residue(problems)
    checked += check_parent_subject(problems)
    checked += check_completeness_claims(problems)
    checked += check_walk_coverage(problems)
    checked += check_walk_page_counts(problems)

    for problem in sorted(set(problems)):
        print(problem)
    if problems:
        print(f"\n{len(set(problems))} stale claim(s) in the living documents")
        return 1
    print(f"{checked} claims in the living documents all check out")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

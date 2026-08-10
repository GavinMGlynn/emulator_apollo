#!/usr/bin/env python3
"""Compare a `--dump-state` file against the oracle's `apollo_dump_state` file.

The two cores name nothing alike. Ours emits `scope.index type value`, MAME's
emits `module.tag.name.block.index type value` from its own save registry, and
neither ordering means anything to the other. So a diff needs a **mapping**, and
this tool is built around the mapping being incomplete for a long time.

That shapes what it reports. An honest comparison of two partially-mapped dumps
has three populations, and conflating them is how a differential produces
confident nonsense:

  matched     mapped on both sides -- the only lines whose values mean anything
  unmapped    present, with no counterpart declared yet -- *not* a difference
  missing     mapped, but the named field is absent from that side -- a real
              finding, because it says one core models something the other does
              not, or a name has changed under us

The tool never guesses a correspondence from a similar-looking name. A wrong
mapping shows two unrelated fields agreeing, which is worse than no mapping at
all: it is a silent false negative in exactly the place a differential is
supposed to be trustworthy.

Usage:
    tools/state-diff.py OURS THEIRS [--map FILE] [--show-unmapped]

The map file is lines of `ours <TAB> theirs`, `#` for comments. Both sides may
use a trailing `*` to map a whole scope positionally, which is how a device gets
mapped in one line once its field order is known to agree.
"""

import argparse
import sys


def load(path):
    """A dump as {key: (type, value)}, preserving order for positional maps."""
    fields = {}
    order = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            # **Split from the right.** MAME's registry names contain spaces
            # -- `Motorola MC68030/:maincpu/0/REG_D().0.0` -- so splitting on
            # whitespace takes the key to be "Motorola" and silently maps
            # nothing. The type and value are the last two fields; everything
            # before them is the name, spaces and all.
            parts = line.rsplit(None, 2)
            if len(parts) < 3:
                continue
            key, kind, value = parts[0], parts[1], parts[2]
            try:
                fields[key] = (kind, int(value, 16))
            except ValueError:
                continue
            order.append(key)
    return fields, order


def load_map(path):
    """Explicit pairs and positional scope pairs, kept apart."""
    pairs, scopes = [], []
    if path is None:
        return pairs, scopes
    with open(path) as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            # Tab-separated for the same reason: a name with spaces in it is
            # one field, and only a tab can say where it ends.
            bits = [b.strip() for b in line.split("\t") if b.strip()]
            if len(bits) != 2:
                print(f"state-diff: ignoring malformed map line: {line}",
                      file=sys.stderr)
                continue
            a, b = bits
            (scopes if a.endswith("*") and b.endswith("*") else pairs).append(
                (a.rstrip("*"), b.rstrip("*")))
    return pairs, scopes


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("ours")
    ap.add_argument("theirs")
    ap.add_argument("--map", help="field correspondence, ours<TAB>theirs")
    ap.add_argument("--show-unmapped", action="store_true",
                    help="list fields with no declared counterpart")
    args = ap.parse_args()

    ours, ours_order = load(args.ours)
    theirs, theirs_order = load(args.theirs)
    pairs, scopes = load_map(args.map)

    # A positional scope pair maps the Nth field of one prefix to the Nth of the
    # other. Only sound once the orders are known to agree, which is why it is
    # opt-in per scope rather than the default for everything.
    for a_prefix, b_prefix in scopes:
        a_keys = [k for k in ours_order if k.startswith(a_prefix)]
        b_keys = [k for k in theirs_order if k.startswith(b_prefix)]
        if len(a_keys) != len(b_keys):
            print(f"  SCOPE SIZE  {a_prefix}* has {len(a_keys)} field(s), "
                  f"{b_prefix}* has {len(b_keys)} -- not mapped positionally, "
                  f"because a length mismatch means the orders cannot be assumed"
                  f" to correspond")
            continue
        pairs.extend(zip(a_keys, b_keys))

    matched = differing = missing = 0
    for a, b in pairs:
        if a not in ours or b not in theirs:
            missing += 1
            side = "ours" if a not in ours else "theirs"
            print(f"  MISSING     {a} <-> {b}: absent from {side}")
            continue
        matched += 1
        (ka, va), (kb, vb) = ours[a], theirs[b]
        if va != vb:
            differing += 1
            print(f"  DIFFERS     {a} = {va:016X} ({ka})   "
                  f"{b} = {vb:016X} ({kb})")

    mapped_ours = {a for a, _ in pairs}
    mapped_theirs = {b for _, b in pairs}
    unmapped_ours = [k for k in ours_order if k not in mapped_ours]
    unmapped_theirs = [k for k in theirs_order if k not in mapped_theirs]

    if args.show_unmapped:
        for k in unmapped_ours:
            print(f"  UNMAPPED    ours   {k}")
        for k in unmapped_theirs:
            print(f"  UNMAPPED    theirs {k}")

    print(f"\n{matched} matched, {differing} differing, {missing} missing; "
          f"{len(unmapped_ours)} unmapped here, {len(unmapped_theirs)} there")
    # Unmapped fields are not a failure: the mapping is expected to be partial
    # for a long time, and reporting them as differences would drown the ones
    # that are.
    return 1 if differing or missing else 0


if __name__ == "__main__":
    sys.exit(main())

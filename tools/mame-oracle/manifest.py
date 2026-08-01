#!/usr/bin/env python3
"""Pin a disk image we built, without redistributing it.

The Domain/OS disk image this project's install produces cannot be committed.
The *volume* is ours -- we formatted it -- but its contents are Apollo's
operating system, restored verbatim from HP/Apollo installation cartridges, and
arranging vendor binaries onto a disk we made does not change who owns them.
`CLAUDE.md` says the same thing as a standing rule, and this repository is
public.

What can be committed is a **manifest**: enough to say exactly which image was
built, from which media, by which procedure, so that

  - a later rebuild can be checked against this one byte for byte;
  - someone holding the same five cartridges can tell whether their media
    matches ours before wondering why their result does not;
  - and a bug found months from now can be tied to a specific image rather than
    to "the install".

The image itself stays in `media/`, gitignored, exactly where it was.

Usage:

    python3 tools/mame-oracle/manifest.py media/dn3500.awd \\
        --media media/domainos --out docs/references/DOMAINOS_IMAGE.md

Hashing 348 Mbyte takes a few seconds; the cartridges are 50-60 Mbyte each.
"""

from __future__ import annotations

import argparse
import hashlib
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent


def digest(path: Path, chunk: int = 1 << 22) -> tuple[str, int]:
    sha = hashlib.sha256()
    size = 0
    with open(path, "rb") as handle:
        while True:
            block = handle.read(chunk)
            if not block:
                break
            sha.update(block)
            size += len(block)
    return sha.hexdigest(), size


def git(*args: str) -> str:
    try:
        out = subprocess.run(["git", "-C", str(REPO)] + list(args),
                             capture_output=True, text=True, timeout=30)
        return out.stdout.strip()
    except Exception:
        return "(unavailable)"


def submodule_pin(name: str) -> str:
    line = git("submodule", "status", "--cached", "ext/%s" % name)
    return line.split()[0].lstrip("+-") if line else "(unavailable)"


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description="Write a manifest pinning a built disk image.")
    parser.add_argument("image", type=Path)
    parser.add_argument("--media", type=Path,
                        help="directory of source cartridges to hash too")
    parser.add_argument("--out", type=Path,
                        help="write here instead of standard output")
    parser.add_argument("--label", default="",
                        help="what stage of the install this image is")
    args = parser.parse_args(argv)

    if not args.image.is_file():
        sys.stderr.write("manifest: no image at %s\n" % args.image)
        return 2

    image_sha, image_size = digest(args.image)

    cartridges = []
    if args.media is not None and args.media.is_dir():
        for path in sorted(args.media.glob("*.ct")):
            sha, size = digest(path)
            cartridges.append((path.name, sha, size))

    lines = []
    lines.append("# The Domain/OS disk image, pinned rather than committed")
    lines.append("")
    lines.append(
        "The image itself is **not** in this repository and will not be. We\n"
        "formatted the volume; its contents are Apollo's operating system,\n"
        "restored verbatim from HP/Apollo installation cartridges, and arranging\n"
        "vendor binaries onto a disk we made does not change who owns them.\n"
        "`CLAUDE.md` says the same as a standing rule, and this repository is\n"
        "public.")
    lines.append("")
    lines.append(
        "This file is what can be published: enough to identify the image\n"
        "exactly, so that a rebuild can be checked against it, and so that anyone\n"
        "holding the same media can confirm their cartridges match ours before\n"
        "wondering why their result does not.")
    lines.append("")
    lines.append("## The image")
    lines.append("")
    if args.label:
        lines.append("Stage: **%s**" % args.label)
        lines.append("")
    lines.append("| Field | Value |")
    lines.append("| --- | --- |")
    resolved = args.image.resolve()
    if resolved.is_relative_to(REPO):
        where = "`%s` (gitignored)" % resolved.relative_to(REPO)
    else:
        where = "`%s`" % resolved.name
    lines.append("| File | %s |" % where)
    lines.append("| Size | %d bytes |" % image_size)
    lines.append("| SHA-256 | `%s` |" % image_sha)
    lines.append("")
    lines.append("## What produced it")
    lines.append("")
    lines.append("| Field | Value |")
    lines.append("| --- | --- |")
    lines.append("| Oracle | MAME pinned at `%s`, built `SUBTARGET=apollo`, "
                 "with `APOLLO_XXL` enabled |" % submodule_pin("mame"))
    lines.append("| Machine | `dn3500` |")
    lines.append("| Driver | `tools/mame-oracle/mdsession.py` at commit `%s` |"
                 % git("rev-parse", "--short", "HEAD"))
    lines.append("| Procedure | `FINDINGS.md` C47-C54, and the plan's "
                 "first-boot item |")
    lines.append("")
    lines.append(
        "**A rebuild is not expected to be bit-identical.** The install is a\n"
        "conversation paced by the host, the volume carries timestamps, and the\n"
        "RTC is not under our control (C53). The hash identifies *this* image; it\n"
        "is not a reproducibility claim, and nothing timed may be measured through\n"
        "an image built this way.")
    lines.append("")

    if cartridges:
        lines.append("## The source media")
        lines.append("")
        lines.append(
            "Also gitignored. Hashed so that a mismatch is found here rather than\n"
            "three hours into an install.")
        lines.append("")
        lines.append("| Cartridge | Size | SHA-256 |")
        lines.append("| --- | --- | --- |")
        for name, sha, size in cartridges:
            lines.append("| `%s` | %d | `%s` |" % (name, size, sha))
        lines.append("")

    text = "\n".join(lines) + "\n"
    if args.out is not None:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text)
        sys.stderr.write("manifest: wrote %s\n" % args.out)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

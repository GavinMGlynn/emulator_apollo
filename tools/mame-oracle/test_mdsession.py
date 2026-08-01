#!/usr/bin/env python3
"""Test mdsession.py's driving logic against a stub MAME.

The same split `test_oracle.py` makes, and for the same reason. Whether the
install *works* needs a real emulator, a real cartridge and a quarter of an
hour. Whether the driver reaches a prompt, sends the right commands in the right
order, refuses to mistake an old prompt for a new one, and fails loudly instead
of hanging is ordinary program logic that needs no MAME at all.

The stub is written to the shapes that actually bite here:

  - it is **deaf until spoken to**, like the firmware's autobaud: it prints
    nothing at all until it has received a character. A driver that waited for
    output before sending any would wait forever, and that is the failure the
    knock loop exists to prevent (`FINDINGS.md` C45);
  - it prints a **repeating prompt**, because every MD command ends at the same
    `>`. A driver matching against the whole history would satisfy each
    expectation with the previous command's prompt and run the whole script into
    a machine that never answered;
  - it can be told to **die**, because a machine that exits must be reported as
    such rather than waited out to the timeout;
  - it can be told to **say nothing ever**, because that is the hang this tool
    is most likely to meet and it must end in a message rather than in a stall.

    python3 tools/mame-oracle/test_mdsession.py
"""

from __future__ import annotations

import os
import stat
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
DRIVER = HERE / "mdsession.py"

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
        sys.stderr.write("FAIL %s\n  %r not found in:\n%s\n"
                         % (name, needle, haystack))
    else:
        sys.stdout.write("ok   %s\n" % name)


# A stand-in for MAME with the stdio terminal compiled in. It speaks MD's
# format from docs/references/MD.md -- sign-on, then CR LF CR LF '>' for each
# carriage return -- and, like the real device, echoes what it receives.
#
# `MDSTUB_MODE` picks the behaviour under test; `MDSTUB_RECORD` names a file to
# append every received byte to, which is how the ordering of a stage is checked
# without parsing the driver's own log.
STUB = r"""#!/usr/bin/env python3
import os, sys

mode = os.environ.get("MDSTUB_MODE", "normal")
record = os.environ.get("MDSTUB_RECORD")

if mode == "silent":
    # Reads and answers nothing, ever. The machine that hangs.
    while True:
        try:
            os.read(0, 1)
        except OSError:
            break
    sys.exit(0)

out = sys.stdout
signed_on = False
seen = 0
line = ""

while True:
    try:
        data = os.read(0, 4096)
    except OSError:
        break
    if not data:
        break
    if record:
        with open(record, "ab") as handle:
            handle.write(data)
    for byte in data:
        char = bytes([byte])
        if not signed_on:
            # Deaf until spoken to: the sign-on is provoked, not spontaneous.
            signed_on = True
            out.write("\nMD7C REV 8.00, 1989/08/16.17:23:52\n")
        if char == b"\r":
            seen += 1
            if mode == "die" and seen >= 3:
                out.flush()
                sys.exit(4)
            if line.strip() == "re":
                # Reset System: the machine goes deaf again, exactly as the
                # real one does, and only a further knock brings it back.
                # Without this the stub cannot exercise the case that cost a
                # whole session (C50).
                signed_on = False
                line = ""
                continue
            line = ""
            out.write("\n\n>")
        else:
            line += char.decode("latin-1")
            out.write(char.decode("latin-1"))
        out.flush()
"""


def write_stub(directory: Path) -> Path:
    path = directory / "apollo"
    path.write_text(STUB)
    path.chmod(path.stat().st_mode | stat.S_IEXEC | stat.S_IXGRP | stat.S_IXOTH)
    return path


def run(stub: Path, extra, environment=None, timeout=60):
    env = dict(os.environ)
    env.update(environment or {})
    # A run directory of its own, beside the stub. The driver wipes whatever it
    # is given, and the default is the one a real session uses -- so a test left
    # on the default would delete a live install's nvram and cfg out from under
    # it, on a run that takes a quarter of an hour to reach its first prompt.
    extra = ["--rundir", str(stub.parent / "run")] + list(extra)
    # A short settle: the default is sized for a machine that has to be
    # scheduled and emulated before it reads what was written to it, and a stub
    # reads it immediately. Kept non-zero rather than removed, because zero
    # would stop this suite from covering the race the settle exists for.
    command = [sys.executable, str(DRIVER), "--mame", str(stub),
               "--settle", "0.5"] + list(extra)
    proc = subprocess.run(command, capture_output=True, text=True, env=env,
                          timeout=timeout)
    return proc


def main() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        stub = write_stub(work)

        # Reaching the prompt at all. The stub says nothing until it is sent
        # something, so this passes only if the driver knocks first.
        proc = run(stub, ["--stage", "prompt"])
        check("a silent machine is knocked into answering", proc.returncode, 0)
        check_in("the prompt is recognised", "at the MD prompt", proc.stderr)
        check_in("the sign-on reaches the console", "MD7C REV 8.00",
                 proc.stdout)

        # The whole invol stage, in order, read back from what the machine
        # received rather than from what the driver said it sent.
        record = work / "received"
        proc = run(stub, ["--stage", "invol"],
                   {"MDSTUB_RECORD": str(record)})
        check("the invol stage runs to its end", proc.returncode, 0)
        received = record.read_bytes().decode("latin-1")
        commands = [line for line in received.split("\r") if line]
        check("the commands arrive in the procedure's order",
              commands, ["re", "re", "di c", "ex invol"])

        # A machine that exits mid-script is reported as a machine that exited,
        # not waited out. The stub dies on the third carriage return, which is
        # inside the stage rather than before it.
        proc = run(stub, ["--stage", "invol", "--knock-timeout", "10",
                          "--timeout", "10"],
                   {"MDSTUB_MODE": "die"})
        check("a machine that exits fails the run", proc.returncode, 1)
        check_in("and says that it exited", "the machine exited", proc.stderr)

        # A machine that never answers ends in a message rather than a stall.
        proc = run(stub, ["--stage", "prompt", "--knock-timeout", "3"],
                   {"MDSTUB_MODE": "silent"})
        check("a machine that never prompts fails", proc.returncode, 1)
        check_in("and says it was knocking", "no prompt after",
                 proc.stderr)

        # The disk image is made when it is missing, at the size the install
        # procedure names -- recent MAME creates nothing and fails instead
        # (FINDINGS.md C47), so this is the driver's job and not a convenience.
        disk = work / "made" / "dn3500.awd"
        proc = run(stub, ["--stage", "prompt", "--disk", str(disk)])
        check("a missing disk image is created", disk.is_file(), True)
        check("at 348 MB", disk.stat().st_size, 348 * 1024 * 1024)

        # And not re-made over an existing one. An install is many stages long;
        # a driver that truncated the image on the second stage would destroy
        # exactly the work the first one did.
        disk.write_bytes(b"already installed")
        proc = run(stub, ["--stage", "prompt", "--disk", str(disk)])
        check("an existing image is left alone", disk.read_bytes(),
              b"already installed")

        # A followed command file, written before the run: the directives parse
        # and arrive in order, and !quit ends the session rather than the
        # timeout doing it.
        record = work / "received-file"
        commands = work / "commands.txt"
        commands.write_text(
            "# a comment, which is not sent\n"
            "\n"
            "re\n"
            "!knock \\n\\n>\n"
            "di c\n"
            "!raw x\n"
            "!quit\n"
        )
        # `!knock` and not `!expect` after `re`, and the stub is what enforces
        # it: a reset machine is deaf, so an expectation here waits for output
        # that cannot come. The first version of this fixture used `!expect`
        # and passed only because the stub could not yet model a reset.
        # A blank line in the file is *not* an empty answer, and must not be:
        # a file being appended to a line at a time is full of momentarily
        # blank tails. `!cr` is how an empty answer is spelt.
        proc = run(stub, ["--stage", "prompt", "--commands", str(commands),
                          "--timeout", "10"],
                   {"MDSTUB_RECORD": str(record)})
        check("a followed command file runs to !quit", proc.returncode, 0)
        received = record.read_bytes().decode("latin-1")
        # The leading "\r" is the knock that reached the power-on prompt, and
        # the doubled one after "re" is the knock that brought the reset
        # machine back.
        check("its directives arrive in order and !raw adds no return",
              received, "\rre\r\rdi c\rx")

        # `!cr` sends a bare carriage return and `!raw` interprets escapes, so
        # an answer that is nothing at all can be spelt. INVOL ends its badspot
        # list with one, and the first attempt at it went out as a space --
        # which is a different byte and, on the knock path, a fatal one (C50).
        record = work / "received-cr"
        commands = work / "cr.txt"
        commands.write_text("!cr\n!raw a\\rb\n!quit\n")
        proc = run(stub, ["--stage", "prompt", "--commands", str(commands),
                          "--timeout", "10"],
                   {"MDSTUB_RECORD": str(record)})
        check("!cr and !raw escapes run", proc.returncode, 0)
        check("!cr is a bare return and !raw \\r is a real one",
              record.read_bytes().decode("latin-1"), "\r\ra\rb")

        # `!knock` is a directive rather than a stage's private trick, because
        # every stage that resets needs it: after `re` the machine is deaf and
        # an expectation waits for output it cannot produce.
        record = work / "received-knock"
        commands = work / "knock.txt"
        commands.write_text("re\n!knock MD7C\n!quit\n")
        proc = run(stub, ["--stage", "prompt", "--commands", str(commands),
                          "--timeout", "10", "--knock-timeout", "20"],
                   {"MDSTUB_RECORD": str(record)})
        check("!knock reaches a machine that has gone quiet",
              proc.returncode, 0)

        # And the character it knocks with is settable, which is what let the
        # carriage-return claim in C50 be measured instead of asserted.
        record = work / "received-char"
        commands = work / "char.txt"
        commands.write_text("!quit\n")
        proc = run(stub, ["--stage", "prompt", "--commands", str(commands),
                          "--knock-char", "\r", "--timeout", "10"],
                   {"MDSTUB_RECORD": str(record)})
        check("the knock character is what reaches the machine",
              record.read_bytes().decode("latin-1"), "\r")

        # And followed *while running*, which is the property the whole
        # mechanism exists for: a stage learnt from the machine's own output
        # must be answerable without restarting the machine, because reaching
        # some of these prompts costs ten minutes of emulated tape.
        record = work / "received-live"
        commands = work / "live.txt"
        commands.write_text("")

        import threading

        def append_later():
            import time as _time
            _time.sleep(3.0)
            with open(commands, "a") as handle:
                handle.write("late\n!quit\n")

        writer = threading.Thread(target=append_later)
        writer.start()
        proc = run(stub, ["--stage", "prompt", "--commands", str(commands),
                          "--commands-timeout", "30", "--timeout", "10"],
                   {"MDSTUB_RECORD": str(record)})
        writer.join()
        check("a line appended after the run started is still sent",
              proc.returncode, 0)
        check_in("and reaches the machine", "late",
                 record.read_bytes().decode("latin-1"))

        # Relative image paths are resolved against the caller's directory, not
        # MAME's. The driver runs the emulator from its own directory, so an
        # unresolved path is created here and looked for there.
        relative = Path("relative-disk.awd")
        cwd = os.getcwd()
        os.chdir(work)
        try:
            proc = run(stub, ["--stage", "prompt", "--disk", str(relative)])
            check_in("a relative disk path is resolved for MAME",
                     str(work / relative), proc.stderr)
        finally:
            os.chdir(cwd)

    if failures:
        sys.stderr.write("\n%d check(s) failed\n" % failures)
        return 1
    sys.stdout.write("\nall checks passed\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
